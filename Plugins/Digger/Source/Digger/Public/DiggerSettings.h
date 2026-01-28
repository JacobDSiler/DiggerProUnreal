#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "DiggerSettings.generated.h"

/**
 * Runtime settings for Digger.
 * These settings exist in the game build and control generation logic.
 */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Digger Runtime"))
class DIGGER_API UDiggerSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UDiggerSettings();
	static const UDiggerSettings* Get();

	// --- Mesh Generation ---
	UPROPERTY(EditAnywhere, Config, Category="Mesh Generation")
	float SkirtRadiusWorld;

	UPROPERTY(EditAnywhere, Config, Category="Mesh Generation")
	float SkirtClipBias;

	UPROPERTY(EditAnywhere, Config, Category="Mesh Generation")
	float VerticalSearchBand;

	// --- Hole Spawning ---
	UPROPERTY(EditAnywhere, Config, Category="Hole Spawning")
	float ProximityToleranceMultiplier = 0.6f;

	UPROPERTY(EditAnywhere, Config, Category="Hole Spawning")
	float FallbackTraceHeightMultiplier = 2.0f;

	UPROPERTY(EditAnywhere, Config, Category="Hole Spawning")
	float ChunkRetryOffset = 5.0f;

	UPROPERTY(EditAnywhere, Config, Category="Hole Spawning")
	float ScaleDivisor = 47.0f;
};
