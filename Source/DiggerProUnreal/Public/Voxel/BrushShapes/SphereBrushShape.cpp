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
    const FVector AdjustedBrushPos = Stroke.BrushPosition + Stroke.BrushOffset;
    const float Distance = FVector::Dist(WorldPos, AdjustedBrushPos);

    // Define fade zone
    const float OuterRadius = Stroke.BrushRadius + Stroke.BrushFalloff;
    const float MaxFadeDistance = OuterRadius + FVoxelConversion::LocalVoxelSize;

    // Outside influence zone — skip writing
    if (Distance > MaxFadeDistance)
        return 0.0f;

    float SDFValue;
    const bool bIsBelowTerrain = WorldPos.Z < TerrainHeight;

    // Smooth falloff factor
    const float FadeRange = Stroke.BrushFalloff + FVoxelConversion::LocalVoxelSize;
    const float t = FMath::Clamp((Distance - Stroke.BrushRadius) / FadeRange, 0.0f, 1.0f);
    const float SmoothT = FMath::SmoothStep(0.0f, 1.0f, t);

    if (Stroke.bDig)
    {
        if (Distance <= Stroke.BrushRadius)
        {
            SDFValue = FVoxelConversion::SDF_AIR;
        }
        else
        {
            SDFValue = bIsBelowTerrain
                ? FMath::Lerp(FVoxelConversion::SDF_AIR, FVoxelConversion::SDF_SOLID, SmoothT)
                : FMath::Lerp(FVoxelConversion::SDF_AIR, 0.0f, SmoothT);
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
            SDFValue = FMath::Lerp(FVoxelConversion::SDF_SOLID, 0.0f, SmoothT);
        }
    }

    // Clamp and threshold to avoid unnecessary writes
    const float FinalSDF = FMath::Clamp(SDFValue * Stroke.BrushStrength, -1.0f, 1.0f);
    return FMath::Abs(FinalSDF) > 0.01f ? FinalSDF : 0.0f;
}



bool USphereBrushShape::IsWithinBounds(const FVector& WorldPos, const FBrushStroke& Stroke) const
{
    const FVector AdjustedBrushPos = Stroke.BrushPosition + Stroke.BrushOffset;
    const float MaxRadius = Stroke.BrushRadius + Stroke.BrushFalloff + FVoxelConversion::LocalVoxelSize;
    const float DistanceSq = FVector::DistSquared(WorldPos, AdjustedBrushPos);
    return DistanceSq <= FMath::Square(MaxRadius);
}
