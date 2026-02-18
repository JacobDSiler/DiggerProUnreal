#include "Shapes/CubeBrushShape.h"

#include "FBrushStroke.h"
#include "VoxelConversion.h"

float UCubeBrushShape::CalculateSDF_Implementation(
    const FVector& WorldPos,
    const FBrushStroke& Stroke,
    float TerrainHeight
) const
{
    const FVector Center = Stroke.BrushPosition + Stroke.BrushOffset;
    FVector LocalPos = WorldPos - Center;

    if (!Stroke.BrushRotation.IsNearlyZero())
    {
        LocalPos = Stroke.BrushRotation.UnrotateVector(LocalPos);
    }

    const FVector HalfExtents = Stroke.bUseAdvancedCubeBrush
        ? FVector(
            Stroke.AdvancedCubeHalfExtentX,
            Stroke.AdvancedCubeHalfExtentY,
            Stroke.AdvancedCubeHalfExtentZ)
        : FVector(Stroke.BrushRadius);

    const FVector q(
        FMath::Abs(LocalPos.X),
        FMath::Abs(LocalPos.Y),
        FMath::Abs(LocalPos.Z)
    );

    const FVector d = q - HalfExtents;

    const float OutsideDist = FVector(
        FMath::Max(d.X, 0.0f),
        FMath::Max(d.Y, 0.0f),
        FMath::Max(d.Z, 0.0f)
    ).Size();

    const float InsideDist = FMath::Min(FMath::Max(d.X, FMath::Max(d.Y, d.Z)), 0.0f);

    const float SignedDist = OutsideDist + InsideDist;

    const float HalfFalloff = Stroke.BrushFalloff * 0.5f;

    // ⭐ HARD CLAMP: no effect outside falloff
    if (SignedDist > HalfFalloff)
    {
        return 0.0f;
    }

    float RawSDF = Stroke.bDig ? -SignedDist : SignedDist;

    if (Stroke.BrushFalloff > KINDA_SMALL_NUMBER &&
        FMath::Abs(SignedDist) < HalfFalloff)
    {
        float Alpha = (SignedDist + HalfFalloff) / Stroke.BrushFalloff;
        Alpha = FMath::SmoothStep(0.0f, 1.0f, Alpha);

        RawSDF = Stroke.bDig
            ? FMath::Lerp(FVoxelConversion::SDF_AIR, FVoxelConversion::SDF_SOLID, Alpha)
            : FMath::Lerp(FVoxelConversion::SDF_SOLID, FVoxelConversion::SDF_AIR, Alpha);
    }

    return RawSDF * Stroke.BrushStrength;
}




// Keep the Fixed Bounds check from before
bool UCubeBrushShape::IsWithinBounds(const FVector& WorldPos, const FBrushStroke& Stroke) const
{
    const FVector Center = Stroke.BrushPosition + Stroke.BrushOffset;
    FVector LocalPos = WorldPos - Center;

    if (!Stroke.BrushRotation.IsNearlyZero())
    {
        LocalPos = Stroke.BrushRotation.UnrotateVector(LocalPos);
    }

    const FVector HalfExtents = Stroke.bUseAdvancedCubeBrush
        ? FVector(
            Stroke.AdvancedCubeHalfExtentX,
            Stroke.AdvancedCubeHalfExtentY,
            Stroke.AdvancedCubeHalfExtentZ)
        : FVector(Stroke.BrushRadius);

    const FVector SearchExtents = HalfExtents + FVector(Stroke.BrushFalloff + 2.0f);

    return FMath::Abs(LocalPos.X) <= SearchExtents.X &&
           FMath::Abs(LocalPos.Y) <= SearchExtents.Y &&
           FMath::Abs(LocalPos.Z) <= SearchExtents.Z;
}

bool UCubeBrushShape::IsWithinInterior(const FVector& WorldPos, const FBrushStroke& Stroke) const
{
    // 1. Transform into brush-local space
    const FVector Center = Stroke.BrushPosition + Stroke.BrushOffset;
    FVector LocalPos = WorldPos - Center;

    if (!Stroke.BrushRotation.IsNearlyZero())
    {
        LocalPos = Stroke.BrushRotation.UnrotateVector(LocalPos);
    }

    // 2. True half-extents (must match SDF exactly)
    const FVector HalfExtents = Stroke.bUseAdvancedCubeBrush
        ? FVector(
            Stroke.AdvancedCubeHalfExtentX,
            Stroke.AdvancedCubeHalfExtentY,
            Stroke.AdvancedCubeHalfExtentZ)
        : FVector(Stroke.BrushRadius);

    // 3. Interior check: inside means all axes within half-extents
    return  FMath::Abs(LocalPos.X) <= HalfExtents.X &&
            FMath::Abs(LocalPos.Y) <= HalfExtents.Y &&
            FMath::Abs(LocalPos.Z) <= HalfExtents.Z;
}


void UCubeBrushShape::GetPreviewData(
    FVector& OutCenter,
    FVector& OutExtents,
    FQuat& OutRotation,
    float& OutFalloff,
    EVoxelBrushType& OutBrushType,
    const FBrushStroke& Stroke) const
{
    OutCenter = Stroke.BrushPosition + Stroke.BrushOffset;

    // These are HALF-extents — must match SDF and interior check
    OutExtents = Stroke.bUseAdvancedCubeBrush
        ? FVector(
            Stroke.AdvancedCubeHalfExtentX,
            Stroke.AdvancedCubeHalfExtentY,
            Stroke.AdvancedCubeHalfExtentZ)
        : FVector(Stroke.BrushRadius);

    OutRotation = Stroke.BrushRotation.Quaternion();
    OutFalloff  = Stroke.BrushFalloff;
    OutBrushType = EVoxelBrushType::Cube;
}





