#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VoxelConversionDebugger.generated.h"

/**
 * Debug actor to help diagnose voxel coordinate conversion issues
 */
UCLASS()
class DIGGERPROUNREAL_API AVoxelConversionDebugger : public AActor
{
	GENERATED_BODY()

public:
	AVoxelConversionDebugger();

	// Test a world position and display all conversion results
	UFUNCTION(BlueprintCallable, Category = "Voxel Debug")
	void TestPositionConversion(FVector WorldPosition);

	// Visualize chunk boundaries at a given position
	UFUNCTION(BlueprintCallable, Category = "Voxel Debug")
	void VisualizeChunkBoundaries(FVector WorldPosition, float Duration = 10.0f);

	// Visualize the voxel at a given position
	UFUNCTION(BlueprintCallable, Category = "Voxel Debug")
	void VisualizeVoxelAtPosition(FVector WorldPosition, float Duration = 10.0f);

	// Round trip test (World -> Voxel -> World)
	UFUNCTION(BlueprintCallable, Category = "Voxel Debug")
	void TestRoundTripConversion(FVector WorldPosition);

private:
	// Draw debug chunks
	void DrawDebugChunk(const FIntVector& ChunkCoords, float Duration);
};
