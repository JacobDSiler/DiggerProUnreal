#pragma once

#include "CoreMinimal.h"
#include "VoxelInstance.generated.h"

USTRUCT(BlueprintType)
struct FVoxelInstance
{
    GENERATED_BODY();

    FVoxelInstance()
        : GlobalVoxel(FIntVector::ZeroValue)
        , ChunkCoords(FIntVector::ZeroValue)
        , LocalVoxel(FIntVector::ZeroValue)
    {}

    FVoxelInstance(const FIntVector& InGlobal, const FIntVector& InChunk, const FIntVector& InLocal)
        : GlobalVoxel(InGlobal)
        , ChunkCoords(InChunk)
        , LocalVoxel(InLocal)
    {}

    UPROPERTY(BlueprintReadWrite)
    FIntVector GlobalVoxel;

    UPROPERTY(BlueprintReadWrite)
    FIntVector ChunkCoords;

    UPROPERTY(BlueprintReadWrite)
    FIntVector LocalVoxel;
};


