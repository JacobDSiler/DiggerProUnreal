#include "ConeBrushShape.h"

#include "FBrushStroke.h"
#include "VoxelConversion.h"

// --- Cone Brush SDF ---
// --- Cone Brush SDF ---
float UConeBrushShape::CalculateSDF_Implementation(const FVector& WorldPos, const FBrushStroke& Stroke, float TerrainHeight) const
{
    FVector LocalPos = WorldPos - (Stroke.BrushPosition + Stroke.BrushOffset);
    if (!Stroke.BrushRotation.IsNearlyZero())
    {
        LocalPos = Stroke.BrushRotation.UnrotateVector(LocalPos);
    }

    float Height = Stroke.BrushLength;
    float AngleRad = FMath::DegreesToRadians(Stroke.BrushAngle);
    float RadiusAtBase = Height * FMath::Tan(AngleRad);

    FVector2D q(FVector2D(LocalPos.X, LocalPos.Y).Size(), LocalPos.Z);

    float k = RadiusAtBase / Height;
    float Distance = 0.f;

    if (q.Y < 0.0f || q.Y > Height)
    {
        float dx = q.X;
        float dz = FMath::Min(FMath::Abs(q.Y), FMath::Abs(q.Y - Height));
        Distance = FVector2D(dx, dz).Size();
    }
    else
    {
        Distance = q.X - k * q.Y;
    }

    // Adjust for filled mode
    if (Stroke.bIsFilled)
    {
        Distance = FMath::Abs(Distance); // Make negative distance solid when filled
    }

    float t = (Stroke.BrushFalloff > 0.0f)
        ? FMath::SmoothStep(0.0f, 1.0f, FMath::Clamp(Distance / Stroke.BrushFalloff, 0.0f, 1.0f))
        : 0.0f;

    float SDFValue = Stroke.bDig
        ? FMath::Lerp(FVoxelConversion::SDF_AIR, FVoxelConversion::SDF_SOLID, t)
        : FMath::Lerp(FVoxelConversion::SDF_SOLID, 0.0f, t);

    return SDFValue * Stroke.BrushStrength;
}

bool UConeBrushShape::IsWithinBounds(const FVector& WorldPos, const FBrushStroke& Stroke) const
{
    FVector LocalPos = WorldPos - (Stroke.BrushPosition + Stroke.BrushOffset);
    if (!Stroke.BrushRotation.IsNearlyZero())
    {
        LocalPos = Stroke.BrushRotation.UnrotateVector(LocalPos);
    }

    float Height = Stroke.BrushLength;
    float AngleRad = FMath::DegreesToRadians(Stroke.BrushAngle);
    float RadiusAtBase = Height * FMath::Tan(AngleRad);

    FVector2D q(FVector2D(LocalPos.X, LocalPos.Y).Size(), LocalPos.Z);
    float k = RadiusAtBase / Height;
    float Distance = 0.f;

    if (q.Y < 0.0f || q.Y > Height)
    {
        float dx = q.X;
        float dz = FMath::Min(FMath::Abs(q.Y), FMath::Abs(q.Y - Height));
        Distance = FVector2D(dx, dz).Size();
    }
    else
    {
        Distance = q.X - k * q.Y;
    }

    if (Stroke.bIsFilled)
    {
        Distance = FMath::Abs(Distance); // Include interior for filled cones
    }

    return Distance <= Stroke.BrushFalloff;
}
