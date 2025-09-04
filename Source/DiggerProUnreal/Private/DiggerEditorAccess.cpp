#include "DiggerEditorAccess.h"

FOnDiggerEditorModeChanged FDiggerEditorAccess::OnEditorModeChanged;
static bool GDiggerModeActive = false;

bool FDiggerEditorAccess::IsDiggerModeActive()
{
	return GDiggerModeActive;
}

void FDiggerEditorAccess::SetEditorModeActive(bool bActive)
{
	if (GDiggerModeActive != bActive)
	{
		GDiggerModeActive = bActive;
		OnEditorModeChanged.Broadcast(bActive);
	}
}

