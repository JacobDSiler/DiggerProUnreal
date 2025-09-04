#include "SmoothBrushShape.h"

#include "FBrushStroke.h"
#include "VoxelConversion.h"
#include "Math/UnrealMathUtility.h"

float USmoothBrushShape::CalculateSDF_Implementation(
		const FVector& WorldPos,
		const FBrushStroke& Stroke,
		float TerrainHeight
		) const
{
	const FVector LocalPos = WorldPos - Stroke.BrushPosition;
	const float Distance = LocalPos.Size();
    
	// Early exit if outside brush radius
	if (Distance > Stroke.BrushRadius + Stroke.BrushFalloff)
		return 0.0f;
    
	// Calculate falloff factor
	const float FalloffFactor = 1.0f - FMath::Clamp(Distance / (Stroke.BrushRadius + Stroke.BrushFalloff), 0.0f, 1.0f);
    
	if (Stroke.bDig)
	{
		// Sharpen mode - create small positive values to erode/sharpen edges
		const float SharpenStrength = 0.02f * Stroke.BrushStrength; // Very small positive value
		return SharpenStrength * FalloffFactor;
	}
	else
	{
		// Smooth mode - create small negative values to fill/smooth
		const float SmoothStrength = -0.01f * Stroke.BrushStrength; // Very small negative value
		return SmoothStrength * FalloffFactor;
	}
}

bool USmoothBrushShape::IsWithinBounds(const FVector& WorldPos, const FBrushStroke& Stroke) const
{
	// Temporary fallback - use simple sphere bounds
	const FVector Delta = WorldPos - Stroke.BrushPosition;
	const float DistanceSq = Delta.SizeSquared();
	const float RadiusSq = Stroke.BrushRadius * Stroke.BrushRadius;
	return DistanceSq <= RadiusSq;
}

