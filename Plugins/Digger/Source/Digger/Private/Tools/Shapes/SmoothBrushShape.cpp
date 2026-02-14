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

	// Normalized distance
	const float t = Distance / Stroke.BrushRadius;

	// ⭐ Gaussian falloff (ZBrush-like)
	const float Falloff = FMath::Exp(-FMath::Square(t * 2.5f));

	// ⭐ Strength (no pressure yet)
	float Strength = Stroke.BrushStrength;

	// ⭐ Time ramp (fake pressure)
	Strength *= FMath::Clamp(Stroke.ElapsedTime * 0.5f, 0.f, 1.f);

	// ⭐ 9-point kernel (ZBrush-like)
	const int32 Offset = 32;

	float Sum = 0.f;
	int Count = 0;

	for (int dx = -1; dx <= 1; dx++)
	{
		for (int dy = -1; dy <= 1; dy++)
		{
			FVector SamplePos = WorldPos + FVector(dx * Offset, dy * Offset, 0);
			Sum += FVoxelConversion::GetTerrainHeight(SamplePos);
			Count++;
		}
	}

	const float Avg = Sum / Count;
	const float C = TerrainHeight;

	float Delta = (Avg - C) * Strength * Falloff;

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
