#pragma once

#include "CoreMinimal.h"

class DIGGERPROUNREAL_API FDiggerEditorAccess
{
public:
	// This function pointer is set by DiggerEditor at runtime
	static TFunction<bool()> IsDiggerEditorModeActive;

	// This is what other modules (like SelectableBase) call
	static bool IsEditorModeActive()
	{
		return IsDiggerEditorModeActive ? IsDiggerEditorModeActive() : false;
	}
};
