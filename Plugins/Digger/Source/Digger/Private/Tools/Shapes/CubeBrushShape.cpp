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
        LocalPos = Stroke.BrushRotation.UnrotateVector(LocalPos);

    const FVector HalfExtents = Stroke.bUseAdvancedCubeBrush
        ? FVector(Stroke.AdvancedCubeHalfExtentX, Stroke.AdvancedCubeHalfExtentY, Stroke.AdvancedCubeHalfExtentZ)
        : FVector(Stroke.BrushRadius);

    // Standard box SDF: negative inside, positive outside, 0 on surface
    const FVector q(FMath::Abs(LocalPos.X), FMath::Abs(LocalPos.Y), FMath::Abs(LocalPos.Z));
    const FVector d = q - HalfExtents;
    const float OutsideDist = FVector(FMath::Max(d.X,0.f), FMath::Max(d.Y,0.f), FMath::Max(d.Z,0.f)).Size();
    const float InsideDist  = FMath::Min(FMath::Max(d.X, FMath::Max(d.Y, d.Z)), 0.0f);
    const float SignedDist  = OutsideDist + InsideDist;

    float HalfFalloff = FMath::Max(Stroke.BrushFalloff * 0.5f, KINDA_SMALL_NUMBER);

    // Fully outside — no effect
    if (SignedDist > HalfFalloff)
        return 0.0f;

    const float RefSize  = HalfExtents.GetMax();
    const float SDFRange = 5.0f;
    float NormalisedSDF;

    if (SignedDist < -HalfFalloff)
    {
        // Deep interior: full gradient normalised to SDFRange
        NormalisedSDF = FMath::Clamp(-SignedDist / RefSize, 0.0f, 1.0f) * SDFRange;
    }
    else
    {
        // Transition band centred on the cube surface
        float Alpha = (SignedDist + HalfFalloff) / (2.0f * HalfFalloff);
        Alpha = FMath::SmoothStep(0.0f, 1.0f, Alpha);

        float InnerValue = FMath::Clamp(HalfFalloff / RefSize, 0.0f, 1.0f) * SDFRange;
        NormalisedSDF = FMath::Lerp(InnerValue, 0.0f, Alpha);
    }

    // Flip for Add mode
    if (!Stroke.bDig)
        NormalisedSDF = -NormalisedSDF;

    return NormalisedSDF * Stroke.BrushStrength;
}

bool UCubeBrushShape::IsWithinBounds(const FVector& WorldPos, const FBrushStroke& Stroke) const
{
    const FVector Center = Stroke.BrushPosition + Stroke.BrushOffset;
    FVector LocalPos = WorldPos - Center;

    if (!Stroke.BrushRotation.IsNearlyZero())
        LocalPos = Stroke.BrushRotation.UnrotateVector(LocalPos);

    const FVector HalfExtents = Stroke.bUseAdvancedCubeBrush
        ? FVector(Stroke.AdvancedCubeHalfExtentX, Stroke.AdvancedCubeHalfExtentY, Stroke.AdvancedCubeHalfExtentZ)
        : FVector(Stroke.BrushRadius);

    const FVector SearchExtents = HalfExtents + FVector(Stroke.BrushFalloff + 2.0f);

    return FMath::Abs(LocalPos.X) <= SearchExtents.X &&
           FMath::Abs(LocalPos.Y) <= SearchExtents.Y &&
           FMath::Abs(LocalPos.Z) <= SearchExtents.Z;
}

bool UCubeBrushShape::IsWithinInterior(const FVector& WorldPos, const FBrushStroke& Stroke) const
{
    const FVector Center = Stroke.BrushPosition + Stroke.BrushOffset;
    FVector LocalPos = WorldPos - Center;

    if (!Stroke.BrushRotation.IsNearlyZero())
        LocalPos = Stroke.BrushRotation.UnrotateVector(LocalPos);

    const FVector HalfExtents = Stroke.bUseAdvancedCubeBrush
        ? FVector(Stroke.AdvancedCubeHalfExtentX, Stroke.AdvancedCubeHalfExtentY, Stroke.AdvancedCubeHalfExtentZ)
        : FVector(Stroke.BrushRadius);

    return FMath::Abs(LocalPos.X) <= HalfExtents.X &&
           FMath::Abs(LocalPos.Y) <= HalfExtents.Y &&
           FMath::Abs(LocalPos.Z) <= HalfExtents.Z;
}

void UCubeBrushShape::GetPreviewData(
    FVector& OutCenter, FVector& OutExtents, FQuat& OutRotation,
    float& OutFalloff, EVoxelBrushType& OutBrushType, const FBrushStroke& Stroke) const
{
    OutCenter = Stroke.BrushPosition + Stroke.BrushOffset;
    OutExtents = Stroke.bUseAdvancedCubeBrush
        ? FVector(Stroke.AdvancedCubeHalfExtentX, Stroke.AdvancedCubeHalfExtentY, Stroke.AdvancedCubeHalfExtentZ)
        : FVector(Stroke.BrushRadius);
    OutRotation  = Stroke.BrushRotation.Quaternion();
    OutFalloff   = Stroke.BrushFalloff;
    OutBrushType = EVoxelBrushType::Cube;
}