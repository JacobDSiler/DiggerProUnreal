#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TestVoxelConversion.generated.h"

/**
 * Simple test actor that can be dropped into a level to verify voxel conversion functionality.
 */
UCLASS()
class DIGGERPROUNREAL_API ATestVoxelConversion : public AActor
{
	GENERATED_BODY()
    
public:    
	ATestVoxelConversion();

	virtual void BeginPlay() override;
    
	// Editor properties to configure the test
	UPROPERTY(EditAnywhere, Category = "Voxel Test")
	int32 ChunkSize = 8;
    
	UPROPERTY(EditAnywhere, Category = "Voxel Test")
	int32 Subdivisions = 4;
    
	UPROPERTY(EditAnywhere, Category = "Voxel Test")
	float TerrainGridSize = 100.0f;
    
	UPROPERTY(EditAnywhere, Category = "Voxel Test")
	FVector VoxelOrigin = FVector::ZeroVector;
    
	// Test functions
	UFUNCTION(BlueprintCallable, Category = "Voxel Test")
	void RunTests();
    
	UFUNCTION(BlueprintCallable, Category = "Voxel Test")
	void TestPositionAtDistance(float Distance);
    
	UFUNCTION(BlueprintCallable, Category = "Voxel Test")
	void VisualizeChunksAround(float Radius);
};
