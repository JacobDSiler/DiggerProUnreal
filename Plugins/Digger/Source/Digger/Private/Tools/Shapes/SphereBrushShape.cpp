#include "Shapes/SphereBrushShape.h"
#include "FBrushStroke.h"
#include "VoxelConversion.h"

float USphereBrushShape::CalculateSDF_Implementation(
    const FVector& WorldPos,
    const FBrushStroke& Stroke,
    float TerrainHeight
) const
{
    // 1. Calculate Signed Distance to Sphere Surface
    // Negative = Inside, Positive = Outside
    // 0.0 = Surface
    FVector Center = Stroke.BrushPosition + Stroke.BrushOffset;
    float Distance = FVector::Dist(WorldPos, Center);
    float SignedDist = Distance - Stroke.BrushRadius;

    // 2. Define Falloff Window
    // We want the transition to happen centered on the surface (Radius)
    // Window: [-HalfFalloff ... +HalfFalloff]
    float HalfFalloff = Stroke.BrushFalloff * 0.5f;

    // 3. Dig Logic (Air vs Solid)
    // SDF Convention: Positive = Air, Negative = Solid
    // We scale the distance so that 1 unit of distance ~= 1 unit of SDF density
    // This preserves the natural gradient for normals.
    
    float ResultSDF = 0.0f;

    if (Stroke.bDig)
    {
        // DIGGING:
        // Inside sphere (SignedDist < 0) -> Positive Density (Air)
        // Outside sphere (SignedDist > 0) -> Negative Density (Solid/Existing)
        
        // Invert distance because Air is Positive
        ResultSDF = -SignedDist;
    }
    else
    {
        // ADDING:
        // Inside sphere (SignedDist < 0) -> Negative Density (Solid)
        // Outside sphere (SignedDist > 0) -> Positive Density (Air)
        
        // Direct distance (Inside is negative)
        ResultSDF = SignedDist;
    }

    // 4. Apply Smoothing / Falloff
    // If we are strictly outside the falloff zone, we just return the raw distance.
    // If we are INSIDE the transition zone, we smooth it.
    
    if (FMath::Abs(SignedDist) < HalfFalloff && Stroke.BrushFalloff > KINDA_SMALL_NUMBER)
    {
        // Normalize position within falloff window to 0..1
        float Alpha = (SignedDist + HalfFalloff) / Stroke.BrushFalloff;
        
        // SmoothStep creates the "S" curve for organic blending
        Alpha = FMath::SmoothStep(0.0f, 1.0f, Alpha);

        if (Stroke.bDig)
        {
            // Dig: Blend from Air (Inside) to Solid (Outside)
            ResultSDF = FMath::Lerp(FVoxelConversion::SDF_AIR, FVoxelConversion::SDF_SOLID, Alpha);
        }
        else
        {
            // Add: Blend from Solid (Inside) to Air (Outside)
            ResultSDF = FMath::Lerp(FVoxelConversion::SDF_SOLID, FVoxelConversion::SDF_AIR, Alpha);
        }
    }
    else
    {
        // 5. CRITICAL FIX FOR SMOOTHNESS:
        // Outside the smoothing window, we do NOT clamp to a flat value immediately.
        // We allow the gradient to continue slightly to ensure normals have data.
        // However, we clamp to the grid limits (e.g. +/- 5.0) to avoid overflow.
        
        // Ideally, we just return ResultSDF (which is the raw distance).
        // The Voxel Grid storage will clamp it naturally.
        
        // To force "Max Strength", we can multiply, but keeping raw distance 
        // ensures 1:1 correlation with world space.
    }
    
    // Apply Strength multiplier
    return ResultSDF * Stroke.BrushStrength;
}

// Bounds checks remain the same
bool USphereBrushShape::IsWithinBounds(const FVector& WorldPos, const FBrushStroke& Stroke) const
{
    FVector Center = Stroke.BrushPosition + Stroke.BrushOffset;
    float DistanceSq = FVector::DistSquared(WorldPos, Center);
    float Radius = Stroke.BrushRadius + Stroke.BrushFalloff + 2.0f; // Padding
    return DistanceSq <= (Radius * Radius);
}

bool USphereBrushShape::IsWithinInterior(const FVector& WorldPos, const FBrushStroke& Stroke) const
{
    FVector Center = Stroke.BrushPosition + Stroke.BrushOffset;
    float DistanceSq = FVector::DistSquared(WorldPos, Center);
    float Radius = Stroke.BrushRadius;
    return DistanceSq <= (Radius * Radius);
}

void USphereBrushShape::GetPreviewData(FVector& OutCenter, FVector& OutExtents, FQuat& OutRotation, float& OutFalloff, EVoxelBrushType& OutBrushType, const FBrushStroke& Stroke) const
{
    OutCenter = Stroke.BrushPosition + Stroke.BrushOffset;
    OutExtents = FVector(Stroke.BrushRadius);
    OutRotation = FQuat::Identity;
    OutFalloff = Stroke.BrushFalloff;
    OutBrushType = EVoxelBrushType::Sphere;
}