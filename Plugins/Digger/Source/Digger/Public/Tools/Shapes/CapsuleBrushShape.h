#pragma once

#include "VoxelBrushShape.h"
#include "CapsuleBrushShape.generated.h"

UCLASS()
class DIGGER_API UCapsuleBrushShape : public UVoxelBrushShape
{
	GENERATED_BODY()
public:
	// Example for SphereBrushShape.h
	virtual float CalculateSDF_Implementation(
		const FVector& WorldPos,
		const FBrushStroke& Stroke,
		float TerrainHeight
	) const override;

	virtual bool IsWithinBounds(const FVector& WorldPos, const FBrushStroke& Stroke) const override;

	virtual bool IsWithinInterior(const FVector& WorldPos, const FBrushStroke& Stroke) const override;

	// PURE DATA ONLY — NO EDITOR TYPES
	virtual void GetPreviewData(
		FVector& OutCenter,
		FVector& OutExtents,
		FQuat& OutRotation,
		float& OutFalloff,
		EVoxelBrushType& OutBrushType,   // or your own enum
		const FBrushStroke& Stroke) const;

};