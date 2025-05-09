#pragma once

#include "CoreMinimal.h"

class DiggerManager;

// Simple struct for integer‐voxel coordinates
struct FVoxelConversion
{
    // Chunk size in grid squares, and size of each grid square in world units
    static int32   ChunkSize;         // Number of grid squares per chunk (e.g., 8)
    static int32   Subdivisions;      // Number of voxels per grid square (e.g., 4)
    static float   TerrainGridSize;   // Size of a grid square in world units (e.g., 100)
    static float   LocalVoxelSize;    // Size of a voxel in world units (TerrainGridSize / Subdivisions)
    static FVector Origin;            // World origin of the voxel grid

    /** Chunk coords → World center of that chunk (center is the origin in our schema) */
    static FVector ChunkToWorld(const FIntVector& ChunkCoords)
    {
        float ChunkWorldSize = ChunkSize * TerrainGridSize;
        FVector ChunkCenter = FVector(ChunkCoords) * ChunkWorldSize;
        
        UE_LOG(LogTemp, Verbose, TEXT("[ChunkToWorld] ChunkCoords: %s, ChunkWorldSize: %f, ChunkCenter: %s"), 
               *ChunkCoords.ToString(), ChunkWorldSize, *ChunkCenter.ToString());
               
        return ChunkCenter; // This is the center/origin of the chunk
    }

    /** World → Chunk coordinates (which chunk contains this world-pos) */
    static FIntVector WorldToChunk(const FVector& WorldPos)
    {
        float ChunkWorldSize = ChunkSize * TerrainGridSize;
        
        // Calculate chunk coordinates directly
        // We use FloorToInt for negative coordinates to work correctly
        int32 ChunkX = FMath::FloorToInt(WorldPos.X / ChunkWorldSize);
        int32 ChunkY = FMath::FloorToInt(WorldPos.Y / ChunkWorldSize);
        int32 ChunkZ = FMath::FloorToInt(WorldPos.Z / ChunkWorldSize);

        FIntVector ChunkCoords(ChunkX, ChunkY, ChunkZ);

        UE_LOG(LogTemp, Warning, TEXT("[WorldToChunk] WorldPos: %s, ChunkWorldSize: %f, ChunkCoords: %s"),
               *WorldPos.ToString(), ChunkWorldSize, *ChunkCoords.ToString());

        return ChunkCoords;
    }

    /** World → Chunk-local Voxel coords (returns indices centered around 0) */
    static FIntVector WorldToLocalVoxel(const FVector& WorldPos)
    {
        // Get chunk coordinates and center (origin)
        const FIntVector ChunkCoords = WorldToChunk(WorldPos);
        const FVector ChunkOrigin = ChunkToWorld(ChunkCoords); // This is the center of the chunk
        
        // Calculate local position within the chunk (relative to center/origin)
        const FVector LocalInChunk = WorldPos - ChunkOrigin;
        
        // Calculate local voxel coordinates within the chunk
        //FIntVector LocalVoxel;
        
        // For each axis, calculate the voxel index
      /*  for (int32 i = 0; i < 3; i++)
        {
            float localCoord = i == 0 ? LocalInChunk.X : (i == 1 ? LocalInChunk.Y : LocalInChunk.Z);
            
            // Convert to voxel space
            float voxelCoord = localCoord / TerrainGridSize;
            
            // Floor to get the voxel index, but we need to handle the center-origin convention
            int32 voxelIndex = FMath::FloorToInt(voxelCoord);
            
            // Ensure it's within valid range for a center-origin chunk
            int32 halfSize = ChunkSize / 2;
            voxelIndex = FMath::Clamp(voxelIndex, -halfSize, halfSize - 1);
            
            // Assign to the appropriate component
            if (i == 0) LocalVoxel.X = voxelIndex;
            else if (i == 1) LocalVoxel.Y = voxelIndex;
            else LocalVoxel.Z = voxelIndex;
        }*/
        // Replace the for-loop with direct calculation
        FVector LocalOffset = (WorldPos - ChunkOrigin) / TerrainGridSize;
        FIntVector LocalVoxel = FIntVector(
            FMath::FloorToInt(LocalOffset.X),
            FMath::FloorToInt(LocalOffset.Y),
            FMath::FloorToInt(LocalOffset.Z)
        );

        // Logging remains the same

        
        UE_LOG(LogTemp, Warning, TEXT("[WorldToLocalVoxel] WorldPos: %s, ChunkOrigin: %s, LocalInChunk: %s, LocalVoxel: %s"),
               *WorldPos.ToString(), *ChunkOrigin.ToString(), *LocalInChunk.ToString(), *LocalVoxel.ToString());
               
        return LocalVoxel;
    }

    /** Convert global voxel coordinates to world-space position (center of voxel) */
    static FVector LocalVoxelToWorld(const FIntVector& GlobalVoxelCoords)
    {
        // Calculate voxels per chunk dimension
        int32 VoxelsPerChunk = ChunkSize;
        int32 HalfChunkSize = ChunkSize / 2;
        
        // Determine which chunk this voxel belongs to
        // For center-origin chunks, we need to handle the division differently
        FIntVector ChunkCoords(
            FMath::FloorToInt((float)(GlobalVoxelCoords.X + HalfChunkSize) / VoxelsPerChunk),
            FMath::FloorToInt((float)(GlobalVoxelCoords.Y + HalfChunkSize) / VoxelsPerChunk),
            FMath::FloorToInt((float)(GlobalVoxelCoords.Z + HalfChunkSize) / VoxelsPerChunk)
        );

        
        // Calculate local coordinates within the chunk
        FIntVector LocalInChunk(
            GlobalVoxelCoords.X - ChunkCoords.X * VoxelsPerChunk,
            GlobalVoxelCoords.Y - ChunkCoords.Y * VoxelsPerChunk,
            GlobalVoxelCoords.Z - ChunkCoords.Z * VoxelsPerChunk
        );
        
        // Adjust for center-origin convention
        // If local coordinate is >= HalfChunkSize, it should be mapped to negative values
        //if (LocalInChunk.X >= HalfChunkSize) LocalInChunk.X -= VoxelsPerChunk;
        //if (LocalInChunk.Y >= HalfChunkSize) LocalInChunk.Y -= VoxelsPerChunk;
        //if (LocalInChunk.Z >= HalfChunkSize) LocalInChunk.Z -= VoxelsPerChunk;
        
        // Get the chunk origin (center)
        FVector ChunkOrigin = ChunkToWorld(ChunkCoords);
        
        // Calculate final world position (center of voxel)
        FVector WorldPos = ChunkOrigin + FVector(LocalInChunk) * TerrainGridSize;
            
        UE_LOG(LogTemp, Verbose, TEXT("[LocalVoxelToWorld] GlobalVoxelCoords: %s, ChunkCoords: %s, LocalInChunk: %s, WorldPos: %s"),
               *GlobalVoxelCoords.ToString(), *ChunkCoords.ToString(), *LocalInChunk.ToString(), *WorldPos.ToString());
               
        return WorldPos;
    }

    /** Get the world position of a voxel within a specific chunk */
    static FVector ChunkVoxelToWorld(const FIntVector& ChunkCoords, const FIntVector& LocalVoxel)
    {
        // Get the chunk center
        FVector ChunkOrigin = ChunkToWorld(ChunkCoords);
        
        // Calculate voxel position (center of voxel)
        // LocalVoxel is already in center-origin format (-4 to +3 for an 8×8×8 chunk)
        FVector WorldPos = ChunkOrigin + FVector(LocalVoxel) * TerrainGridSize;
            
        UE_LOG(LogTemp, Verbose, TEXT("[ChunkVoxelToWorld] ChunkCoords: %s, LocalVoxel: %s, WorldPos: %s"),
               *ChunkCoords.ToString(), *LocalVoxel.ToString(), *WorldPos.ToString());
               
        return WorldPos;
    }

    /** Initialize static variables from configuration */
    static void InitFromConfig(int32 InChunkSize, int32 InSubdivisions, float InTerrainGridSize, FVector InOrigin)
    {
        ChunkSize = InChunkSize;
        Subdivisions = InSubdivisions;
        TerrainGridSize = InTerrainGridSize > 0.0f ? InTerrainGridSize : 100.0f; // Default to 100.0f if 0.0f
        LocalVoxelSize = TerrainGridSize / Subdivisions;
        Origin = InOrigin;
        
        UE_LOG(LogTemp, Display, TEXT("[InitFromConfig] ChunkSize: %d, Subdivisions: %d, TerrainGridSize: %f, LocalVoxelSize: %f, Origin: %s"),
            ChunkSize, Subdivisions, TerrainGridSize, LocalVoxelSize, *Origin.ToString());
    }
};
