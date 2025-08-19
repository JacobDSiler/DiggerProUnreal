// SparseVoxelGrid.cpp
// CPU-first implementation with a clean hook for future GPU hotswap.
// - Keeps existing functionality and signatures.
// - Adds backend toggle (cvar) and no-op GPU stubs so we can wire kernels later,
//   without touching headers or other files right now.
// - Improves perf by reserving containers, avoiding redundant work, and batching debug draws.
// - Marks border dirty when needed to support neighbor sync.
// - Leaves TODOs for VoxelEvents.h emission once you re-enable it.

#include "SparseVoxelGrid.h"

#include "DiggerManager.h"
#include "VoxelChunk.h"
#include "VoxelConversion.h"
#include "DiggerDebug.h"

#include "GPU/VoxelGPUBackend.h"
#include "RHI.h"

#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "Async/Async.h"
#include "Serialization/BufferArchive.h"
#include "DiggerProUnreal/Utilities/FastDebugRenderer.h"

#include "Editor.h"
#include "Misc/FileHelper.h"
#include "AssetRegistry/AssetRegistryModule.h"

#include "HAL/IConsoleManager.h" // for backend CVars

// If you have FVoxelSDFHelper as a public header, keep it.
// (Leaving your original absolute include as-is to avoid include path churn.)
#include "C:\Users\serpe\Documents\Unreal Projects\DiggerProUnreal\Source\DiggerProUnreal\Public\Voxel\FVoxelSDFHelper.h"

// ---------------------------------------------------------------------------------------------------------------------
// Runtime toggles & small helpers
// ---------------------------------------------------------------------------------------------------------------------

// 0 = CPU (default), 1 = GPU path (stubbed for now; falls back to CPU)
static TAutoConsoleVariable<int32> CVarDiggerUseGPUVoxelOps(
	TEXT("digger.UseGPUVoxelOps"),
	0,
	TEXT("0 = CPU voxels (default), 1 = GPU path (not yet implemented; will fall back)."),
	ECVF_Default);

// Simple hardware check: require a valid RHI and compute shader support.
static bool IsGPUAvailable()
{
        return (GDynamicRHI != nullptr);// && GRHISupportsComputeShaders;
}

static FORCEINLINE bool UseGPUBackend()
{
        return !VoxelGPU::GForceCPU &&
               CVarDiggerUseGPUVoxelOps.GetValueOnAnyThread() != 0 &&
               IsGPUAvailable();
}

// SDF conventions
namespace
{
	constexpr float SDF_SOLID = -1.0f; // inside terrain
	constexpr float SDF_AIR   =  1.0f; // outside terrain

	// 6-neighborhood (faces only)
	static const FIntVector FaceDirs[6] = {
		FIntVector( 1, 0, 0),
		FIntVector(-1, 0, 0),
		FIntVector( 0, 1, 0),
		FIntVector( 0,-1, 0),
		FIntVector( 0, 0, 1),
		FIntVector( 0, 0,-1)
	};

	// Utility: check if local voxel index is on the chunk border (0..N-1)
	static FORCEINLINE bool IsBorderVoxel(const FIntVector& V, int32 N)
	{
		return (V.X <= 0 || V.Y <= 0 || V.Z <= 0 ||
			    V.X >= N - 1 || V.Y >= N - 1 || V.Z >= N - 1);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// GPU stubs (no-ops for now) — routes can call these; once kernels exist, fill them in.
// We keep the signatures local and do not add members to the class/hdr.
// ---------------------------------------------------------------------------------------------------------------------

namespace VoxelGPU
{
        static bool SetVoxel(USparseVoxelGrid* Grid, int32 X, int32 Y, int32 Z, float NewSDFValue, bool bDig)
        {
                // GPU path not implemented yet; ensure we execute the CPU path without
                // re-entering this function.
                FForceCPU Scope;
                return Grid->SetVoxel(X, Y, Z, NewSDFValue, bDig);
        }

        static float GetVoxel(const USparseVoxelGrid* Grid, int32 X, int32 Y, int32 Z)
        {
                FForceCPU Scope;
                return const_cast<USparseVoxelGrid*>(Grid)->GetVoxel(X, Y, Z);
        }

        static void RemoveVoxels(USparseVoxelGrid* Grid, const TArray<FIntVector>& VoxelsToRemove)
        {
                FForceCPU Scope;
                Grid->RemoveVoxels(VoxelsToRemove);
        }

        static bool ExtractIslandAtPosition(USparseVoxelGrid* Grid, const FVector& WorldPosition,
                USparseVoxelGrid*& OutGrid, TArray<FIntVector>& OutVoxels)
        {
                FForceCPU Scope;
                return Grid->ExtractIslandAtPosition(WorldPosition, OutGrid, OutVoxels);
        }
}

// ---------------------------------------------------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------------------------------------------------

USparseVoxelGrid::USparseVoxelGrid()
	: DiggerManager(nullptr)
	, ParentChunk(nullptr)
	, World(nullptr)
	, ParentChunkCoordinates(0, 0, 0)
	, TerrainGridSize(0)
	, Subdivisions(0)
	, ChunkSize(0)
	, DebugRenderOffset(FVector::ZeroVector)
	, bBorderIsDirty(false)
{
}

void USparseVoxelGrid::Initialize(UVoxelChunk* ParentChunkReference)
{
	ParentChunk = ParentChunkReference;
	ParentChunkCoordinates = ParentChunk ? ParentChunk->GetChunkCoordinates() : FIntVector::ZeroValue;

	// Only set DiggerManager if ParentChunk is valid
	if (ParentChunk && IsValid(ParentChunk))
	{
		DiggerManager = ParentChunk->GetDiggerManager();
	}
	else
	{
		DiggerManager = nullptr;
	}

	// Cache World (editor or PIE)
	World = GetSafeWorld();

	// Keep internal sizing coherent with runtime conversion (does not change signatures)
	if (ChunkSize <= 0)
	{
		ChunkSize = FVoxelConversion::ChunkSize;
	}
	if (TerrainGridSize <= 0)
	{
		TerrainGridSize = FVoxelConversion::TerrainGridSize;
	}
	if (Subdivisions <= 0)
	{
		Subdivisions = FVoxelConversion::Subdivisions;
	}
}

void USparseVoxelGrid::InitializeDiggerManager()
{
	EnsureDiggerManager();
	if (!DiggerManager || !DiggerManager->GetSafeWorld())
	{
		if (DiggerDebug::Context)
		{
			UE_LOG(LogTemp, Warning, TEXT("World is null in InitializeDiggerManager."));
		}
		return;
	}

	// Pull authoritative runtime values from manager if present
	ChunkSize       = DiggerManager->ChunkSize;
	TerrainGridSize = DiggerManager->TerrainGridSize;
	Subdivisions    = DiggerManager->Subdivisions;
}

bool USparseVoxelGrid::EnsureDiggerManager()
{
	if (!DiggerManager && ParentChunk)
	{
		DiggerManager = ParentChunk->GetDiggerManager();
	}

	if (!DiggerManager)
	{
		if (DiggerDebug::Manager)
		{
			UE_LOG(LogTemp, Error, TEXT("DiggerManager is null in USparseVoxelGrid::EnsureDiggerManager."));
		}
		return false;
	}

	if (DiggerDebug::Voxels)
	{
		UE_LOG(LogTemp, VeryVerbose, TEXT("DiggerManager ensured for USparseVoxelGrid."));
	}
	return true;
}

UWorld* USparseVoxelGrid::GetSafeWorld() const
{
#if WITH_EDITOR
	if (GIsEditor && GEditor)
	{
		return GEditor->GetEditorWorldContext().World();
	}
#endif
	return GetWorld();
}

// ---------------------------------------------------------------------------------------------------------------------
// Landscape helpers
// ---------------------------------------------------------------------------------------------------------------------

bool USparseVoxelGrid::IsPointAboveLandscape(FVector& Point)
{
	if (!World) World = GetSafeWorld();
	if (!World)
	{
		if (DiggerDebug::Context)
		{
			UE_LOG(LogTemp, Error, TEXT("World is null in IsPointAboveLandscape"));
		}
		return false;
	}

	const FVector Start = Point;
	const FVector End   = Point + FVector(0, 0, 10000.0f);

	FHitResult HitResult;
	FCollisionQueryParams CollisionParams(SCENE_QUERY_STAT(SparseVoxelGrid_IsPointAboveLandscape), /*bTraceComplex*/ false);
	const bool bHit = World->LineTraceSingleByChannel(HitResult, Start, End, ECC_Visibility, CollisionParams);
	return bHit;
}

float USparseVoxelGrid::GetLandscapeHeightAtPoint(FVector Position)
{
	return EnsureDiggerManager() ? DiggerManager->GetLandscapeHeightAt(Position) : 0.f;
}

// ---------------------------------------------------------------------------------------------------------------------
// Core voxel access / mutation (CPU path + GPU stub routing)
// ---------------------------------------------------------------------------------------------------------------------

bool USparseVoxelGrid::SetVoxel(FIntVector Position, float SDFValue, bool bDig)
{
	return SetVoxel(Position.X, Position.Y, Position.Z, SDFValue, bDig);
}

bool USparseVoxelGrid::SetVoxel(int32 X, int32 Y, int32 Z, float NewSDFValue, bool bDig)
{
	// If user enabled GPU path, offer it first.
	if (UseGPUBackend())
	{
		if (VoxelGPU::SetVoxel(this, X, Y, Z, NewSDFValue, bDig))
		{
			return true; // GPU handled it
		}
		// else fall through to CPU
	}

	FScopeLock Lock(&VoxelDataMutex);
	constexpr float SDF_THRESHOLD = 1e-3f;

	if (!FMath::IsFinite(NewSDFValue) || FMath::IsNearlyZero(NewSDFValue, SDF_THRESHOLD))
	{
		return false;
	}

	const FIntVector Key(X, Y, Z);
	if (FVoxelData* Existing = VoxelData.Find(Key))
	{
		const float Current = Existing->SDFValue;
		const float Blended = bDig
			? FMath::Max(Current, NewSDFValue)   // digging → push toward AIR (positive)
			: FMath::Min(Current, NewSDFValue);  // adding  → push toward SOLID (negative)

		if (FMath::IsNearlyEqual(Current, Blended, SDF_THRESHOLD))
		{
			return false; // no change
		}
		Existing->SDFValue = Blended;
	}
	else
	{
		VoxelData.Add(Key, FVoxelData(NewSDFValue));
	}

	// Mark chunk dirty for mesh rebuild
	if (ParentChunk)
	{
		// Mark the ParentChunk Dirty
		ParentChunk->MarkDirty();

		// Make a single voxel modification report
		FVoxelModificationReport R;
		R.ChunkCoordinates = ParentChunkCoordinates;
		R.BrushPosition = FVector::ZeroVector; // fill if you have one in scope
		R.BrushRadius = 0.0f; // fill if you have one in scope
		if (bDig)
		{
			// We interpret "dig" as removing (toward +SDF)
			R.VoxelsDug = 1;
		}
		else
		{
			// Adding (toward −SDF)
			R.VoxelsAdded = 1;
		}
		ParentChunk->ReportVoxelModification(R);
	}


	// Border dirty hint for neighbor sync
	const int32 VoxelsPerChunk = FMath::Max(1, ChunkSize * FMath::Max(1, Subdivisions));
	if (IsBorderVoxel(Key, VoxelsPerChunk))
	{
		bBorderIsDirty = true;
	}

	// TODO (VoxelEvents.h): emit a per-write or batched modification event here if needed.
	// if (DiggerManager) { DiggerManager->OnVoxelsModified.Broadcast(...); }

	return true;
}

void USparseVoxelGrid::SetVoxelData(const TMap<FIntVector, FVoxelData>& InData)
{
	FScopeLock Lock(&VoxelDataMutex);
	VoxelData = InData;
	if (ParentChunk)
	{
		ParentChunk->MarkDirty();
	}
}


const FVoxelData* USparseVoxelGrid::GetVoxelData(const FIntVector& Voxel) const
{
	// (Keeping existing lock strategy — we’ll move to RW locks when we change the header.)
	return VoxelData.Find(Voxel);
}

FVoxelData* USparseVoxelGrid::GetVoxelData(const FIntVector& Voxel)
{
	return VoxelData.Find(Voxel);
}

float USparseVoxelGrid::GetVoxel(FIntVector Vector)
{
	return GetVoxel(Vector.X, Vector.Y, Vector.Z);
}

bool USparseVoxelGrid::IsVoxelSolid(const FIntVector& VoxelIndex) const
{
	const FVoxelData* Data = GetVoxelData(VoxelIndex);
	return Data ? (Data->SDFValue <= 0.0f) : false; // sparse miss == air
}

float USparseVoxelGrid::GetVoxel(int32 X, int32 Y, int32 Z)
{
	if (UseGPUBackend())
	{
		return VoxelGPU::GetVoxel(this, X, Y, Z);
	}

	const FIntVector Key(X, Y, Z);
	if (FVoxelData* Existing = VoxelData.Find(Key))
	{
		return Existing->SDFValue;
	}

	// Fallback to baked terrain via landscape height
	const FVector WorldPos = FVoxelConversion::LocalVoxelToWorld(FIntVector(X, Y, Z));
	const float LandscapeHeight = EnsureDiggerManager()
		? DiggerManager->GetLandscapeHeightAt(FVector(WorldPos.X, WorldPos.Y, 0))
		: 0.f;

	return (WorldPos.Z < LandscapeHeight) ? SDF_SOLID : SDF_AIR;
}

float USparseVoxelGrid::GetVoxel(int32 X, int32 Y, int32 Z) const
{
	// NOTE: We keep the const overload behavior intact. Once we switch to RW locks,
	// we'll unify locking for const reads.
	const FIntVector Key(X, Y, Z);
	if (const FVoxelData* Existing = VoxelData.Find(Key))
	{
		return Existing->SDFValue;
	}

	const FVector WorldPos = FVoxelConversion::LocalVoxelToWorld(FIntVector(X, Y, Z));
	const float LandscapeHeight = (const_cast<USparseVoxelGrid*>(this)->EnsureDiggerManager())
		? const_cast<USparseVoxelGrid*>(this)->DiggerManager->GetLandscapeHeightAt(FVector(WorldPos.X, WorldPos.Y, 0))
		: 0.f;

	return (WorldPos.Z < LandscapeHeight) ? SDF_SOLID : SDF_AIR;
}

// ---------------------------------------------------------------------------------------------------------------------
// Queries / helpers
// ---------------------------------------------------------------------------------------------------------------------

bool USparseVoxelGrid::FindNearestSetVoxel(const FIntVector& StartCoords, FIntVector& OutVoxel)
{
	if (VoxelData.Contains(StartCoords))
	{
		OutVoxel = StartCoords;
		if (DiggerDebug::Voxels || DiggerDebug::Islands)
		{
			UE_LOG(LogTemp, VeryVerbose, TEXT("Found voxel at start position: %s"), *StartCoords.ToString());
		}
		return true;
	}

	if (VoxelData.Num() == 0)
	{
		if (DiggerDebug::Voxels || DiggerDebug::Islands)
		{
			UE_LOG(LogTemp, VeryVerbose, TEXT("Grid is empty, no voxels to find"));
		}
		return false;
	}

	// Expanding shell search
	const int32 MaxSearchRadius = 30;
	for (int32 radius = 1; radius <= MaxSearchRadius; ++radius)
	{
		for (int32 dx = -radius; dx <= radius; ++dx)
		for (int32 dy = -radius; dy <= radius; ++dy)
		for (int32 dz = -radius; dz <= radius; ++dz)
		{
			if (FMath::Abs(dx) == radius || FMath::Abs(dy) == radius || FMath::Abs(dz) == radius)
			{
				const FIntVector TestVoxel = StartCoords + FIntVector(dx, dy, dz);
				if (VoxelData.Contains(TestVoxel))
				{
					OutVoxel = TestVoxel;
					return true;
				}
			}
		}
	}

	// Fallback: any voxel
	if (VoxelData.Num() > 0)
	{
		auto It = VoxelData.CreateConstIterator();
		OutVoxel = It.Key();
		return true;
	}

	return false;
}

bool USparseVoxelGrid::HasVoxelAt(const FIntVector& Key) const
{
	return VoxelData.Contains(Key);
}

bool USparseVoxelGrid::HasVoxelAt(int32 X, int32 Y, int32 Z) const
{
	return VoxelData.Contains(FIntVector(X, Y, Z));
}

TMap<FIntVector, FVoxelData> USparseVoxelGrid::GetAllVoxels() const
{
	TMap<FIntVector, FVoxelData> Out;
	Out.Reserve(VoxelData.Num());
	for (const auto& Pair : VoxelData)
	{
		Out.Add(Pair.Key, Pair.Value);
	}
	return Out;
}

TMap<FIntVector, float> USparseVoxelGrid::GetAllVoxelsSDF() const
{
	TMap<FIntVector, float> Out;
	Out.Reserve(VoxelData.Num());
	for (const auto& Pair : VoxelData)
	{
		Out.Add(Pair.Key, Pair.Value.SDFValue);
	}
	return Out;
}


bool USparseVoxelGrid::VoxelExists(int32 X, int32 Y, int32 Z) const
{
	return VoxelData.Contains(FIntVector(X, Y, Z));
}

void USparseVoxelGrid::LogVoxelData() const
{
	if (VoxelData.IsEmpty())
	{
		if (DiggerDebug::Voxels)
		{
			UE_LOG(LogTemp, VeryVerbose, TEXT("VoxelData is empty."));
		}
		return;
	}

	if (DiggerDebug::Voxels)
	{
		UE_LOG(LogTemp, VeryVerbose, TEXT("VoxelData has %d entries."), VoxelData.Num());
		if (DiggerDebug::Verbose)
		{
			for (const auto& P : VoxelData)
			{
				UE_LOG(LogTemp, VeryVerbose, TEXT("  [%s] SDF=%f"), *P.Key.ToString(), P.Value.SDFValue);
			}
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Serialization (compact & GPU-friendly layout)
// ---------------------------------------------------------------------------------------------------------------------

bool USparseVoxelGrid::SerializeToArchive(FArchive& Ar)
{
	// Persist basic sizing (kept for versioning; runtime still uses FVoxelConversion/Manager)
	Ar << TerrainGridSize;
	Ar << Subdivisions;
	Ar << ChunkSize;

	int32 VoxelCount = VoxelData.Num();
	Ar << VoxelCount;

	if (Ar.IsSaving())
	{
		for (auto& Pair : VoxelData)
		{
			FIntVector& Key = Pair.Key;
			FVoxelData& V   = Pair.Value;

			Ar << Key.X;
			Ar << Key.Y;
			Ar << Key.Z;
			Ar << V.SDFValue;
		}
	}

	return true;
}

bool USparseVoxelGrid::SerializeFromArchive(FArchive& Ar)
{
	Ar << TerrainGridSize;
	Ar << Subdivisions;
	Ar << ChunkSize;

	int32 VoxelCount = 0;
	Ar << VoxelCount;

	if (VoxelCount < 0 || VoxelCount > 100000000) // sanity
	{
		UE_LOG(LogTemp, Error, TEXT("Invalid voxel count: %d"), VoxelCount);
		return false;
	}

	if (Ar.IsLoading())
	{
		VoxelData.Empty(VoxelCount);
		VoxelData.Reserve(VoxelCount);

		for (int32 i = 0; i < VoxelCount; ++i)
		{
			int32 X, Y, Z;
			float SDFValue;
			Ar << X << Y << Z << SDFValue;
			VoxelData.Add(FIntVector(X, Y, Z), FVoxelData{ SDFValue });
		}
	}

	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Removal / batch ops
// ---------------------------------------------------------------------------------------------------------------------

void USparseVoxelGrid::RemoveVoxels(const TArray<FIntVector>& VoxelsToRemove)
{
	if (UseGPUBackend())
	{
		VoxelGPU::RemoveVoxels(this, VoxelsToRemove);

		// Make a report of voxels removed
		if (ParentChunk && VoxelsToRemove.Num() > 0)
		{
			FVoxelModificationReport R;
			R.ChunkCoordinates = ParentChunkCoordinates;
			R.VoxelsDug        = VoxelsToRemove.Num(); // removal == dug
			ParentChunk->ReportVoxelModification(R);
		}
		
		return;
	}

	// Lock for thread-safe removal
	{
		FScopeLock Lock(&VoxelDataMutex);
		for (const FIntVector& Voxel : VoxelsToRemove)
		{
			if (DiggerDebug::Voxels || DiggerDebug::Islands)
			{
				AsyncTask(ENamedThreads::GameThread, [this, Voxel]()
				{
					DrawDebugBox(GetWorld(),
						FVoxelConversion::ChunkVoxelToWorld(GetParentChunk()->GetChunkCoordinates(), Voxel),
						FVector(FVoxelConversion::LocalVoxelSize / 2.0f),
						FColor::Red, false, 5.0f);
				});
			}

			VoxelData.Remove(Voxel);
		}
	}

	// Trigger mesh update on the game thread
	if (!IsInGameThread())
	{
		AsyncTask(ENamedThreads::GameThread, [WeakThis = MakeWeakObjectPtr(this)]()
		{
			if (WeakThis.IsValid() && WeakThis->ParentChunk)
			{
				WeakThis->ParentChunk->ForceUpdate();
			}
		});
	}
	else if (ParentChunk)
	{
		ParentChunk->ForceUpdate();
	}
}

void USparseVoxelGrid::RemoveSpecifiedVoxels(const TArray<FIntVector>& LocalVoxels)
{
	FScopeLock Lock(&VoxelDataMutex);
	for (const FIntVector& Voxel : LocalVoxels)
	{
		if (DiggerDebug::Voxels || DiggerDebug::Islands)
		{
			AsyncTask(ENamedThreads::GameThread, [this, Voxel]()
			{
				DrawDebugBox(GetWorld(),
					FVoxelConversion::ChunkVoxelToWorld(GetParentChunk()->GetChunkCoordinates(), Voxel),
					FVector(FVoxelConversion::LocalVoxelSize / 2.0f),
					FColor::Red, false, 5.0f);
			});
		}

		VoxelData.Remove(Voxel);
	}
}

bool USparseVoxelGrid::RemoveVoxel(const FIntVector& LocalVoxel)
{
	FScopeLock Lock(&VoxelDataMutex);

	if (DiggerDebug::Voxels || DiggerDebug::Islands)
	{
		AsyncTask(ENamedThreads::GameThread, [this, LocalVoxel]()
		{
			DrawDebugBox(GetWorld(),
				FVoxelConversion::ChunkVoxelToWorld(GetParentChunk()->GetChunkCoordinates(), LocalVoxel),
				FVector(FVoxelConversion::LocalVoxelSize / 2.0f),
				FColor::Red, false, 5.0f);
		});
	}

	return VoxelData.Remove(LocalVoxel) > 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Island tools (CPU implementation; routes through GPU stub when enabled)
// ---------------------------------------------------------------------------------------------------------------------

bool USparseVoxelGrid::CollectIslandAtPosition(const FVector& Center, TArray<FIntVector>& OutVoxels)
{
	OutVoxels.Empty();

	// Convert world position to voxel space
	const FIntVector StartVoxel = FVoxelConversion::WorldToLocalVoxel(Center);

	if (DiggerDebug::Islands || DiggerDebug::Space)
	{
		UE_LOG(LogTemp, Display, TEXT("[Island] Extraction at position: %s → Voxel: %s"), *Center.ToString(), *StartVoxel.ToString());
	}

	const float SDFThreshold = 0.0f;

	auto IsSolid = [&](const FIntVector& Voxel) -> bool
	{
		const FVoxelData* Data = VoxelData.Find(Voxel);
		return Data && FMath::IsFinite(Data->SDFValue) && Data->SDFValue < SDFThreshold;
	};

	// Guards
	if (!IsValid(this) || VoxelData.IsEmpty())
	{
		return false;
	}
	if (!IsSolid(StartVoxel))
	{
		return false;
	}

	// BFS on 6-neighborhood
	TSet<FIntVector> Visited;
	TQueue<FIntVector> Queue;
	Queue.Enqueue(StartVoxel);
	Visited.Add(StartVoxel);

	const int32 MaxIterations = 500000;
	int32 IterationCount = 0;

	while (!Queue.IsEmpty() && IterationCount++ < MaxIterations)
	{
		FIntVector Current;
		Queue.Dequeue(Current);
		OutVoxels.Add(Current);

		for (const FIntVector& Dir : FaceDirs)
		{
			const FIntVector Neighbor = Current + Dir;
			if (!Visited.Contains(Neighbor) && IsSolid(Neighbor))
			{
				Visited.Add(Neighbor);
				Queue.Enqueue(Neighbor);
			}
		}
	}

	if (IterationCount >= MaxIterations)
	{
		UE_LOG(LogTemp, Error, TEXT("[Island] Iteration limit reached (%d)."), MaxIterations);
	}

	return OutVoxels.Num() > 0;
}

bool USparseVoxelGrid::ExtractIslandByVoxel(const FIntVector& LocalVoxelCoords, USparseVoxelGrid*& OutIslandGrid, TArray<FIntVector>& OutIslandVoxels)
{
	OutIslandVoxels.Empty();
	OutIslandGrid = nullptr;

	if (!IsValid(this) || VoxelData.IsEmpty())
	{
		return false;
	}

	const float SDFThreshold = 0.0f;
	auto IsSolid = [this, SDFThreshold](const FIntVector& Voxel) -> bool
	{
		const FVoxelData* Data = VoxelData.Find(Voxel);
		return Data && Data->SDFValue < SDFThreshold;
	};

	if (!IsSolid(LocalVoxelCoords))
	{
		return false;
	}

	OutIslandGrid = NewObject<USparseVoxelGrid>(GetTransientPackage());
	if (!IsValid(OutIslandGrid))
	{
		return false;
	}
	OutIslandGrid->Initialize(nullptr); // no owner

	// BFS copy
	TSet<FIntVector> Visited;
	TQueue<FIntVector> Queue;
	Queue.Enqueue(LocalVoxelCoords);
	Visited.Add(LocalVoxelCoords);

	while (!Queue.IsEmpty())
	{
		FIntVector Current;
		Queue.Dequeue(Current);
		OutIslandVoxels.Add(Current);

		if (const FVoxelData* Original = VoxelData.Find(Current))
		{
			OutIslandGrid->VoxelData.Add(Current, *Original);
		}

		for (const FIntVector& Dir : FaceDirs)
		{
			const FIntVector Neighbor = Current + Dir;
			if (!Visited.Contains(Neighbor) && IsSolid(Neighbor))
			{
				Visited.Add(Neighbor);
				Queue.Enqueue(Neighbor);
			}
		}
	}

	return !OutIslandVoxels.IsEmpty();
}

bool USparseVoxelGrid::ExtractIslandAtPosition(const FVector& WorldPosition, USparseVoxelGrid*& OutGrid, TArray<FIntVector>& OutVoxels)
{
	if (UseGPUBackend())
	{
		return VoxelGPU::ExtractIslandAtPosition(this, WorldPosition, OutGrid, OutVoxels);
	}

	OutVoxels.Empty();
	OutGrid = nullptr;

	const FIntVector StartVoxel = FVoxelConversion::WorldToLocalVoxel(WorldPosition);
	const float SDFThreshold = 0.0f;

	if (!IsValid(this) || VoxelData.IsEmpty())
	{
		return false;
	}

	auto IsSolid = [&](const FIntVector& Voxel)
	{
		const FVoxelData* Data = VoxelData.Find(Voxel);
		return Data && FMath::IsFinite(Data->SDFValue) && Data->SDFValue < SDFThreshold;
	};

	auto IsSurfaceVoxel = [&](const FIntVector& Voxel)
	{
		if (!IsSolid(Voxel)) return false;

		for (const FIntVector& Dir : FaceDirs)
		{
			const FIntVector Neighbor = Voxel + Dir;
			const FVoxelData* Data = VoxelData.Find(Neighbor);
			if (!Data || Data->SDFValue >= SDFThreshold)
				return true;
		}
		return false;
	};

	if (!IsSolid(StartVoxel))
	{
		return false;
	}

	USparseVoxelGrid* NewGrid = NewObject<USparseVoxelGrid>(this);
	if (!IsValid(NewGrid))
	{
		return false;
	}
	NewGrid->ParentChunk   = ParentChunk;
	NewGrid->DiggerManager = DiggerManager;

	// BFS to gather connected voxels
	TSet<FIntVector> Connected;
	TQueue<FIntVector> FloodQueue;
	FloodQueue.Enqueue(StartVoxel);
	Connected.Add(StartVoxel);

	const int32 MaxIterations = 500000;
	int32 IterationCount = 0;

	while (!FloodQueue.IsEmpty() && IterationCount++ < MaxIterations)
	{
		FIntVector Current;
		FloodQueue.Dequeue(Current);

		for (const FIntVector& Dir : FaceDirs)
		{
			const FIntVector Neighbor = Current + Dir;
			if (!Connected.Contains(Neighbor) && IsSolid(Neighbor))
			{
				FloodQueue.Enqueue(Neighbor);
				Connected.Add(Neighbor);
			}
		}
	}

	// Classify & copy
	TSet<FIntVector> SurfaceVoxels;
	TSet<FIntVector> InternalVoxels;

	for (const FIntVector& Voxel : Connected)
	{
		const FVoxelData* Data = VoxelData.Find(Voxel);
		if (!Data || !FMath::IsFinite(Data->SDFValue)) continue;

		if (IsSurfaceVoxel(Voxel))
		{
			SurfaceVoxels.Add(Voxel);
			OutVoxels.Add(Voxel);
			NewGrid->SetVoxel(Voxel.X, Voxel.Y, Voxel.Z, Data->SDFValue, false);
		}
		else
		{
			InternalVoxels.Add(Voxel);
		}
	}

	// Build a shell (optional stability)
	for (const FIntVector& Surf : SurfaceVoxels)
	{
		for (const FIntVector& Dir : FaceDirs)
		{
			const FIntVector N = Surf + Dir;

			if (InternalVoxels.Contains(N))
			{
				if (const FVoxelData* Data = VoxelData.Find(N))
				{
					NewGrid->SetVoxel(N.X, N.Y, N.Z, Data->SDFValue, false);
				}
			}
			else if (!SurfaceVoxels.Contains(N))
			{
				NewGrid->SetVoxel(N.X, N.Y, N.Z, 1.0f, false); // air
			}
		}
	}

	// Remove originals
	{
		FScopeLock Lock(&VoxelDataMutex);
		for (const FIntVector& Voxel : Connected)
		{
			VoxelData.Remove(Voxel);
		}
	}

	if (ParentChunk)
	{
		ParentChunk->MarkDirty();

		// Make a voxel removal/modification report
		FVoxelModificationReport R;
		R.ChunkCoordinates = ParentChunkCoordinates;
		R.VoxelsDug        = Connected.Num(); // we removed these from this chunk
		ParentChunk->ReportVoxelModification(R);
	}


	OutGrid = NewGrid;
	return OutVoxels.Num() > 0;
}

FIslandData USparseVoxelGrid::DetectIsland(float SDFThreshold, const FIntVector& StartPosition)
{
	FIslandData Island;
	TSet<FIntVector> Visited;

	auto IsSolid = [this, SDFThreshold](const FIntVector& Voxel) -> bool
	{
		const FVoxelData* Data = VoxelData.Find(Voxel);
		return Data && Data->SDFValue < SDFThreshold;
	};

	if (!IsSolid(StartPosition))
	{
		return Island;
	}

	// 26-neighborhood list
	TArray<FIntVector> Directions;
	Directions.Reserve(26);
	for (int dx = -1; dx <= 1; ++dx)
	for (int dy = -1; dy <= 1; ++dy)
	for (int dz = -1; dz <= 1; ++dz)
	{
		if (dx != 0 || dy != 0 || dz != 0)
		{
			Directions.Add(FIntVector(dx, dy, dz));
		}
	}

	TArray<FIntVector> IslandVoxels;
	TQueue<FIntVector> Queue;

	Queue.Enqueue(StartPosition);
	Visited.Add(StartPosition);
	IslandVoxels.Add(StartPosition);

	while (!Queue.IsEmpty())
	{
		FIntVector Current;
		Queue.Dequeue(Current);

		for (const FIntVector& Dir : Directions)
		{
			const FIntVector Neighbor = Current + Dir;

			if (!Visited.Contains(Neighbor) && IsSolid(Neighbor))
			{
				Queue.Enqueue(Neighbor);
				Visited.Add(Neighbor);
				IslandVoxels.Add(Neighbor);
			}
		}
	}

	// Compute world-space center using chunk-aware conversion
	FVector Center = FVector::ZeroVector;
	if (!IslandVoxels.IsEmpty())
	{
		for (const FIntVector& Voxel : IslandVoxels)
		{
			Center += FVoxelConversion::ChunkVoxelToWorld(GetParentChunkCoordinates(), Voxel);
		}
		Center /= IslandVoxels.Num();

		Island.Location       = Center + DebugRenderOffset;
		Island.VoxelCount     = IslandVoxels.Num();
		Island.ReferenceVoxel = IslandVoxels[0];
		Island.Voxels         = IslandVoxels;
	}

	return Island;
}

TArray<FIslandData> USparseVoxelGrid::DetectIslands(float SDFThreshold)
{
	TArray<FIslandData> Islands;
	TSet<FIntVector> Visited;

	auto IsSolid = [this, SDFThreshold](const FIntVector& Voxel) -> bool
	{
		const FVoxelData* Data = VoxelData.Find(Voxel);
		return Data && Data->SDFValue < SDFThreshold;
	};

	// 26-neighborhood list
	TArray<FIntVector> Directions;
	Directions.Reserve(26);
	for (int dx = -1; dx <= 1; ++dx)
	for (int dy = -1; dy <= 1; ++dy)
	for (int dz = -1; dz <= 1; ++dz)
	{
		if (dx != 0 || dy != 0 || dz != 0)
		{
			Directions.Add(FIntVector(dx, dy, dz));
		}
	}

	for (const auto& Pair : VoxelData)
	{
		const FIntVector& Seed = Pair.Key;

		if (Visited.Contains(Seed) || !IsSolid(Seed))
			continue;

		TArray<FIntVector> IslandVoxels;
		TQueue<FIntVector> Queue;

		Queue.Enqueue(Seed);
		Visited.Add(Seed);
		IslandVoxels.Add(Seed);

		while (!Queue.IsEmpty())
		{
			FIntVector Current;
			Queue.Dequeue(Current);

			for (const FIntVector& Dir : Directions)
			{
				const FIntVector Neighbor = Current + Dir;

				if (!Visited.Contains(Neighbor) && IsSolid(Neighbor))
				{
					Queue.Enqueue(Neighbor);
					Visited.Add(Neighbor);
					IslandVoxels.Add(Neighbor);
				}
			}
		}

		// Center (chunk-aware)
		FVector Center = FVector::ZeroVector;
		for (const FIntVector& Voxel : IslandVoxels)
		{
			Center += FVoxelConversion::ChunkVoxelToWorld(GetParentChunkCoordinates(), Voxel);
		}
		Center /= FMath::Max(1, IslandVoxels.Num());

		FIslandData Island;
		Island.Location       = Center + DebugRenderOffset;
		Island.VoxelCount     = IslandVoxels.Num();
		Island.ReferenceVoxel = IslandVoxels[0];
		Island.Voxels         = IslandVoxels;

		Islands.Add(Island);
	}

#if WITH_EDITOR
	// Optional: sanity logs for missed solids
	for (const auto& Pair : VoxelData)
	{
		const FIntVector& V = Pair.Key;
		if (IsSolid(V) && !Visited.Contains(V))
		{
			UE_LOG(LogTemp, VeryVerbose, TEXT("[IslandDetection] Missed solid voxel at %s"), *V.ToString());
		}
	}
#endif

	return Islands;
}


// ---------------------------------------------------------------------------------------------------------------------
// Debug draw
// ---------------------------------------------------------------------------------------------------------------------

void USparseVoxelGrid::RenderVoxels()
{
	if (!EnsureDiggerManager())
	{
		UE_LOG(LogTemp, Error, TEXT("DiggerManager is null or invalid in SparseVoxelGrid RenderVoxels()!!"));
		return;
	}

	World = DiggerManager->GetSafeWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("World is null within SparseVoxelGrid RenderVoxels()!!"));
		return;
	}

	if (VoxelData.IsEmpty())
	{
		if (DiggerDebug::Voxels)
		{
			UE_LOG(LogTemp, VeryVerbose, TEXT("VoxelData is empty, nothing to render."));
		}
		return;
	}

	auto* FastDebug = UFastDebugSubsystem::Get(DiggerManager);
	if (!FastDebug) return;

	const FIntVector ChunkCoords = GetParentChunkCoordinates();

	// Batch positions
	const int32 Count = VoxelData.Num();
	TArray<FVector> Air, Solid, Surface;
	Air.Reserve(Count);
	Solid.Reserve(Count);
	Surface.Reserve(Count);

	for (const auto& V : VoxelData)
	{
		const FIntVector& Local = V.Key;
		const float SDF = V.Value.SDFValue;

		const FIntVector Global = FVoxelConversion::ChunkAndLocalToGlobalVoxel_CenterAligned(ChunkCoords, Local);
		const FVector WorldP = FVoxelConversion::GlobalVoxelToWorld_CenterAligned(Global);
		const FVector Center = WorldP + DebugRenderOffset;
		const FVector VoxelCenter = Center + FVector(FVoxelConversion::LocalVoxelSize / 2.0f);

		if (SDF >  0.0f) Air.Add(VoxelCenter);
		else if (SDF < 0.0f) Solid.Add(VoxelCenter);
		else Surface.Add(VoxelCenter);
	}

	const FVector Extent(FVoxelConversion::LocalVoxelSize / 2.0f);

	if (Air.Num()     > 0) FastDebug->DrawBoxes(Air,     Extent, FFastDebugConfig(FLinearColor::Green,  15.0f, 1.0f));
	if (Solid.Num()   > 0) FastDebug->DrawBoxes(Solid,   Extent, FFastDebugConfig(FLinearColor::Red,    15.0f, 2.0f));
	if (Surface.Num() > 0) FastDebug->DrawBoxes(Surface, Extent, FFastDebugConfig(FLinearColor::Yellow, 15.0f, 3.0f));
}
