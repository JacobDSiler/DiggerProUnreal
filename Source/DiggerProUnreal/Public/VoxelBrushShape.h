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
	bool EnsureDiggerManager();

	// Initialize the brush
	void InitializeBrush(EVoxelBrushType InBrushType, float InSize, FVector InLocation, ADiggerManager* DiggerManagerRef);
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
	void ApplyCubeBrush(FBrushStroke* BrushStroke);
	// Sphere brush logic
	void ApplySphereBrush(FBrushStroke* BrushStroke);
	// Cone brush logic
	void ApplyConeBrush(FBrushStroke* BrushStroke);
	// Custom brush logic
	void ApplyCustomBrush(FVector3d BrushPosition);

	bool IsVoxelWithinBounds(const FVector3d& VoxelPosition, const FVector3d& MinBounds, const FVector3d& MaxBounds);
	bool IsVoxelWithinSphere(const FVector3d& VoxelPosition, const FVector3d& SphereCenter, double Radius);

private:
	//World
	UPROPERTY()
	UWorld* World;
	
	FHitResult PerformComplexTrace(const FVector& Start, const FVector& End, AActor* IgnoredActor);
	bool IsHoleBPActor(AActor* Actor) const;
	FHitResult TraceThroughHole(const FHitResult& HoleHit, const FVector& End, AActor* IgnoredActor);
	FHitResult TraceBehindLandscape(const FHitResult& LandscapeHit, const FVector& End, AActor* IgnoredActor, AActor* HoleActor);

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
