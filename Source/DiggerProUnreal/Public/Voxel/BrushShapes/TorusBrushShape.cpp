#include "TorusBrushShape.h"

#include "FBrushStroke.h"
#include "VoxelConversion.h"

float UTorusBrushShape::CalculateSDF_Implementation(
    const FVector& WorldPos,
    const FBrushStroke& Stroke,
    float TerrainHeight
) const
{
    // Apply brush offset
    FVector AdjustedBrushPos = Stroke.BrushPosition + Stroke.BrushOffset;
    
    // Transform to local space
    FVector LocalPos = WorldPos - AdjustedBrushPos;
    if (!Stroke.BrushRotation.IsZero())
    {
        FQuat InverseRotation = Stroke.BrushRotation.Quaternion().Inverse();
        LocalPos = InverseRotation.RotateVector(LocalPos);
    }
    
    // Use TorusInnerRadius for more control
    float MajorRadius = Stroke.BrushRadius;
    float MinorRadius = Stroke.TorusInnerRadius;
    
    // Distance from Z-axis in XY plane
    float RadialDistance = FMath::Sqrt(LocalPos.X * LocalPos.X + LocalPos.Y * LocalPos.Y);
    
    // Distance to the torus surface
    float TorusDistance = RadialDistance - MajorRadius;
    float Distance = FMath::Sqrt(TorusDistance * TorusDistance + LocalPos.Z * LocalPos.Z) - MinorRadius;
    
    if (Distance > Stroke.BrushFalloff)
        return 0.0f;

    // Apply SDF logic
    float SDFValue;
    bool bIsBelowTerrain = WorldPos.Z < TerrainHeight;

    if (Stroke.bDig)
    {
        if (Distance <= 0.0f)
        {
            SDFValue = FVoxelConversion::SDF_AIR;
        }
        else
        {
            float t = Distance / Stroke.BrushFalloff;
            t = FMath::SmoothStep(0.0f, 1.0f, t);
            SDFValue = bIsBelowTerrain ? 
                FMath::Lerp(FVoxelConversion::SDF_AIR, FVoxelConversion::SDF_SOLID, t) :
                FMath::Lerp(FVoxelConversion::SDF_AIR, 0.0f, t);
        }
    }
    else
    {
        if (Distance <= 0.0f)
        {
            SDFValue = FVoxelConversion::SDF_SOLID;
        }
        else
        {
            float t = Distance / Stroke.BrushFalloff;
            t = FMath::SmoothStep(0.0f, 1.0f, t);
            SDFValue = FMath::Lerp(FVoxelConversion::SDF_SOLID, 0.0f, t);
        }
    }

    return SDFValue * Stroke.BrushStrength;
}

bool UTorusBrushShape::IsWithinBounds(const FVector& WorldPos, const FBrushStroke& Stroke) const
{
    return Super::IsWithinBounds(WorldPos, Stroke);
}


