#pragma once

#include "CoreMinimal.h"
#include "VoxelBrushShape.h"
#include "NoiseBrushShape.generated.h"

UCLASS()
class DIGGERPROUNREAL_API UNoiseBrushShape : public UVoxelBrushShape
{
	GENERATED_BODY()

public:
	virtual float CalculateSDF_Implementation(
		const FVector& WorldPos,
		const FBrushStroke& Stroke,
		float TerrainHeight
	) const override;
};