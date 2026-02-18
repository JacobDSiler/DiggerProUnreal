// BrushPreviewLightComponent.cpp
#include "BrushPreviewLightComponent.h"

UBrushPreviewLightComponent::UBrushPreviewLightComponent()
{
	// Settings to ensure it behaves like a preview light
	bHiddenInGame = true;
	bAffectsWorld = true;
	CastShadows = false;

	// These flags help prevent the Editor from drawing the Sprite/Gizmo
	bVisualizeComponent = false;
    
	// Ensure it is movable so it can follow the brush
	SetMobility(EComponentMobility::Movable);
	SetFlags(RF_Transient); // Don't save this component
}