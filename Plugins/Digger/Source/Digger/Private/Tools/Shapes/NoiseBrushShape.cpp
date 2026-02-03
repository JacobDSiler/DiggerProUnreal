#include "Shapes/NoiseBrushShape.h"

#include "FBrushStroke.h"
#include "VoxelConversion.h"
#include "Math/UnrealMathUtility.h"

FORCEINLINE uint32 HashUint(uint32 x)
{
    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    x *= 0x846ca68b;
    x ^= x >> 16;
    return x;
}

FORCEINLINE float DeterministicNoise3D(const FIntVector& P)
{
    uint32 h = HashUint(HashUint(HashUint(P.X) ^ P.Y) ^ P.Z);
    return (float)(h & 0xFFFFFF) / (float)0xFFFFFF * 2.f - 1.f; // -1..1
}

float UNoiseBrushShape::CalculateSDF_Implementation(
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

    const float NoiseFreq = 0.02f;

    FIntVector Cell(
        FMath::FloorToInt(WorldPos.X * NoiseFreq),
        FMath::FloorToInt(WorldPos.Y * NoiseFreq),
        FMath::FloorToInt(WorldPos.Z * NoiseFreq)
    );

    float Noise = DeterministicNoise3D(Cell); // -1..1

    float Displacement = Noise * Stroke.BrushStrength * Stroke.BrushRadius * 0.25f;

    if (Stroke.bDig)
        Displacement = -Displacement;

    return Displacement * Falloff;
}


bool UNoiseBrushShape::IsWithinBounds(const FVector& WorldPos, const FBrushStroke& Stroke) const
{
	// Temporary fallback - use simple sphere bounds
	const FVector Delta = WorldPos - Stroke.BrushPosition;
	const float DistanceSq = Delta.SizeSquared();
	const float RadiusSq = Stroke.BrushRadius * Stroke.BrushRadius;
	return DistanceSq <= RadiusSq;
}

void UNoiseBrushShape::GetPreviewData(
    FVector& OutCenter,
    FVector& OutExtents,
    FQuat& OutRotation,
    float& OutFalloff,
    EVoxelBrushType& OutBrushType,
    const FBrushStroke& Stroke
) const
{
    OutCenter = Stroke.BrushPosition + Stroke.BrushOffset;

    // Noise brush is spherical for preview
    OutExtents = FVector(Stroke.BrushRadius);

    OutRotation = Stroke.BrushRotation.Quaternion();
    OutFalloff = Stroke.BrushFalloff;
    OutBrushType = EVoxelBrushType::Noise;
}
