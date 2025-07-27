#pragma once

#include "CoreMinimal.h"
#include "FLightBrushTypes.h"
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
		: Location(FVector::ZeroVector),
		  Rotation(FRotator::ZeroRotator),
		  Scale(FVector(1.0f)),
		  LightType(ELightBrushType::Point),
		  LightColor(FLinearColor::White),
		  Intensity(1000.f),
		  Radius(100.f),
		  Falloff(300.f),
		  Angle(45.f)
	{}
};
