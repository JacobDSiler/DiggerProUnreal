#include "Shapes/CapsuleBrushShape.h"

#include "FBrushStroke.h"
#include "VoxelConversion.h"
#include "Math/UnrealMathUtility.h"

float UCapsuleBrushShape::CalculateSDF_Implementation(
    const FVector& WorldPos,
    const FBrushStroke& Stroke,
    float TerrainHeight
) const
{
    // 1. Transform WorldPos to Local Brush Space
    FVector Center = Stroke.BrushPosition + Stroke.BrushOffset;
    FVector LocalPos = WorldPos - Center;

    // Unrotate so the capsule is always vertical (aligned with Z) in local space
    if (!Stroke.BrushRotation.IsNearlyZero())
    {
        LocalPos = Stroke.BrushRotation.UnrotateVector(LocalPos);
    }

    // 2. Define the Capsule Line Segment (Start to End)
    // In local space, the capsule is centered at 0,0,0 and extends along Z.
    float HalfHeight = Stroke.BrushLength * 0.5f;
    FVector A = FVector(0, 0, -HalfHeight);
    FVector B = FVector(0, 0,  HalfHeight);

    // 3. Compute Distance to Line Segment (AB)
    // Vector PA = Point - A
    // Vector BA = B - A
    FVector PA = LocalPos - A;
    FVector BA = B - A;

    // Project Point onto Line (clamped 0..1)
    float h = FMath::Clamp(FVector::DotProduct(PA, BA) / FVector::DotProduct(BA, BA), 0.0f, 1.0f);

    // Closest point on the line segment
    FVector ClosestPoint = A + (BA * h);
    
    // Distance from our point to that closest point
    float DistToLine = FVector::Dist(LocalPos, ClosestPoint);

    // 4. SDF Calculation (Distance to Line - Radius)
    // This gives us the distance to the surface of the capsule
    // Note: We include Falloff in the effective radius calculation logic later, 
    // but the raw SDF is Dist - Radius.
    
    float SignedDist = DistToLine - Stroke.BrushRadius;

    // 5. Apply Falloff and Dig/Add logic (Standard boilerplate)
    float SDFValue = 0.0f;

    if (Stroke.bDig)
    {
        if (SignedDist <= 0.0f) // Inside the hard capsule
        {
            SDFValue = FVoxelConversion::SDF_AIR;
        }
        else // In the falloff zone
        {
            float t = 0.0f;
            if (Stroke.BrushFalloff > 0.001f)
            {
                t = FMath::Clamp(SignedDist / Stroke.BrushFalloff, 0.0f, 1.0f);
                t = FMath::SmoothStep(0.0f, 1.0f, t);
            }
            else
            {
                t = 1.0f;
            }
            SDFValue = FMath::Lerp(FVoxelConversion::SDF_AIR, FVoxelConversion::SDF_SOLID, t);
        }
    }
    else // Adding
    {
        if (SignedDist <= 0.0f)
        {
            SDFValue = FVoxelConversion::SDF_SOLID;
        }
        else
        {
            float t = 0.0f;
            if (Stroke.BrushFalloff > 0.001f)
            {
                t = FMath::Clamp(SignedDist / Stroke.BrushFalloff, 0.0f, 1.0f);
                t = FMath::SmoothStep(0.0f, 1.0f, t);
            }
            else
            {
                t = 1.0f;
            }
            SDFValue = FMath::Lerp(FVoxelConversion::SDF_SOLID, FVoxelConversion::SDF_AIR, t);
        }
    }

    return SDFValue * Stroke.BrushStrength;
}

bool UCapsuleBrushShape::IsWithinBounds(const FVector& WorldPos, const FBrushStroke& Stroke) const
{
    // Quick Bounding Box check first
    // Capsule Bounds = Radius + HalfHeight
    
    // We already handle this in UVoxelChunk::CalculateBrushBounds, 
    // but for the per-voxel iteration inside the chunk, we can do a quick local check.
    
    FVector Center = Stroke.BrushPosition + Stroke.BrushOffset;
    FVector LocalPos = WorldPos - Center;
    
    if (!Stroke.BrushRotation.IsNearlyZero())
    {
        LocalPos = Stroke.BrushRotation.UnrotateVector(LocalPos);
    }

    // Check Height (Z)
    float HalfHeight = Stroke.BrushLength * 0.5f;
    float VerticalLimit = HalfHeight + Stroke.BrushRadius + Stroke.BrushFalloff + 2.0f; // +Safety
    
    if (FMath::Abs(LocalPos.Z) > VerticalLimit) return false;

    // Check Width (XY)
    float HorizontalLimit = Stroke.BrushRadius + Stroke.BrushFalloff + 2.0f;
    float HorizontalDistSq = LocalPos.X * LocalPos.X + LocalPos.Y * LocalPos.Y;
    
    if (HorizontalDistSq > HorizontalLimit * HorizontalLimit) return false;

    return true;
}


bool UCapsuleBrushShape::IsWithinInterior(const FVector& WorldPos, const FBrushStroke& Stroke) const
{
    FVector Center = Stroke.BrushPosition + Stroke.BrushOffset;
    FVector LocalPos = WorldPos - Center;

    if (!Stroke.BrushRotation.IsNearlyZero())
    {
        LocalPos = Stroke.BrushRotation.UnrotateVector(LocalPos);
    }

    // Tighter height check
    float HalfHeight = Stroke.BrushLength * 0.5f;
    float VerticalLimit = HalfHeight + Stroke.BrushRadius; // no falloff, no padding

    if (FMath::Abs(LocalPos.Z) > VerticalLimit)
        return false;

    // Tighter width check
    float HorizontalLimit = Stroke.BrushRadius; // no falloff, no padding
    float HorizontalDistSq = LocalPos.X * LocalPos.X + LocalPos.Y * LocalPos.Y;

    if (HorizontalDistSq > HorizontalLimit * HorizontalLimit)
        return false;

    return true;
}

void UCapsuleBrushShape::GetPreviewData(
    FVector& OutCenter,
    FVector& OutExtents,
    FQuat& OutRotation,
    float& OutFalloff,
    EVoxelBrushType& OutBrushType,
    const FBrushStroke& Stroke
) const
{
    OutCenter = Stroke.BrushPosition + Stroke.BrushOffset;

    // Capsule extents: radius in X/Y, half-height in Z
    float HalfHeight = Stroke.BrushLength * 0.5f;
    OutExtents = FVector(Stroke.BrushRadius, Stroke.BrushRadius, HalfHeight);

    OutRotation = Stroke.BrushRotation.Quaternion();
    OutFalloff = Stroke.BrushFalloff;
    OutBrushType = EVoxelBrushType::Capsule;
}
