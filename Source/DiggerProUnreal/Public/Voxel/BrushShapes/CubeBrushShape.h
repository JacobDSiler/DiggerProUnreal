// CubeBrushShape.h
#pragma once

#include "VoxelBrushShape.h"
#include "CubeBrushShape.generated.h"

UCLASS()
class DIGGERPROUNREAL_API UCubeBrushShape : public UVoxelBrushShape
{
    GENERATED_BODY()
public:
    // Example for SphereBrushShape.h
    virtual float CalculateSDF_Implementation(
        const FVector& WorldPos,
        const FBrushStroke& Stroke,
        float TerrainHeight
    ) const override;
};