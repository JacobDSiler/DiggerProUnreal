#include "GPU/VoxelGPUBackend.h"

// Minimal shader plumbing — compiles, but does nothing yet
#include "GlobalShader.h"
#include "ShaderPermutation.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"
#include "RenderGraphBuilder.h"
#include "RHI.h"
#include "RHICommandList.h"

namespace VoxelGPU
{
        // Default to GPU enabled; individual calls can force CPU fallback by
        // toggling this thread-local flag via FForceCPU.
        thread_local bool GForceCPU = false;
}

//-------------------------------------------------------------------------------------------------
// A placeholder compute shader so the pipeline links cleanly.
// IMPORTANT: the .usf is not actually used until we bind resources.
//-------------------------------------------------------------------------------------------------

class FApplyBrushCS : public FGlobalShader
{
	using FPermutationDomain = FShaderPermutationNone;

public:
	DECLARE_GLOBAL_SHADER(FApplyBrushCS);
	SHADER_USE_PARAMETER_STRUCT(FApplyBrushCS, FGlobalShader);

	// TODO: Define dispatch dimensions based on voxel bounds
	// FIntVector DispatchSize = ...;

	// Keep this EMPTY for now to avoid UAV binding errors (the source .usf can exist but won’t be used)
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector3f, ChunkOriginWS)
		SHADER_PARAMETER(float,     VoxelSize)
		SHADER_PARAMETER(int32,     VoxelsPerSide)
		SHADER_PARAMETER(int32,     bDig)
		// When we wire real buffers, we’ll add:
		// SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float>, SDFOut)
		// SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float>,   SDFIn)
		// SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>,  ModifiedCount)
		// + dispatch region
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Params)
	{
		return true; // build for all RHIs for now
	}
};

IMPLEMENT_GLOBAL_SHADER(FApplyBrushCS, "/DiggerProUnreal/Shaders/VoxelBrushCS.usf", "CSMain", SF_Compute);

//-------------------------------------------------------------------------------------------------
// Public API — stub that never dispatches (yet). Keeps compile green.
//-------------------------------------------------------------------------------------------------

namespace VoxelGPU
{
	bool TryApplyBrushGPU(const UVoxelChunk* /*Chunk*/, const FBrushStroke& /*Stroke*/, const FBrushDispatchDesc& /*Desc*/)
	{
		// Stub: we return false so callers can fall back to CPU path.
		// Once we wire the buffers, we’ll build an RDG pass here and dispatch.
		return false;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		// Example: OutEnvironment.SetDefine(TEXT("MY_DEFINE"), 1);
	}

}
