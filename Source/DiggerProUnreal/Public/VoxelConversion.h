#pragma once

#include "CoreMinimal.h"

class DiggerManager;

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
        // Calculate chunk center using the same logic as DebugDrawChunk
        FVector ChunkCenter = Origin + FVector(ChunkCoords) * ChunkSize * TerrainGridSize;
        
        UE_LOG(LogTemp, Verbose, TEXT("[ChunkToWorld] ChunkCoords: %s, ChunkCenter: %s"),
            *ChunkCoords.ToString(), *ChunkCenter.ToString());
        
        return ChunkCenter;
    }

    /**
     * Converts a world position to chunk coordinates.
     * Handles edge cases with precise boundary safety.
     * 
     * @param WorldPos - Position in world space
     * @return Integer coordinates of the chunk containing the position
     */
    static FIntVector WorldToChunk(const FVector& WorldPos)
    {
        
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
        const int32 MinValid = -1;
        
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
        // Calculate chunk dimensions
        const int32 VoxelsPerChunk = ChunkSize * Subdivisions;
        
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
        // Get the chunk this world position belongs to
        FIntVector ChunkCoords = WorldToChunk(WorldPos);
    
        // Get the center of the chunk in world space
        FVector ChunkCenter = ChunkToWorld(ChunkCoords);
    
        // Calculate the world-space min corner of the chunk 
        FVector ChunkExtent = FVector(ChunkWorldSize * 0.5f);
        FVector ChunkMinCorner = ChunkCenter - ChunkExtent;
    
        // Compute the local position within the chunk
        FVector LocalInChunk = WorldPos - ChunkMinCorner;

        // Compute the voxel index (chunk-local, within bounds of the chunk)
        float VoxelSize = TerrainGridSize / Subdivisions;  // Make sure Subdivisions > 0
        FIntVector LocalVoxel(
            FMath::FloorToInt(LocalInChunk.X / VoxelSize),
            FMath::FloorToInt(LocalInChunk.Y / VoxelSize),
            FMath::FloorToInt(LocalInChunk.Z / VoxelSize)
        );

        UE_LOG(LogTemp, Warning, TEXT("[WorldToLocalVoxel] WorldPos: %s → Chunk: %s → LocalVoxel: %s"),
            *WorldPos.ToString(), *ChunkCoords.ToString(), *LocalVoxel.ToString());

        return LocalVoxel;
    }


    /**
     * Converts global voxel coordinates to the world position of the voxel's center.
     * Global voxel coordinates are continuous across chunk boundaries.
     * 
     * @param GlobalVoxelCoords - Integer coordinates of the voxel in global space
     * @return World position of the voxel's center
     */
    static FVector LocalVoxelToWorld(const FIntVector& GlobalVoxelCoords)
    {
        int32 VoxelsPerChunk = ChunkSize * Subdivisions;

        FIntVector ChunkCoords(
            FMath::RoundToInt((float)GlobalVoxelCoords.X / VoxelsPerChunk),
            FMath::RoundToInt((float)GlobalVoxelCoords.Y / VoxelsPerChunk),
            FMath::RoundToInt((float)GlobalVoxelCoords.Z / VoxelsPerChunk)
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

    /**
     * Converts local voxel coordinates within a specific chunk to world position.
     * 
     * @param ChunkCoords - Integer coordinates of the chunk in the grid
     * @param LocalVoxel - Integer coordinates of the voxel within the chunk
     * @return World position of the voxel's center
     */
    static FVector ChunkVoxelToWorld(const FIntVector& ChunkCoords, const FIntVector& LocalVoxel)
    {
        FVector ChunkOrigin = ChunkToWorld(ChunkCoords);
        FVector WorldPos = ChunkOrigin + FVector(LocalVoxel) * LocalVoxelSize;

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

        UE_LOG(LogTemp, Display, TEXT("[InitFromConfig] ChunkSize: %d, Subdivisions: %d, TerrainGridSize: %f, LocalVoxelSize: %f, Origin: %s"),
            ChunkSize, Subdivisions, TerrainGridSize, LocalVoxelSize, *Origin.ToString());
    }
};
