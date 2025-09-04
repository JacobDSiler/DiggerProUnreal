// SmoothBrushShape.h
#pragma once

#include "VoxelBrushShape.h"
#include "SmoothBrushShape.generated.h"

UCLASS()
class DIGGERPROUNREAL_API USmoothBrushShape : public UVoxelBrushShape
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
