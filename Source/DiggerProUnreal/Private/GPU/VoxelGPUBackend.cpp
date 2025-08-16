#include "GPU/VoxelGPUBackend.h"
#include "VoxelChunk.h"
#include "RHI.h"
#include "RenderGraphUtils.h"
#include "RenderGraphBuilder.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "Misc/ScopeRWLock.h"
#include "VoxelConversion.h"

// ---------- Compute shader wrapper ----------

class FApplyBrushCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FApplyBrushCS);
	SHADER_USE_PARAMETER_STRUCT(FApplyBrushCS, FGlobalShader);

public:
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RW_TEXTURE3D(float, RW_SDF)
		SHADER_PARAMETER(FVector3f, ChunkOriginWS)     // world origin of chunk
		SHADER_PARAMETER(float,   VoxelSize)           // world size of one voxel
		SHADER_PARAMETER(FVector3f, BrushPosWS)        // world
		SHADER_PARAMETER(float,   BrushRadius)
		SHADER_PARAMETER(float,   BrushFalloff)
		SHADER_PARAMETER(uint32,  bDig)                // 1 dig (air), 0 add (solid)
		SHADER_PARAMETER(FIntVector3, BoundsMin)       // dispatch min
		SHADER_PARAMETER(FIntVector3, BoundsMax)       // dispatch max (exclusive)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FApplyBrushCS, "/Digger/VoxelBrushCS.usf", "CSMain", SF_Compute);

// ---------- Backend ----------

FVoxelGPUBackend& FVoxelGPUBackend::Get()
{
	static FVoxelGPUBackend S;
	return S;
}

static int32 GetVoxelsPerChunk()
{
	return FMath::Max(1, FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions);
}

void FVoxelGPUBackend::CreateSDFTexture(FVoxelGPUResources& Out, int32 N)
{
	FRHIResourceCreateInfo Info(TEXT("Digger_SDF3D"));
	EPixelFormat Format = PF_R32_FLOAT;

	Out.SDFTex = RHICreateTexture3D(N, N, N, Format, 1, TexCreate_ShaderResource | TexCreate_UAV | TexCreate_Transient, Info);
	Out.SDF_UAV = RHICreateUnorderedAccessView(Out.SDFTex);
	Out.SDF_SRV = RHICreateShaderResourceView(Out.SDFTex);
	Out.N = N;

	// Optionally initialize to AIR (1.0f). We can skip and assume landscape fallback on sampling.
}

void FVoxelGPUBackend::InitForChunk(UVoxelChunk* Chunk, int32 VoxelsPerDim)
{
	if (!IsValid(Chunk)) return;

	FVoxelGPUResources* Found = Resources.Find(Chunk);
	if (Found && Found->IsValid() && Found->N == VoxelsPerDim) return;

	FVoxelGPUResources& Res = Resources.FindOrAdd(Chunk);
	CreateSDFTexture(Res, VoxelsPerDim);
}

void FVoxelGPUBackend::ReleaseForChunk(UVoxelChunk* Chunk)
{
	if (!Chunk) return;
	Resources.Remove(Chunk);
}

const FVoxelGPUResources* FVoxelGPUBackend::GetResources(UVoxelChunk* Chunk) const
{
	return Resources.Find(Chunk);
}

int32 FVoxelGPUBackend::ApplyBrush(UVoxelChunk* Chunk, const FBrushStroke& Stroke)
{
	if (!IsValid(Chunk)) return 0;

	const int32 N = GetVoxelsPerChunk();
	InitForChunk(Chunk, N);

	FVoxelGPUResources* Res = Resources.Find(Chunk);
	if (!Res || !Res->IsValid()) return 0;

	// Compute local voxel AABB for dispatch bounds (tight box)
	const FVector ChunkOrigin = FVoxelConversion::ChunkToWorld(Chunk->GetChunkCoordinates());
	const float VoxelSize = FVoxelConversion::LocalVoxelSize;

	// Conservative world AABB (use your existing CalculateBrushBounds)
	// NOTE: CalculateBrushBounds is private; replicate minimal logic here to avoid header churn.
	const float OuterR = Stroke.BrushRadius + Stroke.BrushFalloff;
	const FVector HalfExt(OuterR, OuterR, (Stroke.BrushLength > 0.f ? Stroke.BrushLength * 0.5f + Stroke.BrushFalloff : OuterR));
	const FBox WorldAABB = FBox(Stroke.BrushPosition - HalfExt, Stroke.BrushPosition + HalfExt);

	// Convert world AABB to voxel coords (0..N-1)
	auto WorldToLocalVoxel = [&](const FVector& W)->FIntVector
	{
		const FVector Local = (W - ChunkOrigin) / VoxelSize + FVector(N * 0.5f);
		return FIntVector(
			FMath::FloorToInt(Local.X),
			FMath::FloorToInt(Local.Y),
			FMath::FloorToInt(Local.Z));
	};

	FIntVector VMin = WorldToLocalVoxel(WorldAABB.Min) - FIntVector(1);
	FIntVector VMax = WorldToLocalVoxel(WorldAABB.Max) + FIntVector(2);

	// Clamp to [0, N)
	VMin.X = FMath::Clamp(VMin.X, 0, N);
	VMin.Y = FMath::Clamp(VMin.Y, 0, N);
	VMin.Z = FMath::Clamp(VMin.Z, 0, N);
	VMax.X = FMath::Clamp(VMax.X, 0, N);
	VMax.Y = FMath::Clamp(VMax.Y, 0, N);
	VMax.Z = FMath::Clamp(VMax.Z, 0, N);

	if (VMin.X >= VMax.X || VMin.Y >= VMax.Y || VMin.Z >= VMax.Z) return 0;

	FRHICommandListImmediate& RHICmdList = GRHICommandList.GetImmediateCommandList();

	FRDGBuilder GraphBuilder(RHICmdList);
	FRDGTextureRef RDGTex = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(Res->SDFTex, TEXT("Digger_SDF3D_RT")));

	FApplyBrushCS::FParameters* Pass = GraphBuilder.AllocParameters<FApplyBrushCS::FParameters>();
	Pass->RW_SDF           = GraphBuilder.CreateUAV(RDGTex);
	Pass->ChunkOriginWS    = (FVector3f)ChunkOrigin;
	Pass->VoxelSize        = VoxelSize;
	Pass->BrushPosWS       = (FVector3f)Stroke.BrushPosition;
	Pass->BrushRadius      = Stroke.BrushRadius;
	Pass->BrushFalloff     = Stroke.BrushFalloff;
	Pass->bDig             = Stroke.bDig ? 1u : 0u;
	Pass->BoundsMin        = FIntVector3(VMin.X, VMin.Y, VMin.Z);
	Pass->BoundsMax        = FIntVector3(VMax.X, VMax.Y, VMax.Z);

	auto* ComputeShader = GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader<FApplyBrushCS>();
	const FIntVector GroupCounts(
		FMath::DivideAndRoundUp(VMax.X - VMin.X, 8),
		FMath::DivideAndRoundUp(VMax.Y - VMin.Y, 8),
		FMath::DivideAndRoundUp(VMax.Z - VMin.Z, 8));

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("Digger.ApplyBrushCS"),
		ComputeShader,
		Pass,
		GroupCounts);

	GraphBuilder.Execute();

	// If you want exact modified counts, add an RW buffer counter and read it back occasionally.
	// For now, return a rough bound (volume of dispatch box).
	return (VMax.X - VMin.X) * (VMax.Y - VMin.Y) * (VMax.Z - VMin.Z);
}

// TODO: Add exact modified voxel count
//   -> Implement RWStructuredBuffer<uint> counter in shader
//   -> Increment only when oldSDF != newSDF
//   -> Read back on brush end or at interval for accuracy in FVoxelModificationReport

// TODO: Support all brush shapes on GPU
//   -> Sphere implemented
//   -> Add Cube, Capsule, Cylinder, Cone, Torus, Pyramid, Icosphere, Smooth, Noise
//   -> Share math with CPU UVoxelBrushShape where possible

// TODO: GPU → CPU sync
//   -> On brush end, read back touched region into SparseVoxelGrid for save/serialization
//   -> Optimize with staging buffer and batched updates

// TODO: GPU marching cubes (future)
//   -> Use Res->SDF_SRV to run marching cubes in compute
//   -> Replace CPU mesh extraction for large performance win
