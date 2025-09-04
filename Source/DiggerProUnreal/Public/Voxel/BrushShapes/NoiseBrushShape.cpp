#include "NoiseBrushShape.h"

#include "FBrushStroke.h"
#include "VoxelConversion.h"
#include "Math/UnrealMathUtility.h"

float UNoiseBrushShape::CalculateSDF_Implementation(
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
    
	// Generate 3D noise
	const float NoiseScale = 0.1f; // Adjust for noise frequency
	const float NoiseX = WorldPos.X * NoiseScale;
	const float NoiseY = WorldPos.Y * NoiseScale;
	const float NoiseZ = WorldPos.Z * NoiseScale;
    
	// Simple 3D noise using sine waves (you could use Perlin noise here)
	// Replace in UNoiseBrushShape::CalculateSDF_Implementation:
	const float Noise = HashNoise3D(WorldPos * 0.1f);
	const float NoiseValue = (Noise + 1.0f) * 0.5f; // Normalize to 0-1
    
	// Apply spherical falloff
	const float SphereSDF = Distance - Stroke.BrushRadius;
	float FalloffSDF = SphereSDF;
    
	if (SphereSDF > -Stroke.BrushFalloff && SphereSDF < Stroke.BrushFalloff)
	{
		const float FalloffFactor = 1.0f - FMath::Abs(SphereSDF) / Stroke.BrushFalloff;
        
		// Modulate the SDF with noise
		const float NoiseModulation = (NoiseValue - 0.5f) * Stroke.BrushRadius * 0.3f; // 30% of radius
		FalloffSDF = SphereSDF + NoiseModulation;
        
		FalloffSDF *= FalloffFactor;
	}
    
	return Stroke.bDig ? FalloffSDF * Stroke.BrushStrength : -FalloffSDF * Stroke.BrushStrength;
}

bool UNoiseBrushShape::IsWithinBounds(const FVector& WorldPos, const FBrushStroke& Stroke) const
{
	// Temporary fallback - use simple sphere bounds
	const FVector Delta = WorldPos - Stroke.BrushPosition;
	const float DistanceSq = Delta.SizeSquared();
	const float RadiusSq = Stroke.BrushRadius * Stroke.BrushRadius;
	return DistanceSq <= RadiusSq;
}


