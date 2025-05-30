// In a header, e.g. VoxelBrushHelpers.h
#pragma once

#include "VoxelConversion.h"
#include "SparseVoxelGrid.h" // or wherever USparseVoxelGrid is defined

// Call this after setting air voxels for a dig brush
inline void SetDigShellVoxels(
    USparseVoxelGrid* Grid,
    const FVector& BrushCenter,
    float Radius,
    float Falloff,
    float VoxelSize,
    TFunction<float(const FVector&)> GetTerrainHeightAt
)
{
    float OuterRadius = Radius + Falloff;
    float ShellInner = OuterRadius;
    float ShellOuter = OuterRadius + VoxelSize * 1.5f;

    int MinX = FMath::FloorToInt((BrushCenter.X - ShellOuter) / VoxelSize);
    int MaxX = FMath::CeilToInt((BrushCenter.X + ShellOuter) / VoxelSize);
    int MinY = FMath::FloorToInt((BrushCenter.Y - ShellOuter) / VoxelSize);
    int MaxY = FMath::CeilToInt((BrushCenter.Y + ShellOuter) / VoxelSize);
    int MinZ = FMath::FloorToInt((BrushCenter.Z - ShellOuter) / VoxelSize);
    int MaxZ = FMath::CeilToInt((BrushCenter.Z + ShellOuter) / VoxelSize);

    for (int x = MinX; x <= MaxX; ++x)
    for (int y = MinY; y <= MaxY; ++y)
    {
        // Cache terrain height for this column (x, y)
        FVector XYWorld(x * VoxelSize, y * VoxelSize, 0);
        float TerrainHeight = GetTerrainHeightAt(XYWorld);

        for (int z = MinZ; z <= MaxZ; ++z)
        {
            FVector WorldPos(x * VoxelSize, y * VoxelSize, z * VoxelSize);
            float Dist = FVector::Dist(WorldPos, BrushCenter);

            if (Dist >= ShellInner && Dist <= ShellOuter)
            {
                // Only set solid if this voxel is strictly below the terrain at this XY
                if (WorldPos.Z < TerrainHeight)
                {
                    FIntVector VoxelCoords(x, y, z);
                    if (!Grid->VoxelData.Contains(VoxelCoords))
                    {
                        Grid->SetVoxel(x, y, z, FVoxelConversion::SDF_SOLID, false);
                    }
                }
            }
        }
    }
}



inline void SetDigShellVoxels_Generic(
    USparseVoxelGrid* Grid,
    const FBox& BrushBounds, // World-space bounds of the brush
    float VoxelSize,
    TFunction<float(const FVector&)> GetTerrainHeightAt,
    TFunction<bool(const FVector&)> IsAir // Returns true if this world pos is air (inside brush)
)
{
    FIntVector Min = FIntVector(
        FMath::FloorToInt(BrushBounds.Min.X / VoxelSize),
        FMath::FloorToInt(BrushBounds.Min.Y / VoxelSize),
        FMath::FloorToInt(BrushBounds.Min.Z / VoxelSize)
    );
    FIntVector Max = FIntVector(
        FMath::CeilToInt(BrushBounds.Max.X / VoxelSize),
        FMath::CeilToInt(BrushBounds.Max.Y / VoxelSize),
        FMath::CeilToInt(BrushBounds.Max.Z / VoxelSize)
    );

    // 6-way neighbor offsets
    static const FIntVector NeighborOffsets[6] = {
        FIntVector(1,0,0), FIntVector(-1,0,0),
        FIntVector(0,1,0), FIntVector(0,-1,0),
        FIntVector(0,0,1), FIntVector(0,0,-1)
    };

    for (int x = Min.X; x <= Max.X; ++x)
    for (int y = Min.Y; y <= Max.Y; ++y)
    for (int z = Min.Z; z <= Max.Z; ++z)
    {
        FVector WorldPos(x * VoxelSize, y * VoxelSize, z * VoxelSize);

        // Only consider voxels below the terrain
        float TerrainHeight = GetTerrainHeightAt(WorldPos);
        if (WorldPos.Z >= TerrainHeight)
            continue;

        // Skip if this voxel is already air (set by the brush)
        if (IsAir(WorldPos))
            continue;

        // Check if any neighbor is air
        bool bAdjacentToAir = false;
        for (const FIntVector& Offset : NeighborOffsets)
        {
            FVector NeighborPos = WorldPos + FVector(Offset) * VoxelSize;
            if (IsAir(NeighborPos))
            {
                bAdjacentToAir = true;
                break;
            }
        }

        if (bAdjacentToAir)
        {
            FIntVector VoxelCoords(x, y, z);
            if (!Grid->VoxelData.Contains(VoxelCoords))
            {
                Grid->SetVoxel(x, y, z, FVoxelConversion::SDF_SOLID, false);
            }
        }
    }
}

