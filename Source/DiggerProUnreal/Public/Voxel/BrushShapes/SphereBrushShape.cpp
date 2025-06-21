#include "SphereBrushShape.h"

#include "FBrushStroke.h"
#include "VoxelConversion.h"

float USphereBrushShape::CalculateSDF_Implementation(
const FVector& WorldPos,
const FBrushStroke& Stroke,
float TerrainHeight
) const
{
    // Apply brush offset
    FVector AdjustedBrushPos = Stroke.BrushPosition + Stroke.BrushOffset;
    
    float Distance = FVector::Dist(WorldPos, AdjustedBrushPos);
    float OuterRadius = Stroke.BrushRadius + Stroke.BrushFalloff;
    
    if (Distance > OuterRadius)
        return 0.0f;

    float SDFValue;
    bool bIsBelowTerrain = WorldPos.Z < TerrainHeight;

    if (Stroke.bDig)
    {
        if (Distance <= Stroke.BrushRadius)
        {
            SDFValue = FVoxelConversion::SDF_AIR;
        }
        else
        {
            float t = (Distance - Stroke.BrushRadius) / Stroke.BrushFalloff;
            t = FMath::SmoothStep(0.0f, 1.0f, t);
            SDFValue = bIsBelowTerrain ? 
                FMath::Lerp(FVoxelConversion::SDF_AIR, FVoxelConversion::SDF_SOLID, t) :
                FMath::Lerp(FVoxelConversion::SDF_AIR, 0.0f, t);
        }
    }
    else
    {
        if (Distance <= Stroke.BrushRadius)
        {
            SDFValue = FVoxelConversion::SDF_SOLID;
        }
        else
        {
            float t = (Distance - Stroke.BrushRadius) / Stroke.BrushFalloff;
            t = FMath::SmoothStep(0.0f, 1.0f, t);
            SDFValue = FMath::Lerp(FVoxelConversion::SDF_SOLID, 0.0f, t);
        }
    }

    return SDFValue * Stroke.BrushStrength;
}
