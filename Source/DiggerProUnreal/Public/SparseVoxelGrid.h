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
    USparseVoxelGrid();

    void Initialize(UVoxelChunk* ParentChunkReference);
    void InitializeDiggerManager();
    bool EnsureDiggerManager();
    
    // Changed to use double precision math
    FIntVector WorldToVoxelSpace(const FVector3d& WorldCoords);
    
    bool IsPointAboveLandscape(FVector3d& Point);
    
    //UFUNCTION(BlueprintCallable)
    double GetLandscapeHeightAtPoint(FVector3d Position);
    
    // Changed to return FVector3d
    FVector3d VoxelToWorldSpace(const FIntVector& VoxelCoords);

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
        double X=ParentChunkCoordinates.X;
        double Y=ParentChunkCoordinates.Y;
        double Z=ParentChunkCoordinates.Z;
        return FVector3d(X,Y,Z);
    }

    FIntVector GetParentChunkCoordinates() const
    {
        return ParentChunkCoordinates;
    }

    // The sparse voxel data map, keyed by 3D coordinates, storing FVoxelData
    TMap<FIntVector, FVoxelData> VoxelData;


private:

    int32 ChunkSize = 32;  // Number of subdivisions per grid size

    //This is the voxel size in voxel space units
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
    FIntVector WorldToChunkSpace(double x, double y, double z) const
    {/* Calculate chunk space coordinates*/ return FIntVector(x, y, z) / (ChunkSize * TerrainGridSize);}

public:
    [[nodiscard]] UVoxelChunk* GetParentChunk() const
    {
        return ParentChunk;
    }

private:
    UPROPERTY()
    FIntVector ParentChunkCoordinates;

    // Changed from int to double as per requirements
    int TerrainGridSize;
    
    int Subdivisions; // Kept as int as per requirements
};
