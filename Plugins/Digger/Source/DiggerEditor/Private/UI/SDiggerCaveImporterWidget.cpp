#include "SDiggerCaveImporterWidget.h"
#include "DiggerManager.h"
#include "DiggerDebug.h"
#include "DiggerFeatureFlags.h"
#include "DesktopPlatformModule.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "EditorViewportClient.h"
#include "Editor.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "Components/SplineComponent.h"
#include "PropertyEditorModule.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SDiggerCaveImporterWidget"

void SDiggerCaveImporterWidget::Construct(const FArguments& InArgs, ADiggerManager* InManager)
{
    DiggerManager = InManager;
    CaveImporter.Reset(NewObject<UProcgenArcanaCaveImporter>(GetTransientPackage()));

    // Init Options
    HeightModeOptions.Add(MakeShareable(new FString("Flat Cave")));
    HeightModeOptions.Add(MakeShareable(new FString("Descending Cave")));
    HeightModeOptions.Add(MakeShareable(new FString("Rolling Hills")));
    HeightModeOptions.Add(MakeShareable(new FString("Mountainous")));
    HeightModeOptions.Add(MakeShareable(new FString("Custom")));

    PivotModeOptions.Add(MakeShareable(new FString("Spline Center")));
    PivotModeOptions.Add(MakeShareable(new FString("First Entrance")));
    PivotModeOptions.Add(MakeShareable(new FString("First Exit")));
    PivotModeOptions.Add(MakeShareable(new FString("Spline Start")));
    PivotModeOptions.Add(MakeShareable(new FString("Spline End")));

    // Default Selection
    SelectedSVGFilePath = TEXT("");

    ChildSlot
    [
        SNew(SVerticalBox)

        // --- File Selection Section ---
        + SVerticalBox::Slot().AutoHeight().Padding(2)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.0f)
            [
                SNew(SEditableTextBox)
                .Text_Lambda([this](){ return FText::FromString(SelectedSVGFilePath.IsEmpty() ? TEXT("No file selected...") : SelectedSVGFilePath); })
                .IsReadOnly(true)
                .HintText(FText::FromString(TEXT("Select an SVG file from ProcgenArcana's map generator")))
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)
            [
                SNew(SButton)
                .Text(FText::FromString("Browse..."))
                .OnClicked(this, &SDiggerCaveImporterWidget::OnBrowseFile)
            ]
        ]

        // --- Import Settings Header ---
        + SVerticalBox::Slot().AutoHeight().Padding(2, 8, 2, 2)
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("Import Settings")))
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
        ]

        // --- Output Format Selection ---
        + SVerticalBox::Slot().AutoHeight().Padding(2)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)
            [
                SNew(STextBlock).Text(FText::FromString(TEXT("Output Format:")))
            ]
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SHorizontalBox)
                // Single Spline
                + SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
                [
                    SNew(SButton)
                    .ButtonColorAndOpacity_Lambda([this]() { return OutputFormat == 0 ? FLinearColor(0.1f, 0.5f, 1.0f, 1.0f) : FLinearColor::White; })
                    .Text(FText::FromString(TEXT("Single\nSpline")))
                    .OnClicked_Lambda([this]() { OutputFormat = 0; return FReply::Handled(); })
                ]
                // Multi-Spline
                + SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
                [
                    SNew(SButton)
                    .ButtonColorAndOpacity_Lambda([this]() { return OutputFormat == 1 ? FLinearColor(0.1f, 0.5f, 1.0f, 1.0f) : FLinearColor::White; })
                    .Text(FText::FromString(TEXT("Multi\nSpline")))
                    .OnClicked_Lambda([this]() { OutputFormat = 1; return FReply::Handled(); })
                ]
                // Placeholders for future features
                + SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
                [
                    SNew(SButton).IsEnabled(false).Text(FText::FromString(TEXT("Proc\nMesh")))
                ]
                + SHorizontalBox::Slot().FillWidth(1.0f).Padding(1)
                [
                    SNew(SButton).IsEnabled(false).Text(FText::FromString(TEXT("SDF\nBrush")))
                ]
            ]
        ]

        // --- Pivot Mode Selection ---
        + SVerticalBox::Slot().AutoHeight().Padding(2)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(0.4f)[ SNew(STextBlock).Text(FText::FromString(TEXT("Origin Point:"))) ]
                + SHorizontalBox::Slot().FillWidth(0.6f)
                [
                    SNew(SComboBox<TSharedPtr<FString>>)
                    .OptionsSource(&PivotModeOptions)
                    .OnGenerateWidget_Lambda([](TSharedPtr<FString> InOption) { return SNew(STextBlock).Text(FText::FromString(*InOption)); })
                    .OnSelectionChanged_Lambda([this](TSharedPtr<FString> NewSelection, ESelectInfo::Type)
                    {
                        if (NewSelection.IsValid())
                        {
                            PivotMode = PivotModeOptions.IndexOfByPredicate([&](const TSharedPtr<FString>& Item) { return Item.Get() == NewSelection.Get(); });
                            if (bHasActivePreview) { PreviewProcgenArcanaCave(); }
                        }
                    })
                    [
                        SNew(STextBlock).Text_Lambda([this]() { return PivotModeOptions.IsValidIndex(PivotMode) ? FText::FromString(*PivotModeOptions[PivotMode]) : FText::FromString("Spline Center"); })
                    ]
                ]
            ]
            
            // --- Manual Position Offset / World Scale ---
            + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
            [
                SNew(SVerticalBox)
                .Visibility_Lambda([this]() { return bHasActivePreview ? EVisibility::Visible : EVisibility::Collapsed; })
                
                // Toggle World Scale
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(0.6f)
                    [
                        SNew(STextBlock).Text(FText::FromString(TEXT("Preview Position:"))).Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
                    ]
                    + SHorizontalBox::Slot().FillWidth(0.4f)
                    [
                        SNew(SCheckBox)
                        .IsChecked_Lambda([this]() { return bUseWorldScalePositioning ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                        .OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
                        {
                            bUseWorldScalePositioning = (NewState == ECheckBoxState::Checked);
                            bUseWorldScalePositioning ? PlaceWorldScaleIndicator() : ClearWorldScaleIndicator();
                        })
                        [
                            SNew(STextBlock).Text(FText::FromString(TEXT("World Scale"))).Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
                        ]
                    ]
                ]

                // World Scale Controls
                + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
                [
                    SNew(SVerticalBox)
                    .Visibility_Lambda([this]() { return bUseWorldScalePositioning ? EVisibility::Visible : EVisibility::Collapsed; })
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 1)
                    [
                        SNew(STextBlock).Text(FText::FromString(TEXT("Ctrl+Click in viewport to place. Indicator shows placement location.")))
                        .Font(FCoreStyle::GetDefaultFontStyle("Italic", 7)).ColorAndOpacity(FSlateColor(FLinearColor::Gray))
                    ]
                    // Snap Settings
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().FillWidth(0.3f)
                        [
                            SNew(SCheckBox)
                            .IsChecked_Lambda([this]() { return bSnapToGrid ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                            .OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { bSnapToGrid = (NewState == ECheckBoxState::Checked); })
                            [
                                SNew(STextBlock).Text(FText::FromString(TEXT("Snap"))).Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
                            ]
                        ]
                        + SHorizontalBox::Slot().FillWidth(0.7f)
                        [
                            SNew(SHorizontalBox).IsEnabled_Lambda([this]() { return bSnapToGrid; })
                            + SHorizontalBox::Slot().FillWidth(0.7f)
                            [
                                SNew(SSlider)
                                .Value_Lambda([this]() { return FMath::Clamp((SnapIncrement - 10.0f) / 990.0f, 0.0f, 1.0f); })
                                .OnValueChanged_Lambda([this](float V) { SnapIncrement = 10.0f + (V * 990.0f); })
                            ]
                            + SHorizontalBox::Slot().FillWidth(0.3f)
                            [
                                SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%.0fu"), SnapIncrement)); })
                            ]
                        ]
                    ]
                ]

                // Local Offset Sliders
                + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
                [
                    SNew(SVerticalBox)
                    .Visibility_Lambda([this]() { return !bUseWorldScalePositioning ? EVisibility::Visible : EVisibility::Collapsed; })
                    // X
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 1)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().FillWidth(0.2f)[ SNew(STextBlock).Text(FText::FromString(TEXT("X:"))) ]
                        + SHorizontalBox::Slot().FillWidth(0.6f)
                        [
                            SNew(SSlider)
                            .Value_Lambda([this]() { return (PreviewPositionOffset.X + 1000.0f) / 2000.0f; })
                            .OnValueChanged_Lambda([this](float V) { PreviewPositionOffset.X = (V * 2000.0f) - 1000.0f; if(bHasActivePreview) UpdatePreviewPosition(); })
                        ]
                        + SHorizontalBox::Slot().FillWidth(0.2f)[ SNew(STextBlock).Text_Lambda([this](){ return FText::AsNumber((int)PreviewPositionOffset.X); }) ]
                    ]
                    // Y
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 1)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().FillWidth(0.2f)[ SNew(STextBlock).Text(FText::FromString(TEXT("Y:"))) ]
                        + SHorizontalBox::Slot().FillWidth(0.6f)
                        [
                            SNew(SSlider)
                            .Value_Lambda([this]() { return (PreviewPositionOffset.Y + 1000.0f) / 2000.0f; })
                            .OnValueChanged_Lambda([this](float V) { PreviewPositionOffset.Y = (V * 2000.0f) - 1000.0f; if(bHasActivePreview) UpdatePreviewPosition(); })
                        ]
                        + SHorizontalBox::Slot().FillWidth(0.2f)[ SNew(STextBlock).Text_Lambda([this](){ return FText::AsNumber((int)PreviewPositionOffset.Y); }) ]
                    ]
                    // Z
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 1)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().FillWidth(0.2f)[ SNew(STextBlock).Text(FText::FromString(TEXT("Z:"))) ]
                        + SHorizontalBox::Slot().FillWidth(0.6f)
                        [
                            SNew(SSlider)
                            .Value_Lambda([this]() { return (PreviewPositionOffset.Z + 500.0f) / 1000.0f; })
                            .OnValueChanged_Lambda([this](float V) { PreviewPositionOffset.Z = (V * 1000.0f) - 500.0f; if(bHasActivePreview) UpdatePreviewPosition(); })
                        ]
                        + SHorizontalBox::Slot().FillWidth(0.2f)[ SNew(STextBlock).Text_Lambda([this](){ return FText::AsNumber((int)PreviewPositionOffset.Z); }) ]
                    ]
                ]

                // Reset Button
                + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
                [
                    SNew(SButton).Text(FText::FromString(TEXT("Reset Position"))).OnClicked_Lambda([this]()
                    {
                        PreviewPositionOffset = FVector::ZeroVector;
                        WorldScalePosition = FVector::ZeroVector;
                        if (bHasActivePreview) {
                            if (bUseWorldScalePositioning) PlaceWorldScaleIndicator();
                            PreviewProcgenArcanaCave();
                        }
                        return FReply::Handled();
                    })
                ]
            ]
        ]

        // --- Cave Scale ---
        + SVerticalBox::Slot().AutoHeight().Padding(2)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(0.4f)[ SNew(STextBlock).Text(FText::FromString(TEXT("Cave Scale:"))) ]
            + SHorizontalBox::Slot().FillWidth(0.6f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.0f)
                [
                    SNew(SSlider)
                    .Value_Lambda([this]() { return CaveScale / 500.0f; })
                    .OnValueChanged_Lambda([this](float V) { CaveScale = V * 500.0f; })
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)
                [
                    SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%.0f"), CaveScale)); }).MinDesiredWidth(30)
                ]
            ]
        ]

        // --- Simplification ---
        + SVerticalBox::Slot().AutoHeight().Padding(2)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(0.4f)[ SNew(STextBlock).Text(FText::FromString(TEXT("Simplification:"))) ]
            + SHorizontalBox::Slot().FillWidth(0.6f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.0f)
                [
                    SNew(SSlider)
                    .Value_Lambda([this]() { return SimplificationLevel; })
                    .OnValueChanged_Lambda([this](float V) { SimplificationLevel = V; })
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)
                [
                    SNew(STextBlock).Text_Lambda([this]() { 
                        return FText::FromString(SimplificationLevel < 0.25f ? TEXT("High Detail") : SimplificationLevel < 0.5f ? TEXT("Medium") : TEXT("Simplified")); 
                    }).MinDesiredWidth(60)
                ]
            ]
        ]

        // --- Max Points ---
        + SVerticalBox::Slot().AutoHeight().Padding(2)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(0.4f)[ SNew(STextBlock).Text(FText::FromString(TEXT("Max Points:"))) ]
            + SHorizontalBox::Slot().FillWidth(0.6f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.0f)
                [
                    SNew(SSlider)
                    .Value_Lambda([this]() { return (MaxSplinePoints - 50) / 450.0f; })
                    .OnValueChanged_Lambda([this](float V) { MaxSplinePoints = 50 + (V * 450.0f); })
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)
                [
                    SNew(STextBlock).Text_Lambda([this]() { return FText::AsNumber(MaxSplinePoints); }).MinDesiredWidth(30)
                ]
            ]
        ]

        // --- Height Settings ---
        + SVerticalBox::Slot().AutoHeight().Padding(2, 8, 2, 2)
        [
            SNew(STextBlock).Text(FText::FromString(TEXT("Height Settings"))).Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(2)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(0.4f)[ SNew(STextBlock).Text(FText::FromString(TEXT("Height Mode:"))) ]
            + SHorizontalBox::Slot().FillWidth(0.6f)
            [
                SNew(SComboBox<TSharedPtr<FString>>)
                .OptionsSource(&HeightModeOptions)
                .OnGenerateWidget_Lambda([](TSharedPtr<FString> InOption) { return SNew(STextBlock).Text(FText::FromString(*InOption)); })
                .OnSelectionChanged_Lambda([this](TSharedPtr<FString> NewSelection, ESelectInfo::Type)
                {
                    if (NewSelection.IsValid())
                    {
                        for(int32 i=0; i<HeightModeOptions.Num(); ++i) {
                            if(HeightModeOptions[i] == NewSelection) { HeightMode = i; break; }
                        }
                    }
                })
                [
                    SNew(STextBlock).Text_Lambda([this](){ return HeightModeOptions.IsValidIndex(HeightMode) ? FText::FromString(*HeightModeOptions[HeightMode]) : FText::FromString("Descending Cave"); })
                ]
            ]
        ]

        // --- Height Variation ---
        + SVerticalBox::Slot().AutoHeight().Padding(2)
        [
            SNew(SHorizontalBox)
            .Visibility_Lambda([this](){ return HeightMode != 0 ? EVisibility::Visible : EVisibility::Collapsed; })
            + SHorizontalBox::Slot().FillWidth(0.4f)[ SNew(STextBlock).Text(FText::FromString(TEXT("Height Variation:"))) ]
            + SHorizontalBox::Slot().FillWidth(0.6f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.0f)
                [
                    SNew(SSlider)
                    .Value_Lambda([this]() { return HeightVariation / 200.0f; })
                    .OnValueChanged_Lambda([this](float V) { HeightVariation = V * 200.0f; })
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)
                [
                    SNew(STextBlock).Text_Lambda([this]() { return FText::AsNumber((int)HeightVariation); }).MinDesiredWidth(30)
                ]
            ]
        ]

        // --- Descent Rate ---
        + SVerticalBox::Slot().AutoHeight().Padding(2)
        [
            SNew(SHorizontalBox)
            .Visibility_Lambda([this](){ return HeightMode == 1 ? EVisibility::Visible : EVisibility::Collapsed; }) // Descending only
            + SHorizontalBox::Slot().FillWidth(0.4f)[ SNew(STextBlock).Text(FText::FromString(TEXT("Descent Rate:"))) ]
            + SHorizontalBox::Slot().FillWidth(0.6f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.0f)
                [
                    SNew(SSlider)
                    .Value_Lambda([this]() { return DescentRate; })
                    .OnValueChanged_Lambda([this](float V) { DescentRate = V; })
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)
                [
                    SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(DescentRate < 0.2f ? TEXT("Gentle") : DescentRate < 0.6f ? TEXT("Moderate") : TEXT("Steep")); }).MinDesiredWidth(50)
                ]
            ]
        ]

        // --- Entrance Detection ---
        + SVerticalBox::Slot().AutoHeight().Padding(2, 8, 2, 2)
        [
            SNew(STextBlock).Text(FText::FromString(TEXT("Entrance Detection"))).Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(2)
        [
            SNew(SCheckBox)
            .IsChecked_Lambda([this]() { return bAutoDetectEntrance ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { bAutoDetectEntrance = (NewState == ECheckBoxState::Checked); })
            [
                SNew(STextBlock).Text(FText::FromString(TEXT("Auto-detect entrance from SVG")))
            ]
        ]

        // --- Action Buttons ---
        + SVerticalBox::Slot().AutoHeight().Padding(2, 8, 2, 2)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
            [
                SNew(SHorizontalBox)
                // Preview Button
                + SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 2, 0)
                [
                    SNew(SButton)
                    .IsEnabled_Lambda([this]() { return !SelectedSVGFilePath.IsEmpty(); })
                    .OnClicked(this, &SDiggerCaveImporterWidget::OnPreviewClicked)
                    [
                        SNew(STextBlock).Text(FText::FromString(TEXT("Preview"))).Justification(ETextJustify::Center)
                    ]
                ]
                // Clear Button
                + SHorizontalBox::Slot().FillWidth(1.0f).Padding(2, 0, 0, 0)
                [
                    SNew(SButton)
                    .IsEnabled_Lambda([this]() { return bHasActivePreview; })
                    .OnClicked(this, &SDiggerCaveImporterWidget::OnClearPreviewClicked)
                    [
                        SNew(STextBlock).Text(FText::FromString(TEXT("Clear"))).Justification(ETextJustify::Center)
                    ]
                ]
            ]
            // Import Button
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SButton)
                .IsEnabled_Lambda([this]() { return !SelectedSVGFilePath.IsEmpty(); })
                .ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
                .OnClicked(this, &SDiggerCaveImporterWidget::OnImportClicked)
                [
                    SNew(STextBlock).Text(FText::FromString(TEXT("Import Cave"))).Justification(ETextJustify::Center)
                ]
            ]
        ]

        // --- Status Text ---
        + SVerticalBox::Slot().AutoHeight().Padding(2)
        [
            SNew(STextBlock)
            .Text_Lambda([this]() {
                if (SelectedSVGFilePath.IsEmpty()) return FText::FromString(TEXT("Select an SVG file to begin..."));
                FString FileName = FPaths::GetCleanFilename(SelectedSVGFilePath);
                return FText::FromString(FString::Printf(TEXT("Ready to import: %s"), *FileName));
            })
            .ColorAndOpacity_Lambda([this]() {
                return SelectedSVGFilePath.IsEmpty() ? FSlateColor(FLinearColor::Gray) : FSlateColor(FLinearColor::Green);
            })
            .Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
        ]
    ];
}

FReply SDiggerCaveImporterWidget::OnBrowseFile()
{
    TArray<FString> OutFileNames;
    bool bFileSelected = FDesktopPlatformModule::Get()->OpenFileDialog(
        FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
        TEXT("Select ProcgenArcana Cave SVG File"), TEXT(""), TEXT(""),
        TEXT("SVG Files (*.svg)|*.svg"), EFileDialogFlags::None, OutFileNames
    );

    if (bFileSelected && OutFileNames.Num() > 0)
    {
        SelectedSVGFilePath = OutFileNames[0];
    }
    return FReply::Handled();
}

FReply SDiggerCaveImporterWidget::OnPreviewClicked()
{
    PreviewProcgenArcanaCave();
    return FReply::Handled();
}

FReply SDiggerCaveImporterWidget::OnClearPreviewClicked()
{
    ClearCavePreview();
    return FReply::Handled();
}

FReply SDiggerCaveImporterWidget::OnImportClicked()
{
    ImportProcgenArcanaCave();
    return FReply::Handled();
}

void SDiggerCaveImporterWidget::PreviewProcgenArcanaCave()
{
    if (!CaveImporter) return;
    if (SelectedSVGFilePath.IsEmpty()) return;

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World) return;

    // Clear existing
    ClearCavePreview();

    FProcgenArcanaImportSettings PreviewSettings;
    PreviewSettings.SVGFilePath = SelectedSVGFilePath;
    PreviewSettings.CaveScale = CaveScale;
    PreviewSettings.SimplificationLevel = SimplificationLevel;
    PreviewSettings.MaxSplinePoints = MaxSplinePoints;
    PreviewSettings.HeightMode = static_cast<EHeightMode>(HeightMode);
    PreviewSettings.HeightVariation = HeightVariation;
    PreviewSettings.DescentRate = DescentRate;
    PreviewSettings.bAutoDetectEntrance = bAutoDetectEntrance;
    PreviewSettings.ManualEntrancePoint = ManualEntrancePoint;
    PreviewSettings.bPreviewMode = true;

    TArray<FVector> PreviewPoints;
    TArray<FVector> EntrancePoints;
    TArray<FVector> ExitPoints;

    try
    {
        if (!FPaths::FileExists(PreviewSettings.SVGFilePath)) return;
        PreviewPoints = CaveImporter->PreviewCaveFromSVG(PreviewSettings);
    }
    catch (...) { return; }

    if (PreviewPoints.Num() > 0)
    {
        EntrancePoints.Add(PreviewPoints[0]);
        if (PreviewPoints.Num() > 1) ExitPoints.Add(PreviewPoints.Last());

        FVector PivotOffset = CalculatePivotOffset(PreviewPoints, EntrancePoints, ExitPoints);

        for (FVector& P : PreviewPoints) P -= PivotOffset;
        for (FVector& P : EntrancePoints) P -= PivotOffset;
        for (FVector& P : ExitPoints) P -= PivotOffset;

        StoredPreviewPoints = PreviewPoints;
        StoredEntrancePoints = EntrancePoints;
        StoredExitPoints = ExitPoints;
        bHasActivePreview = true;

        // Draw Debug
        for (int32 i = 0; i < PreviewPoints.Num() - 1; i++)
        {
            DrawDebugLine(World, PreviewPoints[i], PreviewPoints[i + 1], FColor::Green, true, 60.0f, 0, 8.0f);
        }
        for (const FVector& Ent : EntrancePoints)
        {
            DrawDebugSphere(World, Ent, 75.0f, 12, FColor::Red, true, 60.0f, 0, 5.0f);
        }
        for (const FVector& Exit : ExitPoints)
        {
            DrawDebugSphere(World, Exit, 75.0f, 12, FColor::Blue, true, 60.0f, 0, 5.0f);
        }
        DrawDebugSphere(World, -PivotOffset, 50.0f, 12, FColor::Magenta, true, 60.0f, 0, 3.0f); // Origin

        if (GEditor) GEditor->RedrawLevelEditingViewports();
    }
}

void SDiggerCaveImporterWidget::ImportProcgenArcanaCave()
{
    if (OutputFormat == 1)
    {
        ImportMultiSplineCave();
        return;
    }

    if (!CaveImporter || SelectedSVGFilePath.IsEmpty()) return;

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World) return;

    FProcgenArcanaImportSettings ImportSettings;
    ImportSettings.SVGFilePath = SelectedSVGFilePath;
    ImportSettings.CaveScale = CaveScale;
    ImportSettings.SimplificationLevel = SimplificationLevel;
    ImportSettings.MaxSplinePoints = MaxSplinePoints;
    ImportSettings.HeightMode = static_cast<EHeightMode>(HeightMode);
    ImportSettings.HeightVariation = HeightVariation;
    ImportSettings.DescentRate = DescentRate;
    ImportSettings.bAutoDetectEntrance = bAutoDetectEntrance;
    ImportSettings.ManualEntrancePoint = ManualEntrancePoint;
    ImportSettings.bPreviewMode = false;

    // Use cached preview data if available for pivot calculation
    TArray<FVector> OriginalPoints = StoredPreviewPoints;
    TArray<FVector> EntrancePoints = StoredEntrancePoints;
    TArray<FVector> ExitPoints = StoredExitPoints;

    if (OriginalPoints.Num() == 0)
    {
        try { OriginalPoints = CaveImporter->PreviewCaveFromSVG(ImportSettings); }
        catch (...) { return; }
        if (OriginalPoints.Num() > 0) {
            EntrancePoints.Add(OriginalPoints[0]);
            ExitPoints.Add(OriginalPoints.Last());
        }
    }

    USplineComponent* SplineComp = nullptr;
    try { SplineComp = CaveImporter->ImportCaveFromSVG(ImportSettings, World, nullptr); }
    catch (...) { return; }

    if (SplineComp)
    {
        AActor* NewActor = CreateCaveSplineActor(SplineComp, OriginalPoints, EntrancePoints, ExitPoints);
        if (NewActor)
        {
            FNotificationInfo Info(FText::FromString(TEXT("Cave imported successfully!")));
            Info.ExpireDuration = 3.0f;
            FSlateNotificationManager::Get().AddNotification(Info);
        }
    }
}

void SDiggerCaveImporterWidget::ImportMultiSplineCave()
{
    if (!CaveImporter || SelectedSVGFilePath.IsEmpty()) return;
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World) return;

    FProcgenArcanaImportSettings ImportSettings;
    ImportSettings.SVGFilePath = SelectedSVGFilePath;
    ImportSettings.CaveScale = CaveScale;
    ImportSettings.SimplificationLevel = SimplificationLevel;
    ImportSettings.MaxSplinePoints = MaxSplinePoints;
    ImportSettings.HeightMode = static_cast<EHeightMode>(HeightMode);
    ImportSettings.HeightVariation = HeightVariation;
    ImportSettings.DescentRate = DescentRate;
    ImportSettings.bAutoDetectEntrance = bAutoDetectEntrance;
    ImportSettings.ManualEntrancePoint = ManualEntrancePoint;
    ImportSettings.bPreviewMode = false;

    FMultiSplineCaveData CaveData;
    try { CaveData = CaveImporter->ImportMultiSplineCaveFromSVG(ImportSettings); }
    catch (...)
    {
        OutputFormat = 0; // Fallback
        ImportProcgenArcanaCave();
        return;
    }

    if (CaveData.Splines.Num() == 0)
    {
        OutputFormat = 0; // Fallback
        ImportProcgenArcanaCave();
        return;
    }

    // Calculate Pivot
    FVector PivotOffset = FVector::ZeroVector;
    if (bUseWorldScalePositioning)
    {
        PivotOffset = -WorldScalePosition;
    }
    else
    {
        TArray<FVector> AllPoints;
        for (const auto& S : CaveData.Splines) AllPoints.Append(S.Points);
        
        if (AllPoints.Num() > 0)
        {
            FVector BasePivot = CalculatePivotOffset(AllPoints, CaveData.EntrancePoints, CaveData.ExitPoints);
            PivotOffset = BasePivot - PreviewPositionOffset;
        }
    }

    AActor* CaveActor = nullptr;
    try
    {
        if (CaveImporter->FindFunction(TEXT("CreateMultiSplineCaveActor")))
        {
            CaveActor = CaveImporter->CreateMultiSplineCaveActor(CaveData, World, PivotOffset);
        }
        else
        {
            CaveActor = CreateManualMultiSplineActor(CaveData, World, PivotOffset);
        }
    }
    catch (...) { return; }

    if (CaveActor)
    {
        ClearCavePreview();
        if (GEditor)
        {
            GEditor->SelectNone(false, true);
            GEditor->SelectActor(CaveActor, true, true);
        }
        FNotificationInfo Info(FText::FromString(TEXT("Multi-spline cave created!")));
        Info.ExpireDuration = 3.0f;
        FSlateNotificationManager::Get().AddNotification(Info);
    }
}

void SDiggerCaveImporterWidget::ClearCavePreview()
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (World)
    {
        FlushPersistentDebugLines(World);
        FlushDebugStrings(World);
        if (GEditor) GEditor->RedrawLevelEditingViewports();
    }
    StoredPreviewPoints.Empty();
    StoredEntrancePoints.Empty();
    StoredExitPoints.Empty();
    bHasActivePreview = false;
}

FVector SDiggerCaveImporterWidget::CalculatePivotOffset(const TArray<FVector>& Points, const TArray<FVector>& Entrances, const TArray<FVector>& Exits)
{
    if (Points.Num() == 0) return FVector::ZeroVector;

    switch (PivotMode)
    {
    case 0: // Center
        {
            FVector Sum = FVector::ZeroVector;
            for (const FVector& P : Points) Sum += P;
            return Sum / Points.Num();
        }
    case 1: // First Entrance
        return (Entrances.Num() > 0) ? Entrances[0] : Points[0];
    case 2: // First Exit
        return (Exits.Num() > 0) ? Exits[0] : Points.Last();
    case 3: // Spline Start
        return Points[0];
    case 4: // Spline End
        return Points.Last();
    default:
        return FVector::ZeroVector;
    }
}

FString SDiggerCaveImporterWidget::GetPivotModeDisplayName(int32 Mode)
{
    switch (Mode)
    {
    case 0: return TEXT("Spline Center");
    case 1: return TEXT("First Entrance");
    case 2: return TEXT("First Exit");
    case 3: return TEXT("Spline Start");
    case 4: return TEXT("Spline End");
    default: return TEXT("Unknown");
    }
}

void SDiggerCaveImporterWidget::PlaceWorldScaleIndicator()
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World) return;

    ClearWorldScaleIndicator();

    if (WorldScalePosition == FVector::ZeroVector)
    {
        if (GEditor && GEditor->GetActiveViewport())
        {
            FEditorViewportClient* VC = (FEditorViewportClient*)GEditor->GetActiveViewport()->GetClient();
            if (VC) WorldScalePosition = VC->GetViewLocation();
        }
    }

    if (bSnapToGrid)
    {
        WorldScalePosition.X = FMath::RoundToFloat(WorldScalePosition.X / SnapIncrement) * SnapIncrement;
        WorldScalePosition.Y = FMath::RoundToFloat(WorldScalePosition.Y / SnapIncrement) * SnapIncrement;
        WorldScalePosition.Z = FMath::RoundToFloat(WorldScalePosition.Z / SnapIncrement) * SnapIncrement;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    SpawnParams.ObjectFlags = RF_Transient;
    
    AActor* Indicator = World->SpawnActor<AActor>(AActor::StaticClass(), WorldScalePosition, FRotator::ZeroRotator, SpawnParams);
    if (Indicator)
    {
        Indicator->SetActorLabel(TEXT("Procgen_Indicator"));
        Indicator->SetActorEnableCollision(false);
        
        // Draw debug visuals (persistent)
        DrawDebugSphere(World, WorldScalePosition, 200.0f, 16, FColor::Magenta, true, 3600.0f, 0, 10.0f);
        DrawDebugString(World, WorldScalePosition + FVector(0,0,250), TEXT("PLACEMENT INDICATOR"), nullptr, FColor::Magenta, 3600.0f);
        
        WorldScaleIndicatorActor = Indicator;
        if(bHasActivePreview) UpdatePreviewPosition();
    }
}

void SDiggerCaveImporterWidget::ClearWorldScaleIndicator()
{
    if (WorldScaleIndicatorActor.IsValid())
    {
        WorldScaleIndicatorActor->Destroy();
        WorldScaleIndicatorActor.Reset();
    }
    
    // Also clear debug lines associated with it (Global flush might be aggressive but safe for this context)
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if(World) FlushPersistentDebugLines(World);
    
    // If we cleared lines, we need to redraw preview if active
    if (bHasActivePreview)
    {
        PreviewProcgenArcanaCave();
    }
}

void SDiggerCaveImporterWidget::HandleWorldScaleClick(const FVector& ClickLocation)
{
    if (!bUseWorldScalePositioning) return;

    WorldScalePosition = ClickLocation;
    
    if (bSnapToGrid)
    {
        WorldScalePosition.X = FMath::RoundToFloat(WorldScalePosition.X / SnapIncrement) * SnapIncrement;
        WorldScalePosition.Y = FMath::RoundToFloat(WorldScalePosition.Y / SnapIncrement) * SnapIncrement;
        WorldScalePosition.Z = FMath::RoundToFloat(WorldScalePosition.Z / SnapIncrement) * SnapIncrement;
    }

    PlaceWorldScaleIndicator();
}

void SDiggerCaveImporterWidget::UpdatePreviewPosition()
{
    // Re-run preview logic which recalculates offsets based on new sliders/worldpos
    PreviewProcgenArcanaCave();
}

AActor* SDiggerCaveImporterWidget::CreateCaveSplineActor(USplineComponent* SplineComponent, const TArray<FVector>& OriginalPoints, const TArray<FVector>& EntrancePoints, const TArray<FVector>& ExitPoints)
{
    if (!SplineComponent) return nullptr;
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World) return nullptr;

    FVector TotalPivotOffset;
    if (bUseWorldScalePositioning)
    {
        TotalPivotOffset = CalculatePivotOffset(OriginalPoints, EntrancePoints, ExitPoints) - WorldScalePosition;
    }
    else
    {
        TotalPivotOffset = CalculatePivotOffset(OriginalPoints, EntrancePoints, ExitPoints) - PreviewPositionOffset;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.Name = MakeUniqueObjectName(World, AActor::StaticClass(), *FString::Printf(TEXT("CaveSpline_%s"), *FPaths::GetBaseFilename(SelectedSVGFilePath)));
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AActor* CaveActor = World->SpawnActor<AActor>(AActor::StaticClass(), SpawnParams);
    if (!CaveActor) return nullptr;

    USplineComponent* EditableSpline = NewObject<USplineComponent>(CaveActor, USplineComponent::StaticClass(), TEXT("CaveSpline"));
    EditableSpline->CreationMethod = EComponentCreationMethod::UserConstructionScript;
    EditableSpline->SetFlags(RF_Transactional | RF_DefaultSubObject);
    EditableSpline->ClearSplinePoints();

    int32 Num = SplineComponent->GetNumberOfSplinePoints();
    for (int32 i = 0; i < Num; i++)
    {
        FVector Loc = SplineComponent->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World) - TotalPivotOffset;
        FVector Tan = SplineComponent->GetTangentAtSplinePoint(i, ESplineCoordinateSpace::World);
        EditableSpline->AddSplinePoint(Loc, ESplineCoordinateSpace::Local);
        EditableSpline->SetTangentAtSplinePoint(i, Tan, ESplineCoordinateSpace::Local);
        EditableSpline->SetSplinePointType(i, SplineComponent->GetSplinePointType(i));
    }
    EditableSpline->SetClosedLoop(SplineComponent->IsClosedLoop());
    EditableSpline->UpdateSpline();

    CaveActor->SetRootComponent(EditableSpline);
    EditableSpline->RegisterComponentWithWorld(World);
    CaveActor->AddInstanceComponent(EditableSpline);
    
    CaveActor->SetActorLabel(SpawnParams.Name.ToString());
    CaveActor->Tags.Add(TEXT("ProcgenArcanaImport"));
    CaveActor->SetActorLocation(FVector::ZeroVector);

    // Set transactional
    CaveActor->SetFlags(RF_Transactional);
    EditableSpline->SetFlags(RF_Transactional);
    CaveActor->MarkPackageDirty();

    ClearCavePreview();
    
    if (GEditor)
    {
        GEditor->SelectNone(false, true);
        GEditor->SelectActor(CaveActor, true, true);
    }

    return CaveActor;
}

AActor* SDiggerCaveImporterWidget::CreateManualMultiSplineActor(const FMultiSplineCaveData& CaveData, UWorld* World, const FVector& PivotOffset)
{
    if (!World || CaveData.Splines.Num() == 0) return nullptr;

    FActorSpawnParameters SpawnParams;
    SpawnParams.Name = MakeUniqueObjectName(World, AActor::StaticClass(), *FString::Printf(TEXT("MultiSplineCave_%s"), *FPaths::GetBaseFilename(SelectedSVGFilePath)));
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AActor* CaveActor = World->SpawnActor<AActor>(AActor::StaticClass(), SpawnParams);
    if (!CaveActor) return nullptr;

    USceneComponent* Root = NewObject<USceneComponent>(CaveActor, USceneComponent::StaticClass(), TEXT("RootComponent"));
    CaveActor->SetRootComponent(Root);
    Root->RegisterComponentWithWorld(World);

    for (int32 i = 0; i < CaveData.Splines.Num(); i++)
    {
        const auto& SplineData = CaveData.Splines[i];
        if (SplineData.Points.Num() < 2) continue;

        FString Name = FString::Printf(TEXT("Spline_%d"), i);
        USplineComponent* SC = NewObject<USplineComponent>(CaveActor, USplineComponent::StaticClass(), *Name);
        SC->CreationMethod = EComponentCreationMethod::UserConstructionScript;
        SC->SetFlags(RF_Transactional);
        
        SC->ClearSplinePoints();
        for (int32 j = 0; j < SplineData.Points.Num(); j++)
        {
            FVector Loc = SplineData.Points[j] - PivotOffset;
            SC->AddSplinePoint(Loc, ESplineCoordinateSpace::Local);
            SC->SetSplinePointType(j, ESplinePointType::Curve);
        }
        SC->UpdateSpline();
        SC->AttachToComponent(Root, FAttachmentTransformRules::KeepWorldTransform);
        SC->RegisterComponentWithWorld(World);
        CaveActor->AddInstanceComponent(SC);
    }

    CaveActor->SetActorLabel(SpawnParams.Name.ToString());
    CaveActor->Tags.Add(TEXT("MultiSpline"));
    CaveActor->SetActorLocation(FVector::ZeroVector);
    CaveActor->SetFlags(RF_Transactional);
    CaveActor->MarkPackageDirty();

    return CaveActor;
}

FLinearColor SDiggerCaveImporterWidget::GetSplineColorForType(ECavePassageType Type)
{
    switch (Type)
    {
        case ECavePassageType::MainTunnel:   return FLinearColor::Green;
        case ECavePassageType::SideBranch:   return FLinearColor::Yellow;
        case ECavePassageType::Chamber:      return FColor::Cyan;
        case ECavePassageType::Connector:    return FColor::Orange;
        case ECavePassageType::Entrance:     return FLinearColor::Red;
        case ECavePassageType::Exit:         return FLinearColor::Blue;
        default:                             return FLinearColor::White;
    }
}

#undef LOCTEXT_NAMESPACE