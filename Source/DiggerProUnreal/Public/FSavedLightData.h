#pragma once

#include "CoreMinimal.h"
#include "FLightBrushTypes.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Engine/DirectionalLight.h"
#include "FSavedLightData.generated.h"

USTRUCT(BlueprintType)
struct FSavedLightData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector Location;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FRotator Rotation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector Scale;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	ELightBrushType LightType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FLinearColor LightColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Intensity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Radius;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Falloff;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Angle;

	FSavedLightData()
		: Location(FVector::ZeroVector)
		, Rotation(FRotator::ZeroRotator)
		, Scale(FVector(1.0f))
		, LightType(ELightBrushType::Point)
		, LightColor(FLinearColor::White)
		, Intensity(1000.f)
		, Radius(100.f)
		, Falloff(300.f)
		, Angle(45.f)
	{}

	/** Capture light properties from an existing light actor */
	void CaptureFromLightActor(const AActor* LightActor)
	{
		if (!LightActor) return;

		Location = LightActor->GetActorLocation();
		Rotation = LightActor->GetActorRotation();
		Scale = LightActor->GetActorScale3D();

		if (const APointLight* PointLight = Cast<APointLight>(LightActor))
		{
			if (UPointLightComponent* PointComp = Cast<UPointLightComponent>(PointLight->GetLightComponent()))
			{
				LightType = ELightBrushType::Point;
				LightColor = PointComp->GetLightColor();
				Intensity = PointComp->Intensity;
				Radius = PointComp->AttenuationRadius;
				Falloff = PointComp->LightFalloffExponent;
			}
		}
		else if (const ASpotLight* SpotLight = Cast<ASpotLight>(LightActor))
		{
			if (USpotLightComponent* SpotComp = Cast<USpotLightComponent>(SpotLight->GetLightComponent()))
			{
				LightType = ELightBrushType::Spot;
				LightColor = SpotComp->GetLightColor();
				Intensity = SpotComp->Intensity;
				Radius = SpotComp->AttenuationRadius;
				Falloff = SpotComp->LightFalloffExponent;
				Angle = SpotComp->InnerConeAngle;
			}
		}
		else if (const ADirectionalLight* DirLight = Cast<ADirectionalLight>(LightActor))
		{
			if (UDirectionalLightComponent* DirComp = Cast<UDirectionalLightComponent>(DirLight->GetLightComponent()))
			{
				LightType = ELightBrushType::Directional;
				LightColor = DirComp->GetLightColor();
				Intensity = DirComp->Intensity;
				// Directional lights donâ€™t use Radius/Falloff/Angle
			}
		}
	}

	/** Spawn a light actor in the given world using this saved data */
	AActor* SpawnLightActor(UWorld* World) const
	{
		if (!World) return nullptr;

		AActor* NewLight = nullptr;

		switch (LightType)
		{
		case ELightBrushType::Point:
		{
			APointLight* PointLight = World->SpawnActor<APointLight>(Location, Rotation);
			PointLight->SetActorScale3D(Scale);
			if (UPointLightComponent* PointComp = Cast<UPointLightComponent>(PointLight->GetLightComponent()))
			{
				PointComp->SetLightColor(LightColor);
				PointComp->Intensity = Intensity;
				PointComp->AttenuationRadius = Radius;
				PointComp->LightFalloffExponent = Falloff;
			}
			NewLight = PointLight;
			break;
		}
		case ELightBrushType::Spot:
		{
			ASpotLight* SpotLight = World->SpawnActor<ASpotLight>(Location, Rotation);
			SpotLight->SetActorScale3D(Scale);
			if (USpotLightComponent* SpotComp = Cast<USpotLightComponent>(SpotLight->GetLightComponent()))
			{
				SpotComp->SetLightColor(LightColor);
				SpotComp->Intensity = Intensity;
				SpotComp->AttenuationRadius = Radius;
				SpotComp->LightFalloffExponent = Falloff;
				SpotComp->InnerConeAngle = Angle;
			}
			NewLight = SpotLight;
			break;
		}
		case ELightBrushType::Directional:
		{
			ADirectionalLight* DirLight = World->SpawnActor<ADirectionalLight>(Location, Rotation);
			DirLight->SetActorScale3D(Scale);
			if (UDirectionalLightComponent* DirComp = Cast<UDirectionalLightComponent>(DirLight->GetLightComponent()))
			{
				DirComp->SetLightColor(LightColor);
				DirComp->Intensity = Intensity;
			}
			NewLight = DirLight;
			break;
		}
		}

		return NewLight;
	}
};

