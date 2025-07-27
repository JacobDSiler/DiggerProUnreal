#pragma once

#include "CoreMinimal.h"
#include "SelectableBase.h"
#include "FLightBrushTypes.h"
#include "DynamicLightActor.generated.h"

UCLASS()
class DIGGERPROUNREAL_API ADynamicLightActor : public ASelectableBase
{
	GENERATED_BODY()

public:
	ADynamicLightActor();

	void InitializeFromBrush(const struct FBrushStroke& BrushStroke);
	void InitializeFromSavedData(const struct FSavedLightData& SavedData);

protected:
	virtual void OnConstruction(const FTransform& Transform) override;

private:
	void CreateLightComponent();
	void SetupEditorSprite();

	UPROPERTY()
	ELightBrushType LightType;

	UPROPERTY()
	FLinearColor LightColor;

	UPROPERTY()
	float Intensity;

	UPROPERTY()
	float Radius;

	UPROPERTY()
	float Falloff;

	UPROPERTY()
	float Angle;

	UPROPERTY(Transient)
	class ULightComponent* LightComponent;

#if WITH_EDITORONLY_DATA
	UPROPERTY(Transient)
	class UBillboardComponent* SpriteComponent;
#endif
};
