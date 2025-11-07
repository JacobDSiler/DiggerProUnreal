#pragma once

#include "CoreMinimal.h"
#include "DiggerDebug.h"

/**
 * FVoxelConversion - Utility struct for converting between different coordinate spaces in a voxel-based terrain system.
 *
 * Single source of truth for:
 * - World coordinates
 * - Chunk coordinates
 * - Voxel coordinates (integer indices represent sample points; no implicit +0.5f center bias)
 */
struct FVoxelConversion
{
    static int32  ChunkSize;          // grid squares per chunk (e.g., 8)
    static int32  Subdivisions;       // voxels per grid square (e.g., 4)
    static float  TerrainGridSize;    // grid square size in UU (e.g., 100)
    static float  LocalVoxelSize;     // TerrainGridSize / Subdivisions
    static FVector Origin;            // world-origin of the voxel system
    static float  ChunkWorldSize;     // ChunkSize * TerrainGridSize

    static constexpr float SDF_SOLID = -1.0f;
    static constexpr float SDF_AIR   =  1.0f;
    static constexpr int   DUG_HOLE_SHELL_VOXEL_THICKNESS = 3;

    // New: Explicit min-corner vs center for chunk space

    // Returns the chunk's MIN CORNER in world-space (authoritative)
    static FVector ChunkMinCornerToWorld(const FIntVector& ChunkCoords)
    {
        const FVector MinCorner = Origin + FVector(ChunkCoords) * ChunkWorldSize;

        if (DiggerDebug::VoxelConv)
        UE_LOG(LogTemp, Verbose, TEXT("[ChunkMinCornerToWorld] Chunk=%s -> MinCorner=%s"),
            *ChunkCoords.ToString(), *MinCorner.ToString());

        return MinCorner;
    }

    // Returns the chunk's CENTER in world-space (derived from min corner)
    static FVector ChunkCenterToWorld(const FIntVector& ChunkCoords)
    {
        return ChunkMinCornerToWorld(ChunkCoords) + FVector(ChunkWorldSize * 0.5f);
    }

    // Back-compat wrapper: previously named ChunkToWorld but returned min-corner.
    // Keep to avoid churn; update call sites gradually to ChunkMinCornerToWorld.
    UE_DEPRECATED(5.4, "Use ChunkMinCornerToWorld for clarity.")
    static FVector ChunkToWorld(const FIntVector& ChunkCoords)
    {
        return ChunkMinCornerToWorld(ChunkCoords);
    }

    // World -> Chunk using symmetric rounding for negative coords parity with positive
    static FIntVector WorldToChunk(const FVector& WorldPos)
    {
        const FVector LocalizedPos = WorldPos - Origin;

        const int32 X = FMath::RoundToInt(LocalizedPos.X / ChunkWorldSize);
        const int32 Y = FMath::RoundToInt(LocalizedPos.Y / ChunkWorldSize);
        const int32 Z = FMath::RoundToInt(LocalizedPos.Z / ChunkWorldSize);

        return FIntVector(X, Y, Z);
    }

    // Global voxel index (integer) -> world position of the SAMPLE POINT.
    // Removed +0.5f: indices map directly via scale and origin. Centers must be explicit.
    static FVector GlobalVoxelToWorld_CenterAligned(const FIntVector& GlobalVoxelCoords)
    {
        const FVector WorldPos = Origin + FVector(GlobalVoxelCoords) * LocalVoxelSize;

        if (DiggerDebug::VoxelConv)
        UE_LOG(LogTemp, VeryVerbose, TEXT("[GlobalVoxelToWorld] Voxel=%s -> World=%s"),
            *GlobalVoxelCoords.ToString(), *WorldPos.ToString());

        return WorldPos;
    }

    // World -> Global voxel index (integer), symmetric rounding
    static FIntVector WorldToGlobalVoxel_CenterAligned(const FVector& WorldPos)
    {
        const FVector LocalizedPos = WorldPos - Origin;

        return FIntVector(
            FMath::RoundToInt(LocalizedPos.X / LocalVoxelSize),
            FMath::RoundToInt(LocalizedPos.Y / LocalVoxelSize),
            FMath::RoundToInt(LocalizedPos.Z / LocalVoxelSize)
        );
    }

    // Global voxel index -> (ChunkCoords, LocalVoxelIndex)
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

    // Chunk + Local Voxel -> Global Voxel (no implicit center)
    static FIntVector ChunkAndLocalToGlobalVoxel_CenterAligned(
        const FIntVector& ChunkCoords,
        const FIntVector& LocalVoxel)
    {
        const int32 VoxelsPerChunk = ChunkSize * Subdivisions;
        return ChunkCoords * VoxelsPerChunk + LocalVoxel;
    }

    // Valid range with 1-voxel overflow in each dimension; keep -2 if relied upon
    static bool IsValidVoxelIndex(const FIntVector& VoxelIndex)
    {
        const int32 VoxelsPerChunk = ChunkSize * Subdivisions;
        const int32 MinValid = -1;     // exactly one overflow voxel
        const int32 MaxValid = VoxelsPerChunk;

        const bool bValid =
            (VoxelIndex.X >= MinValid && VoxelIndex.X <= MaxValid) &&
            (VoxelIndex.Y >= MinValid && VoxelIndex.Y <= MaxValid) &&
            (VoxelIndex.Z >= MinValid && VoxelIndex.Z <= MaxValid);

        if (!bValid && DiggerDebug::VoxelConv)
        {
            UE_LOG(LogTemp, Verbose, TEXT("[IsValidVoxelIndex] INVALID %s (range [%d,%d])"),
                *VoxelIndex.ToString(), MinValid, MaxValid);
        }
        return bValid;
    }

    // Min-corner local voxel index -> world position (center must be explicit if needed)
    static FVector MinCornerVoxelToWorld(const FIntVector& ChunkCoords, const FIntVector& VoxelIndex)
    {
        const FVector ChunkMinCorner = ChunkMinCornerToWorld(ChunkCoords);
        return ChunkMinCorner + FVector(VoxelIndex) * LocalVoxelSize;
    }

    // World -> Min-corner local voxel index (no center bias)
    static FIntVector WorldToMinCornerVoxel(const FVector& WorldPos)
    {
        const FIntVector ChunkCoords = WorldToChunk(WorldPos);
        const FVector ChunkMinCorner = ChunkMinCornerToWorld(ChunkCoords);
        const FVector LocalInChunk = WorldPos - ChunkMinCorner;

        FIntVector VoxelIndex(
            FMath::FloorToInt(LocalInChunk.X / LocalVoxelSize),
            FMath::FloorToInt(LocalInChunk.Y / LocalVoxelSize),
            FMath::FloorToInt(LocalInChunk.Z / LocalVoxelSize)
        );

        if (DiggerDebug::VoxelConv)
        UE_LOG(LogTemp, Verbose, TEXT("[WorldToMinCornerVoxel] World=%s -> Chunk=%s Local=%s"),
            *WorldPos.ToString(), *ChunkCoords.ToString(), *VoxelIndex.ToString());

        return VoxelIndex;
    }

    // Convenience: World -> (Chunk, Local Min-corner Voxel)
    static void WorldToChunkAndVoxel(const FVector& WorldPos, FIntVector& OutChunkCoords, FIntVector& OutVoxelIndex)
    {
        OutChunkCoords = WorldToChunk(WorldPos);
        OutVoxelIndex = WorldToMinCornerVoxel(WorldPos);
    }

    // World -> Local (min-corner) Voxel
    static FIntVector WorldToLocalVoxel(const FVector& WorldPos)
    {
        const FIntVector ChunkCoords = WorldToChunk(WorldPos);
        const FVector ChunkMinCorner = ChunkMinCornerToWorld(ChunkCoords);
        const FVector LocalInChunk = WorldPos - ChunkMinCorner;

        return FIntVector(
            FMath::FloorToInt(LocalInChunk.X / LocalVoxelSize),
            FMath::FloorToInt(LocalInChunk.Y / LocalVoxelSize),
            FMath::FloorToInt(LocalInChunk.Z / LocalVoxelSize)
        );
    }

    // Global (continuous) voxel -> world (no implicit center bias)
    static FVector LocalVoxelToWorld(const FIntVector& GlobalVoxelCoords)
    {
        const FVector WorldPos = Origin + FVector(GlobalVoxelCoords) * LocalVoxelSize;

        if (DiggerDebug::VoxelConv)
        UE_LOG(LogTemp, Verbose, TEXT("[LocalVoxelToWorld] Voxel=%s -> World=%s"),
            *GlobalVoxelCoords.ToString(), *WorldPos.ToString());

        return WorldPos;
    }

    // Chunk-local voxel -> world (min-corner)
    static FVector ChunkVoxelToWorld(const FIntVector& ChunkCoords, const FIntVector& LocalVoxel)
    {
        const FVector WorldPos = ChunkMinCornerToWorld(ChunkCoords) + FVector(LocalVoxel) * LocalVoxelSize;

        if (DiggerDebug::VoxelConv)
        UE_LOG(LogTemp, Verbose, TEXT("[ChunkVoxelToWorld] Chunk=%s Local=%s -> World=%s"),
            *ChunkCoords.ToString(), *LocalVoxel.ToString(), *WorldPos.ToString());

        return WorldPos;
    }

    // Init
    static void InitFromConfig(int32 InChunkSize, int32 InSubdivisions, float InTerrainGridSize, FVector InOrigin)
    {
        ChunkSize        = InChunkSize;
        Subdivisions     = InSubdivisions;
        TerrainGridSize  = InTerrainGridSize > 0.0f ? InTerrainGridSize : 100.0f;
        LocalVoxelSize   = TerrainGridSize / Subdivisions;
        Origin           = InOrigin;
        ChunkWorldSize   = ChunkSize * TerrainGridSize;

        if (DiggerDebug::VoxelConv)
        UE_LOG(LogTemp, Display, TEXT("[InitFromConfig] ChunkSize=%d Subdiv=%d Grid=%f Voxel=%f Origin=%s"),
            ChunkSize, Subdivisions, TerrainGridSize, LocalVoxelSize, *Origin.ToString());
    }

    // Neighbor helpers unchanged...
    static FIntVector GetDirectionVector(int32 Index)
    {
        static const FIntVector Directions[6] = {
            FIntVector(1, 0, 0), FIntVector(-1, 0, 0),
            FIntVector(0, 1, 0), FIntVector(0, -1, 0),
            FIntVector(0, 0, 1), FIntVector(0, 0, -1)
        };
        return (Index >= 0 && Index < 6) ? Directions[Index] : FIntVector::ZeroValue;
    }

    static FIntVector GetDirectionVector26(int32 Index)
    {
        static const FIntVector Directions[26] = {
            FIntVector(1,0,0), FIntVector(-1,0,0), FIntVector(0,1,0), FIntVector(0,-1,0), FIntVector(0,0,1), FIntVector(0,0,-1),
            FIntVector(1,1,0), FIntVector(1,-1,0), FIntVector(-1,1,0), FIntVector(-1,-1,0),
            FIntVector(1,0,1), FIntVector(1,0,-1), FIntVector(-1,0,1), FIntVector(-1,0,-1),
            FIntVector(0,1,1), FIntVector(0,1,-1), FIntVector(0,-1,1), FIntVector(0,-1,-1),
            FIntVector(1,1,1), FIntVector(1,1,-1), FIntVector(1,-1,1), FIntVector(1,-1,-1),
            FIntVector(-1,1,1), FIntVector(-1,1,-1), FIntVector(-1,-1,1), FIntVector(-1,-1,-1)
        };
        return (Index >= 0 && Index < 26) ? Directions[Index] : FIntVector::ZeroValue;
    }
};