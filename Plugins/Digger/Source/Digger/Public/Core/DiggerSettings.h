#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "DiggerSettings.generated.h"

class UHoleShapeLibrary;

/**
 * Runtime settings for the Digger system.
 * Appears in Project Settings → Digger → Runtime Settings.
 */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Runtime Settings"))
class DIGGER_API UDiggerSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UDiggerSettings();
    static const UDiggerSettings* Get();

    // --- Mesh Generation ---
    UPROPERTY(EditAnywhere, Config, Category="Mesh Generation",
        meta=(ToolTip="Radius of the skirt mesh around hole edges, in world units."))
    float SkirtRadiusWorld;

    UPROPERTY(EditAnywhere, Config, Category="Mesh Generation",
        meta=(ToolTip="Bias used when clipping skirt geometry against the landscape."))
    float SkirtClipBias;

    UPROPERTY(EditAnywhere, Config, Category="Mesh Generation",
        meta=(ToolTip="Vertical search range used when detecting terrain height for hole placement."))
    float VerticalSearchBand;

    // --- Hole Spawning ---
    UPROPERTY(EditAnywhere, Config, Category="Hole Spawning",
        meta=(ToolTip="Multiplier used when checking if a brush stroke is close enough to terrain to spawn a hole."))
    float ProximityToleranceMultiplier = 0.6f;

    UPROPERTY(EditAnywhere, Config, Category="Hole Spawning",
        meta=(ToolTip="Multiplier applied to fallback trace height when terrain detection fails."))
    float FallbackTraceHeightMultiplier = 2.0f;

    UPROPERTY(EditAnywhere, Config, Category="Hole Spawning",
        meta=(ToolTip="Offset applied when retrying chunk resolution near hole spawn locations."))
    float ChunkRetryOffset = 5.0f;

    UPROPERTY(EditAnywhere, Config, Category="Hole Spawning",
        meta=(ToolTip="Controls the final size of spawned hole actors.\nBrushRadius / ScaleDivisor = Actor Scale.\nLower values = larger holes. Higher values = smaller holes."))
    float ScaleDivisor = 47.0f;

    // Meshing
    UPROPERTY(EditAnywhere, Config, Category="Mesh Generation",
    meta=(ClampMin="0.0001", ClampMax="5.0"))
    float WeldVertexThreshold = 0.02f;


    // --- Resources ---
    UPROPERTY(EditAnywhere, Config, Category="Resources",
        meta=(AllowedClasses="/Script/Digger.HoleShapeLibrary",
              ToolTip="Default hole shape library used when spawning holes."))
    TSoftObjectPtr<UHoleShapeLibrary> DefaultHoleLibrary;

    UPROPERTY(EditAnywhere, Config, Category="Resources",
        meta=(MetaClass="Actor",
              ToolTip="Actor class spawned to represent a dynamic hole in the landscape."))
    TSoftClassPtr<AActor> DefaultHoleActorClass;
};