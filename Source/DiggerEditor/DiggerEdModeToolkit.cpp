// DiggerEdModeToolkit.cpp
#include "DiggerEdModeToolkit.h"
#include "ContentBrowserModule.h"
#include "DiggerEdMode.h"
#include "DiggerManager.h"
#include "EditorModeManager.h"
#include "EditorStyleSet.h"
#include "EngineUtils.h"
#include "IContentBrowserSingleton.h"
#include "Brushes/SlateImageBrush.h"
#include "Engine/StaticMesh.h"
#include "Experimental/EditorInteractiveToolsFramework/Public/Behaviors/2DViewportBehaviorTargets.h"
#include "Experimental/EditorInteractiveToolsFramework/Public/Behaviors/2DViewportBehaviorTargets.h"
#include "Experimental/EditorInteractiveToolsFramework/Public/Behaviors/2DViewportBehaviorTargets.h"
#include "Experimental/EditorInteractiveToolsFramework/Public/Behaviors/2DViewportBehaviorTargets.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SSlider.h" // <-- Add this line!
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Text/STextBlock.h"



#define LOCTEXT_NAMESPACE "FDiggerEdModeToolkit"

void FDiggerEdModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost)
{
    AssetThumbnailPool = MakeShareable(new FAssetThumbnailPool(32, true));
    
    // Initialize the grid (typically in your Init or Construct function)
    IslandGrid = SNew(SUniformGridPanel).SlotPadding(2.0f);
    
    ToolkitWidget = SNew(SVerticalBox)

    // --- Brush Shape Section ---
    + SVerticalBox::Slot().AutoHeight().Padding(4)
[
    SNew(STextBlock).Text(FText::FromString("Brush Shape"))
]
+ SVerticalBox::Slot().AutoHeight().Padding(4)
[
    SNew(SHorizontalBox)
    // Sphere
    + SHorizontalBox::Slot().AutoWidth().Padding(2)
    [
        SNew(SCheckBox)
        .Style(FAppStyle::Get(), "RadioButton")
        .IsChecked_Lambda([this]() { return (CurrentBrushType == EVoxelBrushType::Sphere) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
        .OnCheckStateChanged_Lambda([this](ECheckBoxState State) {
            if (State == ECheckBoxState::Checked)
            {
                CurrentBrushType = EVoxelBrushType::Sphere;
                if (ADiggerManager* Manager = GetDiggerManager())
                {
                    Manager->EditorBrushType = EVoxelBrushType::Sphere;
                }
            }
        })
        [
            SNew(STextBlock).Text(FText::FromString("Sphere"))
        ]
    ]
    // Cube
    + SHorizontalBox::Slot().AutoWidth().Padding(2)
    [
        SNew(SCheckBox)
        .Style(FAppStyle::Get(), "RadioButton")
        .IsChecked_Lambda([this]() { return (CurrentBrushType == EVoxelBrushType::Cube) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
        .OnCheckStateChanged_Lambda([this](ECheckBoxState State) {
            if (State == ECheckBoxState::Checked)
            {
                CurrentBrushType = EVoxelBrushType::Cube;
                if (ADiggerManager* Manager = GetDiggerManager())
                {
                    Manager->EditorBrushType = EVoxelBrushType::Cube;
                }
            }
        })
        [
            SNew(STextBlock).Text(FText::FromString("Cube"))
        ]
    ]
    // Cylinder
    + SHorizontalBox::Slot().AutoWidth().Padding(2)
    [
        SNew(SCheckBox)
        .Style(FAppStyle::Get(), "RadioButton")
        .IsChecked_Lambda([this]() { return (CurrentBrushType == EVoxelBrushType::Cylinder) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
        .OnCheckStateChanged_Lambda([this](ECheckBoxState State) {
            if (State == ECheckBoxState::Checked)
            {
                CurrentBrushType = EVoxelBrushType::Cylinder;
                if (ADiggerManager* Manager = GetDiggerManager())
                {
                    Manager->EditorBrushType = EVoxelBrushType::Cylinder;
                }
            }
        })
        [
            SNew(STextBlock).Text(FText::FromString("Cylinder"))
        ]
    ]
    // Cone
    + SHorizontalBox::Slot().AutoWidth().Padding(2)
    [
        SNew(SCheckBox)
        .Style(FAppStyle::Get(), "RadioButton")
        .IsChecked_Lambda([this]() { return (CurrentBrushType == EVoxelBrushType::Cone) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
        .OnCheckStateChanged_Lambda([this](ECheckBoxState State) {
            if (State == ECheckBoxState::Checked)
            {
                CurrentBrushType = EVoxelBrushType::Cone;
                if (ADiggerManager* Manager = GetDiggerManager())
                {
                    Manager->EditorBrushType = EVoxelBrushType::Cone;
                }
            }
        })
        [
            SNew(STextBlock).Text(FText::FromString("Cone"))
        ]
    ]
    // Smooth
    + SHorizontalBox::Slot().AutoWidth().Padding(2)
    [
        SNew(SCheckBox)
        .Style(FAppStyle::Get(), "RadioButton")
        .IsChecked_Lambda([this]() { return (CurrentBrushType == EVoxelBrushType::Smooth) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
        .OnCheckStateChanged_Lambda([this](ECheckBoxState State) {
            if (State == ECheckBoxState::Checked)
            {
                CurrentBrushType = EVoxelBrushType::Smooth;
                if (ADiggerManager* Manager = GetDiggerManager())
                {
                    Manager->EditorBrushType = EVoxelBrushType::Smooth;
                }
            }
        })
        [
            SNew(STextBlock).Text(FText::FromString("Smooth"))
        ]
    ]
    // Custom
    + SHorizontalBox::Slot().AutoWidth().Padding(2)
    [
        SNew(SCheckBox)
        .Style(FAppStyle::Get(), "RadioButton")
        .IsChecked_Lambda([this]() { return (CurrentBrushType == EVoxelBrushType::Custom) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
        .OnCheckStateChanged_Lambda([this](ECheckBoxState State) {
            if (State == ECheckBoxState::Checked)
            {
                CurrentBrushType = EVoxelBrushType::Custom;
                if (ADiggerManager* Manager = GetDiggerManager())
                {
                    Manager->EditorBrushType = EVoxelBrushType::Custom;
                }
            }
        })
        [
            SNew(STextBlock).Text(FText::FromString("Custom"))
        ]
    ]
]
        
        // --- Height/Length UI: Only visible for Cylinder or Cone brush ---
+ SVerticalBox::Slot().AutoHeight().Padding(4)
[
    SNew(SBox)
    .Visibility_Lambda([this]() {
        EVoxelBrushType BrushType = GetCurrentBrushType();
        return (BrushType == EVoxelBrushType::Cylinder || BrushType == EVoxelBrushType::Cone) ? EVisibility::Visible : EVisibility::Collapsed;
    })
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
        [
            SNew(STextBlock)
            .Text(FText::FromString("Height"))
        ]
        + SHorizontalBox::Slot().FillWidth(1.0f).Padding(4,0)
        [
            SNew(SSlider)
            .Value_Lambda([this]() {
                float Normalized = (BrushLength - MinBrushLength) / (MaxBrushLength - MinBrushLength);
                return FMath::Clamp(Normalized, 0.0f, 1.0f);
            })
            .OnValueChanged_Lambda([this](float NewValue)
            {
                BrushLength = FMath::Clamp(MinBrushLength + NewValue * (MaxBrushLength - MinBrushLength), MinBrushLength, MaxBrushLength);
            })
        ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
        [
            SNew(SNumericEntryBox<float>)
            .Value_Lambda([this]() { return BrushLength; })
            .OnValueChanged_Lambda([this](float NewValue) {
                BrushLength = FMath::Clamp(NewValue, MinBrushLength, MaxBrushLength);
            })
            .MinValue(MinBrushLength)
            .MaxValue(MaxBrushLength)
            .MinSliderValue(MinBrushLength)
            .MaxSliderValue(MaxBrushLength)
            .AllowSpin(true)
        ]
    ]
]

    // --- Cone Angle UI: Only visible for Cone brush ---
    + SVerticalBox::Slot().AutoHeight().Padding(4)
    [
        SNew(SBox)
        .Visibility_Lambda([this]()
        {
            return (GetCurrentBrushType() == EVoxelBrushType::Cone) ? EVisibility::Visible : EVisibility::Collapsed;
        })
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(FText::FromString("Cone Angle"))
            ]
            + SHorizontalBox::Slot().FillWidth(1.0f).Padding(4, 0)
            [
                SNew(SSlider)
                .Value_Lambda([this]()
                {
                    // Clamp to avoid out-of-range
                    float Normalized = (ConeAngle - MinConeAngle) / (MaxConeAngle - MinConeAngle);
                    return FMath::Clamp(Normalized, 0.0f, 1.0f);
                })
                .OnValueChanged_Lambda([this](float NewValue)
                {
                    ConeAngle = FMath::Clamp(MinConeAngle + NewValue * (MaxConeAngle - MinConeAngle), MinConeAngle,
                                             MaxConeAngle);
                })
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [
                SNew(SNumericEntryBox<float>)
                .Value_Lambda([this]() { return ConeAngle; })
                .OnValueChanged_Lambda([this](float NewValue)
                {
                    ConeAngle = FMath::Clamp(NewValue, MinConeAngle, MaxConeAngle);
                })
                .MinValue(MinConeAngle)
                .MaxValue(MaxConeAngle)
                .MinSliderValue(MinConeAngle)
                .MaxSliderValue(MaxConeAngle)
                .AllowSpin(true)
            ]
        ]
    ]

+ SVerticalBox::Slot().AutoHeight().Padding(4)
[
    SNew(SBox)
    .Visibility_Lambda([this]() {
        return (GetCurrentBrushType() == EVoxelBrushType::Smooth) ? EVisibility::Visible : EVisibility::Collapsed;
    })
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
        [
            SNew(STextBlock)
            .Text(FText::FromString("Smooth Iterations"))
        ]
        + SHorizontalBox::Slot().FillWidth(1.0f).Padding(4,0)
        [
            SNew(SSlider)
            .Value_Lambda([this]() {
                float Normalized = (SmoothIterations - 1.0f) / 9.0f; // (max-min)
                return FMath::Clamp(Normalized, 0.0f, 1.0f);
            })
            .OnValueChanged_Lambda([this](float NewValue)
            {
                SmoothIterations = FMath::Clamp(FMath::RoundToInt(1.0f + NewValue * 9.0f), 1, 10);
            })
        ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
        [
            SNew(SNumericEntryBox<int32>)
            .Value_Lambda([this]() { return SmoothIterations; })
            .OnValueChanged_Lambda([this](int32 NewValue) {
                SmoothIterations = FMath::Clamp(NewValue, 1, 10);
            })
            .MinValue(1)
            .MaxValue(10)
            .MinSliderValue(1)
            .MaxSliderValue(10)
            .AllowSpin(true)
        ]
    ]
]



// --- Operation (Add/Subtract) Section ---
+ SVerticalBox::Slot().AutoHeight().Padding(8, 12, 8, 4)
[
    SNew(STextBlock).Text(FText::FromString("Operation"))
]
+ SVerticalBox::Slot().AutoHeight().Padding(4)
[
    SNew(SHorizontalBox)
    // Add (Build)
    + SHorizontalBox::Slot().AutoWidth().Padding(2)
    [
        SNew(SCheckBox)
        .Style(FAppStyle::Get(), "RadioButton")
        .IsChecked_Lambda([this]() { return !bBrushDig ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
        .OnCheckStateChanged_Lambda([this](ECheckBoxState State) {
            if (State == ECheckBoxState::Checked)
            {
                bBrushDig = false;
                if (ADiggerManager* Manager = GetDiggerManager())
                {
                    Manager->EditorBrushDig = false;
                }
            }
        })
        [
            SNew(STextBlock).Text(FText::FromString("Add (Build)"))
        ]
    ]
    // Subtract (Dig)
    + SHorizontalBox::Slot().AutoWidth().Padding(2)
    [
        SNew(SCheckBox)
        .Style(FAppStyle::Get(), "RadioButton")
        .IsChecked_Lambda([this]() { return bBrushDig ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
        .OnCheckStateChanged_Lambda([this](ECheckBoxState State) {
            if (State == ECheckBoxState::Checked)
            {
                bBrushDig = true;
                if (ADiggerManager* Manager = GetDiggerManager())
                {
                    Manager->EditorBrushDig = true;
                }
            }
        })
        [
            SNew(STextBlock).Text(FText::FromString("Subtract (Dig)"))
        ]
    ]
]





    // Brush Radius
    + SVerticalBox::Slot().AutoHeight().Padding(8, 16, 8, 4)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,8,0)
        [
            SNew(STextBlock).Text(FText::FromString("Radius:")).MinDesiredWidth(60)
        ]
        + SHorizontalBox::Slot().FillWidth(1.0f).Padding(0,0,8,0)
        [
            SNew(SSlider)
            .Value_Lambda([this]() { return (BrushRadius - 10.0f) / (256.0f - 10.0f); })
            .OnValueChanged_Lambda([this](float NewValue) { BrushRadius = 10.0f + NewValue * (256.0f - 10.0f); })
        ]
        + SHorizontalBox::Slot().AutoWidth()
        [
            SNew(SNumericEntryBox<float>)
            .Value_Lambda([this]() { return BrushRadius; })
            .OnValueChanged_Lambda([this](float NewValue) { BrushRadius = FMath::Clamp(NewValue, 10.0f, 256.0f); })
            .MinValue(10.0f)
            .MaxValue(256.0f)
            .AllowSpin(true)
            .MinDesiredValueWidth(50)
        ]
    ]
    // Brush Strength
    + SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 4)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,8,0)
        [
            SNew(STextBlock).Text(FText::FromString("Strength:")).MinDesiredWidth(60)
        ]
        + SHorizontalBox::Slot().FillWidth(1.0f).Padding(0,0,8,0)
        [
            SNew(SSlider)
            .Value_Lambda([this]() { return BrushStrength; })
            .OnValueChanged_Lambda([this](float NewValue) { BrushStrength = NewValue; })
        ]
        + SHorizontalBox::Slot().AutoWidth()
        [
            SNew(SNumericEntryBox<float>)
            .Value_Lambda([this]() { return BrushStrength; })
            .OnValueChanged_Lambda([this](float NewValue) { BrushStrength = FMath::Clamp(NewValue, 0.0f, 1.0f); })
            .MinValue(0.0f)
            .MaxValue(1.0f)
            .AllowSpin(true)
            .MinDesiredValueWidth(50)
        ]
    ]
    // Brush Falloff
    + SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 8)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,8,0)
        [
            SNew(STextBlock).Text(FText::FromString("Falloff:")).MinDesiredWidth(60)
        ]
        + SHorizontalBox::Slot().FillWidth(1.0f).Padding(0,0,8,0)
        [
            SNew(SSlider)
            .Value_Lambda([this]() { return BrushFalloff; })
            .OnValueChanged_Lambda([this](float NewValue) { BrushFalloff = NewValue; })
        ]
        + SHorizontalBox::Slot().AutoWidth()
        [
            SNew(SNumericEntryBox<float>)
            .Value_Lambda([this]() { return BrushFalloff; })
            .OnValueChanged_Lambda([this](float NewValue) { BrushFalloff = FMath::Clamp(NewValue, 0.0f, 1.0f); })
            .MinValue(0.0f)
            .MaxValue(1.0f)
            .AllowSpin(true)
            .MinDesiredValueWidth(50)
        ]
    ]

    // Rotate Button
    + SVerticalBox::Slot().AutoHeight().Padding(8, 8, 8, 4)
    [
        SNew(SButton)
        .Text(FText::FromString("Rotate Brush"))
        .OnClicked_Lambda([this]() { bShowRotation = !bShowRotation; return FReply::Handled(); })
    ]

    // Rotation Sliders (collapsible)
    + SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 8)
    [
        SNew(SVerticalBox)
        .Visibility_Lambda([this]() { return bShowRotation ? EVisibility::Visible : EVisibility::Collapsed; })

        // X Rotation
        + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,8,0)
            [
                SNew(STextBlock).Text(FText::FromString("X:")).MinDesiredWidth(20)
            ]
            + SHorizontalBox::Slot().FillWidth(1.0f).Padding(0,0,8,0)
            [
                SNew(SSlider)
                .Value_Lambda([this]() { return BrushRotX / 360.0f; })
                .OnValueChanged_Lambda([this](float NewValue) { BrushRotX = NewValue * 360.0f; })
            ]
            + SHorizontalBox::Slot().AutoWidth()
            [
                SNew(SNumericEntryBox<float>)
                .Value_Lambda([this]() { return BrushRotX; })
                .OnValueChanged_Lambda([this](float NewValue) { BrushRotX = FMath::Fmod(NewValue, 360.0f); })
                .MinValue(0.0f).MaxValue(360.0f)
                .AllowSpin(true)
                .MinDesiredValueWidth(50)
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(2,0)
            [ MakeAngleButton(45.f, BrushRotX, TEXT("45")) ]
            + SHorizontalBox::Slot().AutoWidth().Padding(2,0)
            [ MakeAngleButton(90.f, BrushRotX, TEXT("90")) ]
            + SHorizontalBox::Slot().AutoWidth().Padding(2,0)
            [ MakeAngleButton(180.f, BrushRotX, TEXT("180")) ]
            + SHorizontalBox::Slot().AutoWidth().Padding(2,0)
            [ MakeMirrorButton(BrushRotX, TEXT("Mirror")) ]
        ]
        // Y Rotation
        + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,8,0)
            [
                SNew(STextBlock).Text(FText::FromString("Y:")).MinDesiredWidth(20)
            ]
            + SHorizontalBox::Slot().FillWidth(1.0f).Padding(0,0,8,0)
            [
                SNew(SSlider)
                .Value_Lambda([this]() { return BrushRotY / 360.0f; })
                .OnValueChanged_Lambda([this](float NewValue) { BrushRotY = NewValue * 360.0f; })
            ]
            + SHorizontalBox::Slot().AutoWidth()
            [
                SNew(SNumericEntryBox<float>)
                .Value_Lambda([this]() { return BrushRotY; })
                .OnValueChanged_Lambda([this](float NewValue) { BrushRotY = FMath::Fmod(NewValue, 360.0f); })
                .MinValue(0.0f).MaxValue(360.0f)
                .AllowSpin(true)
                .MinDesiredValueWidth(50)
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(2,0)
            [ MakeAngleButton(45.f, BrushRotY, TEXT("45")) ]
            + SHorizontalBox::Slot().AutoWidth().Padding(2,0)
            [ MakeAngleButton(90.f, BrushRotY, TEXT("90")) ]
            + SHorizontalBox::Slot().AutoWidth().Padding(2,0)
            [ MakeAngleButton(180.f, BrushRotY, TEXT("180")) ]
            + SHorizontalBox::Slot().AutoWidth().Padding(2,0)
            [ MakeMirrorButton(BrushRotY, TEXT("Mirror")) ]
        ]
        // Z Rotation
        + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,8,0)
            [
                SNew(STextBlock).Text(FText::FromString("Z:")).MinDesiredWidth(20)
            ]
            + SHorizontalBox::Slot().FillWidth(1.0f).Padding(0,0,8,0)
            [
                SNew(SSlider)
                .Value_Lambda([this]() { return BrushRotZ / 360.0f; })
                .OnValueChanged_Lambda([this](float NewValue) { BrushRotZ = NewValue * 360.0f; })
            ]
            + SHorizontalBox::Slot().AutoWidth()
            [
                SNew(SNumericEntryBox<float>)
                .Value_Lambda([this]() { return BrushRotZ; })
                .OnValueChanged_Lambda([this](float NewValue) { BrushRotZ = FMath::Fmod(NewValue, 360.0f); })
                .MinValue(0.0f).MaxValue(360.0f)
                .AllowSpin(true)
                .MinDesiredValueWidth(50)
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(2,0)
            [ MakeAngleButton(45.f, BrushRotZ, TEXT("45")) ]
            + SHorizontalBox::Slot().AutoWidth().Padding(2,0)
            [ MakeAngleButton(90.f, BrushRotZ, TEXT("90")) ]
            + SHorizontalBox::Slot().AutoWidth().Padding(2,0)
            [ MakeAngleButton(180.f, BrushRotZ, TEXT("180")) ]
            + SHorizontalBox::Slot().AutoWidth().Padding(2,0)
            [ MakeMirrorButton(BrushRotZ, TEXT("Mirror")) ]
        ]
    ]

    // Custom Brushes Section
    + SVerticalBox::Slot().AutoHeight().Padding(8, 8, 8, 4)
    [
        SNew(STextBlock).Text(FText::FromString("Custom Brushes"))
    ]
    + SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 4)
    [
        SAssignNew(CustomBrushGrid, SUniformGridPanel)
    ]
    + SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 8)
    [
        SNew(SButton)
        .Text(FText::FromString("Add Custom Brush"))
        .OnClicked_Lambda([this]() -> FReply
        {
            FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
            FOpenAssetDialogConfig DialogConfig;
            DialogConfig.DialogTitleOverride = FText::FromString("Select Static Mesh Brush");
            DialogConfig.AssetClassNames.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("StaticMesh")));
            DialogConfig.bAllowMultipleSelection = true;
            TArray<FAssetData> SelectedAssets = ContentBrowserModule.Get().CreateModalOpenAssetDialog(DialogConfig);

            for (const FAssetData& AssetData : SelectedAssets)
            {
                TSoftObjectPtr<UStaticMesh> MeshPtr = Cast<UStaticMesh>(AssetData.GetAsset());
                if (MeshPtr.IsValid())
                {
                    CustomBrushMeshes.AddUnique(MeshPtr);
                }
            }
            RebuildCustomBrushGrid();
            return FReply::Handled();
        })
    ]

    // --- Build Settings Roll-down ---
    + SVerticalBox::Slot().AutoHeight().Padding(8, 16, 8, 4)
    [
        SNew(SButton)
        .Text(FText::FromString("Build/Export Settings"))
        .OnClicked_Lambda([this]() { bShowBuildSettings = !bShowBuildSettings; return FReply::Handled(); })
    ]
    + SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 8)
    [
        SNew(SVerticalBox)
        .Visibility_Lambda([this]() { return bShowBuildSettings ? EVisibility::Visible : EVisibility::Collapsed; })

        // Collision Checkbox
        + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
        [
            SNew(SCheckBox)
            .IsChecked_Lambda([this]() { return bEnableCollision ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bEnableCollision = (State == ECheckBoxState::Checked); })
            [
                SNew(STextBlock).Text(FText::FromString("Enable Collision"))
            ]
        ]
        // Nanite Checkbox
        + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
        [
            SNew(SCheckBox)
            .IsChecked_Lambda([this]() { return bEnableNanite ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bEnableNanite = (State == ECheckBoxState::Checked); })
            [
                SNew(STextBlock).Text(FText::FromString("Enable Nanite"))
            ]
        ]
        // Detail Reduction Slider (stub)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [
                SNew(STextBlock).Text(FText::FromString("Detail Reduction:"))
            ]
            + SHorizontalBox::Slot().FillWidth(1.0f).Padding(4, 0)
            [
                SNew(SSlider)
                .Value_Lambda([this]() { return BakeDetail; })
                .OnValueChanged_Lambda([this](float NewValue) { BakeDetail = NewValue; })
            ]
            + SHorizontalBox::Slot().AutoWidth()
            [
                SNew(SNumericEntryBox<float>)
                .Value_Lambda([this]() { return BakeDetail; })
                .OnValueChanged_Lambda([this](float NewValue) { BakeDetail = FMath::Clamp(NewValue, 0.0f, 1.0f); })
                .MinValue(0.0f)
                .MaxValue(1.0f)
                .AllowSpin(true)
                .MinDesiredValueWidth(50)
            ]
        ]
        // Bake Button
        + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
        [
            SNew(SButton)
            .Text(FText::FromString("Bake to Static Mesh"))
            .OnClicked_Lambda([this]()
            {
                // Find the DiggerManager in the worl
                if (ADiggerManager* Manager = GetDiggerManager())
                {
                    Manager->BakeToStaticMesh(bEnableCollision, bEnableNanite, BakeDetail);
                }
                return FReply::Handled();
            })

        ]
    ]
        
    //Islands Rollout Menu
    + SVerticalBox::Slot().AutoHeight().Padding(8, 12, 8, 4)
    [
        SNew(SExpandableArea)
                             .AreaTitle(FText::FromString("Islands"))
                             .InitiallyCollapsed(true) // Collapsed by default
                             .BodyContent()
        [
            SNew(SVerticalBox)
            // --- Island Operations Section ---
            + SVerticalBox::Slot().AutoHeight().Padding(4)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(2)
                [
                    SNew(SButton)
                    .Text(FText::FromString("Convert To Physics Object"))
                    .IsEnabled_Lambda([this]() { return SelectedIslandIndex != INDEX_NONE; })
                    .OnClicked_Lambda([this]()
                    {
                        /* Convert logic here */
                        return FReply::Handled();
                    })
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(2)
                [
                    SNew(SButton)
                    .Text(FText::FromString("Remove"))
                    .IsEnabled_Lambda([this]() { return SelectedIslandIndex != INDEX_NONE; })
                    .OnClicked_Lambda([this]()
                    {
                        /* Remove logic here */
                        return FReply::Handled();
                    })
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(2)
                [
                    SNew(SButton)
                    .Text(FText::FromString("Duplicate"))
                    .IsEnabled_Lambda([this]() { return SelectedIslandIndex != INDEX_NONE; })
                    .OnClicked_Lambda([this]()
                    {
                        /* Duplicate logic here */
                        return FReply::Handled();
                    })
                ]
            ]
            // --- Change Rotation Section (vertical) ---
            + SVerticalBox::Slot().AutoHeight().Padding(4)
            [
                SNew(SExpandableArea)
                .AreaTitle(FText::FromString("Change Rotation"))
                .InitiallyCollapsed(true)
                .BodyContent()
                [
                    SNew(SVerticalBox)

                    // Pitch (X)
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
                        [
                            SNew(STextBlock).Text(FText::FromString("Pitch:")).MinDesiredWidth(20)
                        ]
                        + SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 8, 0)
                        [
                            SNew(SSlider)
                            .Value_Lambda([this]() { return IslandRotation.Pitch / 360.0f; })
                            .OnValueChanged_Lambda([this](float NewValue)
                            {
                                IslandRotation.Pitch = NewValue * 360.0f;
                                UE_LOG(LogTemp, Warning, TEXT("IslandRotation.Pitch changed: %f"),
                                       IslandRotation.Pitch);
                            })
                        ]
                        + SHorizontalBox::Slot().AutoWidth()
                        [
                            SNew(SNumericEntryBox<float>)
                            .Value_Lambda([this]() { return IslandRotation.Pitch; })
                            .OnValueChanged_Lambda([this](float NewValue)
                            {
                                IslandRotation.Pitch = FMath::Fmod(NewValue, 360.0f);
                                UE_LOG(LogTemp, Warning, TEXT("IslandRotation.Pitch changed: %f"),
                                       IslandRotation.Pitch);
                            })
                            .MinValue(0.0f)
                            .MaxValue(360.0f)
                            .AllowSpin(true)
                            .MinDesiredValueWidth(50)
                        ]
                        + SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
                        [
                            MakeAngleButton(45.f, IslandRotation.Pitch, TEXT("45"))
                        ]
                        + SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
                        [
                            MakeAngleButton(90.f, IslandRotation.Pitch, TEXT("90"))
                        ]
                        + SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
                        [
                            MakeAngleButton(180.f, IslandRotation.Pitch, TEXT("180"))
                        ]
                        + SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
                        [
                            MakeMirrorButton(IslandRotation.Pitch, TEXT("Mirror"))
                        ]
                    ]
                    // Yaw (Y)
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
                        [
                            SNew(STextBlock).Text(FText::FromString("Yaw:")).MinDesiredWidth(20)
                        ]
                        + SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 8, 0)
                        [
                            SNew(SSlider)
                            .Value_Lambda([this]() { return IslandRotation.Yaw / 360.0f; })
                            .OnValueChanged_Lambda([this](float NewValue)
                            {
                                IslandRotation.Yaw = NewValue * 360.0f;
                                UE_LOG(LogTemp, Warning, TEXT("IslandRotation.Yaw changed: %f"), IslandRotation.Yaw);
                            })
                        ]
                        + SHorizontalBox::Slot().AutoWidth()
                        [
                            SNew(SNumericEntryBox<float>)
                            .Value_Lambda([this]() { return IslandRotation.Yaw; })
                            .OnValueChanged_Lambda([this](float NewValue)
                            {
                                IslandRotation.Yaw = FMath::Fmod(NewValue, 360.0f);
                                UE_LOG(LogTemp, Warning, TEXT("IslandRotation.Yaw changed: %f"), IslandRotation.Yaw);
                            })
                            .MinValue(0.0f)
                            .MaxValue(360.0f)
                            .AllowSpin(true)
                            .MinDesiredValueWidth(50)
                        ]
                        + SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
                        [
                            MakeAngleButton(45.0, IslandRotation.Yaw, TEXT("45"))
                        ]
                        + SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
                        [
                            MakeAngleButton(90.0, IslandRotation.Yaw, TEXT("90"))
                        ]
                        + SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
                        [
                            MakeAngleButton(180.0, IslandRotation.Yaw, TEXT("180"))
                        ]
                        + SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
                        [
                            MakeMirrorButton(IslandRotation.Yaw, TEXT("Mirror"))
                        ]
                    ]
                    // Roll (Z)
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
                        [
                            SNew(STextBlock).Text(FText::FromString("Roll:")).MinDesiredWidth(20)
                        ]
                        + SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 8, 0)
                        [
                            SNew(SSlider)
                            .Value_Lambda([this]() { return IslandRotation.Roll / 360.0f; })
                            .OnValueChanged_Lambda([this](float NewValue)
                            {
                                IslandRotation.Roll = NewValue * 360.0f;
                                UE_LOG(LogTemp, Warning, TEXT("IslandRotation.Roll changed: %f"), IslandRotation.Roll);
                            })
                        ]
                        + SHorizontalBox::Slot().AutoWidth()
                        [
                            SNew(SNumericEntryBox<float>)
                            .Value_Lambda([this]() { return IslandRotation.Roll; })
                            .OnValueChanged_Lambda([this](float NewValue)
                            {
                                IslandRotation.Roll = FMath::Fmod(NewValue, 360.0f);
                                UE_LOG(LogTemp, Warning, TEXT("IslandRotation.Roll changed: %f"), IslandRotation.Roll);
                            })
                            .MinValue(0.0f)
                            .MaxValue(360.0f)
                            .AllowSpin(true)
                            .MinDesiredValueWidth(50)
                        ]
                        + SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
                        [
                            MakeAngleButton(45.f, IslandRotation.Roll, TEXT("45"))
                        ]
                        + SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
                        [
                            MakeAngleButton(90.f, IslandRotation.Roll, TEXT("90"))
                        ]
                        + SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
                        [
                            MakeAngleButton(180.f, IslandRotation.Roll, TEXT("180"))
                        ]
                        + SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
                        [
                            MakeMirrorButton(IslandRotation.Roll, TEXT("Mirror"))
                        ]
                    ]
                    // Apply Button
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 8)
                    [
                        SNew(SButton)
                        .Text(FText::FromString("Apply"))
                        .OnClicked_Lambda([this]()
                        {
                            UE_LOG(LogTemp, Warning, TEXT("Island Rotation Applied: Pitch=%f, Yaw=%f, Roll=%f"),
                                   IslandRotation.Pitch, IslandRotation.Yaw, IslandRotation.Roll);
                            // Your apply logic here
                            return FReply::Handled();
                        })
                    ]



                    // --- Island Grid Section ---
                    + SVerticalBox::Slot().AutoHeight().Padding(4)
                    [
                        SAssignNew(IslandGridContainer, SBox)
                        [
                            MakeIslandGridWidget()
                        ]
                    ]
                    ]
                ]
            ]
        ];




    

    if (CustomBrushGrid.IsValid())
    {
        RebuildCustomBrushGrid();
    }

    FModeToolkit::Init(InitToolkitHost);
}


//Helper Methods



//AGetDiggerManager
ADiggerManager* FDiggerEdModeToolkit::GetDiggerManager()
{
    if (GEditor)
    {
        UWorld* World = GEditor->GetEditorWorldContext().World();
        for (TActorIterator<ADiggerManager> It(World); It; ++It)
        {
            return *It;                 // <-- Return the manager pointer
        }
    }
    return nullptr;
}

//Make Island Widget <Here>

// DiggerEdModeToolkit.cpp

void FDiggerEdModeToolkit::RebuildIslandGrid()
{
    UE_LOG(LogTemp, Warning, TEXT("RebuildIslandGrid called!"));
    if (IslandGridContainer.IsValid())
    {
        IslandGridContainer->SetContent(MakeIslandGridWidget());
    }
}



// In DiggerEdModeToolkit.cpp
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeIslandGridWidget()
{
    // Example implementation:
    const int32 NumColumns = 4;
    TSharedRef<SUniformGridPanel> IslandGridPanel = SNew(SUniformGridPanel).SlotPadding(2.0f);

    for (int32 i = 0; i < Islands.Num(); ++i)
    {
        IslandGridPanel->AddSlot(i % NumColumns, i / NumColumns)
        [
            SNew(SButton)
            .ButtonColorAndOpacity_Lambda([this, i]() { return (SelectedIslandIndex == i) ? FLinearColor::Yellow : FLinearColor::White; })
            .OnClicked_Lambda([this, i]() { SelectedIslandIndex = i; return FReply::Handled(); })
            [
                SNew(STextBlock)
                .Text(FText::Format(FText::FromString("Island {0}"), FText::AsNumber(i+1)))
            ]
        ];
    }

    return IslandGridPanel;
}



TSharedRef<SWidget> FDiggerEdModeToolkit::MakeAngleButton(float Angle, float& Target, const FString& Label)
{
    return SNew(SButton)
        .Text(FText::FromString(Label))
        .OnClicked_Lambda([&Target, Angle]() {
            Target = Angle;
            return FReply::Handled();
        })
        .ToolTipText(FText::FromString(FString::Printf(TEXT("Set to %s"), *Label)))
        .ContentPadding(FMargin(2,0));
}

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeAngleButton(double Angle, double& Target, const FString& Label)
{
    return SNew(SButton)
        .Text(FText::FromString(Label))
        .OnClicked_Lambda([&Target, Angle]() {
            Target = Angle;
            return FReply::Handled();
        })
        .ToolTipText(FText::FromString(FString::Printf(TEXT("Set to %s"), *Label)))
        .ContentPadding(FMargin(2,0));
}

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeMirrorButton(float& Target, const FString& Label)
{
    return SNew(SButton)
        .Text(FText::FromString(Label))
        .OnClicked_Lambda([&Target]() {
            Target = FMath::Fmod(Target + 180.0f, 360.0f);
            if (Target < 0) Target += 360.0f;
            return FReply::Handled();
        })
        .ToolTipText(FText::FromString("Mirror (add 180°)"))
        .ContentPadding(FMargin(2,0));
}

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeMirrorButton(double& Target, const FString& Label)
{
    return SNew(SButton)
        .Text(FText::FromString(Label))
        .OnClicked_Lambda([&Target]() {
            Target = FMath::Fmod(Target + 180.0f, 360.0f);
            if (Target < 0) Target += 360.0f;
            return FReply::Handled();
        })
        .ToolTipText(FText::FromString("Mirror (add 180°)"))
        .ContentPadding(FMargin(2,0));
}


void FDiggerEdModeToolkit::RebuildCustomBrushGrid()
{
    if (!CustomBrushGrid.IsValid()) return;
    CustomBrushGrid->ClearChildren();
    int32 Cols = 4;
    for (int32 i = 0; i < CustomBrushMeshes.Num(); ++i)
    {
        TSoftObjectPtr<UStaticMesh> MeshPtr = CustomBrushMeshes[i];
        TSharedPtr<SWidget> ThumbWidget;

        if (MeshPtr.IsValid())
        {
            FAssetData AssetData(MeshPtr.Get());
            TSharedPtr<FAssetThumbnail> AssetThumbnail = MakeShareable(new FAssetThumbnail(AssetData, 64, 64, AssetThumbnailPool));
            ThumbWidget = SNew(SBox)
                .WidthOverride(64)
                .HeightOverride(64)
                [
                    AssetThumbnail->MakeThumbnailWidget()
                ];
        }
        else
        {
            ThumbWidget = SNew(SBox)
                .WidthOverride(64)
                .HeightOverride(64)
                [
                    SNew(STextBlock).Text(FText::FromString(TEXT("?")))
                ];
        }

        CustomBrushGrid->AddSlot(i % Cols, i / Cols)
        [
            SNew(SButton)
            .ButtonStyle(FAppStyle::Get(), "HoverHintOnly")
            .OnClicked_Lambda([this, i]() {
                SelectedBrushIndex = i;
                UE_LOG(LogTemp, Log, TEXT("Custom brush %d selected"), i);
                return FReply::Handled();
            })
            [
                ThumbWidget.ToSharedRef()
            ]
        ];
    }
}




FName FDiggerEdModeToolkit::GetToolkitFName() const { return FName("DiggerEdMode"); }
FText FDiggerEdModeToolkit::GetBaseToolkitName() const { return LOCTEXT("ToolkitName", "Digger Toolkit"); }
FEdMode* FDiggerEdModeToolkit::GetEditorMode() const { return GLevelEditorModeTools().GetActiveMode(FDiggerEdMode::EM_DiggerEdModeId); }
TSharedPtr<SWidget> FDiggerEdModeToolkit::GetInlineContent() const { return ToolkitWidget; }

#undef LOCTEXT_NAMESPACE
