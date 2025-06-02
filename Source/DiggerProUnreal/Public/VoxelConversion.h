#pragma once

#include "CoreMinimal.h"

class DiggerManager;

struct FVoxelConversion
{
    // Configurable parameters
    static int32 ChunkSize;        // Number of grid squares per chunk (e.g., 8)
    static int32 Subdivisions;     // Voxels per grid square (e.g., 4)
    static float TerrainGridSize;  // Grid square size in world units (e.g., 100)
    static float LocalVoxelSize;   // Computed: TerrainGridSize / Subdivisions
    static FVector Origin;         // World origin of the voxel grid

    static constexpr float SDF_SOLID = -1.0f;
    static constexpr float SDF_AIR = 1.0f;
    static constexpr int DUG_HOLE_SHELL_VOXEL_THICKNESS = 3;

    // Chunk coordinate → world position of chunk center
    static FVector ChunkToWorld(const FIntVector& ChunkCoords)
    {
        // Calculate chunk center using the same logic as DebugDrawChunk
        FVector ChunkCenter = Origin + FVector(ChunkCoords) * ChunkSize * TerrainGridSize;
        
        UE_LOG(LogTemp, Verbose, TEXT("[ChunkToWorld] ChunkCoords: %s, ChunkCenter: %s"),
            *ChunkCoords.ToString(), *ChunkCenter.ToString());
        
        return ChunkCenter;
    }


    
    // World position → chunk coordinate with precise boundary safety
    static FIntVector WorldToChunk(const FVector& WorldPos)
    {
        const float ChunkWorldSize = ChunkSize * TerrainGridSize;
        
        // Localized position relative to grid origin
        const FVector LocalizedPos = WorldPos - Origin;
        
        // Divide by chunk world size to get chunk coordinates
        const int32 X = FMath::RoundToInt(LocalizedPos.X / ChunkWorldSize);
        const int32 Y = FMath::RoundToInt(LocalizedPos.Y / ChunkWorldSize);
        const int32 Z = FMath::RoundToInt(LocalizedPos.Z / ChunkWorldSize);
        
        const FIntVector ChunkCoords(X, Y, Z);
        
        // Calculate chunk center for logging
        const FVector ChunkCenter = Origin + FVector(ChunkCoords) * ChunkWorldSize;
        
        UE_LOG(LogTemp, Warning, TEXT("[WorldToChunk] WorldPos: %s, LocalizedPos: %s, ChunkCoords: %s, ChunkCenter: %s"),
            *WorldPos.ToString(), *LocalizedPos.ToString(), *ChunkCoords.ToString(), *ChunkCenter.ToString());
        
        return ChunkCoords;
    }



// Add these methods to your FVoxelConversion struct

// Validates if a voxel index is within valid range (including overflow regions)
static bool IsValidVoxelIndex(const FIntVector& VoxelIndex)
{
    // Calculate the valid range including overflow
    const int32 VoxelsPerChunk = ChunkSize * Subdivisions;
    
    // Allow 1 voxel overflow on the negative side
    const int32 MinValid = -1;
    
    // Allow 1 voxel overflow on the positive side
    const int32 MaxValid = VoxelsPerChunk;
    
    // Check if the index is within the valid range
    bool bValidX = (VoxelIndex.X >= MinValid && VoxelIndex.X <= MaxValid);
    bool bValidY = (VoxelIndex.Y >= MinValid && VoxelIndex.Y <= MaxValid);
    bool bValidZ = (VoxelIndex.Z >= MinValid && VoxelIndex.Z <= MaxValid);
    
    return bValidX && bValidY && bValidZ;
}

// Converts indices where 0,0,0 is the minimum corner to world position
static FVector MinCornerVoxelToWorld(const FIntVector& ChunkCoords, const FIntVector& VoxelIndex)
{
    // Calculate chunk dimensions
    const int32 VoxelsPerChunk = ChunkSize * Subdivisions;
    const float ChunkWorldSize = ChunkSize * TerrainGridSize;
    
    // Calculate the chunk center
    FVector ChunkCenter = Origin + FVector(ChunkCoords) * ChunkWorldSize;
    
    // Calculate the chunk minimum corner
    FVector ChunkExtent = FVector(ChunkWorldSize * 0.5f);
    FVector ChunkMinCorner = ChunkCenter - ChunkExtent;
    
    // Calculate the world position of the voxel
    // Add 0.5f to get the center of the voxel
    FVector VoxelCenter = ChunkMinCorner + 
                         (FVector(VoxelIndex) + FVector(0.5f)) * LocalVoxelSize;
    
    UE_LOG(LogTemp, Verbose, TEXT("[MinCornerVoxelToWorld] Chunk: %s, VoxelIndex: %s → World: %s"),
        *ChunkCoords.ToString(), *VoxelIndex.ToString(), *VoxelCenter.ToString());
    
    return VoxelCenter;
}

// Converts world position to voxel index where 0,0,0 is the minimum corner
static FIntVector WorldToMinCornerVoxel(const FVector& WorldPos)
{
    // Get the chunk coordinates
    FIntVector ChunkCoords = WorldToChunk(WorldPos);
    
    // Calculate chunk dimensions
    const float ChunkWorldSize = ChunkSize * TerrainGridSize;
    
    // Calculate the chunk center
    FVector ChunkCenter = Origin + FVector(ChunkCoords) * ChunkWorldSize;
    
    // Calculate the chunk minimum corner
    FVector ChunkExtent = FVector(ChunkWorldSize * 0.5f);
    FVector ChunkMinCorner = ChunkCenter - ChunkExtent;
    
    // Calculate the local position within the chunk
    FVector LocalInChunk = WorldPos - ChunkMinCorner;
    
    // Convert to voxel coordinates
    FIntVector VoxelIndex(
        FMath::FloorToInt(LocalInChunk.X / LocalVoxelSize),
        FMath::FloorToInt(LocalInChunk.Y / LocalVoxelSize),
        FMath::FloorToInt(LocalInChunk.Z / LocalVoxelSize)
    );
    
    UE_LOG(LogTemp, Verbose, TEXT("[WorldToMinCornerVoxel] WorldPos: %s → Chunk: %s, VoxelIndex: %s"),
        *WorldPos.ToString(), *ChunkCoords.ToString(), *VoxelIndex.ToString());
    
    return VoxelIndex;
}

// Get the chunk and voxel index (where 0,0,0 is minimum corner) from a world position
static void WorldToChunkAndVoxel(const FVector& WorldPos, FIntVector& OutChunkCoords, FIntVector& OutVoxelIndex)
{
    OutChunkCoords = WorldToChunk(WorldPos);
    OutVoxelIndex = WorldToMinCornerVoxel(WorldPos);
}



// World position → local voxel coordinates inside a chunk
static FIntVector WorldToLocalVoxel(const FVector& WorldPos)
{
    FIntVector ChunkCoords = WorldToChunk(WorldPos);
    FVector ChunkCenter = ChunkToWorld(ChunkCoords);
    
    // Calculate chunk extent and minimum corner
    FVector ChunkExtent = FVector(ChunkSize * TerrainGridSize / 2.0f);
    FVector ChunkMinCorner = ChunkCenter - ChunkExtent;
    
    // Calculate local position within the chunk
    FVector LocalInChunk = WorldPos - ChunkMinCorner;
    
    // Convert to voxel coordinates
    FIntVector LocalVoxel(
        FMath::FloorToInt(LocalInChunk.X / LocalVoxelSize),
        FMath::FloorToInt(LocalInChunk.Y / LocalVoxelSize),
        FMath::FloorToInt(LocalInChunk.Z / LocalVoxelSize)
    );
    
    UE_LOG(LogTemp, Warning, TEXT("[WorldToLocalVoxel] WorldPos: %s → LocalVoxel: %s"),
        *WorldPos.ToString(), *LocalVoxel.ToString());
    
    return LocalVoxel;
}


    // Global voxel coordinate → world position of voxel center
    static FVector LocalVoxelToWorld(const FIntVector& GlobalVoxelCoords)
    {
        int32 VoxelsPerChunk = ChunkSize * Subdivisions;

        FIntVector ChunkCoords(
            FMath::FloorToInt((float)GlobalVoxelCoords.X / VoxelsPerChunk),
            FMath::FloorToInt((float)GlobalVoxelCoords.Y / VoxelsPerChunk),
            FMath::FloorToInt((float)GlobalVoxelCoords.Z / VoxelsPerChunk)
        );

        FIntVector LocalCoords(
            GlobalVoxelCoords.X - ChunkCoords.X * VoxelsPerChunk,
            GlobalVoxelCoords.Y - ChunkCoords.Y * VoxelsPerChunk,
            GlobalVoxelCoords.Z - ChunkCoords.Z * VoxelsPerChunk
        );

        FVector ChunkOrigin = ChunkToWorld(ChunkCoords);
        FVector WorldPos = ChunkOrigin + FVector(LocalCoords) * LocalVoxelSize;

        UE_LOG(LogTemp, Verbose, TEXT("[LocalVoxelToWorld] GlobalVoxelCoords: %s → WorldPos: %s"),
            *GlobalVoxelCoords.ToString(), *WorldPos.ToString());

        return WorldPos;
    }

    // Local voxel → world position in specified chunk
    static FVector ChunkVoxelToWorld(const FIntVector& ChunkCoords, const FIntVector& LocalVoxel)
    {
        FVector ChunkOrigin = ChunkToWorld(ChunkCoords);
        FVector WorldPos = ChunkOrigin + FVector(LocalVoxel) * LocalVoxelSize;

        UE_LOG(LogTemp, Verbose, TEXT("[ChunkVoxelToWorld] Chunk: %s, LocalVoxel: %s → World: %s"),
            *ChunkCoords.ToString(), *LocalVoxel.ToString(), *WorldPos.ToString());

        return WorldPos;
    }

    // Set the configuration values
    static void InitFromConfig(int32 InChunkSize, int32 InSubdivisions, float InTerrainGridSize, FVector InOrigin)
    {
        ChunkSize = InChunkSize;
        Subdivisions = InSubdivisions;
        TerrainGridSize = InTerrainGridSize > 0.0f ? InTerrainGridSize : 100.0f;
        LocalVoxelSize = TerrainGridSize / Subdivisions;
        Origin = InOrigin;

        UE_LOG(LogTemp, Display, TEXT("[InitFromConfig] ChunkSize: %d, Subdivisions: %d, TerrainGridSize: %f, LocalVoxelSize: %f, Origin: %s"),
            ChunkSize, Subdivisions, TerrainGridSize, LocalVoxelSize, *Origin.ToString());
    }
};
