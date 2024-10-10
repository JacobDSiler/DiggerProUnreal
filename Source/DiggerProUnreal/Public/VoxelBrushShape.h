#pragma once

#include "CoreMinimal.h"
#include "VoxelChunk.h"
#include "UObject/NoExportTypes.h"
#include "VoxelBrushShape.generated.h"

// Enum for different brush types
UENUM(BlueprintType)
enum class EVoxelBrushType : uint8
{
	Cube,
	Sphere,
	Cone,
	Custom
};

UCLASS()
class DIGGERPROUNREAL_API UVoxelBrushShape : public UObject
{
	GENERATED_BODY()

public:

	// Initialize the brush
	void InitializeBrush(EVoxelBrushType InBrushType, float InSize, FVector InLocation);
	UVoxelChunk* GetTargetChunkFromBrushPosition(const FVector3d& BrushPosition);

	// Apply the brush to the SDF in the chunk
	void ApplyBrushToChunk(FVector3d BrushPosition, float BrushSize);

	// Debug the brush in the world
	void DebugBrush(UWorld* World);

	[[nodiscard]] bool Dig() const
	{
		return bDig;
	}

	void SetDig(bool setBDig)
	{
		this->bDig = setBDig;
	}

protected:
	// Brush type
	EVoxelBrushType BrushType;

	//Brush Settings
	// Size and location of the brush
	float BrushSize;
	FVector BrushLocation;
	//SDF change variables
	float SDFChange;
	
	//Dig Setting
	bool bDig;

	UFUNCTION(BlueprintCallable, Category = "Brush")
	FVector GetCameraHitLocation();

	// Cube brush logic
	void ApplyCubeBrush(FVector3d BrushPosition);
	// Sphere brush logic
	void ApplySphereBrush(FVector3d BrushPosition, float Radius);
	// Cone brush logic
	void ApplyConeBrush(FVector3d BrushPosition);
	// Custom brush logic
	void ApplyCustomBrush(FVector3d BrushPosition);

	bool IsVoxelWithinBounds(const FVector3d& VoxelPosition, const FVector3d& MinBounds, const FVector3d& MaxBounds);
	bool IsVoxelWithinSphere(const FVector3d& VoxelPosition, const FVector3d& SphereCenter, double Radius);

private:
	//DiggerManager Reference
	UPROPERTY()
	ADiggerManager* DiggerManager;

	//A reference to the current target chunk
	UPROPERTY()
	UVoxelChunk* TargetChunk;
};
