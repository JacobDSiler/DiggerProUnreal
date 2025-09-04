// SparseVoxelGrid.h
// Header that matches the CPU-first/GPU-ready SparseVoxelGrid.cpp you provided.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "HAL/CriticalSection.h"
#include "Containers/Queue.h"
#include "FVoxelData.h"
#include "IslandData.h"
#include "SparseVoxelGrid.generated.h"

// Forward declarations
class ADiggerManager;
class UVoxelChunk;
class UWorld;


UCLASS(BlueprintType)
class DIGGERPROUNREAL_API USparseVoxelGrid : public UObject
{
	GENERATED_BODY()

public:
	// ------------------------------------------------------------------------------------------
	// Lifecycle
	// ------------------------------------------------------------------------------------------
	USparseVoxelGrid();

	/** Initialize grid with parent chunk ref (may be null for standalone use). */
	void Initialize(UVoxelChunk* ParentChunkReference);

	/** Pull runtime values (ChunkSize, TerrainGridSize, Subdivisions) from DiggerManager. */
	void InitializeDiggerManager();

	/** Ensure DiggerManager pointer is valid. */
	bool EnsureDiggerManager();

	/** Safe world getter (works in editor/PIE). */
	UWorld* GetSafeWorld() const;

	// ------------------------------------------------------------------------------------------
	// Landscape helpers
	// ------------------------------------------------------------------------------------------
	bool IsPointAboveLandscape(FVector& Point);
	float GetLandscapeHeightAtPoint(FVector Position);

	// ------------------------------------------------------------------------------------------
	// Core voxel access / mutation
	// ------------------------------------------------------------------------------------------
	bool SetVoxel(FIntVector Position, float SDFValue, bool bDig);
	bool SetVoxel(int32 X, int32 Y, int32 Z, float SDFValue, bool bDig);
	

	void SetVoxelData(const TMap<FIntVector, FVoxelData>& InData);
	const FVoxelData* GetVoxelData(const FIntVector& Voxel) const;
	FVoxelData*       GetVoxelData(const FIntVector& Voxel);

	float GetVoxel(FIntVector Vector);
	float GetVoxel(int32 X, int32 Y, int32 Z);
	float GetVoxel(int32 X, int32 Y, int32 Z) const;

	bool  IsVoxelSolid(const FIntVector& VoxelIndex) const;
	bool  VoxelExists(int32 X, int32 Y, int32 Z) const;

	// ------------------------------------------------------------------------------------------
	// Queries / helpers
	// ------------------------------------------------------------------------------------------
	bool FindNearestSetVoxel(const FIntVector& StartCoords, FIntVector& OutVoxel);

	bool HasVoxelAt(const FIntVector& Key) const;
	bool HasVoxelAt(int32 X, int32 Y, int32 Z) const;
	
	TMap<FIntVector, FVoxelData> GetAllVoxels() const;
	TMap<FIntVector, float> GetAllVoxelsSDF() const;


	void LogVoxelData() const;

	// ------------------------------------------------------------------------------------------
	// Serialization
	// ------------------------------------------------------------------------------------------
	bool SerializeToArchive(FArchive& Ar);
	bool SerializeFromArchive(FArchive& Ar);

	// ------------------------------------------------------------------------------------------
	// Removal / batch ops
	// ------------------------------------------------------------------------------------------
	void RemoveVoxels(const TArray<FIntVector>& VoxelsToRemove);
	void RemoveSpecifiedVoxels(const TArray<FIntVector>& LocalVoxels);
	bool RemoveVoxel(const FIntVector& LocalVoxel);

	// ------------------------------------------------------------------------------------------
	// Island tools
	// ------------------------------------------------------------------------------------------
	bool CollectIslandAtPosition(const FVector& Center, TArray<FIntVector>& OutVoxels);

	bool ExtractIslandByVoxel(const FIntVector& LocalVoxelCoords,
		USparseVoxelGrid*& OutIslandGrid,
		TArray<FIntVector>& OutIslandVoxels);

	bool ExtractIslandAtPosition(const FVector& WorldPosition,
		USparseVoxelGrid*& OutGrid,
		TArray<FIntVector>& OutVoxels);

	FIslandData          DetectIsland(float SDFThreshold, const FIntVector& StartPosition);
	TArray<FIslandData>  DetectIslands(float SDFThreshold);

	// ------------------------------------------------------------------------------------------
	// Debug draw
	// ------------------------------------------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category="Voxel|Debug")
	void RenderVoxels();

	// ------------------------------------------------------------------------------------------
	// Accessors
	// ------------------------------------------------------------------------------------------
	FORCEINLINE UVoxelChunk* GetParentChunk() const { return ParentChunk; }
	FORCEINLINE FIntVector   GetParentChunkCoordinates() const { return ParentChunkCoordinates; }

public:
	// Context
	UPROPERTY() ADiggerManager* DiggerManager = nullptr;
	UPROPERTY() UVoxelChunk*    ParentChunk   = nullptr;
	UPROPERTY() UWorld*         World         = nullptr;

	// Grid settings (cached for convenience)
	UPROPERTY() FIntVector ParentChunkCoordinates = FIntVector::ZeroValue;
	UPROPERTY() int32      TerrainGridSize = 0;
	UPROPERTY() int32      Subdivisions    = 0;
	UPROPERTY() int32      ChunkSize       = 0;

	// Sparse CPU storage
	TMap<FIntVector, FVoxelData> VoxelData;

	// Debug
	UPROPERTY(EditAnywhere, Category="Voxel|Debug")
	FVector DebugRenderOffset = FVector::ZeroVector;

	// Neighbor sync hint
	UPROPERTY(VisibleAnywhere, Category="Voxel")
	bool bBorderIsDirty = false;

private:
	// Thread safety for CPU map (kept private; cpp uses FScopeLock on this)
	mutable FCriticalSection VoxelDataMutex;
};
