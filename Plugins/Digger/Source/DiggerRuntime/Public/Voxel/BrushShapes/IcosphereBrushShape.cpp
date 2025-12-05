#include "IcosphereBrushShape.h"

#include "FBrushStroke.h"
#include "VoxelConversion.h"
#include "Math/UnrealMathUtility.h"

float UIcosphereBrushShape::CalculateSDF_Implementation(
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
    
    float BaseDistance = LocalPos.Size();
    
    // Early out for performance
    if (BaseDistance > Stroke.BrushRadius + Stroke.BrushFalloff)
        return 0.0f;
    
    // Apply icosphere distortion using NumSteps as subdivision level
    float DistortionAmount = 0.1f; // 10% distortion
    if (BaseDistance > 0.0f)
    {
        FVector NormalizedPos = LocalPos / BaseDistance;
        float Distortion = GetIcosphereDistortion(NormalizedPos, Stroke.NumSteps);
        BaseDistance += Distortion * DistortionAmount * Stroke.BrushRadius;
    }
    
    float Distance = BaseDistance - Stroke.BrushRadius;
    
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

float UIcosphereBrushShape::GetIcosphereDistortion(const FVector& NormalizedPos, int32 Subdivisions) const
{
    // Simple approximation of icosphere surface using spherical harmonics
    float Phi = FMath::Atan2(NormalizedPos.Y, NormalizedPos.X);
    float Theta = FMath::Acos(NormalizedPos.Z);
    
    // Create faceted appearance based on subdivision level
    int32 PhiSteps = FMath::Max(1, Subdivisions * 2);
    int32 ThetaSteps = FMath::Max(1, Subdivisions);
    
    float QuantizedPhi = FMath::Floor(Phi * PhiSteps / (2.0f * PI)) * (2.0f * PI) / PhiSteps;
    float QuantizedTheta = FMath::Floor(Theta * ThetaSteps / PI) * PI / ThetaSteps;
    
    // Create subtle surface variation
    float Distortion = FMath::Sin(QuantizedPhi * 3.0f) * FMath::Sin(QuantizedTheta * 2.0f) * 0.1f;
    
    return Distortion;
}

bool UIcosphereBrushShape::IsWithinBounds(const FVector& WorldPos, const FBrushStroke& Stroke) const
{
    // Temporary fallback - use simple sphere bounds
    const FVector Delta = WorldPos - Stroke.BrushPosition;
    const float DistanceSq = Delta.SizeSquared();
    const float RadiusSq = Stroke.BrushRadius * Stroke.BrushRadius;
    return DistanceSq <= RadiusSq;
}
