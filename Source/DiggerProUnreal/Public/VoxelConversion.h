#pragma once


#include "CoreMinimal.h"
#include "DiggerDebug.h"


/**
 * FVoxelConversion - Utility struct for converting between different coordinate spaces in a voxel-based terrain system.
 * 
 * This class handles conversions between:
 * - World coordinates (Unreal Engine world space)
 * - Chunk coordinates (integer coordinates identifying chunks in the grid)
 * - Voxel coordinates (both chunk-centered and minimum-corner-based systems)
 * 
 * The voxel grid is organized as follows:
 * - The world is divided into chunks of equal size
 * - Each chunk contains a grid of voxels
 * - Chunks are identified by integer coordinates (ChunkCoords)
 * - Voxels within a chunk can be addressed using local coordinates
 * - A 1-voxel overflow region exists on each side of the chunk for seamless transitions
 */
struct FVoxelConversion
{
    /** Number of grid squares per chunk (e.g., 8) */
    static int32 ChunkSize;
    
    /** Voxels per grid square (e.g., 4) */
    static int32 Subdivisions;
    
    /** Grid square size in world units (e.g., 100) */
    static float TerrainGridSize;
    
    /** Size of a single voxel in world units (computed as TerrainGridSize / Subdivisions) */
    static float LocalVoxelSize;
    
    /** World origin of the voxel grid */
    static FVector Origin;

    /** SDF value representing solid terrain */
    static constexpr float SDF_SOLID = -1.0f;
    
    /** SDF value representing air/empty space */
    static constexpr float SDF_AIR = 1.0f;
    
    /** Thickness of the shell around dug holes in voxels */
    static constexpr int DUG_HOLE_SHELL_VOXEL_THICKNESS = 3;
    static float ChunkWorldSize;

    /**
     * Converts chunk coordinates to the world position of the chunk's center.
     * 
     * @param ChunkCoords - Integer coordinates of the chunk in the grid
     * @return World position of the chunk's center
     */
    static FVector ChunkToWorld(const FIntVector& ChunkCoords)
    {
        // Return min corner, not center
        FVector ChunkMinCorner = Origin + FVector(ChunkCoords) * ChunkSize * TerrainGridSize;

        if (DiggerDebug::VoxelConv)
            UE_LOG(LogTemp, Verbose, TEXT("[ChunkToWorld] ChunkCoords: %s, ChunkMinCorner: %s"),
                *ChunkCoords.ToString(), *ChunkMinCorner.ToString());

        return ChunkMinCorner;
    }
    


    /**
     * Converts a world position to chunk coordinates.
     * Handles edge cases with precise boundary safety.
     * 
     * @param WorldPos - Position in world space
     * @return Integer coordinates of the chunk containing the position
     */
    /**
  * Converts a world position to chunk coordinates.
  * Uses FloorToInt for consistent chunk boundary assignment.
  * 
  * @param WorldPos - Position in world space
  * @return Integer coordinates of the chunk containing the position
  */
static FIntVector WorldToChunk(const FVector& WorldPos)
{
    const FVector LocalizedPos = WorldPos - Origin;

    // Use symmetric rounding so negative coordinates map the same way as
    // positive values. This matches the behaviour prior to the flooring
    // change and ensures brushes update neighboring chunks correctly.
    const int32 X = FMath::RoundToInt(LocalizedPos.X / ChunkWorldSize);
    const int32 Y = FMath::RoundToInt(LocalizedPos.Y / ChunkWorldSize);
    const int32 Z = FMath::RoundToInt(LocalizedPos.Z / ChunkWorldSize);

    return FIntVector(X, Y, Z);
}

/**
 * FIXED: Convert world position to CENTER-ALIGNED global voxel coordinates
 * This makes voxels consistent with your center-aligned chunk system
 */
static FIntVector WorldToGlobalVoxel_CenterAligned(const FVector& WorldPos)
{
    const FVector LocalizedPos = WorldPos - Origin;

    // Round so that negative positions align symmetrically with positive ones
    // when computing which voxel a world position belongs to.
    return FIntVector(
        FMath::RoundToInt(LocalizedPos.X / LocalVoxelSize),
        FMath::RoundToInt(LocalizedPos.Y / LocalVoxelSize),
        FMath::RoundToInt(LocalizedPos.Z / LocalVoxelSize)
    );
}

/**
 * FIXED: Convert CENTER-ALIGNED global voxel coordinates to world position
 */
    static FVector GlobalVoxelToWorld_CenterAligned(const FIntVector& GlobalVoxelCoords)
    {
        return Origin + FVector(GlobalVoxelCoords) * LocalVoxelSize + FVector(LocalVoxelSize * 0.5f);
    }

/**
 * FIXED: Convert center-aligned global voxel to chunk and local voxel
 */
    static void GlobalVoxelToChunkAndLocal_CenterAligned(
        const FIntVector& GlobalVoxelCoords,
        FIntVector& OutChunkCoords,
        FIntVector& OutLocalVoxel)
    {
        const int32 VoxelsPerChunk = ChunkSize * Subdivisions;

        OutChunkCoords = FIntVector(
            FMath::FloorToInt((float)GlobalVoxelCoords.X / VoxelsPerChunk),
            FMath::FloorToInt((float)GlobalVoxelCoords.Y / VoxelsPerChunk),
            FMath::FloorToInt((float)GlobalVoxelCoords.Z / VoxelsPerChunk)
        );

        const FIntVector ChunkOriginGlobalVoxel = OutChunkCoords * VoxelsPerChunk;
        OutLocalVoxel = GlobalVoxelCoords - ChunkOriginGlobalVoxel;
    }

    // In FVoxelConversion class (likely in a header file)
    static FIntVector GetDirectionVector(int32 Index)
    {
        // 6-directional connectivity (face neighbors only)
        static const FIntVector Directions[6] = {
            FIntVector(1, 0, 0),   // +X (Right)
            FIntVector(-1, 0, 0),  // -X (Left)
            FIntVector(0, 1, 0),   // +Y (Forward)
            FIntVector(0, -1, 0),  // -Y (Backward)
            FIntVector(0, 0, 1),   // +Z (Up)
            FIntVector(0, 0, -1)   // -Z (Down)
        };
    
        if (Index >= 0 && Index < 6)
        {
            return Directions[Index];
        }
    
        return FIntVector::ZeroValue;
    }

    // Alternative 26-directional connectivity (if you need all neighbors including corners)
    static FIntVector GetDirectionVector26(int32 Index)
    {
        static const FIntVector Directions[26] = {
            // Face neighbors (6)
            FIntVector(1, 0, 0), FIntVector(-1, 0, 0),
            FIntVector(0, 1, 0), FIntVector(0, -1, 0),
            FIntVector(0, 0, 1), FIntVector(0, 0, -1),
        
            // Edge neighbors (12)
            FIntVector(1, 1, 0), FIntVector(1, -1, 0),
            FIntVector(-1, 1, 0), FIntVector(-1, -1, 0),
            FIntVector(1, 0, 1), FIntVector(1, 0, -1),
            FIntVector(-1, 0, 1), FIntVector(-1, 0, -1),
            FIntVector(0, 1, 1), FIntVector(0, 1, -1),
            FIntVector(0, -1, 1), FIntVector(0, -1, -1),
        
            // Corner neighbors (8)
            FIntVector(1, 1, 1), FIntVector(1, 1, -1),
            FIntVector(1, -1, 1), FIntVector(1, -1, -1),
            FIntVector(-1, 1, 1), FIntVector(-1, 1, -1),
            FIntVector(-1, -1, 1), FIntVector(-1, -1, -1)
        };
    
        if (Index >= 0 && Index < 26)
        {
            return Directions[Index];
        }
    
        return FIntVector::ZeroValue;
    }

/**
 * FIXED: Convert chunk coords and center-aligned local voxel to global voxel
 */
static FIntVector ChunkAndLocalToGlobalVoxel_CenterAligned(const FIntVector& ChunkCoords,
                                                          const FIntVector& LocalVoxel)
{
    const int32 VoxelsPerChunk = ChunkSize * Subdivisions;
    
    // Chunk center in global voxel coordinates
    FIntVector ChunkCenterGlobalVoxel = ChunkCoords * VoxelsPerChunk;
    
    // Add local offset
    return ChunkCenterGlobalVoxel + LocalVoxel;
}

    /**
     * Validates if a voxel index is within the valid range, including overflow regions.
     * Valid range is [-1, VoxelsPerChunk] in each dimension, allowing for 1-voxel overflow.
     * 
     * @param VoxelIndex - Integer coordinates of the voxel within a chunk
     * @return True if the index is within valid range (including overflow), false otherwise
     */
    static bool IsValidVoxelIndex(const FIntVector& VoxelIndex)
    {
        // Calculate the valid range including overflow
        const int32 VoxelsPerChunk = ChunkSize * Subdivisions;
        
        // Allow 1 voxel overflow on the negative side
        const int32 MinValid = -2;
        
        // Allow 1 voxel overflow on the positive side
        const int32 MaxValid = VoxelsPerChunk;
        
        // Check if the index is within the valid range
        bool bValidX = (VoxelIndex.X >= MinValid && VoxelIndex.X <= MaxValid);
        bool bValidY = (VoxelIndex.Y >= MinValid && VoxelIndex.Y <= MaxValid);
        bool bValidZ = (VoxelIndex.Z >= MinValid && VoxelIndex.Z <= MaxValid);
        
        return bValidX && bValidY && bValidZ;
    }

    /**
     * Converts voxel indices (where 0,0,0 is the minimum corner) to world position.
     * This coordinate system treats the minimum corner of the chunk as the origin,
     * so (-1,-1,-1) would be the overflow corner in the negative direction.
     * 
     * @param ChunkCoords - Integer coordinates of the chunk in the grid
     * @param VoxelIndex - Integer coordinates of the voxel within the chunk (minimum corner system)
     * @return World position of the voxel's center
     */
    static FVector MinCornerVoxelToWorld(const FIntVector& ChunkCoords, const FIntVector& VoxelIndex)
    {
        // Calculate chunk minimum corner DIRECTLY (same method)
        FVector ChunkMinCorner = Origin + FVector(ChunkCoords) * ChunkWorldSize;
    
        // Calculate the world position of the voxel
        FVector VoxelCenter = ChunkMinCorner + 
                             (FVector(VoxelIndex) + FVector(0.5f)) * LocalVoxelSize;
    
        return VoxelCenter;
    }
    /**
     * Converts a world position to voxel index where 0,0,0 is the minimum corner of the chunk.
     * This is the inverse of MinCornerVoxelToWorld.
     * 
     * @param WorldPos - Position in world space
     * @return Integer coordinates of the voxel (in minimum corner coordinate system)
     */
    static FIntVector WorldToMinCornerVoxel(const FVector& WorldPos)
    {
        // Get the chunk coordinates
        FIntVector ChunkCoords = WorldToChunk(WorldPos);
        
        
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

        if (DiggerDebug::VoxelConv)
        UE_LOG(LogTemp, Verbose, TEXT("[WorldToMinCornerVoxel] WorldPos: %s → Chunk: %s, VoxelIndex: %s"),
            *WorldPos.ToString(), *ChunkCoords.ToString(), *VoxelIndex.ToString());
        
        return VoxelIndex;
    }

    /**
     * Gets both the chunk coordinates and the voxel index (in minimum corner system) from a world position.
     * This is a convenience method combining WorldToChunk and WorldToMinCornerVoxel.
     * 
     * @param WorldPos - Position in world space
     * @param OutChunkCoords - [Out] Integer coordinates of the chunk
     * @param OutVoxelIndex - [Out] Integer coordinates of the voxel within the chunk (minimum corner system)
     */
    static void WorldToChunkAndVoxel(const FVector& WorldPos, FIntVector& OutChunkCoords, FIntVector& OutVoxelIndex)
    {
        OutChunkCoords = WorldToChunk(WorldPos);
        OutVoxelIndex = WorldToMinCornerVoxel(WorldPos);
    }
    
    /**
         * Converts a world position to local voxel coordinates inside a chunk.
         * This uses a chunk-centered coordinate system where (0,0,0) is the center of the chunk.
         * 
         * @param WorldPos - Position in world space
         * @return Integer coordinates of the voxel within the chunk (chunk-centered system)
         */
    static FIntVector WorldToLocalVoxel(const FVector& WorldPos)
    {
        // Get chunk coordinates 
        FIntVector ChunkCoords = WorldToChunk(WorldPos);
    
        // Calculate chunk minimum corner DIRECTLY (avoid center calculation)
        FVector ChunkMinCorner = Origin + FVector(ChunkCoords) * ChunkWorldSize;
    
        // Calculate the local position within the chunk
        FVector LocalInChunk = WorldPos - ChunkMinCorner;
    
        // Convert to voxel coordinates 
        FIntVector VoxelIndex(
            FMath::FloorToInt(LocalInChunk.X / LocalVoxelSize),
            FMath::FloorToInt(LocalInChunk.Y / LocalVoxelSize),
            FMath::FloorToInt(LocalInChunk.Z / LocalVoxelSize)
        );
    
        return VoxelIndex;
    }

    /**
     * Converts global voxel coordinates to the world position of the voxel's center.
     * Global voxel coordinates are continuous across chunk boundaries.
     * 
     * @param GlobalVoxelCoords - Integer coordinates of the voxel in global space
     * @return World position of the voxel's center
     */
    // Remove This 22/08/2025
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

        if (DiggerDebug::VoxelConv)
        UE_LOG(LogTemp, Verbose, TEXT("[LocalVoxelToWorld] GlobalVoxelCoords: %s → WorldPos: %s"),
            *GlobalVoxelCoords.ToString(), *WorldPos.ToString());

        return WorldPos;
    }

    /**
     * Converts local voxel coordinates within a specific chunk to world position.
     * 
     * @param ChunkCoords - Integer coordinates of the chunk in the grid
     * @param LocalVoxel - Integer coordinates of the voxel within the chunk
     * @return World position of the voxel's center
     */
    // Remove This 22/08/2025
    static FVector ChunkVoxelToWorld(const FIntVector& ChunkCoords, const FIntVector& LocalVoxel)
    {
        FVector ChunkOrigin = ChunkToWorld(ChunkCoords);
        FVector WorldPos = ChunkOrigin + FVector(LocalVoxel) * LocalVoxelSize;

        if (DiggerDebug::VoxelConv)
        UE_LOG(LogTemp, Verbose, TEXT("[ChunkVoxelToWorld] Chunk: %s, LocalVoxel: %s → World: %s"),
            *ChunkCoords.ToString(), *LocalVoxel.ToString(), *WorldPos.ToString());

        return WorldPos;
    }

    /**
     * Initializes the static configuration values for the voxel system.
     * This should be called once at startup before using any conversion methods.
     * 
     * @param InChunkSize - Number of grid squares per chunk
     * @param InSubdivisions - Voxels per grid square
     * @param InTerrainGridSize - Grid square size in world units
     * @param InOrigin - World origin of the voxel grid
     */
    static void InitFromConfig(int32 InChunkSize, int32 InSubdivisions, float InTerrainGridSize, FVector InOrigin)
    {
        ChunkSize = InChunkSize;
        Subdivisions = InSubdivisions;
        TerrainGridSize = InTerrainGridSize > 0.0f ? InTerrainGridSize : 100.0f;
        LocalVoxelSize = TerrainGridSize / Subdivisions;
        Origin = InOrigin;
        ChunkWorldSize = ChunkSize * TerrainGridSize;

        if (DiggerDebug::VoxelConv)
        UE_LOG(LogTemp, Display, TEXT("[InitFromConfig] ChunkSize: %d, Subdivisions: %d, TerrainGridSize: %f, LocalVoxelSize: %f, Origin: %s"),
            ChunkSize, Subdivisions, TerrainGridSize, LocalVoxelSize, *Origin.ToString());
    }
};
