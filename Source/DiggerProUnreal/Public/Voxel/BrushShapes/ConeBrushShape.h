// ConeBrushShape.h
#pragma once

#include "VoxelBrushShape.h"
#include "ConeBrushShape.generated.h"

UCLASS()
class DIGGERPROUNREAL_API UConeBrushShape : public UVoxelBrushShape
{
	GENERATED_BODY()
public:
	virtual float CalculateSDF_Implementation(
		const FVector& WorldPos,
		const FVector& BrushCenter,
		float Radius,
		float Strength,
		float Falloff,
		float TerrainHeight,
		bool bDig
	) const override;
};