// BrushPreviewLightComponent.cpp
#include "BrushPreviewLightComponent.h"

UBrushPreviewLightComponent::UBrushPreviewLightComponent()
{
	bHiddenInGame = true;
	bAffectsWorld = true;
	CastShadows = false;

	SetVisibility(true);
	SetMobility(EComponentMobility::Movable);
	SetFlags(RF_Transient);
}
