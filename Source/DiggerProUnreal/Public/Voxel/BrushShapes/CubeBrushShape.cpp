#include "CubeBrushShape.h"

#include "FBrushStroke.h"
#include "VoxelConversion.h"


float UCubeBrushShape::CalculateSDF_Implementation(
    const FVector& WorldPos,
    const FBrushStroke& Stroke,
    float TerrainHeight
) const
{
    FVector Center = Stroke.BrushPosition + Stroke.BrushOffset;
    FVector LocalPos = WorldPos - Center;
    if (!Stroke.BrushRotation.IsNearlyZero())
    {
        LocalPos = Stroke.BrushRotation.UnrotateVector(LocalPos);
    }

    FVector HalfExtents = Stroke.bUseAdvancedCubeBrush
        ? FVector(
            Stroke.AdvancedCubeHalfExtentX,
            Stroke.AdvancedCubeHalfExtentY,
            Stroke.AdvancedCubeHalfExtentZ
        )
        : FVector(Stroke.BrushRadius);

    // Calculate distance to each face
    const float DistX = FMath::Abs(LocalPos.X) - HalfExtents.X;
    const float DistY = FMath::Abs(LocalPos.Y) - HalfExtents.Y;
    const float DistZ = FMath::Abs(LocalPos.Z) - HalfExtents.Z;

    float Distance;
    if (DistX <= 0 && DistY <= 0 && DistZ <= 0)
    {
        Distance = FMath::Max(FMath::Max(DistX, DistY), DistZ);
    }
    else
    {
        const float OutsideX = FMath::Max(DistX, 0.0f);
        const float OutsideY = FMath::Max(DistY, 0.0f);
        const float OutsideZ = FMath::Max(DistZ, 0.0f);
        Distance = FMath::Sqrt(OutsideX * OutsideX + OutsideY * OutsideY + OutsideZ * OutsideZ);
    }

    float t = 0.0f;
    if (Stroke.BrushFalloff > 0.0f)
    {
        t = FMath::Clamp(Distance / Stroke.BrushFalloff, 0.0f, 1.0f);
        t = FMath::SmoothStep(0.0f, 1.0f, t);
    }

    float SDFValue = 0.0f;

    if (Stroke.bDig)
    {
        if (Distance <= 0.0f)
        {
            SDFValue = FVoxelConversion::SDF_AIR;
        }
        else
        {
            SDFValue = FMath::Lerp(FVoxelConversion::SDF_AIR, FVoxelConversion::SDF_SOLID, t);
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
            SDFValue = FMath::Lerp(FVoxelConversion::SDF_SOLID, 0.0f, t);
        }
    }

    return SDFValue * Stroke.BrushStrength;
}


bool UCubeBrushShape::IsWithinBounds(const FVector& WorldPos, const FBrushStroke& Stroke) const
{
    const FVector Center = Stroke.BrushPosition + Stroke.BrushOffset;
    FVector LocalPos = WorldPos - Center;

    // Match the rotation logic exactly as used in CalculateSDF
    if (!Stroke.BrushRotation.IsNearlyZero())
    {
        LocalPos = Stroke.BrushRotation.UnrotateVector(LocalPos);
    }

    const FVector HalfExtents = Stroke.bUseAdvancedCubeBrush
        ? FVector(
            Stroke.AdvancedCubeHalfExtentX,
            Stroke.AdvancedCubeHalfExtentY,
            Stroke.AdvancedCubeHalfExtentZ
        )
        : FVector(Stroke.BrushRadius);

    // Same box SDF logic from CalculateSDF
    FVector Q = FVector(
        FMath::Abs(LocalPos.X),
        FMath::Abs(LocalPos.Y),
        FMath::Abs(LocalPos.Z)
    ) - HalfExtents;

    float OutsideDistance = FVector(
        FMath::Max(Q.X, 0.f),
        FMath::Max(Q.Y, 0.f),
        FMath::Max(Q.Z, 0.f)
    ).Size();

    float InsideDistance = FMath::Min(FMath::Max(FMath::Max(Q.X, Q.Y), Q.Z), 0.f);

    float Distance = OutsideDistance + InsideDistance;

    return Distance <= 0.0f;
}

