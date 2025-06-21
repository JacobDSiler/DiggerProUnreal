#include "CubeBrushShape.h"

#include "FBrushStroke.h"
#include "VoxelConversion.h"

float UCubeBrushShape::CalculateSDF_Implementation(
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
    
    const float Height = Stroke.BrushLength;
    const float Angle = Stroke.BrushAngle; // You'll need to add this to FBrushStroke
    const float AngleRad = FMath::DegreesToRadians(Angle);
    const float RadiusAtBase = Height * FMath::Tan(AngleRad);
    
    // Calculate SDF for cone
    float radiusAtZ = (LocalPos.Z / Height) * RadiusAtBase;
    float d = FVector2D(LocalPos.X, LocalPos.Y).Size() - radiusAtZ;
    
    float dist;
    if (Stroke.bIsFilled)
    {
        // Hard caps: bottom at Z=0, top at Z=Height
        float cappedZ = FMath::Clamp(LocalPos.Z, 0.0f, Height);
        radiusAtZ = (cappedZ / Height) * RadiusAtBase;
        d = FVector2D(LocalPos.X, LocalPos.Y).Size() - radiusAtZ;
        
        // Only inside the cylinder-ish cone body
        float dz = 0.0f;
        if (LocalPos.Z < 0.0f) dz = LocalPos.Z;
        else if (LocalPos.Z > Height) dz = LocalPos.Z - Height;
        
        dist = FMath::Max(d, dz); // flat bottom/top cap
    }
    else
    {
        // Hollow cone shell
        const float ShellThickness = FMath::Max(FVoxelConversion::LocalVoxelSize * 1.5f, RadiusAtBase * 0.05f);
        
        // Calculate base cone SDF
        float baseDist = FMath::Max(d, -LocalPos.Z);
        baseDist = FMath::Min(baseDist, FMath::Max(d, LocalPos.Z - Height));
        
        // Convert to shell
        if (FMath::Abs(baseDist) < ShellThickness)
        {
            dist = FMath::Abs(baseDist) - ShellThickness;
        }
        else
        {
            dist = FMath::Max(baseDist, ShellThickness);
        }
    }
    
    // Fix for tip issue: Add a small bias to ensure the tip is properly handled
    if (!Stroke.bDig && LocalPos.Z < FVoxelConversion::LocalVoxelSize)
    {
        // Near the tip, make sure the SDF is negative enough
        dist = FMath::Min(dist, -0.1f);
    }
    
    // Apply falloff if within falloff range
    if (Stroke.BrushFalloff > 0.0f && FMath::Abs(dist) < Stroke.BrushFalloff)
    {
        const float FalloffFactor = 1.0f - (FMath::Abs(dist) / Stroke.BrushFalloff);
        dist = dist * FalloffFactor;
    }
    
    return dist;
}