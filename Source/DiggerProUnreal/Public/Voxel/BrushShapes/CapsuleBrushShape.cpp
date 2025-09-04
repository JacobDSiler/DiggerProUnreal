#include "CapsuleBrushShape.h"

#include "FBrushStroke.h"
#include "VoxelConversion.h"
#include "Math/UnrealMathUtility.h"

float UCapsuleBrushShape::CalculateSDF_Implementation(
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
    
    // Use BrushLength for the cylindrical part
    float CylinderLength = Stroke.BrushLength;
    float HalfLength = CylinderLength * 0.5f;
    float Radius = Stroke.BrushRadius;
    
    // Clamp the Z coordinate to the cylindrical part
    float ClampedZ = FMath::Clamp(LocalPos.Z, -HalfLength, HalfLength);
    
    // Find the closest point on the central axis
    FVector ClosestPoint = FVector(0.0f, 0.0f, ClampedZ);
    
    // Distance from world position to closest point on axis
    float Distance = FVector::Dist(LocalPos, ClosestPoint) - Radius;
    
    if (Distance > Stroke.BrushFalloff)
        return 0.0f;

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

bool UCapsuleBrushShape::IsWithinBounds(const FVector& WorldPos, const FBrushStroke& Stroke) const
{
    // Temporary fallback - use simple sphere bounds
    const FVector Delta = WorldPos - Stroke.BrushPosition;
    const float DistanceSq = Delta.SizeSquared();
    const float RadiusSq = Stroke.BrushRadius * Stroke.BrushRadius;
    return DistanceSq <= RadiusSq;
}

