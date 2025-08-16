#pragma once

#include "CoreMinimal.h"
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
	FBrushStroke CreateBrushStroke(const FHitResult& HitResult, bool bIsDig) const;

	UFUNCTION(BlueprintCallable, Category = "Brush")
	bool GetCameraHitLocation(FHitResult& OutHitResult);
	void DebugDrawLineIfEnabled( FVector& Start, FVector& End, FColor Color, float Duration) const;

	// In VoxelBrushShape.h
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Voxel")
	float CalculateSDF(
		const FVector& WorldPos,
		const FBrushStroke& Stroke,
		float TerrainHeight
	) const;

	virtual float CalculateSDF_Implementation(
		const FVector& WorldPos,
		const FBrushStroke& Stroke,
		float TerrainHeight
	) const { return 0.0f; }
	
	
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

	// In UVoxelBrushShape.h
	virtual bool IsWithinBounds(const FVector& WorldPos, const FBrushStroke& Stroke) const;

protected:
	//Brush Settings
	// Size and location of the brush
	float BrushSize;
	FVector BrushLocation;
	//SDF change variables
	float SDFChange;
	
	//Dig Setting
	bool bIsDigging;


private:
	//World
	UPROPERTY()
	UWorld* World;
public:
	
	// Public so you can toggle from blueprint or details panel
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Debug")
	bool bEnableDebugDrawing = false;

	void DebugDrawPointIfEnabled(FVector& Location, FColor Color, float Size, float Duration) const;
	void DebugDrawSphereIfEnabled(FVector& Location, FColor Color, float Size, float Duration) const;
	UFUNCTION(BlueprintCallable, Category="Debug")
	void SetDebugDrawingEnabled(bool bEnabled);

	UFUNCTION(BlueprintCallable, Category="Debug")
	bool GetDebugDrawingEnabled();
	
	FHitResult PerformComplexTrace(FVector& Start, FVector& End, AActor* IgnoredActor) const;
	bool IsLandscape(const AActor* Actor) const;
	bool IsProceduralMesh(const AActor* Actor) const;

	FHitResult RecursiveTraceThroughHoles(
		FVector& Start,
		FVector& End,
		TArray<AActor*>& IgnoredActors,
		bool bPassedThroughHole,
		bool bIgnoreHolesNow,
		int32 Depth
	) const;

	FHitResult RecursiveTraceThroughHoles_Internal(
		FVector& Start,
		FVector& End,
		TArray<AActor*>& IgnoredActors,
		int32 Depth,
		const FVector& OriginalDirection, bool bPassedThroughHole
	) const;
	
	FHitResult SmartTrace(const FVector& Start, const FVector& End);

	bool IsHoleBPActor(const AActor* Actor) const;

	FRotator BrushRotation;
	float BrushLength;
	float BrushAngle;

	void SetWorld(UWorld* SetWorld)
	{
		UE_LOG(LogTemp, Error, TEXT("Brush shape: Set World used!"));
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