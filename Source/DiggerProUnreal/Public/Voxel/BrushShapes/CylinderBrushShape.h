#pragma once

#include "VoxelBrushShape.h"
#include "CylinderBrushShape.generated.h"

UCLASS()
class DIGGERPROUNREAL_API UCylinderBrushShape : public UVoxelBrushShape
{
	GENERATED_BODY()
public:
	// Example for SphereBrushShape.h
	virtual float CalculateSDF_Implementation(
		const FVector& WorldPos,
		const FBrushStroke& Stroke,
		float TerrainHeight
	) const override;

	virtual bool IsWithinBounds(const FVector& WorldPos, const FBrushStroke& Stroke) const override;
};