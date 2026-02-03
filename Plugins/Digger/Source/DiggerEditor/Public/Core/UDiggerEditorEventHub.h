#pragma once

#include "DiggerManager.h"
#include "UDiggerEditorEventHub.generated.h"

UCLASS()
class UDiggerEditorEventHub : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	
	// Generic message bus (HUD, logs, toolkit)
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnDiggerMessage, const FString&);
	static FOnDiggerMessage OnDiggerMessage;

	// Island detection forwarded to editor
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnIslandDetectedEditor, const FIslandData&);
	static FOnIslandDetectedEditor OnIslandDetectedEditor;

	// Voxel modification reports
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnVoxelReportEditor, const FVoxelModificationReport&);
	static FOnVoxelReportEditor OnVoxelReportEditor;

	// Broadcast helpers
	static void BroadcastMessage(const FString& Msg);

	static void BroadcastIslandDetected(const FIslandData& Data);

	static void BroadcastVoxelReport(const FVoxelModificationReport& Report);
};
