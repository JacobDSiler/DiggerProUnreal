#pragma once
#include "CoreMinimal.h"
#include "DiggerDebug.h"

struct DIGGERPROUNREAL_API FVoxelConversion
{
    // Config (set once via InitFromConfig)
    static int32  ChunkSize;        // grid squares per chunk
    static int32  Subdivisions;     // voxels per grid square
    static float  TerrainGridSize;  // cm per grid square
    static float  LocalVoxelSize;   // cm per voxel (= TerrainGridSize / Subdivisions)
    static FVector Origin;          // world-space origin of voxel grid


    static constexpr float SDF_SOLID = -1.0f;
    static constexpr float SDF_AIR   = +1.0f;

    // --- Derived sizes (computed on demand so they never drift) ---
    static FORCEINLINE int32  VoxelsPerChunk() { return ChunkSize * Subdivisions; }
    static FORCEINLINE float  ChunkWorldSize() { return (float)VoxelsPerChunk() * LocalVoxelSize; }

    // --- CHUNK <-> WORLD (MIN-CORNER aligned) ---
    static FORCEINLINE FVector ChunkToWorld_Min(const FIntVector& Chunk)
    {
        const FVector P = Origin + FVector(Chunk) * ChunkWorldSize();
        if (DiggerDebug::VoxelConv)
            UE_LOG(LogTemp, Verbose, TEXT("[ChunkToWorld_Min] %s -> %s"),
                *Chunk.ToString(), *P.ToString());
        return P;
    }

    static FORCEINLINE FIntVector WorldToChunk_Min(const FVector& W)
    {
        const FVector L = W - Origin;
        return FIntVector(
            FMath::FloorToInt(L.X / ChunkWorldSize()),
            FMath::FloorToInt(L.Y / ChunkWorldSize()),
            FMath::FloorToInt(L.Z / ChunkWorldSize()));
    }

    // --- GLOBAL VOXEL <-> WORLD ---
    // Global voxel index (min-corner) -> world MIN-CORNER
    static FORCEINLINE FVector GlobalVoxelMinToWorld(const FIntVector& G)
    {
        return Origin + FVector(G) * LocalVoxelSize;
    }

    // Global voxel index (min-corner) -> world CENTER
    static FORCEINLINE FVector GlobalVoxelCenterToWorld(const FIntVector& G)
    {
        return Origin + (FVector(G) + FVector(0.5f)) * LocalVoxelSize;
    }

    // World -> global voxel index (min-corner aligned)
    static FORCEINLINE FIntVector WorldToGlobalVoxel_Min(const FVector& W)
    {
        const FVector L = W - Origin;
        return FIntVector(
            FMath::FloorToInt(L.X / LocalVoxelSize),
            FMath::FloorToInt(L.Y / LocalVoxelSize),
            FMath::FloorToInt(L.Z / LocalVoxelSize));
    }

    // --- GLOBAL <-> (CHUNK, LOCAL) (both min-corner aligned) ---
    static FORCEINLINE void GlobalVoxelToChunkAndLocal_Min(
        const FIntVector& G, FIntVector& OutChunk, FIntVector& OutLocal)
    {
        const int32 VPC = VoxelsPerChunk();
        OutChunk = FIntVector(
            FMath::FloorToInt((float)G.X / VPC),
            FMath::FloorToInt((float)G.Y / VPC),
            FMath::FloorToInt((float)G.Z / VPC));
        OutLocal = G - OutChunk * VPC;
    }

    static FORCEINLINE FIntVector ChunkAndLocalToGlobalVoxel_Min(
        const FIntVector& Chunk, const FIntVector& Local)
    {
        return Chunk * VoxelsPerChunk() + Local;
    }

    // --- LOCAL (within chunk) <-> WORLD (min-corner aligned) ---
    // Local voxel index inside a chunk (min-corner) -> world CENTER
    static FORCEINLINE FVector ChunkVoxelToWorldCenter_Min(
        const FIntVector& Chunk, const FIntVector& Local)
    {
        return ChunkToWorld_Min(Chunk) + (FVector(Local) + FVector(0.5f)) * LocalVoxelSize;
    }

    // World -> (chunk, local voxel) (both min-corner aligned)
    static FORCEINLINE void WorldToChunkAndLocal_Min(
        const FVector& W, FIntVector& OutChunk, FIntVector& OutLocal)
    {
        OutChunk = WorldToChunk_Min(W);
        const FVector MinCorner = ChunkToWorld_Min(OutChunk);
        const FVector L = W - MinCorner;
        OutLocal = FIntVector(
            FMath::FloorToInt(L.X / LocalVoxelSize),
            FMath::FloorToInt(L.Y / LocalVoxelSize),
            FMath::FloorToInt(L.Z / LocalVoxelSize));
    }

    // Bounds/validation helpers
    static FORCEINLINE bool IsValidLocalIndex_Min(const FIntVector& Local, int32 Overflow = 1)
    {
        const int32 VPC = VoxelsPerChunk();
        const int32 MinV = -Overflow;
        const int32 MaxV = VPC - 1 + Overflow;
        return (Local.X >= MinV && Local.X <= MaxV) &&
               (Local.Y >= MinV && Local.Y <= MaxV) &&
               (Local.Z >= MinV && Local.Z <= MaxV);
    }

    static void InitFromConfig(int32 InChunkSize, int32 InSubdivisions, float InTerrainGridSize, FVector InOrigin)
    {
        ChunkSize       = InChunkSize;
        Subdivisions    = InSubdivisions;
        TerrainGridSize = (InTerrainGridSize > 0 ? InTerrainGridSize : 100.f);
        LocalVoxelSize  = TerrainGridSize / FMath::Max(1, Subdivisions);
        Origin          = InOrigin;

        if (DiggerDebug::VoxelConv)
        {
            UE_LOG(LogTemp, Display, TEXT("[InitFromConfig] ChunkSize:%d Subdiv:%d Grid:%g Voxel:%g Origin:%s ChunkWorld:%g"),
                ChunkSize, Subdivisions, TerrainGridSize, LocalVoxelSize, *Origin.ToString(), ChunkWorldSize());
        }
    }
};

