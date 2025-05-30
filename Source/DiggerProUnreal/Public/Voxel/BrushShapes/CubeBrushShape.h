#pragma once

#include "VoxelBrushShape.h"
#include "CubeBrushShape.generated.h"

UCLASS()
class DIGGERPROUNREAL_API UCubeBrushShape : public UVoxelBrushShape
{
    GENERATED_BODY()
public:
    virtual float CalculateSDF_Implementation(
        const FVector& WorldPos,
        const FVector& BrushCenter,
        float Radius,
        float Strength,
        float Falloff, float TerrainHeight, bool bDig
    ) const override;
};
