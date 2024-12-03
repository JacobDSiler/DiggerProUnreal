#pragma once

#include "CoreMinimal.h"
#include "DiggerManager.h"
#include "SparseVoxelGrid.generated.h"

class ADiggerManager;
// Forward declaration
class UVoxelChunk;

// Struct to hold SDF value and possibly other data like voxel material, etc.
USTRUCT()
struct FVoxelData
{
	GENERATED_BODY()

	// SDF value for marching cubes (negative inside, positive outside, zero on the surface)
	float SDFValue;

	// Optionally, additional data like material or color can be stored here
	// Example: int32 MaterialID;

	FVoxelData() : SDFValue(0.0f) {}
	FVoxelData(float InSDFValue) : SDFValue(InSDFValue) {}
};

UCLASS()
class DIGGERPROUNREAL_API USparseVoxelGrid : public UObject
{
	GENERATED_BODY()

public:
	USparseVoxelGrid();  // Add this line to declare the constructor

	void Initialize(UVoxelChunk* ParentChunkReference);
	void InitializeDiggerManager();
	bool EnsureDiggerManager();
	FIntVector WorldToVoxelSpace(const FVector& WorldCoords);
	bool IsPointAboveLandscape(FVector& Point);
	UFUNCTION(BlueprintCallable)
	float GetLandscapeHeightAtPoint(FVector Position);
	FVector VoxelToWorldSpace(const FIntVector& VoxelCoords);

	//A public getter for VoxelData
	const TMap<FIntVector, FVoxelData>& GetVoxelData() const { return VoxelData; }
	
	// Adds a voxel at the given coordinates with the provided SDF value
	void SetVoxel(FIntVector Position, float SDFValue, bool& bDig);
	void SetVoxel(int32 X, int32 Y, int32 Z, float NewSDFValue, bool& bDig);

	// Retrieves the voxel's SDF value; returns true if the voxel exists
	float GetVoxel(int32 X, int32 Y, int32 Z) const;

	// Method to get all voxel data
	TMap<FVector, float> GetVoxels() const;
	
	// Checks if a voxel exists at the given coordinates
	bool VoxelExists(int32 X, int32 Y, int32 Z) const;
	
	// Logs the current voxels and their SDF values
	void LogVoxelData() const;
	void RenderVoxels();
	

	void SetParentChunkCoordinates(FIntVector& NewParentChunkPosition)
	{
		this->ParentChunkCoordinates = NewParentChunkPosition;
	}
	
	
	FVector3d GetParentChunkCoordinatesV3D() const
	{
		float X=ParentChunkCoordinates.X;
		float Y=ParentChunkCoordinates.Y;
		float Z=ParentChunkCoordinates.Z;
		return FVector3d(X,Y,Z);
	}
	
	FIntVector GetParentChunkCoordinates() const
	{
		return ParentChunkCoordinates;
	}
	
	// The sparse voxel data map, keyed by 3D coordinates, storing FVoxelData
	TMap<FIntVector, FVoxelData> VoxelData;

	
private:
	//Baked SDF for BaseSDF values for after the undo queue brush strokes fall out the end of the queue and get baked.
	TMap<FIntVector, FVoxelData> BakedSDF;
	
	void ApplyBakedSDF();
	
	int32 ChunkSize = 32;  // Number of subdivisions per grid size

	//The VoxelSize which will be set to the VoxelSize within DiggerManager during InitializeDiggerManager;
	int LocalVoxelSize;
	
	//Reference to the DiggerManager
	UPROPERTY()
	ADiggerManager* DiggerManager;
	
	// Declare but don't initialize here
	UPROPERTY()
	UVoxelChunk* ParentChunk;

	UPROPERTY()
	UWorld* World;

public:
	[[nodiscard]] ADiggerManager* GetDiggerManager() const
	{
		return DiggerManager;
	}

private:
	FIntVector WorldToChunkSpace(float x, float y, float z) const
	{/* Calculate chunk space coordinates*/ return FIntVector(x, y, z) / (ChunkSize * TerrainGridSize);}

public:
	[[nodiscard]] UVoxelChunk* GetParentChunk() const
	{
		return ParentChunk;
	}

private:
	UPROPERTY()
	FIntVector ParentChunkCoordinates;
	
	int TerrainGridSize;
	int Subdivisions;
};
