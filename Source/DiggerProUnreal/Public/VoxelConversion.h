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
        float ChunkWorldSize = ChunkSize * TerrainGridSize;
        FVector ChunkCenter = Origin + FVector(ChunkCoords) * ChunkWorldSize;

        UE_LOG(LogTemp, Verbose, TEXT("[ChunkToWorld] ChunkCoords: %s, ChunkCenter: %s"), 
            *ChunkCoords.ToString(), *ChunkCenter.ToString());

        return ChunkCenter;
    }

    // World position → chunk coordinate
    static FIntVector WorldToChunk(const FVector& WorldPos)
    {
        float ChunkWorldSize = ChunkSize * TerrainGridSize;
        FVector LocalizedPos = WorldPos - Origin;

        int32 X = FMath::FloorToInt(LocalizedPos.X / ChunkWorldSize);
        int32 Y = FMath::FloorToInt(LocalizedPos.Y / ChunkWorldSize);
        int32 Z = FMath::FloorToInt(LocalizedPos.Z / ChunkWorldSize);

        FIntVector ChunkCoords(X, Y, Z);

        UE_LOG(LogTemp, Warning, TEXT("[WorldToChunk] WorldPos: %s, ChunkCoords: %s"), 
            *WorldPos.ToString(), *ChunkCoords.ToString());

        return ChunkCoords;
    }

    // World position → local voxel coordinates inside a chunk
    static FIntVector WorldToLocalVoxel(const FVector& WorldPos)
    {
        FIntVector ChunkCoords = WorldToChunk(WorldPos);
        FVector ChunkOrigin = ChunkToWorld(ChunkCoords);

        FVector LocalInChunk = WorldPos - ChunkOrigin;
        FVector VoxelOffset = LocalInChunk / LocalVoxelSize;

        FIntVector LocalVoxel(
            FMath::FloorToInt(VoxelOffset.X),
            FMath::FloorToInt(VoxelOffset.Y),
            FMath::FloorToInt(VoxelOffset.Z)
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
