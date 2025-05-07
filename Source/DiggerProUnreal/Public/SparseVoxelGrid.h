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

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnIslandDetected, const FIslandData&, Island);

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
	FVector VoxelToWorldSpace(const FIntVector& VoxelCoords) const;

	void RemoveVoxels(const TArray<FIntVector>& VoxelsToRemove);

	// Save current voxel data to disk
	bool SaveVoxelDataToFile(const FString& FilePath);

	// Load voxel data from disk into the current grid
	bool LoadVoxelDataFromFile(const FString& FilePath);

	// Delegate to broadcast when a new island is detected
	UPROPERTY(BlueprintAssignable, Category = "Island Detection")
	FOnIslandDetected OnIslandDetected;


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

	bool CollectIslandAtPosition(const FVector& Center, TArray<FIntVector>& OutVoxels);
	

	void SetParentChunkCoordinates(FIntVector& NewParentChunkPosition)
	{
		this->ParentChunkCoordinates = NewParentChunkPosition;
	}
	
	TArray<FIslandData> DetectIslands(float SDFThreshold /* usually 0.0f */) const;
	
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
	
	int32 ChunkSize = 32;  // Number of subdivisions per grid size


private:
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
