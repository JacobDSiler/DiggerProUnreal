#include "Shapes/SphereBrushShape.h"
#include "FBrushStroke.h"
#include "VoxelConversion.h"

float USphereBrushShape::CalculateSDF_Implementation(
    const FVector& WorldPos,
    const FBrushStroke& Stroke,
    float TerrainHeight
) const
{
    FVector Center = Stroke.BrushPosition + Stroke.BrushOffset;
    float Distance = FVector::Dist(WorldPos, Center);

    // True Euclidean SDF in voxel units.
    //
    // (BrushRadius - Distance) / LocalVoxelSize gives:
    //   Positive inside  the sphere (air for dig)
    //   Zero at          the sphere surface (exact zero-crossing)
    //   Negative outside the sphere (solid)
    //
    // Gradient magnitude = exactly 1.0 SDF unit per voxel in all directions.
    // This is the mathematical ideal for marching cubes interpolation —
    // every pair of adjacent voxels bracketing the surface will differ by
    // exactly LocalVoxelSize worth of SDF, giving precise smooth placement
    // of the zero-crossing regardless of brush size.
    const float LocalVoxelSize = FVoxelConversion::LocalVoxelSize;
    const float SDFRange = 5.0f;

    float SDF = (Stroke.BrushRadius - Distance) / LocalVoxelSize;
    SDF = FMath::Clamp(SDF, -SDFRange, SDFRange);

    if (!Stroke.bDig)
        SDF = -SDF;

    return SDF * Stroke.BrushStrength;
}

bool USphereBrushShape::IsWithinBounds(const FVector& WorldPos, const FBrushStroke& Stroke) const
{
    FVector Center = Stroke.BrushPosition + Stroke.BrushOffset;
    float DistanceSq = FVector::DistSquared(WorldPos, Center);
    float Radius = Stroke.BrushRadius + Stroke.BrushFalloff + 2.0f;
    return DistanceSq <= (Radius * Radius);
}

bool USphereBrushShape::IsWithinInterior(const FVector& WorldPos, const FBrushStroke& Stroke) const
{
    FVector Center = Stroke.BrushPosition + Stroke.BrushOffset;
    float DistanceSq = FVector::DistSquared(WorldPos, Center);
    return DistanceSq <= (Stroke.BrushRadius * Stroke.BrushRadius);
}

void USphereBrushShape::GetPreviewData(FVector& OutCenter, FVector& OutExtents, FQuat& OutRotation, float& OutFalloff, EVoxelBrushType& OutBrushType, const FBrushStroke& Stroke) const
{
    OutCenter    = Stroke.BrushPosition + Stroke.BrushOffset;
    OutExtents   = FVector(Stroke.BrushRadius);
    OutRotation  = FQuat::Identity;
    OutFalloff   = Stroke.BrushFalloff;
    OutBrushType = EVoxelBrushType::Sphere;
}