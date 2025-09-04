#pragma once

#include "CoreMinimal.h"
#include "FLightBrushTypes.h"
#include "Delegates/Delegate.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnDiggerEditorModeChanged, bool /* bActive */);

class DIGGERPROUNREAL_API FDiggerEditorAccess
{
public:
	// Exposed delegate SelectableBase will bind to
	static FOnDiggerEditorModeChanged OnEditorModeChanged;

	// For SelectableBase to query current state
	static bool IsDiggerModeActive();

	// For DiggerEditor module to set current state
	static void SetEditorModeActive(bool bActive);
};

