#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/Texture2D.h"
#include "DiggerMaterialTypes.generated.h"

UENUM(BlueprintType)
enum class EDiggerMaterialMode : uint8
{
    VolumetricMeshes UMETA(DisplayName="Volumetric Meshes"),
    LandscapeBlend   UMETA(DisplayName="Landscape Blend")
};

UCLASS(BlueprintType)
class DIGGERPROUNREAL_API UDiggerTextureSet : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Textures")
    TSoftObjectPtr<UTexture2D> BaseColor;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Textures")
    TSoftObjectPtr<UTexture2D> Normal;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Textures")
    TSoftObjectPtr<UTexture2D> ORM;

#if WITH_EDITORONLY_DATA
    UPROPERTY(VisibleAnywhere, Category="Editor Only")
    bool bEnableVirtualTextureStreaming = true;
#endif
};

USTRUCT(BlueprintType)
struct DIGGERPROUNREAL_API FDiggerLayerParams
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, Category="Layer") FString LayerName = TEXT("New Layer");
    UPROPERTY(EditAnywhere, Category="Layer") bool bEnabled = true;
    UPROPERTY(EditAnywhere, Category="Layer") TSoftObjectPtr<UDiggerTextureSet> TextureSet;

    UPROPERTY(EditAnywhere, Category="Mapping") float Tiling = 0.01f;
    UPROPERTY(EditAnywhere, Category="Blend|Height") float MinHeight = 0.f;
    UPROPERTY(EditAnywhere, Category="Blend|Height") float MaxHeight = 200.f;
    UPROPERTY(EditAnywhere, Category="Blend|Noise")  float NoiseAmplitude = 50.f;
    UPROPERTY(EditAnywhere, Category="Blend|Noise")  float NoiseFrequency = 0.002f;
    UPROPERTY(EditAnywhere, Category="Blend|Transition") float EdgeSharpness = 2.f;
    UPROPERTY(EditAnywhere, Category="Blend|Misc") float Seed = 1001.f;

    UPROPERTY(EditAnywhere, Category="Landscape", meta=(EditCondition=false, EditConditionHides))
    FName WeightmapLayerName;
};

UCLASS(BlueprintType)
class DIGGERPROUNREAL_API UDiggerMaterialProfile : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Category="Mode") EDiggerMaterialMode Mode = EDiggerMaterialMode::VolumetricMeshes;
    UPROPERTY(EditAnywhere, Category="Layers") TArray<FDiggerLayerParams> Layers;
    UPROPERTY(EditAnywhere, Category="Mapping") bool bUseTriplanar = true;
};
