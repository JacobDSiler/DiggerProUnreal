#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "ProcgenArcanaCaveImporter.h"
#include "UObject/StrongObjectPtr.h"

class ADiggerManager;
class AActor;

class SDiggerCaveImporterWidget : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SDiggerCaveImporterWidget) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs, ADiggerManager* InManager);

    // Call this if the editor mode detects a click and we are in "World Scale" placement mode
    void HandleWorldScaleClick(const FVector& ClickLocation);

private:
    // Reference to Manager
    TWeakObjectPtr<ADiggerManager> DiggerManager;

    // The Importer Object (Logic Engine)
    TStrongObjectPtr<UProcgenArcanaCaveImporter> CaveImporter;

    // --- State Variables (Moved from Toolkit) ---
    FString SelectedSVGFilePath;
    float CaveScale = 100.0f;
    float SimplificationLevel = 0.5f;
    int32 MaxSplinePoints = 200;
    int32 HeightMode = 1; // Descending
    float HeightVariation = 50.0f;
    float DescentRate = 0.3f;
    bool bAutoDetectEntrance = true;
    FVector2D ManualEntrancePoint = FVector2D::ZeroVector;
    int32 OutputFormat = 0; // 0=Single, 1=Multi
    int32 PivotMode = 0; // Spline Center

    // Preview State
    bool bHasActivePreview = false;
    TArray<FVector> StoredPreviewPoints;
    TArray<FVector> StoredEntrancePoints;
    TArray<FVector> StoredExitPoints;

    // Positioning State
    bool bUseWorldScalePositioning = false;
    bool bSnapToGrid = false;
    float SnapIncrement = 50.0f;
    FVector PreviewPositionOffset = FVector::ZeroVector;
    FVector WorldScalePosition = FVector::ZeroVector;
    TWeakObjectPtr<AActor> WorldScaleIndicatorActor;

    // --- Options Arrays ---
    TArray<TSharedPtr<FString>> HeightModeOptions;
    TArray<TSharedPtr<FString>> PivotModeOptions;

    // --- UI Methods ---
    FReply OnBrowseFile();
    FReply OnPreviewClicked();
    FReply OnClearPreviewClicked();
    FReply OnImportClicked();
    
    // --- Logic Methods (Moved from Toolkit) ---
    void PreviewProcgenArcanaCave();
    void ImportProcgenArcanaCave();
    void ImportMultiSplineCave();
    void ClearCavePreview();
    
    // Helper to calculate pivot
    FVector CalculatePivotOffset(const TArray<FVector>& Points, const TArray<FVector>& Entrances, const TArray<FVector>& Exits);
    FString GetPivotModeDisplayName(int32 Mode);
    
    // World placement logic
    void PlaceWorldScaleIndicator();
    void ClearWorldScaleIndicator();
    void UpdatePreviewPosition();

    // Spawning Logic (Moved from Toolkit)
    AActor* CreateCaveSplineActor(USplineComponent* SplineComponent, const TArray<FVector>& OriginalPoints, const TArray<FVector>& EntrancePoints, const TArray<FVector>& ExitPoints);
    AActor* CreateManualMultiSplineActor(const FMultiSplineCaveData& CaveData, UWorld* World, const FVector& PivotOffset);
    FLinearColor GetSplineColorForType(ECavePassageType Type);
};