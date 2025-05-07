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

    /** Chunk coords → World origin of that chunk */
    static FVector ChunkToWorld(const FIntVector& ChunkCoords)
    {
        float ChunkWorldSize = ChunkSize * TerrainGridSize;
        FVector WorldPos = Origin + FVector(
            ChunkCoords.X * ChunkWorldSize,
            ChunkCoords.Y * ChunkWorldSize,
            ChunkCoords.Z * ChunkWorldSize
        );
        
        UE_LOG(LogTemp, Verbose, TEXT("[ChunkToWorld] ChunkCoords: %s, ChunkWorldSize: %f, Origin: %s, WorldPos: %s"), 
               *ChunkCoords.ToString(), ChunkWorldSize, *Origin.ToString(), *WorldPos.ToString());
               
        return WorldPos;
    }


    /** World → Chunk coordinates (which chunk contains this world-pos) */
    static FIntVector WorldToChunk(const FVector& WorldPos)
    {
        float ChunkWorldSize = ChunkSize * TerrainGridSize;
        FVector Local = WorldPos - Origin;

        // Calculate chunk coordinates
        int32 ChunkX = FMath::FloorToInt(Local.X / ChunkWorldSize);
        int32 ChunkY = FMath::FloorToInt(Local.Y / ChunkWorldSize);
        int32 ChunkZ = FMath::FloorToInt(Local.Z / ChunkWorldSize);

        FIntVector ChunkCoords(ChunkX, ChunkY, ChunkZ);

        UE_LOG(LogTemp, Warning, TEXT("[WorldToChunk] WorldPos: %s, Origin: %s, Local: %s"),
               *WorldPos.ToString(), *Origin.ToString(), *Local.ToString());
        UE_LOG(LogTemp, Warning, TEXT("[WorldToChunk] ChunkWorldSize: %f, ChunkCoords: %s"),
               ChunkWorldSize, *ChunkCoords.ToString());

        return ChunkCoords;
    }



    /** World → Chunk origin position directly */
    static FVector ChunkOriginFromWorldPos(const FVector& WorldPos)
    {
        FVector ChunkOrigin = ChunkToWorld(WorldToChunk(WorldPos));
        UE_LOG(LogTemp, Verbose, TEXT("[ChunkOriginFromWorldPos] WorldPos: %s, ChunkOrigin: %s"), *WorldPos.ToString(), *ChunkOrigin.ToString());
        return ChunkOrigin;
    }

    /** World → Chunk-local Voxel coords (returns indices in [0, VoxelsPerChunk-1]) */
    static FIntVector WorldToLocalVoxel(const FVector& WorldPos)
    {
        const FIntVector ChunkCoords = WorldToChunk(WorldPos);
        const FVector ChunkOrigin = ChunkToWorld(ChunkCoords);
        const FVector Local = WorldPos - ChunkOrigin;
        
        int32 VoxelsPerChunk = ChunkSize * Subdivisions;
        
        // Calculate local voxel coordinates within the chunk
        FIntVector LocalVoxel(
            FMath::Clamp(FMath::FloorToInt(Local.X / LocalVoxelSize), 0, VoxelsPerChunk - 1),
            FMath::Clamp(FMath::FloorToInt(Local.Y / LocalVoxelSize), 0, VoxelsPerChunk - 1),
            FMath::Clamp(FMath::FloorToInt(Local.Z / LocalVoxelSize), 0, VoxelsPerChunk - 1)
        );
        
        UE_LOG(LogTemp, Verbose, TEXT("[WorldToLocalVoxel] WorldPos: %s, ChunkOrigin: %s, Local: %s, LocalVoxel: %s"),
               *WorldPos.ToString(), *ChunkOrigin.ToString(), *Local.ToString(), *LocalVoxel.ToString());
               
        return LocalVoxel;
    }



    /** Voxel coords in world-space (center of that voxel) */
    // In VoxelConversion.h, modify the LocalVoxelToWorld function:
    static FVector LocalVoxelToWorld(const FIntVector& LocalVoxelCoords)
    {
        // Calculate voxels per chunk dimension
        int32 VoxelsPerChunk = ChunkSize * Subdivisions;
        
        // Which chunk is this voxel in?
        const FIntVector ChunkCoords(
            FMath::FloorToInt(float(LocalVoxelCoords.X) / VoxelsPerChunk),
            FMath::FloorToInt(float(LocalVoxelCoords.Y) / VoxelsPerChunk),
            FMath::FloorToInt(float(LocalVoxelCoords.Z) / VoxelsPerChunk)
        );
        
        // Handle negative coordinates properly
        FIntVector LocalInChunk = LocalVoxelCoords;
        if (LocalVoxelCoords.X < 0) LocalInChunk.X = ((LocalVoxelCoords.X % VoxelsPerChunk) + VoxelsPerChunk) % VoxelsPerChunk;
        else LocalInChunk.X = LocalVoxelCoords.X % VoxelsPerChunk;
        
        if (LocalVoxelCoords.Y < 0) LocalInChunk.Y = ((LocalVoxelCoords.Y % VoxelsPerChunk) + VoxelsPerChunk) % VoxelsPerChunk;
        else LocalInChunk.Y = LocalVoxelCoords.Y % VoxelsPerChunk;
        
        if (LocalVoxelCoords.Z < 0) LocalInChunk.Z = ((LocalVoxelCoords.Z % VoxelsPerChunk) + VoxelsPerChunk) % VoxelsPerChunk;
        else LocalInChunk.Z = LocalVoxelCoords.Z % VoxelsPerChunk;
        
        // World position of the chunk's origin
        const FVector ChunkOrigin = ChunkToWorld(ChunkCoords);
        
        // Calculate final world position (center of voxel)
        FVector WorldPos = ChunkOrigin 
            + FVector(LocalInChunk) * LocalVoxelSize 
            + FVector(LocalVoxelSize * 0.5f);  // Center offset
            
        UE_LOG(LogTemp, Verbose, TEXT("[LocalVoxelToWorld] LocalVoxelCoords: %s, ChunkCoords: %s, LocalInChunk: %s, WorldPos: %s"),
               *LocalVoxelCoords.ToString(), *ChunkCoords.ToString(), *LocalInChunk.ToString(), *WorldPos.ToString());
               
        return WorldPos;
    }



    // In FVoxelConversion.cpp
    static void InitFromConfig(int32 InChunkSize, int32 InSubdivisions, float InTerrainGridSize, FVector InOrigin)
    {
        ChunkSize = InChunkSize;
        Subdivisions = InSubdivisions;
        TerrainGridSize = InTerrainGridSize;
        LocalVoxelSize = TerrainGridSize / Subdivisions;
        Origin = InOrigin;
        
        UE_LOG(LogTemp, Display, TEXT("[InitFromConfig] ChunkSize: %d, Subdivisions: %d, TerrainGridSize: %f, LocalVoxelSize: %f, Origin: %s"),
            ChunkSize, Subdivisions, TerrainGridSize, LocalVoxelSize, *Origin.ToString());
    }

};
