// SphereBrushShape.h
#pragma once

#include "VoxelBrushShape.h"
#include "SphereBrushShape.generated.h"

UCLASS()
class DIGGERPROUNREAL_API USphereBrushShape : public UVoxelBrushShape
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
