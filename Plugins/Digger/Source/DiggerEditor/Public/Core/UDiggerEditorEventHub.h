#pragma once

#include "DiggerManager.h"
#include "DiggerIslandRuntimeSubsystem.h"
#include "UDiggerEditorEventHub.generated.h"

/**
 * Layer 3 of the island detection architecture.
 * Editor-only UEditorSubsystem that bridges the runtime island pipeline
 * to Slate UI. Binds to UDiggerIslandRuntimeSubsystem delegates and
 * re-fires them as editor-safe delegates (always game thread).
 */
UCLASS()
class UDiggerEditorEventHub : public UEditorSubsystem
{
	GENERATED_BODY()

public:

	// -----------------------------------------------------------------------
	// Static delegates (backward-compatible, existing API)
	// -----------------------------------------------------------------------

	// Generic message bus (HUD, logs, toolkit)
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnDiggerMessage, const FString&);
	static FOnDiggerMessage OnDiggerMessage;

	// Island detection forwarded to editor
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnIslandDetectedEditor, const FIslandData&);
	static FOnIslandDetectedEditor OnIslandDetectedEditor;

	// Voxel modification reports
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnVoxelReportEditor, const FVoxelModificationReport&);
	static FOnVoxelReportEditor OnVoxelReportEditor;

	// Static broadcast helpers
	static void BroadcastMessage(const FString& Msg);
	static void BroadcastIslandDetected(const FIslandData& Data);
	static void BroadcastVoxelReport(const FVoxelModificationReport& Report);

	// -----------------------------------------------------------------------
	// Instance delegates (new — bound by FDiggerEdModeToolkit)
	// -----------------------------------------------------------------------

	DECLARE_MULTICAST_DELEGATE(FOnIslandScanStartedEditor);
	FOnIslandScanStartedEditor OnIslandScanStartedEditor;

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnIslandHandledEditor, const FIslandData&);
	FOnIslandHandledEditor OnIslandHandledEditor;

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnIslandFragmentCleanedEditor, const FIslandData&);
	FOnIslandFragmentCleanedEditor OnIslandFragmentCleanedEditor;

	// -----------------------------------------------------------------------
	// Connection API
	// -----------------------------------------------------------------------

	/** Bind manager-level events (reserved for future use). */
	void ConnectToManager(ADiggerManager* Manager);

	/** Bind to all four subsystem delegates and re-broadcast as editor delegates. */
	void ConnectToIslandSubsystem(UDiggerIslandRuntimeSubsystem* Sys);

	/** Unbind all stored handles. Safe to call when already disconnected. */
	void DisconnectAll();

private:
	FDelegateHandle Handle_ScanStarted;
	FDelegateHandle Handle_IslandDetected;
	FDelegateHandle Handle_IslandHandled;
	FDelegateHandle Handle_FragmentCleaned;

	TWeakObjectPtr<UDiggerIslandRuntimeSubsystem> BoundSubsystem;
};
