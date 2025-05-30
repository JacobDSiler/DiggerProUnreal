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
	UWorld* GetSafeWorld() const;

	// Use virtual, not pure virtual
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Voxel")
	float CalculateSDF(
		const FVector& WorldPos,
		const FVector& BrushCenter,
		float Radius,
		float Strength,
		float Falloff,
		float TerrainHeight,
		bool bDig
	) const;
	virtual float CalculateSDF_Implementation(
		const FVector& WorldPos,
		const FVector& BrushCenter,
		float Radius,
		float Strength,
		float Falloff, float TerrainHeight, bool bDig
	) const { return 0.0f; } // Default implementation
	
	
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
	bool GetDig() const
	{
		return bIsDigging;
	}
	
	UFUNCTION(BlueprintCallable, Category = "Brush")
	void SetDig(bool setBDig)
	{
		this->bIsDigging = setBDig;
	}

protected:
	//Brush Settings
	// Size and location of the brush
	float BrushSize;
	FVector BrushLocation;
	//SDF change variables
	float SDFChange;
	
	//Dig Setting
	bool bIsDigging;

	UFUNCTION(BlueprintCallable, Category = "Brush")
	FHitResult GetCameraHitLocation();

private:
	//World
	UPROPERTY()
	UWorld* World;
	FRotator BrushRotation;
	float BrushLength;
	float BrushAngle;
public:
	FHitResult PerformComplexTrace(const FVector& Start, const FVector& End, AActor* IgnoredActor);
	bool IsHoleBPActor(AActor* Actor) const;
	FHitResult TraceThroughHole(const FHitResult& HoleHit, const FVector& End, AActor* IgnoredActor);
	FHitResult TraceBehindLandscape(const FHitResult& LandscapeHit, const FVector& End, AActor* IgnoredActor, AActor* HoleActor);


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