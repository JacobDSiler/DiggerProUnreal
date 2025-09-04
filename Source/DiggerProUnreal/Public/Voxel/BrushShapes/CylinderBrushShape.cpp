#include "CylinderBrushShape.h"

#include "FBrushStroke.h"
#include "VoxelConversion.h"
#include "Math/UnrealMathUtility.h"

float UCylinderBrushShape::CalculateSDF_Implementation(
    const FVector& WorldPos,
    const FBrushStroke& Stroke,
    float TerrainHeight
) const
{
    FVector LocalPos = WorldPos - (Stroke.BrushPosition + Stroke.BrushOffset);
    if (!Stroke.BrushRotation.IsNearlyZero())
    {
        LocalPos = Stroke.BrushRotation.UnrotateVector(LocalPos);
    }

    float Radius = Stroke.BrushRadius;
    float HalfHeight = Stroke.BrushLength * 0.5f;
    float Falloff = Stroke.BrushFalloff;

    // SDF for solid or hollow cylinder
    float RadialDist = FVector2D(LocalPos.X, LocalPos.Y).Size();
    float d = 0.0f;

    if (!Stroke.bIsFilled)
    {
        float InnerRadius = FMath::Max(0.0f, Radius - Stroke.WallThickness);
        float distToOuter = FMath::Sqrt(FMath::Square(FMath::Max(RadialDist - Radius, 0.0f)) + FMath::Square(FMath::Max(FMath::Abs(LocalPos.Z) - HalfHeight, 0.0f)));
        float distToInner = FMath::Sqrt(FMath::Square(FMath::Max(InnerRadius - RadialDist, 0.0f)) + FMath::Square(FMath::Max(FMath::Abs(LocalPos.Z) - HalfHeight, 0.0f)));
        d = FMath::Min(distToOuter, distToInner);
        if (RadialDist < InnerRadius && FMath::Abs(LocalPos.Z) < HalfHeight)
            d = -d; // Inside hollow
    }
    else
    {
        float dx = RadialDist - Radius;
        float dz = FMath::Abs(LocalPos.Z) - HalfHeight;
        float outside = FMath::Max(dx, dz);
        float inside = FMath::Min(FMath::Max(dx, dz), 0.0f);
        d = outside + inside;
    }

    float t = 0.0f;
    if (d > 0.0f && Falloff > 0.0f)
    {
        t = FMath::Clamp(FMath::SmoothStep(0.0f, 1.0f, d / Falloff), 0.0f, 1.0f);
    }

    bool bIsBelowTerrain = WorldPos.Z < TerrainHeight;
    float SDFValue = 0.0f;

    if (Stroke.bDig)
    {
        if (bIsBelowTerrain)
        {
            if (d <= 0.0f)
            {
                SDFValue = FVoxelConversion::SDF_AIR;
            }
            else if (d <= Falloff)
            {
                SDFValue = FMath::Lerp(FVoxelConversion::SDF_AIR, 0.0f, t);
            }
        }
    }
    else
    {
        if (!bIsBelowTerrain)
        {
            if (d <= 0.0f)
            {
                SDFValue = FVoxelConversion::SDF_SOLID;
            }
            else if (d <= Falloff)
            {
                SDFValue = FMath::Lerp(FVoxelConversion::SDF_SOLID, 0.0f, t);
            }
        }
    }

    return SDFValue * Stroke.BrushStrength;
}



bool UCylinderBrushShape::IsWithinBounds(const FVector& WorldPos, const FBrushStroke& Stroke) const
{
    // Apply rotation to get local position
    FVector LocalPos = WorldPos - Stroke.BrushPosition;
    if (!Stroke.BrushRotation.IsNearlyZero())
    {
        LocalPos = Stroke.BrushRotation.UnrotateVector(LocalPos);
    }
    
    const float Radius = Stroke.BrushRadius;
    const float HalfHeight = Stroke.BrushLength / 2.0f;
    
    // Check height bounds first (Z-axis in local space)
    if (FMath::Abs(LocalPos.Z) > HalfHeight)
        return false;
    
    // Check radial distance (XY plane in local space)
    const float RadialDistanceSq = LocalPos.X * LocalPos.X + LocalPos.Y * LocalPos.Y;
    const float RadiusSq = Radius * Radius;
    
    return RadialDistanceSq <= RadiusSq;
}
