#pragma once

#include "CoreMinimal.h"
#include "VoxelBrushShape.h"
#include "NoiseBrushShape.generated.h"

UCLASS()
class DIGGER_API UNoiseBrushShape : public UVoxelBrushShape
{
	GENERATED_BODY()

public:
	virtual float CalculateSDF_Implementation(
		const FVector& WorldPos,
		const FBrushStroke& Stroke,
		float TerrainHeight
	) const override;

	virtual bool IsWithinBounds(const FVector& WorldPos, const FBrushStroke& Stroke) const override;

	// For Noise determinism, replace FMath::Sin() noise with a hash-based pseudorandom function like this:

	// For Noise determinism, replace FMath::Sin() noise with a hash-based pseudorandom function like this:

	static float HashNoise3D(const FVector& P)
	{
		FVector N = FVector(FMath::FloorToInt(P.X), FMath::FloorToInt(P.Y), FMath::FloorToInt(P.Z));
		int32 Hash = HashCombine(HashCombine(::GetTypeHash(N.X), ::GetTypeHash(N.Y)), ::GetTypeHash(N.Z));
		return FMath::Frac(FMath::Sin(Hash * 12.9898f) * 43758.5453f) * 2.f - 1.f; // Output: [-1, 1]
	}

	// PURE DATA ONLY — NO EDITOR TYPES
	virtual void GetPreviewData(
		FVector& OutCenter,
		FVector& OutExtents,
		FQuat& OutRotation,
		float& OutFalloff,
		EVoxelBrushType& OutBrushType,   // or your own enum
		const FBrushStroke& Stroke) const;

};