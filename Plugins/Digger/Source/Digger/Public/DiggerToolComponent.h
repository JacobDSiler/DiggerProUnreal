#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DiggerManager.h"
#include "DiggerToolComponent.generated.h"

UCLASS( ClassGroup=(Digger), meta=(BlueprintSpawnableComponent) )
class DIGGER_API UDiggerToolComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	UDiggerToolComponent();

	// --- Configuration ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Digger Tool")
	float BrushRadius = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Digger Tool")
	float BrushFalloff = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Digger Tool")
	float TraceDistance = 5000.0f; // How far the tool reaches

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Digger Tool")
	EVoxelBrushType BrushType = EVoxelBrushType::Sphere;

	// --- Actions ---

	// Digs into the terrain
	UFUNCTION(BlueprintCallable, Category = "Digger Tool")
	bool DigAtLocation(FVector Origin, FVector Direction);

	// Adds to the terrain (fills holes)
	UFUNCTION(BlueprintCallable, Category = "Digger Tool")
	bool AddAtLocation(FVector Origin, FVector Direction, bool bIsDigging);

private:
	UPROPERTY()
	ADiggerManager* CachedManager;
	void EnsureManager();
};