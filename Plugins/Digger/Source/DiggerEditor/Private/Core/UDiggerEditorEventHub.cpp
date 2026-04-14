#include "UDiggerEditorEventHub.h"

// ---------------------------------------------------------------------------
// Static delegate storage
// ---------------------------------------------------------------------------

UDiggerEditorEventHub::FOnDiggerMessage UDiggerEditorEventHub::OnDiggerMessage;
UDiggerEditorEventHub::FOnIslandDetectedEditor UDiggerEditorEventHub::OnIslandDetectedEditor;
UDiggerEditorEventHub::FOnVoxelReportEditor UDiggerEditorEventHub::OnVoxelReportEditor;

// ---------------------------------------------------------------------------
// Static broadcast helpers
// ---------------------------------------------------------------------------

void UDiggerEditorEventHub::BroadcastMessage(const FString& Msg)
{
	OnDiggerMessage.Broadcast(Msg);
}

void UDiggerEditorEventHub::BroadcastIslandDetected(const FIslandData& Data)
{
	OnIslandDetectedEditor.Broadcast(Data);
}

void UDiggerEditorEventHub::BroadcastVoxelReport(const FVoxelModificationReport& Report)
{
	OnVoxelReportEditor.Broadcast(Report);
}

// ---------------------------------------------------------------------------
// Connection API
// ---------------------------------------------------------------------------

void UDiggerEditorEventHub::ConnectToManager(ADiggerManager* Manager)
{
	// Reserved for future manager-level event binding.
	// Currently a no-op — all island events route through the subsystem.
}

void UDiggerEditorEventHub::ConnectToIslandSubsystem(UDiggerIslandRuntimeSubsystem* Sys)
{
	if (!Sys)
	{
		return;
	}

	// Unbind stale handles before rebinding (safe re-entry).
	DisconnectAll();

	BoundSubsystem = Sys;

	Handle_ScanStarted = Sys->OnIslandScanStarted.AddLambda([this]()
	{
		OnIslandScanStartedEditor.Broadcast();
	});

	Handle_IslandDetected = Sys->OnIslandDetected.AddLambda([this](const FIslandData& Island)
	{
		// Re-broadcast through both instance and static delegates.
		OnIslandDetectedEditor.Broadcast(Island);
	});

	Handle_IslandHandled = Sys->OnIslandHandled.AddLambda([this](const FIslandData& Island)
	{
		OnIslandHandledEditor.Broadcast(Island);
	});

	Handle_FragmentCleaned = Sys->OnIslandFragmentCleaned.AddLambda([this](const FIslandData& Island)
	{
		OnIslandFragmentCleanedEditor.Broadcast(Island);
	});
}

void UDiggerEditorEventHub::DisconnectAll()
{
	if (UDiggerIslandRuntimeSubsystem* Sys = BoundSubsystem.Get())
	{
		if (Handle_ScanStarted.IsValid())
		{
			Sys->OnIslandScanStarted.Remove(Handle_ScanStarted);
		}
		if (Handle_IslandDetected.IsValid())
		{
			Sys->OnIslandDetected.Remove(Handle_IslandDetected);
		}
		if (Handle_IslandHandled.IsValid())
		{
			Sys->OnIslandHandled.Remove(Handle_IslandHandled);
		}
		if (Handle_FragmentCleaned.IsValid())
		{
			Sys->OnIslandFragmentCleaned.Remove(Handle_FragmentCleaned);
		}
	}

	Handle_ScanStarted.Reset();
	Handle_IslandDetected.Reset();
	Handle_IslandHandled.Reset();
	Handle_FragmentCleaned.Reset();
	BoundSubsystem.Reset();
}
