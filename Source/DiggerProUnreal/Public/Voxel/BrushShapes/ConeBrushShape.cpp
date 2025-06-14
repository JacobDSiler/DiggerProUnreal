// ConeBrushShape.cpp
#include "ConeBrushShape.h"
#include "VoxelConversion.h"
#include "Math/UnrealMathUtility.h"

// UConeBrushShape.cpp
float UConeBrushShape::CalculateSDF_Implementation(
    const FVector& WorldPos,
    const FVector& BrushCenter,
    float Radius,
    float Strength,
    float Falloff,
    float TerrainHeight,
    bool bDig) const
{
    // Get brush parameters
    float Height = 50.f;
    float Angle = 60.f;
    bool bFilled = true;
//    FRotator BrushRotation = FRotator(180.f,0.f,0.f);
    FVector BrushOffset = FVector(0);
    
    // Apply offset to brush center
    FVector AdjustedBrushCenter = BrushCenter + BrushOffset;
    
    // Transform to brush space
    FTransform BrushTransform(BrushRotation, AdjustedBrushCenter);
    FTransform InvBrushTransform = BrushTransform.Inverse();
    FVector LocalPos = InvBrushTransform.TransformPosition(WorldPos);
    
    // Calculate cone parameters
    float AngleRad = FMath::DegreesToRadians(Angle);
    float RadiusAtBase = Height * FMath::Tan(AngleRad);
    
    // Calculate SDF for cone (matching your original logic)
    float radiusAtZ = (LocalPos.Z / Height) * RadiusAtBase;
    float d = FVector2D(LocalPos.X, LocalPos.Y).Size() - radiusAtZ;
    
    float dist;
    if (bFilled)
    {
        // Hard caps: bottom at Z=0, top at Z=Height
        float cappedZ = FMath::Clamp(LocalPos.Z, 0.0f, Height);
        radiusAtZ = (cappedZ / Height) * RadiusAtBase;
        d = FVector2D(LocalPos.X, LocalPos.Y).Size() - radiusAtZ;

        // Only inside the cylinder-ish cone body
        float dz = 0.0f;
        if (LocalPos.Z < 0.0f) dz = LocalPos.Z;
        else if (LocalPos.Z > Height) dz = LocalPos.Z - Height;

        dist = FMath::Max(d, dz); // flat bottom/top cap
    }
    else
    {
        // Keep the soft SDF for shell-style cones
        dist = FMath::Max(d, -LocalPos.Z);
        dist = FMath::Min(dist, FMath::Max(d, LocalPos.Z - Height));
    }
    
    // Early out if outside influence
    if (dist > Falloff)
        return 0.0f;
    
    // Skip interior if not filled (for hollow cones)
    const float SDFTransitionBand = FVoxelConversion::LocalVoxelSize * 2.0f;
    if (!bFilled && dist < -SDFTransitionBand)
    {
        return 0.0f;
    }
    
    // Fix for tip issue: Add a small bias to ensure the tip is properly handled
    if (!bDig && LocalPos.Z < FVoxelConversion::LocalVoxelSize)
    {
        // Near the tip, make sure the SDF is negative enough
        dist = FMath::Min(dist, -0.1f);
    }
    
    // Convert distance to SDF value
    float SDFValue;
    if (bDig)
    {
        SDFValue = dist <= 0.0f ? 
            FVoxelConversion::SDF_AIR : 
            FMath::Lerp(FVoxelConversion::SDF_AIR, 0.0f, dist / Falloff);
    }
    else
    {
        SDFValue = dist <= 0.0f ? 
            FVoxelConversion::SDF_SOLID : 
            FMath::Lerp(FVoxelConversion::SDF_SOLID, 0.0f, dist / Falloff);
    }

    return SDFValue * Strength;
}