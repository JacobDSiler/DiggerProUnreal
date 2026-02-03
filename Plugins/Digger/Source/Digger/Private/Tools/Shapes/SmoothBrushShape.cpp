#include "Shapes/SmoothBrushShape.h"

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

	if (Distance > Stroke.BrushRadius + Stroke.BrushFalloff)
		return 0.f;

	float t = FMath::Clamp(Distance / Stroke.BrushRadius, 0.f, 1.f);
	float Falloff = 1.f - (t * t * (3.f - 2.f * t));

	// Deterministic sample offset
	const int32 Offset = 32; // integer, fixed

	float C = TerrainHeight;
	float N = FVoxelConversion::GetTerrainHeight(WorldPos + FVector( Offset, 0, 0));
	float S = FVoxelConversion::GetTerrainHeight(WorldPos + FVector(-Offset, 0, 0));
	float E = FVoxelConversion::GetTerrainHeight(WorldPos + FVector(0,  Offset, 0));
	float W = FVoxelConversion::GetTerrainHeight(WorldPos + FVector(0, -Offset, 0));

	float Avg = (C + N + S + E + W) * 0.2f;

	float Delta = (Avg - C) * Stroke.BrushStrength * Falloff;

	if (Stroke.bDig)
		Delta = -Delta;

	return Delta;
}


bool USmoothBrushShape::IsWithinBounds(const FVector& WorldPos, const FBrushStroke& Stroke) const
{
	// Temporary fallback - use simple sphere bounds
	const FVector Delta = WorldPos - Stroke.BrushPosition;
	const float DistanceSq = Delta.SizeSquared();
	const float RadiusSq = Stroke.BrushRadius * Stroke.BrushRadius;
	return DistanceSq <= RadiusSq;
}

void USmoothBrushShape::GetPreviewData(
	FVector& OutCenter,
	FVector& OutExtents,
	FQuat& OutRotation,
	float& OutFalloff,
	EVoxelBrushType& OutBrushType,
	const FBrushStroke& Stroke
) const
{
	OutCenter = Stroke.BrushPosition + Stroke.BrushOffset;

	// Smooth brush is spherical for preview
	OutExtents = FVector(Stroke.BrushRadius);

	OutRotation = Stroke.BrushRotation.Quaternion();
	OutFalloff = Stroke.BrushFalloff;
	OutBrushType = EVoxelBrushType::Smooth;
}
