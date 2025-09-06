#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/Texture2D.h"
#include "DiggerTextureSet.generated.h"

UCLASS(BlueprintType)
class DIGGERPROUNREAL_API UDiggerTextureSet : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Digger|Textures")
	TSoftObjectPtr<UTexture2D> BaseColor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Digger|Textures")
	TSoftObjectPtr<UTexture2D> Normal;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Digger|Textures")
	TSoftObjectPtr<UTexture2D> ORM; // Occlusion-Roughness-Metallic packed
};
