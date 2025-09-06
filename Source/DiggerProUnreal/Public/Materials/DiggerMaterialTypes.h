#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Materials/MaterialInterface.h"
#include "DiggerMaterialTypes.generated.h"

// Forward declare texture set class (define elsewhere as UCLASS)
class UDiggerTextureSet;

/** High-level profile types shown in the DMM dropdown */
UENUM(BlueprintType)
enum class EDiggerProfileType : uint8
{
    Sediment     UMETA(DisplayName="Sediment"),
    Landscape    UMETA(DisplayName="Landscape"),
    Utilities    UMETA(DisplayName="Utilities"),
};

/** Rendering/material mode for the profile */
UENUM(BlueprintType)
enum class EDiggerMaterialMode : uint8
{
    VolumetricMeshes  UMETA(DisplayName="Volumetric Meshes"),
    Landscape         UMETA(DisplayName="Landscape"),
};

USTRUCT(BlueprintType)
struct FDiggerLayerParams
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString LayerName = TEXT("Layer");
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bEnabled = true;

    // Points at a UDiggerTextureSet (Albedo/Normal/ORM), keep SoftObject so assets can be unloaded
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<UDiggerTextureSet> TextureSet;

    // Scalar params the master material expects
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Tiling         = 0.01f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float MinHeight      = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float MaxHeight      = 200.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float NoiseAmplitude = 50.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float NoiseFrequency = 0.002f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float EdgeSharpness  = 2.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Seed           = 1001.0f;
};

/**
 * Data profile for Digger materials (editor-created DataAsset)
 * NOTE: Export macro must match your runtime module's API macro.
 */
UCLASS(BlueprintType, EditInlineNew)
class DIGGERPROUNREAL_API UDiggerMaterialProfile : public UDataAsset
{
    GENERATED_BODY()

public:
    /** For UI/organizational purposes (Sediment/Landscape/Utilities) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Digger|Materials")
    EDiggerProfileType ProfileType = EDiggerProfileType::Sediment;

    /** Render/material binding mode (volumetric meshes vs landscape) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Digger|Materials")
    EDiggerMaterialMode Mode = EDiggerMaterialMode::VolumetricMeshes;

    /** Master material override (optional). If null, use your project default. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Digger|Materials")
    TSoftObjectPtr<UMaterialInterface> MasterMaterialOverride;

    /** Global toggles the master material may read */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Digger|Materials|Global")
    bool bUseTriplanar = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Digger|Materials|Global")
    bool bUseVirtualTexture = false;

    /** Per-layer configuration */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Digger|Materials")
    TArray<FDiggerLayerParams> Layers;
};
