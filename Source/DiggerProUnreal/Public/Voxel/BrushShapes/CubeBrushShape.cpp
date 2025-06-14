// CubeBrushShape.cpp
#include "CubeBrushShape.h"
#include "VoxelConversion.h"
#include "Math/UnrealMathUtility.h"

// UCubeBrushShape.cpp
float UCubeBrushShape::CalculateSDF_Implementation(
    const FVector& WorldPos,
    const FVector& BrushCenter,
    float Radius,
    float Strength,
    float Falloff,
    float TerrainHeight,
    bool bDig) const
{
    // Get brush parameters
//    FRotator BrushRotation = GetBrushRotation();
    FVector BrushOffset = FVector(0);
    
    // Apply offset to brush center
    FVector AdjustedBrushCenter = BrushCenter + BrushOffset;
    
    // Transform to brush space
    FTransform BrushTransform(BrushRotation, AdjustedBrushCenter);
    FTransform InvBrushTransform = BrushTransform.Inverse();
    FVector LocalPos = InvBrushTransform.TransformPosition(WorldPos);
    
    // Calculate distance to cube surface
    FVector HalfExtents(Radius);
    FVector Distances = FVector(
        FMath::Abs(LocalPos.X) - HalfExtents.X,
        FMath::Abs(LocalPos.Y) - HalfExtents.Y,
        FMath::Abs(LocalPos.Z) - HalfExtents.Z
    );
    
    float OutsideDistance = FVector(
        FMath::Max(Distances.X, 0.0f),
        FMath::Max(Distances.Y, 0.0f),
        FMath::Max(Distances.Z, 0.0f)
    ).Size();
    
    float InsideDistance = FMath::Min(FMath::Max(Distances.X, FMath::Max(Distances.Y, Distances.Z)), 0.0f);
    
    float dist = OutsideDistance + InsideDistance;

    // Early out if outside influence
    if (dist > Falloff)
        return 0.0f;

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