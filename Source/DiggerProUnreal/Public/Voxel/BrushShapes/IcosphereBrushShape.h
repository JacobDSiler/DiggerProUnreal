// IcosphereBrushShape.h
#pragma once

#include "VoxelBrushShape.h"
#include "IcosphereBrushShape.generated.h"

UCLASS()
class DIGGERPROUNREAL_API UIcosphereBrushShape : public UVoxelBrushShape
{
	GENERATED_BODY()
public:
	// Example for SphereBrushShape.h
	virtual float CalculateSDF_Implementation(
		const FVector& WorldPos,
		const FBrushStroke& Stroke,
		float TerrainHeight
	) const override;
	float GetIcosphereDistortion(const FVector& NormalizedPos, int32 Subdivisions) const;
};