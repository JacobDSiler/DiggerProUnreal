// PyramidBrushShape.h
#pragma once

#include "VoxelBrushShape.h"
#include "PyramidBrushShape.generated.h"

UCLASS()
class DIGGERPROUNREAL_API UPyramidBrushShape : public UVoxelBrushShape
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
