#include "ConeBrushShape.h"

#include "FBrushStroke.h"
#include "VoxelConversion.h"

float UConeBrushShape::CalculateSDF_Implementation(
    const FVector& WorldPos,
    const FBrushStroke& Stroke,
    float TerrainHeight
) const
{
    const FVector LocalPos = WorldPos - Stroke.BrushPosition;
    
    // Apply rotation if needed
    FVector RotatedPos = LocalPos;
    if (!Stroke.BrushRotation.IsZero())
    {
        RotatedPos = Stroke.BrushRotation.UnrotateVector(LocalPos);
    }
    
    const float ConeHeight = Stroke.BrushLength;
    const float ConeRadius = Stroke.BrushRadius;
    
    // Project onto cone axis (Z-up from brush position)
    const float Height = RotatedPos.Z;
    const float RadialDistance = FVector2D(RotatedPos.X, RotatedPos.Y).Size();
    
    float ConeSDF;
    
    if (Height < 0.0f)
    {
        // Below cone base
        if (RadialDistance <= ConeRadius)
        {
            ConeSDF = -Height; // Distance to base plane
        }
        else
        {
            ConeSDF = FVector2D(RadialDistance - ConeRadius, -Height).Size();
        }
    }
    else if (Height > ConeHeight)
    {
        // Above cone tip
        ConeSDF = FVector2D(RadialDistance, Height - ConeHeight).Size();
    }
    else
    {
        // Within cone height range
        const float NormalizedHeight = Height / ConeHeight;
        const float ConeRadiusAtHeight = ConeRadius * (1.0f - NormalizedHeight);
        
        if (Stroke.bIsFilled)
        {
            // Solid cone
            ConeSDF = RadialDistance - ConeRadiusAtHeight;
        }
        else
        {
            // Hollow cone shell
            const float ShellThickness = FMath::Max(FVoxelConversion::LocalVoxelSize * 2.0f, ConeRadius * 0.08f);
            ConeSDF = FMath::Abs(RadialDistance - ConeRadiusAtHeight) - ShellThickness;
        }
    }
    
    // Apply falloff
    if (ConeSDF > -Stroke.BrushFalloff && ConeSDF < Stroke.BrushFalloff && Stroke.BrushFalloff > 0.0f)
    {
        const float FalloffFactor = 1.0f - (FMath::Abs(ConeSDF) / Stroke.BrushFalloff);
        ConeSDF = ConeSDF * FalloffFactor;
    }
    
    // Handle terrain interaction
    float FinalSDF;
    
    if (Stroke.bDig)
    {
        // Subtractive mode - only affect areas inside the shape
        FinalSDF = (ConeSDF <= 0.0f) ? ConeSDF * Stroke.BrushStrength : 0.0f;
    }
    else
    {
        // Additive mode - only add material where shape is solid
        FinalSDF = (ConeSDF <= 0.0f) ? -ConeSDF * Stroke.BrushStrength : 0.0f;
    }
    
    return FinalSDF;
}