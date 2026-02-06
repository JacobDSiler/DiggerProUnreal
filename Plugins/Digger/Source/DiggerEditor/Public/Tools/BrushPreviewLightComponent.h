// BrushPreviewLightComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/PointLightComponent.h"
#include "BrushPreviewLightComponent.generated.h"

UCLASS(ClassGroup=(Digger), meta=(BlueprintSpawnableComponent))
class DIGGEREDITOR_API UBrushPreviewLightComponent : public UPointLightComponent
{
	GENERATED_BODY()

public:
	UBrushPreviewLightComponent();

	virtual bool ShouldRenderLightGizmo() const { return false; }
	virtual bool ShouldRenderInEditor() const { return false; }
	virtual bool ShouldRenderSelected() const { return false; }
};
