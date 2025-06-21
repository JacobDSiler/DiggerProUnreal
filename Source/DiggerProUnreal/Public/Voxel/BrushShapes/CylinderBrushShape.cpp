#include "CylinderBrushShape.h"

#include "FBrushStroke.h"
#include "VoxelConversion.h"
#include "Math/UnrealMathUtility.h"

float UCylinderBrushShape::CalculateSDF_Implementation(
    const FVector& WorldPos,
    const FBrushStroke& Stroke,
    float TerrainHeight
) const
{
    // Create transform from brush rotation and position
    FTransform BrushTransform(Stroke.BrushRotation, Stroke.BrushPosition);
    FTransform InvBrushTransform = BrushTransform.Inverse();
    
    // Transform world position to local brush space
    FVector LocalPos = InvBrushTransform.TransformPosition(WorldPos);
    
    const float Radius = Stroke.BrushRadius;
    const float Height = Stroke.BrushLength;
    const float HalfHeight = Height * 0.5f;
    
    // Calculate distances
    float RadialDist = FVector2D(LocalPos.X, LocalPos.Y).Size() - Radius;
    float HeightDist = FMath::Abs(LocalPos.Z) - HalfHeight;
    
    // Signed distance: maximum of radial and height for cylinder SDF
    float dist = FMath::Max(RadialDist, HeightDist);
    
    if (!Stroke.bIsFilled)
    {
        // Hollow shell - convert to shell distance
        const float ShellThickness = FMath::Max(FVoxelConversion::LocalVoxelSize * 1.5f, Radius * 0.05f);
        
        // For hollow cylinder, we want the shell around the surface
        if (FMath::Abs(RadialDist) < ShellThickness && HeightDist <= 0)
        {
            // Inside the shell zone
            dist = FMath::Abs(RadialDist) - ShellThickness;
        }
        else
        {
            // Outside shell - make it positive
            dist = FMath::Max(dist, ShellThickness);
        }
    }
    
    // Apply falloff if within falloff range
    if (Stroke.BrushFalloff > 0.0f && FMath::Abs(dist) < Stroke.BrushFalloff)
    {
        const float FalloffFactor = 1.0f - (FMath::Abs(dist) / Stroke.BrushFalloff);
        dist = dist * FalloffFactor;
    }
    
    return dist;
}