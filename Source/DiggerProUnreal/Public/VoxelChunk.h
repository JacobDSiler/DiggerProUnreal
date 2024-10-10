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
	void SetUniqueSectionIndex();

	// Initializes the chunk
	void InitializeChunk(const FIntVector& InChunkCoordinates);

	// Debug function to visualize chunk boundaries
	void DebugDrawChunk() const;

	void MarkDirty();
	void UpdateIfDirty();
	void ForceUpdate();

	// Function to create a sphere-shaped voxel grid
	void CreateSphereVoxelGrid(float Radius) const;

	bool IsValidLocalCoordinate(int32 LocalX, int32 LocalY, int32 LocalZ) const;
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

	// Define the chunk size as a public member
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chunk Settings")
	int32 ChunkSize = 64;

private:
	FIntVector ChunkCoordinates;
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
