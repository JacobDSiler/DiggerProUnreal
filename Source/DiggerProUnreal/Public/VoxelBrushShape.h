#pragma once

#include "CoreMinimal.h"
#include "VoxelChunk.h"
#include "VoxelBrushTypes.h" // Include the shared header
#include "UObject/NoExportTypes.h"
#include "VoxelBrushShape.generated.h"

class ADiggerManager;
class UVoxelChunk;


UCLASS()
class DIGGERPROUNREAL_API UVoxelBrushShape : public UObject
{
	GENERATED_BODY()

public:
	UVoxelBrushShape();

	// Initialize the brush
	void InitializeBrush(EVoxelBrushType InBrushType, float InSize, FVector InLocation);
	UVoxelChunk* GetTargetChunkFromBrushPosition(const FVector3d& BrushPosition);

	// Apply the brush to the SDF in the chunk
	void ApplyBrushToChunk(UVoxelChunk* BrushChunk, FVector3d BrushPosition, float BrushSize);
	void InitializeBrushProperties();

	// Debug the brush in the world
	UFUNCTION(BlueprintCallable, Category = "Brush")
	void DebugBrush();
	
	UFUNCTION(BlueprintCallable, Category = "Brush")
	EVoxelBrushType GetBrushType() const
	{
		return BrushType;
	}
	
	UFUNCTION(BlueprintCallable, Category = "Brush")
	void SetBrushType(EVoxelBrushType NewBrushType)
	{
		this->BrushType = NewBrushType;
	}

	UFUNCTION(BlueprintCallable, Category = "Brush")
	float GetBrushSize() const
	{
		return BrushSize;
	}
	
	UFUNCTION(BlueprintCallable, Category = "Brush")
	void SetBrushSize(float NewBrushSize)
	{
		this->BrushSize = NewBrushSize;
	}

	UFUNCTION(BlueprintCallable, Category = "Brush")
	FVector GetBrushLocation() const
	{
		return BrushLocation;
	}

	UFUNCTION(BlueprintCallable, Category = "Brush")
	void SetBrushLocation(const FVector& NewBrushLocation)
	{
		this->BrushLocation = NewBrushLocation;
	}

	UFUNCTION(BlueprintCallable, Category = "Brush")
	float SdfChange() const
	{
		return SDFChange;
	}

	UFUNCTION(BlueprintCallable, Category = "Brush")
	void SetSdfChange(float NewSdfChange)
	{
		SDFChange = NewSdfChange;
	}

	UFUNCTION(BlueprintCallable, Category = "Brush")
	bool GetDig() const
	{
		return bDig;
	}
	
	UFUNCTION(BlueprintCallable, Category = "Brush")
	void SetDig(bool setBDig)
	{
		this->bDig = setBDig;
	}

protected:
	//Brush Settings
	// Size and location of the brush
	float BrushSize;
	FVector BrushLocation;
	//SDF change variables
	float SDFChange;
	
	//Dig Setting
	bool bDig;
	
	//Debug Brush Timer
	FTimerHandle DebugBrushTimerHandle;

	UFUNCTION(BlueprintCallable, Category = "Brush")
	FHitResult GetCameraHitLocation();

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
	//World
	UPROPERTY()
	UWorld* World;

public:
	void SetWorld(UWorld* SetWorld)
	{
		this->World = SetWorld;
	}

private:
	//DiggerManager Reference
	UPROPERTY()
	ADiggerManager* DiggerManager;

	//A reference to the current target chunk
	UPROPERTY()
	UVoxelChunk* TargetChunk;

	// Brush type
	UPROPERTY()
	EVoxelBrushType BrushType;
};
