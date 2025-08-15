#pragma once

#include "CoreMinimal.h"
#include "SelectableBase.h"
#include "FLightBrushTypes.h"
#include "FBrushStroke.h"
#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "DynamicLightActor.generated.h"

UCLASS()
class DIGGERPROUNREAL_API ADynamicLightActor : public ASelectableBase
{
	GENERATED_BODY()

public:
	ADynamicLightActor();

	void InitLight(ELightBrushType LightType);

	/** Initialize from a light brush stroke (editor or runtime) */
	void InitializeFromBrush(const FBrushStroke& BrushStroke);

protected:
	virtual void OnConstruction(const FTransform& Transform) override;

private:
	/** Creates (or recreates) the correct light component based on current cached data */
	void CreateOrUpdateLightComponent();

	/** Put this actor into a nice nested folder path in the World Outliner (editor only) */
	void AssignOutlinerFolder();

	/** Cached data from last InitializeFromBrush call */
	ELightBrushType CachedType = ELightBrushType::Point;
	FLinearColor    CachedColor = FLinearColor::White;
	float           CachedIntensity = 1000.f;   // lumens (point/spot), lux (directional)
	float           CachedRadius = 500.f;       // cm (attenuation)
	float           CachedFalloff = 2.0f;       // only relevant for point/spot if you use it
	float           CachedAngle = 45.f;         // degrees (spot)

	UPROPERTY(Transient)
	ULightComponent* LightComponent = nullptr;
};
