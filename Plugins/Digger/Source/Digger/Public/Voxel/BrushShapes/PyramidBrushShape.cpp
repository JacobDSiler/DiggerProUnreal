#include "PyramidBrushShape.h"

#include "FBrushStroke.h"
#include "VoxelConversion.h"
#include "Math/UnrealMathUtility.h"

float UPyramidBrushShape::CalculateSDF_Implementation(
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
    
    // Use BrushLength for height, BrushRadius for base size
    float Height = Stroke.BrushLength;
    float HalfHeight = Height * 0.5f;
    float BaseSize = Stroke.BrushRadius;
    
    // Use BrushAngle to control pyramid slope (optional)
    float SlopeModifier = FMath::Tan(FMath::DegreesToRadians(Stroke.BrushAngle)) * 0.1f;
    
    // Calculate pyramid SDF (square pyramid)
    float Distance;
    
    if (LocalPos.Z < -HalfHeight)
    {
        // Below pyramid base
        float BaseDistance = FMath::Max(FMath::Abs(LocalPos.X), FMath::Abs(LocalPos.Y)) - BaseSize;
        if (BaseDistance <= 0.0f)
        {
            Distance = -LocalPos.Z - HalfHeight;
        }
        else
        {
            Distance = FMath::Sqrt(BaseDistance * BaseDistance + (-LocalPos.Z - HalfHeight) * (-LocalPos.Z - HalfHeight));
        }
    }
    else if (LocalPos.Z > HalfHeight)
    {
        // Above pyramid tip
        Distance = FVector::Dist(LocalPos, FVector(0, 0, HalfHeight));
    }
    else
    {
        // Within pyramid height
        float HeightRatio = (LocalPos.Z + HalfHeight) / Height;
        float PyramidSize = BaseSize * (1.0f - HeightRatio + SlopeModifier);
        PyramidSize = FMath::Max(0.0f, PyramidSize);
        
        float HorizontalDistance = FMath::Max(FMath::Abs(LocalPos.X), FMath::Abs(LocalPos.Y));
        Distance = HorizontalDistance - PyramidSize;
    }
    
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

bool UPyramidBrushShape::IsWithinBounds(const FVector& WorldPos, const FBrushStroke& Stroke) const
{
    // Temporary fallback - use simple sphere bounds
    const FVector Delta = WorldPos - Stroke.BrushPosition;
    const float DistanceSq = Delta.SizeSquared();
    const float RadiusSq = Stroke.BrushRadius * Stroke.BrushRadius;
    return DistanceSq <= RadiusSq;
}
