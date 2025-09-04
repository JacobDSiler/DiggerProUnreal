// FCustomSDFBrush.h

#pragma once

#include "CoreMinimal.h"

struct FCustomSDFBrush
{
    FIntVector Dimensions;      // Number of voxels in X, Y, Z
    float VoxelSize;            // Size of each voxel
    FVector OriginOffset;       // World-space position of the grid's minimum corner
    TArray<float> SDFValues;    // Flattened 3D array: X + Y*DimX + Z*DimX*DimY

    FCustomSDFBrush()
        : Dimensions(0,0,0), VoxelSize(1.0f), OriginOffset(FVector::ZeroVector)
    {}

    FORCEINLINE int32 GetIndex(int32 X, int32 Y, int32 Z) const
    {
        return X + Y * Dimensions.X + Z * Dimensions.X * Dimensions.Y;
    }
};

