#pragma once

#include "CoreMinimal.h"
#include "SelectableBase.h"
#include "LightBrushTypes.h" // Your ELightBrushType enum
#include "DynamicLightActor.generated.h"

UCLASS()
class DIGGERPROUNREAL_API ADynamicLightActor : public ASelectableBase
{
	GENERATED_BODY()

public:
	ADynamicLightActor();

	// Initializes light from a brush stroke
	void InitializeFromBrush(const struct FBrushStroke& BrushStroke);

	// Used to rebuild from saved data
	void InitializeFromSavedData(const struct FSavedLightData& SavedData);

	// Light data
	UPROPERTY(VisibleAnywhere)
	ELightBrushType LightType;

	UPROPERTY(VisibleAnywhere)
	FLinearColor LightColor;

	UPROPERTY(VisibleAnywhere)
	float Intensity;

	UPROPERTY(VisibleAnywhere)
	float Radius;

	UPROPERTY(VisibleAnywhere)
	float Falloff;

	UPROPERTY(VisibleAnywhere)
	float Angle;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;

private:
	void CreateLightComponent();
};
