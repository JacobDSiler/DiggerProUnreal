#pragma once

#include "CoreMinimal.h"
#include "FLightBrushTypes.h"
#include "DynamicLightActor.h" // Required to spawn the class
#include "VoxelConversion.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Engine/DirectionalLight.h"
#include "FSavedLightData.generated.h"

// Forward declare to avoid circular include; implementation in .cpp files.
class ADiggerManager;

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
		, Radius(1000.f)
		, Falloff(2.0f)
		, Angle(45.f)
	{}

	/** Capture light properties from an existing light actor */
	void CaptureFromLightActor(const AActor* LightActor)
	{
		if (!LightActor) return;

		Location = LightActor->GetActorLocation();
		Rotation = LightActor->GetActorRotation();
		Scale = LightActor->GetActorScale3D();

		if (const ADynamicLightActor* DynamicLight = Cast<ADynamicLightActor>(LightActor))
		{
			if (ULightComponent* LightComp = DynamicLight->FindComponentByClass<ULightComponent>())
			{
				LightColor = LightComp->GetLightColor();
				Intensity = LightComp->Intensity;

				if (const UPointLightComponent* Point = Cast<UPointLightComponent>(LightComp))
				{
					LightType = ELightBrushType::Point;
					Radius = Point->AttenuationRadius;
					Falloff = Point->LightFalloffExponent;
				}
				else if (const USpotLightComponent* Spot = Cast<USpotLightComponent>(LightComp))
				{
					LightType = ELightBrushType::Spot;
					Radius = Spot->AttenuationRadius;
					Falloff = Spot->LightFalloffExponent;
					Angle = Spot->InnerConeAngle;
				}
				else if (Cast<UDirectionalLightComponent>(LightComp))
				{
					LightType = ELightBrushType::Directional;
				}
			}
			return;
		}

		// Fallback for standard engine lights (legacy support)
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
			}
		}
	}

	/** Spawn a light actor in the given world using this saved data.
	 *
	 *  InDiggerManager: optional.  When provided the spawned light is
	 *  registered in the UID registry so undo/redo can find it by ID.
	 *  Pass nullptr when spawning from a context without a manager (e.g.
	 *  preview, cooking). */
	AActor* SpawnLightActor(UWorld* World, ADiggerManager* InDiggerManager = nullptr) const
	{
		if (!World) return nullptr;

		ADynamicLightActor* NewLight = World->SpawnActor<ADynamicLightActor>(
			ADynamicLightActor::StaticClass(), Location, Rotation);

		if (!NewLight) return nullptr;

		NewLight->SetActorScale3D(Scale);
		NewLight->InitLight(LightType);

		// Wire up manager + allocate UID so the actor enters the undo registry.
		if (InDiggerManager)
		{
			NewLight->SetDiggerManager(InDiggerManager);
			const int32 NewUID = InDiggerManager->AllocateActorUID();
			NewLight->ActorUID = NewUID;
			InDiggerManager->RegisterActorUID(NewUID, NewLight);

			// Assign owning chunk
			FIntVector ChunkCoords = FVoxelConversion::WorldToChunk(Location);
			if (UVoxelChunk* Chunk = InDiggerManager->GetOrCreateChunkAtCoords(ChunkCoords))
				NewLight->SetOwningChunk(Chunk);
		}

		if (ULightComponent* LightComp = NewLight->FindComponentByClass<ULightComponent>())
		{
			LightComp->SetLightColor(LightColor);
			LightComp->SetIntensity(Intensity);

			switch (LightType)
			{
			case ELightBrushType::Point:
				if (UPointLightComponent* Point = Cast<UPointLightComponent>(LightComp))
				{
					Point->SetAttenuationRadius(Radius);
					Point->SetLightFalloffExponent(Falloff);
				}
				break;
			case ELightBrushType::Spot:
				if (USpotLightComponent* Spot = Cast<USpotLightComponent>(LightComp))
				{
					Spot->SetAttenuationRadius(Radius);
					Spot->SetLightFalloffExponent(Falloff);
					Spot->SetInnerConeAngle(Angle);
					Spot->SetOuterConeAngle(Angle + 10.0f);
				}
				break;
			case ELightBrushType::Directional:
				break;
			}
		}

#if WITH_EDITOR
		NewLight->SetFolderPath(FName("Digger/DynamicLights"));
#endif

		return NewLight;
	}

	friend FArchive& operator<<(FArchive& Ar, FSavedLightData& Data)
	{
		Ar << Data.Location;
		Ar << Data.Rotation;
		Ar << Data.Scale;

		if (Ar.IsSaving())
		{
			uint8 TypeByte = (uint8)Data.LightType;
			Ar << TypeByte;
		}
		else
		{
			uint8 TypeByte = 0;
			Ar << TypeByte;
			Data.LightType = (ELightBrushType)TypeByte;
		}

		Ar << Data.LightColor;
		Ar << Data.Intensity;
		Ar << Data.Radius;
		Ar << Data.Falloff;
		Ar << Data.Angle;

		return Ar;
	}
};