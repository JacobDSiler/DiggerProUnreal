#pragma once

#include "CoreMinimal.h"
#include "FVoxelData.generated.h"

USTRUCT(BlueprintType)
struct FVoxelData
{
	GENERATED_BODY()

	FVoxelData() = default;
	explicit FVoxelData(float InSDFValue)
		: SDFValue(InSDFValue) {}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel")
	float SDFValue = 1.0f;
};

