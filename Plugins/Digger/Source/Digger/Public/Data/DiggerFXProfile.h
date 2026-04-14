#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"
#include "DiggerFXProfile.generated.h"

/**
 * Defines the operation type that triggered the FX.
 */
UENUM(BlueprintType)
enum class EDiggerFXOperation : uint8
{
	Dig    UMETA(DisplayName = "Dig"),
	Fill   UMETA(DisplayName = "Fill"),
	Smooth UMETA(DisplayName = "Smooth"),
};

/**
 * A single FX entry: sound + particle effect for one operation type.
 */
USTRUCT(BlueprintType)
struct FDiggerFXEntry
{
	GENERATED_BODY()

	/** 2D sound played for the local user (editor / first-person). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	USoundBase* Sound2D = nullptr;

	/** 3D positional sound spawned at the dig location (3rd-person / multiplayer). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	USoundBase* Sound3D = nullptr;

	/** Volume multiplier for both 2D and 3D sounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float VolumeMultiplier = 1.0f;

	/** Niagara particle system spawned at the dig location. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Particles")
	UNiagaraSystem* NiagaraEffect = nullptr;

	/** Scale applied to the Niagara effect (useful for matching brush size). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Particles", meta = (ClampMin = "0.1"))
	float NiagaraScale = 1.0f;
};

/**
 * Data asset mapping dig operations to audio and particle effects.
 * Assign one of these to ADiggerManager::FXProfile to enable dig feedback.
 */
UCLASS(BlueprintType)
class DIGGER_API UDiggerFXProfile : public UDataAsset
{
	GENERATED_BODY()

public:

	/** Effects played when digging (removing material). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dig")
	FDiggerFXEntry DigFX;

	/** Effects played when filling (adding material). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fill")
	FDiggerFXEntry FillFX;

	/** Effects played when smoothing terrain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Smooth")
	FDiggerFXEntry SmoothFX;

	/** Minimum interval (seconds) between consecutive FX triggers during continuous painting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Throttle", meta = (ClampMin = "0.01", ClampMax = "2.0"))
	float MinInterval = 0.1f;

	/** Returns the FX entry for a given operation type. */
	const FDiggerFXEntry& GetEntry(EDiggerFXOperation Op) const
	{
		switch (Op)
		{
		case EDiggerFXOperation::Dig:    return DigFX;
		case EDiggerFXOperation::Fill:   return FillFX;
		case EDiggerFXOperation::Smooth: return SmoothFX;
		default:                         return DigFX;
		}
	}
};
