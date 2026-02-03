#include "UDiggerEditorEventHub.h"

UDiggerEditorEventHub::FOnDiggerMessage UDiggerEditorEventHub::OnDiggerMessage;
UDiggerEditorEventHub::FOnIslandDetectedEditor UDiggerEditorEventHub::OnIslandDetectedEditor;
UDiggerEditorEventHub::FOnVoxelReportEditor UDiggerEditorEventHub::OnVoxelReportEditor;

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
