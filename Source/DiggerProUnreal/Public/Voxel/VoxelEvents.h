#pragma once

#include "CoreMinimal.h"
#include "VoxelEvents.generated.h"

/** Per-operation summary of voxel changes, safe to pass across threads (copyable POD). */
USTRUCT(BlueprintType)
struct DIGGERPROUNREAL_API FVoxelModificationReport
{
	GENERATED_BODY()

	/** Count of SDFs that moved toward AIR (dug/removed). */
	UPROPERTY(BlueprintReadOnly, Category="Voxels")
	int32 VoxelsDug = 0;

	/** Count of SDFs that moved toward SOLID (added/filled). */
	UPROPERTY(BlueprintReadOnly, Category="Voxels")
	int32 VoxelsAdded = 0;

	/** Chunk this operation targeted (chunk grid coords). */
	UPROPERTY(BlueprintReadOnly, Category="Voxels")
	FIntVector ChunkCoordinates = FIntVector::ZeroValue;

	/** World-space center of the brush/tool (if applicable). */
	UPROPERTY(BlueprintReadOnly, Category="Voxels")
	FVector BrushPosition = FVector::ZeroVector;

	/** Brush radius (if applicable). */
	UPROPERTY(BlueprintReadOnly, Category="Voxels")
	float BrushRadius = 0.0f;
};

/** Global multicast delegate used by chunks to report their voxel-edit summaries. */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnVoxelsModified, const FVoxelModificationReport&);
