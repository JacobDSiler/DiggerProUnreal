#include "CubeBrushShape.h"
#include "FBrushStroke.h"
#include "VoxelConversion.h"

float UCubeBrushShape::CalculateSDF_Implementation(
    const FVector& WorldPos,
    const FBrushStroke& Stroke,
    float TerrainHeight
) const
{
    // 1. Transform Point to Local Space
    FVector Center = Stroke.BrushPosition + Stroke.BrushOffset;
    FVector LocalPos = WorldPos - Center;
    
    if (!Stroke.BrushRotation.IsNearlyZero())
    {
        LocalPos = Stroke.BrushRotation.UnrotateVector(LocalPos);
    }

    // 2. Determine Extents
    FVector HalfExtents = Stroke.bUseAdvancedCubeBrush
        ? FVector(
            Stroke.AdvancedCubeHalfExtentX,
            Stroke.AdvancedCubeHalfExtentY,
            Stroke.AdvancedCubeHalfExtentZ
        )
        : FVector(Stroke.BrushRadius);

    // 3. Compute Exact Signed Distance to Box
    // d = abs(p) - b
    FVector d = FVector(
        FMath::Abs(LocalPos.X),
        FMath::Abs(LocalPos.Y),
        FMath::Abs(LocalPos.Z)
    ) - HalfExtents;

    // Inside (Negative) and Outside (Positive) components
    float InsideDist = FMath::Min(FMath::Max(d.X, FMath::Max(d.Y, d.Z)), 0.0f);
    
    // Euclidean length for outside (Rounded corners)
    FVector OutsideVec = FVector(FMath::Max(d.X, 0.0f), FMath::Max(d.Y, 0.0f), FMath::Max(d.Z, 0.0f));
    float OutsideDist = OutsideVec.Size();

    // The raw distance to the box surface (0 is surface, <0 inside, >0 outside)
    float SignedDistance = InsideDist + OutsideDist;

    // 4. Centered Falloff Logic
    // We want the surface (SDF=0) to align with SignedDistance=0 (The Box Edge).
    // So we shift the transition window to [-Falloff/2, +Falloff/2].
    
    float HalfFalloff = Stroke.BrushFalloff * 0.5f;
    float SDFValue = 0.0f;

    if (Stroke.bDig)
    {
        // DIGGING:
        // Dist <= -HalfFalloff  -> Pure Air (1.0)
        // Dist == 0             -> Surface (0.0) -> Matches Preview
        // Dist >= +HalfFalloff  -> Pure Solid (-1.0)

        if (SignedDistance <= -HalfFalloff)
        {
            SDFValue = FVoxelConversion::SDF_AIR;
        }
        else if (SignedDistance >= HalfFalloff)
        {
            SDFValue = FVoxelConversion::SDF_SOLID;
        }
        else
        {
            // Map range [-HalfFalloff, +HalfFalloff] to [0, 1]
            float t = (SignedDistance + HalfFalloff) / Stroke.BrushFalloff;
            
            // SmoothStep for nice organic blending
            t = FMath::SmoothStep(0.0f, 1.0f, t);
            
            // Lerp from Air (Inside) to Solid (Outside)
            SDFValue = FMath::Lerp(FVoxelConversion::SDF_AIR, FVoxelConversion::SDF_SOLID, t);
        }
    }
    else // Adding
    {
        // ADDING: Inverse logic
        if (SignedDistance <= -HalfFalloff)
        {
            SDFValue = FVoxelConversion::SDF_SOLID;
        }
        else if (SignedDistance >= HalfFalloff)
        {
            SDFValue = FVoxelConversion::SDF_AIR;
        }
        else
        {
            float t = (SignedDistance + HalfFalloff) / Stroke.BrushFalloff;
            t = FMath::SmoothStep(0.0f, 1.0f, t);
            
            // Lerp from Solid (Inside) to Air (Outside)
            SDFValue = FMath::Lerp(FVoxelConversion::SDF_SOLID, FVoxelConversion::SDF_AIR, t);
        }
    }

    return SDFValue * Stroke.BrushStrength;
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

    FVector HalfExtents = Stroke.bUseAdvancedCubeBrush
        ? FVector(Stroke.AdvancedCubeHalfExtentX, Stroke.AdvancedCubeHalfExtentY, Stroke.AdvancedCubeHalfExtentZ)
        : FVector(Stroke.BrushRadius);

    // We check against Extents + Falloff to be safe. 
    // Since we now use centered falloff, the effect ends at +HalfFalloff, 
    // but checking +FullFalloff is safer and cheap.
    FVector SearchExtents = HalfExtents + Stroke.BrushFalloff + 2.0f;

    return FMath::Abs(LocalPos.X) <= SearchExtents.X &&
           FMath::Abs(LocalPos.Y) <= SearchExtents.Y &&
           FMath::Abs(LocalPos.Z) <= SearchExtents.Z;
}