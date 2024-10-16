#pragma once

#include "VoxelChunk.generated.h"

class ADiggerManager;
class USparseVoxelGrid;
class UMarchingCubes;

UCLASS()
class DIGGERPROUNREAL_API UVoxelChunk : public UObject
{
	GENERATED_BODY()

public:
	UVoxelChunk();

	FIntVector WorldToChunkCoordinates(const FVector& WorldCoords) const;
	FVector ChunkToWorldCoordinates(const FVector& ChunkCoords) const;
	void SetUniqueSectionIndex();

	// Initializes the chunk
	void InitializeChunk(const FIntVector& InChunkCoordinates);

	// Debug function to visualize chunk boundaries
	void DebugDrawChunk() const;
	void DebugPrintVoxelData() const;

	void MarkDirty();
	void UpdateIfDirty();
	void ForceUpdate();

	bool IsValidChunkLocalCoordinate(FVector Position) const;
	FVector GetWorldPosition(const FIntVector& VoxelCoordinates) const;
	FVector GetWorldPosition() const;
	bool IsValidChunkLocalCoordinate(int32 LocalX, int32 LocalY, int32 LocalZ) const;
	FVector VoxelToWorldCoordinates(const FIntVector& VoxelCoords) const;
	FIntVector WorldToVoxelCoordinates(const FVector& WorldCoords) const;
	// Sets a voxel's SDF value in the chunk
	void SetVoxel(int32 X, int32 Y, int32 Z, float SDFValue) const;
	void SetVoxel(const FVector& Position, float SDFValue) const;

	// Retrieves voxel data from the sparse voxel grid
	float GetVoxel(const FVector& Position) const;
	float GetVoxel(int32 X, int32 Y, int32 Z) const;
	USparseVoxelGrid* GetSparseVoxelGrid() const;
	int16& GetVoxelSize();

	//Retrieve information from SparseVoxelGrid
	TMap<FVector, float> GetActiveVoxels() const;

	// Generates mesh data by delegating to the MarchingCubes class
	void GenerateMesh() const;
	void GenerateMeshAsync();
	FIntVector WorldToChunkSpace(const FVector& WorldPosition) const;
	FVector ChunkToWorldSpace(const FVector& ChunkCoords) const;

private:
	FIntVector ChunkCoordinates;

public:
	[[nodiscard]] FIntVector GetChunkPosition() const
	{
		return ChunkCoordinates;
	}

	[[nodiscard]] int32 GetChunkSize() const
	{
		return ChunkSize;
	}
	
	[[nodiscard]] ADiggerManager* GetDiggerManager() const
	{
		return DiggerManager;
	}

private:
	int32 ChunkSize;
	int16 TerrainGridSize;  // Default size of 1 meter
	int8 Subdivisions;
	int16 VoxelSize;
	int16 SectionIndex;

public:
	[[nodiscard]] int32 GetSectionIndex() const
	{
		return SectionIndex;
	}

private:
	//DiggerManager Reference
	UPROPERTY()
	ADiggerManager* DiggerManager;

	// Sparse voxel grid reference
	UPROPERTY()
	USparseVoxelGrid* SparseVoxelGrid;

	// Marching Cubes generator instance
	UPROPERTY()
	UMarchingCubes* MarchingCubesGenerator;

	//Dirty flag to update the chunk
	bool bIsDirty;

public:
	[[nodiscard]] bool IsDirty() const
	{
		return bIsDirty;
	}
};
