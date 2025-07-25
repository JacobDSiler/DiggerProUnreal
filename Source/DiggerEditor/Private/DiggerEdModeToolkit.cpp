// DiggerEdModeToolkit.cpp

#include "DiggerEdModeToolkit.h"
#include "DiggerEdMode.h"
#include "DiggerManager.h"
#include "FCustomSDFBrush.h"

// Digger Sync Includes
#include "Interfaces/IPluginManager.h"
#include "SocketIOClientComponent.h"
#include "Widgets/Input/SHyperlink.h"
#include "SocketIOLobbyManager.h"
//#include "SocketIOModule.h"
#include "SocketIOClient.h"
#include "Json.h"
#include "JsonUtilities.h"
#include "Interfaces/IPluginManager.h"




#if WITH_SOCKETIO
#include "DiggerEditor/Public/DiggerEdModeToolkit.h"
#include "SocketIOClientComponent.h"
//#include "SocketIONetworking.h"
#endif

// Unreal Engine - Editor & Engine
#include "EditorModeManager.h"
#include "EditorStyleSet.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"

// Content Browser
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"

// Editor Interactive Tools Framework
#include "Behaviors/2DViewportBehaviorTargets.h" // Cleaned duplicates

// Slate UI - Widgets
#include "BrushAssetEditorUtils.h"
#include "DetailLayoutBuilder.h"
#include "HttpModule.h"
#include "IWebSocket.h"
#include "SocketSubsystem.h"
#include "WebSocketsModule.h"
#include "Behaviors/2DViewportBehaviorTargets.h"
#include "Behaviors/2DViewportBehaviorTargets.h"
#include "Behaviors/2DViewportBehaviorTargets.h"
#include "Behaviors/2DViewportBehaviorTargets.h"
#include "Behaviors/2DViewportBehaviorTargets.h"
#include "Behaviors/2DViewportBehaviorTargets.h"
#include "Brushes/SlateImageBrush.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/FileManager.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSlider.h" // <- You noted this should be added
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FDiggerEdModeToolkit"


FDiggerEdModeToolkit::FDiggerEdModeToolkit()
    : FModeToolkit()
    , SocketIOLobbyManager(nullptr)
{
#if WITH_SOCKETIO
    // Create the LobbyManager UObject once, root it so GC won’t kill it
    SocketIOLobbyManager = NewObject<USocketIOLobbyManager>(GetTransientPackage());
    SocketIOLobbyManager->AddToRoot();
#endif
}

// FDiggerEdModeToolkit::Init

void FDiggerEdModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost)
{
    BindIslandDelegates();
    

    Manager = GetDiggerManager();

    // In your Init() or constructor, before SNew(SVerticalBox):
    LightTypeOptions.Empty();
    for (int32 i = 0; i < StaticEnum<ELightBrushType>()->NumEnums() - 1; ++i)
    {
        LightTypeOptions.Add(MakeShared<ELightBrushType>((ELightBrushType)i));
    }

    PopulateLightTypeOptions();

    if (LightTypeOptions.Num() > 0)
    {
        CurrentLightType = *LightTypeOptions[0];  // Ensure initial state is synced
    }

    //  Accounts for Network google login

    if (!FSlateStyleRegistry::FindSlateStyle("DiggerEditorStyle"))
    {
        DiggerStyleSet = MakeShareable(new FSlateStyleSet("DiggerEditorStyle"));

        DiggerStyleSet->SetContentRoot(FPaths::ProjectContentDir()); // For `/Content/...`

        DiggerStyleSet->Set("DiggerEditor.GoogleIcon", new FSlateImageBrush(
            FSlateImageBrush(
                DiggerStyleSet->RootToContentDir(TEXT("DiggerEditor/Resources/Icons/google_icon"), TEXT(".png")),
                FVector2D(16, 16)
            )
        ));

        FSlateStyleRegistry::RegisterSlateStyle(*DiggerStyleSet);
    } 
    // Account stuff over.

    AssetThumbnailPool = MakeShareable(new FAssetThumbnailPool(32, true));
    IslandGrid = SNew(SUniformGridPanel).SlotPadding(2.0f);

    static float DummyFloat = 0.0f;

    ToolkitWidget = SNew(SVerticalBox)

    // --- Brush Shape Section ---
    + SVerticalBox::Slot().AutoHeight().Padding(4)
    [
        SNew(STextBlock).Text(FText::FromString("Brush Shape"))
    ]
    + SVerticalBox::Slot().AutoHeight().Padding(8, 8, 8, 4)
    [
        MakeBrushShapeSection()
    ]

     // --- Light Type UI: Only visible for Light brush ---
    + SVerticalBox::Slot().AutoHeight().Padding(4)
    [
        SNew(SBox)
        .Visibility_Lambda([this]()
        {
            return (GetCurrentBrushType() == EVoxelBrushType::Light) ? EVisibility::Visible : EVisibility::Collapsed;
        })
        [
            SNew(SVerticalBox) // Changed to vertical box to stack light type and color
            + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString("Light Type"))
                    .Font(IDetailLayoutBuilder::GetDetailFont())
                ]
                + SHorizontalBox::Slot().FillWidth(1.0f).Padding(8, 0)
                [
                    SNew(SComboBox<TSharedPtr<ELightBrushType>>)
                    .OptionsSource(&LightTypeOptions)
                    .OnGenerateWidget_Lambda([](TSharedPtr<ELightBrushType> InItem)
                    {
                        return SNew(STextBlock)
                            .Text(StaticEnum<ELightBrushType>()->GetDisplayNameTextByValue(
                                static_cast<int64>(*InItem)));
                    })
                    .OnSelectionChanged_Lambda([this](TSharedPtr<ELightBrushType> NewSelection, ESelectInfo::Type)
                    {
                        if (NewSelection.IsValid())
                        {
                            CurrentLightType = *NewSelection; // Use CurrentLightType consistently
                        }
                    })
                    .InitiallySelectedItem(LightTypeOptions.Num() > 0 ? LightTypeOptions[0] : nullptr)
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]()
                        {
                            return StaticEnum<ELightBrushType>()->GetDisplayNameTextByValue(
                                static_cast<int64>(CurrentLightType));
                        })
                    ]
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString("Light Color"))
                    .Font(IDetailLayoutBuilder::GetDetailFont())
                ]
                + SHorizontalBox::Slot().FillWidth(1.0f).Padding(8, 0)
                [
                    SNew(SColorBlock)
                    .Color_Lambda([this]()
                    {
                        return CurrentLightColor;
                    })
                    .OnMouseButtonDown_Lambda([this](const FGeometry&, const FPointerEvent&) -> FReply
                    {
                        FColorPickerArgs PickerArgs;
                        PickerArgs.bUseAlpha = false;
                        PickerArgs.InitialColor = CurrentLightColor;
                        PickerArgs.OnColorCommitted = FOnLinearColorValueChanged::CreateLambda(
                            [this](FLinearColor NewColor)
                            {
                                CurrentLightColor = NewColor;
                                OnLightColorChanged(NewColor);
                            });

                        OpenColorPicker(PickerArgs);
                        return FReply::Handled();
                    })
                ]
            ]
        ]
    ]
        

    // --- Height/Length UI: Only visible for Cylinder or Cone brush ---
    + SVerticalBox::Slot().AutoHeight().Padding(4)
    [
        SNew(SBox)
        .Visibility_Lambda([this]() {
            EVoxelBrushType BrushType = GetCurrentBrushType();
            return (BrushType == EVoxelBrushType::Cylinder || BrushType == EVoxelBrushType::Cube|| BrushType == EVoxelBrushType::Capsule || BrushType == EVoxelBrushType::Cone) ? EVisibility::Visible : EVisibility::Collapsed;
        })
        [
            MakeLabeledSliderRow(
                FText::FromString("Height"),
                [this]() { return BrushLength; },
                [this](float NewValue) { BrushLength = FMath::Clamp(NewValue, MinBrushLength, MaxBrushLength); },
                float(MinBrushLength), float(MaxBrushLength),
                TArray<float>({float(MinBrushLength), float((MinBrushLength+MaxBrushLength)/2.f), float(MaxBrushLength)}),
                float(MinBrushLength), 1.0f,
                false, &DummyFloat
            )
        ]
    ]

    // --- Filled/Hollow UI: Visible for shapes that support it ---
    + SVerticalBox::Slot().AutoHeight().Padding(4)
    [
        SNew(SBox)
        .Visibility_Lambda([this]()
        {
            EVoxelBrushType BrushType = GetCurrentBrushType();
            return (BrushType == EVoxelBrushType::Cone ||
                       BrushType == EVoxelBrushType::Cylinder)
                       ? EVisibility::Visible
                       : EVisibility::Collapsed;
        })
        [
            SNew(SCheckBox)
            .IsChecked_Lambda([this]()
            {
                return bIsFilled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
            })
            .OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
            {
                bIsFilled = (NewState == ECheckBoxState::Checked);
            })
            [
                SNew(STextBlock).Text(FText::FromString("Filled"))
            ]
        ]
    ]


    // --- Advanced Cube Brush Checkbox and Roll-down ---
    + SVerticalBox::Slot().AutoHeight().Padding(4)
    [
        SNew(SBox)
        .Visibility_Lambda([this]()
        {
            return (GetCurrentBrushType() == EVoxelBrushType::Cube) ? EVisibility::Visible : EVisibility::Collapsed;
        })
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SCheckBox)
                .IsChecked_Lambda([this]()
                {
                    return bUseAdvancedCubeBrush ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
                {
                        bUseAdvancedCubeBrush = (NewState == ECheckBoxState::Checked);
                        UE_LOG(LogTemp, Warning, TEXT("UI: bUseAdvancedCubeBrush: %s"), bUseAdvancedCubeBrush ? TEXT("true") : TEXT("false"));
                        
                })
                [
                    SNew(STextBlock).Text(FText::FromString("Use Advanced Cube Brush"))
                ]
            ]
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SBox)
                .Visibility_Lambda([this]()
                {
                    return bUseAdvancedCubeBrush ? EVisibility::Visible : EVisibility::Collapsed;
                })
                [
                    // Roll-down menu for advanced parameters
                    SNew(SVerticalBox)
                    // Half Extents X
                    + SVerticalBox::Slot().AutoHeight().Padding(2)
                    [
                        MakeLabeledSliderRow(
                            FText::FromString("Half Extent X"),
                            [this]() { return AdvancedCubeHalfExtentX; },
                            [this](float NewValue)
                            {
                                AdvancedCubeHalfExtentX = FMath::Clamp(NewValue, MinCubeExtent, MaxCubeExtent);
                            },
                            static_cast<float>(MinCubeExtent), static_cast<float>(MaxCubeExtent),
                            TArray<float>({
                                static_cast<float>(MinCubeExtent),
                                static_cast<float>((MinCubeExtent + MaxCubeExtent) / 2.f),
                                static_cast<float>(MaxCubeExtent)
                            }),
                            static_cast<float>(MinCubeExtent), 1.0f,
                            false, &DummyFloat
                        )
                    ]
                    // Half Extents Y
                    + SVerticalBox::Slot().AutoHeight().Padding(2)
                    [
                        MakeLabeledSliderRow(
                            FText::FromString("Half Extent Y"),
                            [this]() { return AdvancedCubeHalfExtentY; },
                            [this](float NewValue)
                            {
                                AdvancedCubeHalfExtentY = FMath::Clamp(NewValue, MinCubeExtent, MaxCubeExtent);
                            },
                            static_cast<float>(MinCubeExtent), static_cast<float>(MaxCubeExtent),
                            TArray<float>({
                                static_cast<float>(MinCubeExtent),
                                static_cast<float>((MinCubeExtent + MaxCubeExtent) / 2.f),
                                static_cast<float>(MaxCubeExtent)
                            }),
                            static_cast<float>(MinCubeExtent), 1.0f,
                            false, &DummyFloat
                        )
                    ]
                    // Half Extents Z
                    + SVerticalBox::Slot().AutoHeight().Padding(2)
                    [
                        MakeLabeledSliderRow(
                            FText::FromString("Half Extent Z"),
                            [this]() { return AdvancedCubeHalfExtentZ; },
                            [this](float NewValue)
                            {
                                AdvancedCubeHalfExtentZ = FMath::Clamp(NewValue, MinCubeExtent, MaxCubeExtent);
                            },
                            static_cast<float>(MinCubeExtent), static_cast<float>(MaxCubeExtent),
                            TArray<float>({
                                static_cast<float>(MinCubeExtent),
                                static_cast<float>((MinCubeExtent + MaxCubeExtent) / 2.f),
                                static_cast<float>(MaxCubeExtent)
                            }),
                            static_cast<float>(MinCubeExtent), 1.0f,
                            false, &DummyFloat
                        )
                    ]
                    // Rotation (if you want to expose it)
                    // Add more sliders or controls as needed for other parameters
                ]
            ]
        ]
    ]


    // --- Inner Radius UI: Only visible for Torus brush ---
    + SVerticalBox::Slot().AutoHeight().Padding(4)
    [
        SNew(SBox)
        .Visibility_Lambda([this]()
        {
            return (GetCurrentBrushType() == EVoxelBrushType::Torus) ? EVisibility::Visible : EVisibility::Collapsed;
        })
        [
            MakeLabeledSliderRow(
                FText::FromString("Inner Radius"),
                [this]() { return TorusInnerRadius; },
                [this](float NewValue)
                {
                    TorusInnerRadius = FMath::Clamp(NewValue, MinTorusInnerRadius, MaxTorusInnerRadius);
                },
                static_cast<float>(MinTorusInnerRadius), static_cast<float>(MaxTorusInnerRadius),
                TArray<float>({
                    static_cast<float>(MinTorusInnerRadius),
                    static_cast<float>((MinTorusInnerRadius + MaxTorusInnerRadius) / 2.f),
                    static_cast<float>(MaxTorusInnerRadius)
                }),
                static_cast<float>(MinTorusInnerRadius), 1.0f,
                false, &DummyFloat
            )
        ]
    ]

       
        
        

    // --- Cone Angle UI: Only visible for Cone and cylinder brushes ---
    + SVerticalBox::Slot().AutoHeight().Padding(4)
    [
        SNew(SBox)
        .Visibility_Lambda([this]() {
            return (GetCurrentBrushType() == EVoxelBrushType::Cone || GetCurrentBrushType() == EVoxelBrushType::Cylinder) ? EVisibility::Visible : EVisibility::Collapsed;
        })
        [
            MakeLabeledSliderRow(
                FText::FromString("Cone Angle"),
                [this]() { return float(ConeAngle); },
                [this](float NewValue) { ConeAngle = FMath::Clamp(double(NewValue), MinConeAngle, MaxConeAngle); },
                float(MinConeAngle), float(MaxConeAngle),
                TArray<float>({float(MinConeAngle), float((MinConeAngle+MaxConeAngle)/2.f), float(MaxConeAngle)}),
                float(MinConeAngle), 1.0f,
                false, &DummyFloat // No mirror for cone angle
            )
        ]
    ]


        
    // --- Smooth Iterations (Smooth only) ---
    + SVerticalBox::Slot().AutoHeight().Padding(4)
    [
        SNew(SBox)
        .Visibility_Lambda([this]() {
            return (GetCurrentBrushType() == EVoxelBrushType::Smooth) ? EVisibility::Visible : EVisibility::Collapsed;
        })
        [
            MakeLabeledSliderRow(
                FText::FromString("Smooth Iterations"),
                [this]() { return float(SmoothIterations); },
                [this](float NewValue) { SmoothIterations = FMath::Clamp(static_cast<int16>(FMath::RoundToInt(NewValue)), int16(1), int16(10)); },
                1.0f, 10.0f,
                TArray<float>({1.f, 5.f, 10.f}),
                1.0f, 1.0f,
                false, &DummyFloat
            )
        ]
    ]

    // --- Operation (Add/Subtract) Section ---
    + SVerticalBox::Slot().AutoHeight().Padding(4)
    [
        MakeOperationSection()
    ]

    // --- Brush Radius, Strength, Falloff ---
    + SVerticalBox::Slot().AutoHeight().Padding(8, 16, 8, 4)
    [
        MakeLabeledSliderRow(
            FText::FromString("Radius"),
            [this]() { return BrushRadius; },
            [this](float NewValue) { BrushRadius = FMath::Clamp(NewValue, 10.0f, 256.0f); },
            10.0f, 256.0f,
            TArray<float>({10.f, 64.f, 128.f, 256.f}),
            10.0f, 1.0f,
            false, &DummyFloat
        )
    ]
    + SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 4)
    [
        MakeLabeledSliderRow(
            FText::FromString("Strength"),
            [this]() { return BrushStrength; },
            [this](float NewValue) { BrushStrength = FMath::Clamp(NewValue, 0.0f, 1.0f); },
            0.0f, 1.0f,
            TArray<float>({0.1f, 0.5f, 1.0f}),
            1.0f, 0.01f,
            false, &DummyFloat
        )
    ]
    + SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 8)
    [
        MakeLabeledSliderRow(
            FText::FromString("Falloff"),
            [this]() { return BrushFalloff; },
            [this](float NewValue) { BrushFalloff = FMath::Clamp(NewValue, 0.0f, 1.0f); },
            0.0f, 1.0f,
            TArray<float>({0.0f, 0.5f, 1.0f}),
            0.0f, 0.01f,
            false, &DummyFloat
        )
    ]

        /// --- Rotate Brush Section (roll-down) ---
    + SVerticalBox::Slot().AutoHeight().Padding(8, 8, 8, 4)
    [
        SNew(SButton)
        .Text(FText::FromString("Rotate Brush"))
        .OnClicked_Lambda([this]() { bShowRotation = !bShowRotation; return FReply::Handled(); })
    ]
    + SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 8)
    [
        SNew(SVerticalBox)
        .Visibility_Lambda([this]() { return bShowRotation ? EVisibility::Visible : EVisibility::Collapsed; })

        + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
        [
            MakeRotationRow(FText::FromString("X"), BrushRotX)
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
        [
            MakeRotationRow(FText::FromString("Y"), BrushRotY)
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
        [
            MakeRotationRow(FText::FromString("Z"), BrushRotZ)
        ]

        // Checkbox for surface normal rotation
        + SVerticalBox::Slot().AutoHeight().Padding(8, 4)
        [
            SNew(SCheckBox)
            .IsChecked_Lambda([this]()
            {
                return bUseSurfaceNormalRotation ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
            })
            .OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
            {
                bUseSurfaceNormalRotation = (NewState == ECheckBoxState::Checked);
            })
            [
                SNew(STextBlock).Text(FText::FromString("Align Rotation to Normal"))
            ]
        ]

        // ✅ Reset Button - newly added
        + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 4)
        [
            SNew(SButton)
            .Text(FText::FromString("Reset All Rotations"))
            .HAlign(HAlign_Fill)
            .ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
            .OnClicked_Lambda([this]()
            {
                BrushRotX = 0.0f;
                BrushRotY = 0.0f;
                BrushRotZ = 0.0f;
                return FReply::Handled();
            })
        ]
    ]

    // --- Offset Brush Section (roll-down) ---
    + SVerticalBox::Slot().AutoHeight().Padding(8, 8, 8, 4)
    [
        SNew(SButton)
        .Text(FText::FromString("Offset Brush"))
        .OnClicked_Lambda([this]() { bShowOffset = !bShowOffset; return FReply::Handled(); })
    ]
    + SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 8)
    [
        SNew(SVerticalBox)
        .Visibility_Lambda([this]() { return bShowOffset ? EVisibility::Visible : EVisibility::Collapsed; })
        + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
        [
            MakeOffsetRow(FText::FromString("X"), BrushOffset.X)
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
        [
            MakeOffsetRow(FText::FromString("Y"), BrushOffset.Y)
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
        [
            MakeOffsetRow(FText::FromString("Z"), BrushOffset.Z)
        ]
        //Checkbox for surface normal alignment
        + SVerticalBox::Slot().AutoHeight().Padding(8, 4)
        [
            SNew(SCheckBox)
            .IsChecked_Lambda([this]()
            {
                return bRotateToSurfaceNormal ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
            })
            .OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
            {
                bRotateToSurfaceNormal = (NewState == ECheckBoxState::Checked);
            })
            [
                SNew(STextBlock).Text(FText::FromString("Rotate to Surface Normal"))
            ]
        ]

        // ✅ Reset Button - newly added
        + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 4)
        [
            SNew(SButton)
            .Text(FText::FromString("Reset All Offsets"))
            .HAlign(HAlign_Fill)
            .ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
            .OnClicked_Lambda([this]()
            {
                BrushOffset = FVector::ZeroVector;
                return FReply::Handled();
            })
        ]
    ]


    // --- Custom Brushes Section ---
+ SVerticalBox::Slot().AutoHeight().Padding(8, 8, 8, 4)
[
    SNew(STextBlock).Text(FText::FromString("Custom Brushes"))
]

// The grid of custom brushes
+ SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 4)
[
    SAssignNew(CustomBrushGrid, SUniformGridPanel)
]

// The "Add Custom Brush" button
+ SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 4)
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
                // Create thumbnail
                TSharedPtr<FAssetThumbnail> AssetThumbnail = MakeShareable(new FAssetThumbnail(AssetData, 64, 64, AssetThumbnailPool));
                FCustomBrushEntry Entry;
                Entry.Mesh = MeshPtr;
                Entry.SDFBrushFilePath = TEXT("");
                Entry.Thumbnail = AssetThumbnail;
                CustomBrushEntries.Add(Entry);
            }
        }
        RebuildCustomBrushGrid();
        return FReply::Handled();
    })
]

// The centralized "Convert to SDF Brush" button
+ SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 8)
[
    SNew(SButton)
    .Text(FText::FromString("Convert to SDF Brush"))
    .IsEnabled_Lambda([this]() {
        return SelectedBrushIndex >= 0
            && CustomBrushEntries.IsValidIndex(SelectedBrushIndex)
            && CustomBrushEntries[SelectedBrushIndex].IsMesh()
            && !CustomBrushEntries[SelectedBrushIndex].IsSDF();
    })
    .OnClicked_Lambda([this]() -> FReply
    {
        if (SelectedBrushIndex >= 0 && CustomBrushEntries.IsValidIndex(SelectedBrushIndex))
        {
            auto& Entry = CustomBrushEntries[SelectedBrushIndex];
            if (Entry.IsMesh() && !Entry.IsSDF())
            {
                FCustomSDFBrush SDFBrush;
                FTransform MeshTransform = FTransform::Identity; // Or user-specified
                float VoxelSize = 2.0f; // Or user-specified
                if (FBrushAssetEditorUtils::GenerateSDFBrushFromStaticMesh(Entry.Mesh.Get(), MeshTransform, VoxelSize, SDFBrush))
                {
                    FString DiggerBrushesDir = FPaths::ProjectContentDir() / TEXT("DiggerCustomBrushes");
                    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
                    if (!PlatformFile.DirectoryExists(*DiggerBrushesDir))
                        PlatformFile.CreateDirectory(*DiggerBrushesDir);

                    FString BrushFileName = Entry.Mesh->GetName() + TEXT(".sdfbrush");
                    FString BrushFilePath = DiggerBrushesDir / BrushFileName;

                    if (FBrushAssetEditorUtils::SaveSDFBrushToFile(SDFBrush, BrushFilePath))
                    {
                        Entry.SDFBrushFilePath = BrushFilePath;
                        Entry.Mesh = nullptr; // Now it's an SDF brush
                        // Optionally: update grid UI
                        RebuildCustomBrushGrid();
                    }
                }
            }
        }
        return FReply::Handled();
    })
]



    // --- Build/Export Settings Section (roll-down) ---
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
        + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
        [
            SNew(SCheckBox)
            .IsChecked_Lambda([this]() { return bEnableCollision ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bEnableCollision = (State == ECheckBoxState::Checked); })
            [
                SNew(STextBlock).Text(FText::FromString("Enable Collision"))
            ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
        [
            SNew(SCheckBox)
            .IsChecked_Lambda([this]() { return bEnableNanite ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bEnableNanite = (State == ECheckBoxState::Checked); })
            [
                SNew(STextBlock).Text(FText::FromString("Enable Nanite"))
            ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
        [
            MakeLabeledSliderRow(
                FText::FromString("Detail Reduction"),
                [this]() { return BakeDetail; },
                [this](float NewValue) { BakeDetail = FMath::Clamp(NewValue, 0.0f, 1.0f); },
                0.0f, 1.0f,
                TArray<float>({0.0f, 0.5f, 1.0f}),
                1.0f, 0.01f,
                false, &DummyFloat
            )
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
        [
            SNew(SButton)
            .Text(FText::FromString("Bake to Static Mesh"))
            .OnClicked_Lambda([this]()
            {
                if (Manager == GetDiggerManager())
                {
                    Manager->BakeToStaticMesh(bEnableCollision, bEnableNanite, BakeDetail);
                }
                return FReply::Handled();
            })
        ]
    ]
        
    // --- Save/Load Section ---
    + SVerticalBox::Slot().AutoHeight().Padding(8, 12, 8, 4)
    [
    MakeSaveLoadSection()
    ]

    // --- Lobby/Multiplayer Section ---
    + SVerticalBox::Slot().AutoHeight().Padding(8, 12, 8, 4)
    [
        MakeLobbySection()
    ]

    // --- Islands Section---
    + SVerticalBox::Slot().AutoHeight().Padding(8, 12, 8, 4)
    [
        MakeIslandsSection()
    ];

    ScanCustomBrushFolder();
    
   /* if (CustomBrushGrid.IsValid())
    {
        RebuildCustomBrushGrid();
    }*/

    FModeToolkit::Init(InitToolkitHost);
}

bool FDiggerEdModeToolkit::CanPaintWithCustomBrush() const
{
    return CurrentBrushType == EVoxelBrushType::Custom
        && SelectedBrushIndex >= 0
        && CustomBrushEntries.IsValidIndex(SelectedBrushIndex)
        && CustomBrushEntries[SelectedBrushIndex].IsSDF();
}



void FDiggerEdModeToolkit::ScanCustomBrushFolder()
{
    FString BrushDir = FPaths::ProjectContentDir() / TEXT("DiggerCustomBrushes");
    TArray<FString> BrushFiles;
    IFileManager::Get().FindFiles(BrushFiles, *(BrushDir / TEXT("*.sdfbrush")), true, false);

    CustomBrushEntries.Empty();

    for (const FString& FileName : BrushFiles)
    {
        FString FullPath = BrushDir / FileName;

        // Optionally, load the SDF brush here to get dimensions, etc.
        FCustomBrushEntry Entry;
        Entry.SDFBrushFilePath = FullPath;
        Entry.Mesh = nullptr;
        // Optionally, set a thumbnail (e.g., a default icon or a generated preview)
        // Entry.Thumbnail = ...;
        CustomBrushEntries.Add(Entry);
    }

    RebuildCustomBrushGrid();
}



//Delegate Setup methods

FDiggerEdModeToolkit::~FDiggerEdModeToolkit()
{
    if (Manager == GetDiggerManager())
    {
        Manager->OnIslandDetected.RemoveAll(this);
    }

#if WITH_SOCKETIO
    if (SocketIOLobbyManager)
    {
        SocketIOLobbyManager->RemoveFromRoot();
        SocketIOLobbyManager = nullptr;
    }
#endif
    
    if (DiggerStyleSet.IsValid())
    {
        FSlateStyleRegistry::UnRegisterSlateStyle(*DiggerStyleSet);
        DiggerStyleSet.Reset();
    }

    
    ShutdownNetworking(); // Ensure connection is cleanly closed
}

#if WITH_SOCKETIO
bool FDiggerEdModeToolkit::IsConnectButtonEnabled() const
{
    return !LoggedInUser.IsEmpty();
}
#endif

#if WITH_SOCKETIO
FReply FDiggerEdModeToolkit::OnConnectClicked()
{
    ConnectToLobbyServer();
    return FReply::Handled();
}

bool FDiggerEdModeToolkit::IsCreateLobbyEnabled() const
{
    return SocketIOLobbyManager
        && SocketIOLobbyManager->IsConnected()
        && LobbyNameTextBox.IsValid()
        && !LobbyNameTextBox->GetText().IsEmpty();
}

bool FDiggerEdModeToolkit::IsJoinLobbyEnabled() const
{
    return SocketIOLobbyManager
        && SocketIOLobbyManager->IsConnected()
        && LobbyIdTextBox.IsValid()
        && !LobbyIdTextBox->GetText().IsEmpty();
}

#endif

void FDiggerEdModeToolkit::OnIslandDetectedHandler(const FIslandData& NewIslandData)
{
    // Only add if not already present (e.g., by IslandID)
   /* if (!Islands.ContainsByPredicate([&](const FIslandData& Island) { return Island.IslandID == NewIslandData.IslandID; }))
    {
        Islands.Add(NewIslandData);
        RebuildIslandGrid();
    }*/
}

bool FDiggerEdModeToolkit::GetBrushIsFilled()
{
    return bIsFilled;
}

void FDiggerEdModeToolkit::AddIsland(const FIslandData& Island)
{
    if (DiggerDebug::Islands)
        UE_LOG(LogTemp, Error, TEXT("AddIsland called on toolkit!"));
    Islands.Add(Island);
    RebuildIslandGrid();
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

void FDiggerEdModeToolkit::BindIslandDelegates()
{
    Manager = GetDiggerManager();

    if (!Manager)
    {
        UE_LOG(LogTemp, Error, TEXT("[Toolkit::BindIslandDelegates] Manager not found. Will retry in 0.25s."));

        if (GEditor)
        {
            TSharedRef<FTimerManager> TimerManager = GEditor->GetTimerManager();
            TimerManager->SetTimerForNextTick([this]()
            {
                UE_LOG(LogTemp, Warning, TEXT("[Toolkit::BindIslandDelegates] Retrying delegate bind..."));
                BindIslandDelegates(); // Retry
            });
        }

        return;
    }

    UE_LOG(LogTemp, Log, TEXT("[Toolkit::BindIslandDelegates] Binding to Manager %p"), Manager);

    Manager->OnIslandsDetectionStarted.RemoveAll(this);
    Manager->OnIslandDetected.RemoveAll(this);

    Manager->OnIslandsDetectionStarted.AddRaw(this, &FDiggerEdModeToolkit::ClearIslands);
    Manager->OnIslandDetected.AddRaw(this, &FDiggerEdModeToolkit::AddIsland);
}



TSharedRef<SWidget> FDiggerEdModeToolkit::MakeQuickSetButtons(
    const TArray<float>& QuickSetValues,
    TFunction<void(float)> Setter,
    float* TargetForMirror,
    bool bIsAngle
)
{
    TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);

    // Get the default Slate font
    FSlateFontInfo DefaultFont = FCoreStyle::Get().GetFontStyle("NormalFont");

    for (float Value : QuickSetValues)
    {
        // Use plain degree symbol as a literal character
        FText DisplayText = bIsAngle
            ? FText::FromString(FString::Printf(TEXT("%.0f°"), Value))
            : FText::AsNumber(Value);

        Box->AddSlot()
        .AutoWidth()
        .Padding(2, 0)
        [
            SNew(SButton)
            .OnClicked_Lambda([Setter, Value]() { Setter(Value); return FReply::Handled(); })
            [
                SNew(STextBlock)
                .Font(DefaultFont)
                .Text(DisplayText)
            ]
        ];
    }
    

    if (bIsAngle && TargetForMirror)
    {
        Box->AddSlot()
        .AutoWidth()
        .Padding(2, 0)
        [
            SNew(SButton)
            .OnClicked_Lambda([TargetForMirror]() {
                *TargetForMirror = FMath::Fmod(*TargetForMirror + 180.0f, 360.0f);
                if (*TargetForMirror < 0) *TargetForMirror += 360.0f;
                return FReply::Handled();
            })
            .ToolTipText(FText::FromString("Mirror (add 180°)"))
            [
                SNew(STextBlock)
                .Font(DefaultFont)
                .Text(FText::FromString("Mirror"))
            ]
        ];
    }

    return Box;
}


void FDiggerEdModeToolkit::SetBrushDigPreviewOverride(bool bInDig)
{
    bUseBrushDigPreviewOverride = true;
    bBrushDigPreviewOverride = bInDig;
}

void FDiggerEdModeToolkit::ClearBrushDigPreviewOverride()
{
    bUseBrushDigPreviewOverride = false;
}



void FDiggerEdModeToolkit::PopulateLightTypeOptions()
{
    LightTypeOptions.Empty();
    LightTypeOptions.Add(MakeShared<ELightBrushType>(ELightBrushType::Point));
    LightTypeOptions.Add(MakeShared<ELightBrushType>(ELightBrushType::Spot));
    LightTypeOptions.Add(MakeShared<ELightBrushType>(ELightBrushType::Directional));
}

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeLightTypeComboWidget(TSharedPtr<ELightBrushType> InItem)
{
    FString EnumName = StaticEnum<ELightBrushType>()->GetDisplayNameTextByValue((int64)*InItem).ToString();
    return SNew(STextBlock).Text(FText::FromString(EnumName));
}

void FDiggerEdModeToolkit::OnLightColorChanged(FLinearColor NewColor)
{
    CurrentLightColor = NewColor;
    // Change 'Manager' to a different variable name to avoid conflict
    if (ADiggerManager* DiggerManager = GetDiggerManager())
    {
        DiggerManager->EditorBrushLightColor = NewColor;
    }
}




// Make sure your OnLightTypeChanged implementation matches the header exactly:
void FDiggerEdModeToolkit::OnLightTypeChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
{
    if (NewSelection.IsValid())
    {
        FString SelectedString = *NewSelection;
        if (SelectedString == TEXT("Point Light"))
        {
            CurrentLightType = ELightBrushType::Point;  // This should work now
        }
        else if (SelectedString == TEXT("Spot Light"))
        {
            CurrentLightType = ELightBrushType::Spot;
        }
        else if (SelectedString == TEXT("Directional Light"))
        {
            CurrentLightType = ELightBrushType::Directional;
        }
    }
}

void FDiggerEdModeToolkit::SetTemporaryDigOverride(TOptional<bool> Override)
{
    TemporaryDigOverride = Override;

    // If you have any preview/hint widgets that show the current mode,
    // this is a good place to refresh them. Example:
    // RefreshBrushPreview(); // hypothetical method to update the UI
}




// Modular, reusable labeled slider row with quick set buttons and reset.
// - Label: The label to display (e.g., "X", "Radius").
// - Getter: Lambda to get the current value.
// - Setter: Lambda to set the value.
// - MinValue/MaxValue: Slider and numeric entry range.
// - QuickSetValues: Array of values for quick-set buttons.
// - ResetValue: Value to set when "Reset" is pressed.
// - Step: Step size for numeric entry (default 1.0f).
// - bIsAngle: true for angle (adds Mirror, °), false for number.
// - TargetForMirror: reference to the value for Mirror (only used if bIsAngle).
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeLabeledSliderRow(
    const FText& Label,
    TFunction<float()> Getter,
    TFunction<void(float)> Setter,
    float MinValue,
    float MaxValue,
    const TArray<float>& QuickSetValues,
    float ResetValue,
    float Step,
    bool bIsAngle,
    float* TargetForMirror
)
{
    // State for expand/collapse
    TSharedPtr<bool> bExpanded = MakeShared<bool>(false);

    return SNew(SHorizontalBox)
            // Label
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(0, 0, 8, 0)
            [
                SNew(STextBlock)
                .Text(Label)
                .MinDesiredWidth(40)
            ]
        // Caret button
        + SHorizontalBox::Slot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        .Padding(0, 0, 4, 0)
        [
            SNew(SButton)
            .ButtonStyle(FCoreStyle::Get(), "NoBorder")
            .OnClicked_Lambda([bExpanded]() { *bExpanded = !*bExpanded; return FReply::Handled(); })
            .Content()
            [
                SNew(STextBlock)
                .Text_Lambda([bExpanded]() { return *bExpanded ? FText::FromString("^") : FText::FromString("^"); })
            ]
        ]
        // Numeric entry (Slider, conditionally visible)
        + SHorizontalBox::Slot()
.AutoWidth()
[
    SNew(SBox)
    .Visibility_Lambda([bExpanded]() { return *bExpanded ? EVisibility::Visible : EVisibility::Collapsed; })
    [
        SNew(SNumericEntryBox<float>)
        .Value_Lambda([=]() { return Getter(); })
        .OnValueChanged_Lambda([=](float NewValue) {
            float ClampedValue = FMath::Clamp(NewValue, MinValue, MaxValue);

            if (bIsAngle)
            {
                ClampedValue = FMath::Fmod(ClampedValue, 360.0f);
                if (ClampedValue < 0.0f)
                {
                    ClampedValue += 360.0f;
                }
            }

            Setter(ClampedValue);
        })
        .MinValue(MinValue)
        .MaxValue(MaxValue)
        .MinSliderValue(MinValue)
        .MaxSliderValue(MaxValue)
        .AllowSpin(true)
        .MinDesiredValueWidth(50)
        .Delta(Step)
    ]
]

// Quick set buttons
+ SHorizontalBox::Slot()
.AutoWidth()
.Padding(2, 0)
[
    MakeQuickSetButtons(QuickSetValues, Setter, TargetForMirror, bIsAngle)
]

// Reset button
+ SHorizontalBox::Slot()
.AutoWidth()
.Padding(8, 0)
[
    SNew(SButton)
    .Text(FText::FromString("Reset"))
    .OnClicked_Lambda([=]() { Setter(ResetValue); return FReply::Handled(); })
];
}



TSharedRef<SWidget> FDiggerEdModeToolkit::MakeBrushShapeSection()
{
    struct FBrushTypeInfo
    {
        EVoxelBrushType Type;
        FString Label;
        FString Tooltip;
    };

    TArray<FBrushTypeInfo> BrushTypes = {
        { EVoxelBrushType::Sphere,       TEXT("Sphere"),        TEXT("Sphere Brush") },
        { EVoxelBrushType::Cube,         TEXT("Cube"),          TEXT("Cube Brush") },
        { EVoxelBrushType::Cylinder,     TEXT("Cylinder"),      TEXT("Cylinder Brush") },
        { EVoxelBrushType::Capsule,     TEXT("Capsule"),      TEXT("Capsule Brush") },
        { EVoxelBrushType::Cone,         TEXT("Cone"),          TEXT("Cone Brush") },
        { EVoxelBrushType::Torus,        TEXT("Torus"),         TEXT("Torus Brush") },
        { EVoxelBrushType::Pyramid,      TEXT("Pyramid"),       TEXT("Pyramid Brush") },
        { EVoxelBrushType::Icosphere,    TEXT("Icosphere"),     TEXT("Icosphere Brush") },
        { EVoxelBrushType::Stairs,       TEXT("Stairs"),        TEXT("Stairs Brush") },
        { EVoxelBrushType::Custom,       TEXT("Custom"),        TEXT("Custom Mesh Brush") },
        { EVoxelBrushType::Smooth,       TEXT("Smooth"),        TEXT("Smooth Brush") },
        { EVoxelBrushType::Noise,       TEXT("Noise"),        TEXT("Noise Brush") },
        { EVoxelBrushType::Light,       TEXT("Light"),        TEXT("Light Brush") },
        { EVoxelBrushType::Debug,        TEXT("Debug"),         TEXT("Debug Clicked Chunk Brush") }
    };


    // Number of columns in the grid (adjust as needed)
    const int32 NumColumns = 3;

    TSharedRef<SUniformGridPanel> ButtonGrid = SNew(SUniformGridPanel).SlotPadding(FMargin(2.0f, 2.0f));

    for (int32 i = 0; i < BrushTypes.Num(); ++i)
    {
        const auto& Info = BrushTypes[i];
        int32 Row = i / NumColumns;
        int32 Col = i % NumColumns;

        ButtonGrid->AddSlot(Col, Row)
        [
            SNew(SCheckBox)
            .Style(FAppStyle::Get(), "RadioButton")
            .IsChecked_Lambda([this, Info]() { return (CurrentBrushType == Info.Type) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this, Info](ECheckBoxState State) {
                if (State == ECheckBoxState::Checked)
                {
                    CurrentBrushType = Info.Type;
                    if (Manager == GetDiggerManager())
                    {
                        Manager->EditorBrushType = Info.Type;
                    }
                }
            })
            [
                SNew(STextBlock).Text(FText::FromString(Info.Label))
            ]
            .ToolTipText(FText::FromString(Info.Tooltip))
        ];
    }

    // Wrap everything in a vertical box
    return SNew(SVerticalBox)

        // Brush shape label and grid
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
            [
                SNew(STextBlock).Text(FText::FromString("Brush Shape:")).MinDesiredWidth(80)
            ]
            + SHorizontalBox::Slot().FillWidth(1.0f)
            [
                ButtonGrid
            ]
        ]

        // Brush radius slider
        /*+ SVerticalBox::Slot().AutoHeight().Padding(8, 16, 8, 4)
        [
            MakeLabeledSliderRow(
                FText::FromString("Radius"),
                [this]() { return BrushRadius; },
                [this](float NewValue) { BrushRadius = FMath::Clamp(NewValue, 10.0f, 256.0f); },
                10.0f, 256.0f, {10.0f, 64.0f, 128.0f, 256.0f}, 10.0f
            )
        ]*/;
}

// Add this method to your FDiggerEdModeToolkit class

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeSaveLoadSection()
{
    return SNew(SVerticalBox)

        // Section Title
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
        [
            SNew(STextBlock)
            .Text(FText::FromString("Chunk Serialization"))
            .Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
            .ColorAndOpacity(FSlateColor::UseForeground())
        ]

        // Save/Load Buttons Row
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
        [
            SNew(SHorizontalBox)

            // Save All Chunks Button
            + SHorizontalBox::Slot().FillWidth(0.5f).Padding(0, 0, 4, 0)
            [
                SNew(SButton)
                .Text(FText::FromString("Save All Chunks"))
                .ToolTipText(FText::FromString("Save all currently loaded chunks to disk"))
                .HAlign(HAlign_Center)
                .VAlign(VAlign_Center)
                .OnClicked_Lambda([this]() -> FReply {
                    if (Manager == GetDiggerManager())
                    {
                        bool bSuccess = Manager->SaveAllChunks();
                        if (bSuccess)
                        {
                            // Show success notification
                            FNotificationInfo Info(FText::FromString("Successfully saved all chunks"));
                            Info.ExpireDuration = 3.0f;
                            Info.bUseSuccessFailIcons = true;
                            FSlateNotificationManager::Get().AddNotification(Info);
                            
                            UE_LOG(LogTemp, Log, TEXT("Save All Chunks: Success"));
                        }
                        else
                        {
                            // Show error notification
                            FNotificationInfo Info(FText::FromString("Failed to save some chunks - check log"));
                            Info.ExpireDuration = 5.0f;
                            Info.bUseSuccessFailIcons = true;
                            FSlateNotificationManager::Get().AddNotification(Info);
                            
                            UE_LOG(LogTemp, Error, TEXT("Save All Chunks: Failed"));
                        }
                    }
                    else
                    {
                        UE_LOG(LogTemp, Error, TEXT("Save All Chunks: No DiggerManager found"));
                    }
                    return FReply::Handled();
                })
                .IsEnabled_Lambda([this]() -> bool {
                    Manager = GetDiggerManager();
                    return Manager != nullptr && Manager->ChunkMap.Num() > 0;
                })
            ]

            // Load All Chunks Button
            + SHorizontalBox::Slot().FillWidth(0.5f).Padding(4, 0, 0, 0)
            [
                SNew(SButton)
                .Text(FText::FromString("Load All Chunks"))
                .ToolTipText(FText::FromString("Load all saved chunks from disk"))
                .HAlign(HAlign_Center)
                .VAlign(VAlign_Center)
                .OnClicked_Lambda([this]() -> FReply {
                    if (Manager == GetDiggerManager())
                    {
                        bool bSuccess = Manager->LoadAllChunks();
                        if (bSuccess)
                        {
                            // Show success notification
                            FNotificationInfo Info(FText::FromString("Successfully loaded all chunks"));
                            Info.ExpireDuration = 3.0f;
                            Info.bUseSuccessFailIcons = true;
                            FSlateNotificationManager::Get().AddNotification(Info);
                            
                            UE_LOG(LogTemp, Log, TEXT("Load All Chunks: Success"));
                        }
                        else
                        {
                            // Show error notification
                            FNotificationInfo Info(FText::FromString("Failed to load some chunks - check log"));
                            Info.ExpireDuration = 5.0f;
                            Info.bUseSuccessFailIcons = true;
                            FSlateNotificationManager::Get().AddNotification(Info);
                            
                            UE_LOG(LogTemp, Error, TEXT("Load All Chunks: Failed"));
                        }
                    }
                    else
                    {
                        UE_LOG(LogTemp, Error, TEXT("Load All Chunks: No DiggerManager found"));
                    }
                    return FReply::Handled();
                })
                .IsEnabled_Lambda([this]() -> bool {
                    Manager = GetDiggerManager();
                    if (!Manager) return false;
                    
                    // Check if there are any saved chunks to load
                    TArray<FIntVector> SavedChunks = Manager->GetAllSavedChunkCoordinates();
                    return SavedChunks.Num() > 0;
                })
            ]
        ]

        // Statistics Row
        + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
        [
            SNew(SHorizontalBox)

            // Loaded Chunks Count
            + SHorizontalBox::Slot().FillWidth(0.5f).Padding(0, 0, 4, 0)
            [
                SNew(STextBlock)
                .Text_Lambda([this]() -> FText {
                    if (Manager == GetDiggerManager())
                    {
                        return FText::FromString(FString::Printf(TEXT("Loaded: %d"), Manager->ChunkMap.Num()));
                    }
                    return FText::FromString("Loaded: 0");
                })
                .Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
                .ToolTipText(FText::FromString("Number of chunks currently loaded in memory"))
            ]

            // Saved Chunks Count
            + SHorizontalBox::Slot().FillWidth(0.5f).Padding(4, 0, 0, 0)
            [
                SNew(STextBlock)
                .Text_Lambda([this]() -> FText {
                    if (Manager == GetDiggerManager())
                    {
                        TArray<FIntVector> SavedChunks = Manager->GetAllSavedChunkCoordinates();
                        return FText::FromString(FString::Printf(TEXT("Saved: %d"), SavedChunks.Num()));
                    }
                    return FText::FromString("Saved: 0");
                })
                .Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
                .ToolTipText(FText::FromString("Number of chunks saved to disk"))
            ]
        ];
}

bool FDiggerEdModeToolkit::IsSocketIOPluginAvailable() const
{
    return IPluginManager::Get().FindPlugin("SocketIOClient").IsValid();
}


TSharedRef<SWidget> FDiggerEdModeToolkit::MakeLobbySection()
{
    const bool bHasPlugin = IsSocketIOPluginAvailable();

    return SNew(SVerticalBox)

    // 1) Google/Username login (optional)
    + SVerticalBox::Slot().AutoHeight().Padding(8)
    [
        SNew(SButton)
        .IsEnabled(bHasPlugin)
        .OnClicked_Lambda([this, bHasPlugin]() { return bHasPlugin ? ShowLoginModal() : FReply::Handled(); })
        [
            SNew(STextBlock)
            .Text(FText::FromString("Sign in with Google"))
        ]
    ]

    // 2) Connect button (plain text)
#if WITH_SOCKETIO
    + SVerticalBox::Slot().AutoHeight().Padding(8)
    [
        SNew(SButton)
        .IsEnabled(bHasPlugin)
        .OnClicked_Lambda([this]() -> FReply
        {
            ConnectToLobbyServer();
            return FReply::Handled();
        })
        [
            SNew(STextBlock)
            .Text(FText::FromString("Connect to Lobby Server"))
        ]
    ]
#endif

    // 3) Networking panel
    + SVerticalBox::Slot().AutoHeight().Padding(8)
    [
#if WITH_SOCKETIO
        bHasPlugin
            ? MakeNetworkingWidget()
            : MakeNetworkingHelpWidget()
#else
        MakeNetworkingHelpWidget()
#endif
    ];
}



//------------------------------------------------------------------------------
// Fallback UI when Socket.IO is missing or disabled
//------------------------------------------------------------------------------
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeNetworkingHelpWidget()
{
    TSharedPtr<SProgressBar> DownloadProgress;

    // Auto-installer: leave in for now, rip out later
    return SNew(SVerticalBox)

        + SVerticalBox::Slot().AutoHeight().Padding(4)
        [
            SNew(STextBlock)
            .Text(FText::FromString(
                "Multiplayer features require the SocketIO plugin."))
        ]

        + SVerticalBox::Slot().AutoHeight().Padding(4)
        [
            SNew(SButton)
            .OnClicked_Lambda([DownloadProgress]() -> FReply
            {
                // …existing HTTP + ZipUtility download logic…
                return FReply::Handled();
            })
            [
                SNew(STextBlock)
                .Text(FText::FromString("Auto-Install SocketIO Plugin"))
            ]
        ]

        + SVerticalBox::Slot().AutoHeight().Padding(4)
        [
            SAssignNew(DownloadProgress, SProgressBar)
            .Percent(0.0f)
        ]

        + SVerticalBox::Slot().AutoHeight().Padding(4)
        [
            SNew(SButton)
            .IsEnabled(true) // Always shown; change logic later
            .OnClicked_Lambda([]() -> FReply
            {
                FMessageDialog::Open(EAppMsgType::Ok,
                    NSLOCTEXT("DiggerPlugin","EnableZip",
                    "Enable the ZipUtility plugin and restart to auto-install."));
                return FReply::Handled();
            })
            [
                SNew(STextBlock)
                .Text(FText::FromString("Enable ZipUtility Plugin"))
            ]
        ];
}

void FDiggerEdModeToolkit::CreateLobbyManager(UWorld* WorldContext)
{
    if (!WorldContext)
    {
        UE_LOG(LogTemp, Warning, TEXT("CreateLobbyManager: no world!"));
        return;
    }

    // Only create once
    if ( !SocketIOLobbyManager )
    {
        // Instantiate on the WorldSettings so it lives with the world
        AWorldSettings* WS = WorldContext->GetWorldSettings();
        USocketIOLobbyManager* NewMgr =
            NewObject<USocketIOLobbyManager>(WS, USocketIOLobbyManager::StaticClass());
        
        NewMgr->Initialize(WorldContext);
        

        UE_LOG(LogTemp, Log, TEXT("SocketIOLobbyManager created and initialized."));
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("SocketIOLobbyManager already exists."));
    }
}

void FDiggerEdModeToolkit::ConnectToLobbyServer()
{
#if WITH_SOCKETIO

    // 1) Grab the editor world
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (World == nullptr)
    {
        UE_LOG(LogTemp, Error, TEXT("ConnectToLobbyServer: No valid EditorWorldContext."));
        return;
    }

    // 2) Ensure we have a lobby manager instance in our SharedPtr
    if (!SocketIOLobbyManager)
    {
        // Choose an Outer so UE GC keeps this alive
        UObject* Outer = World->GetGameInstance()
            ? static_cast<UObject*>(World->GetGameInstance())
            : static_cast<UObject*>(World->GetWorldSettings());

        // Create the UObject and root it
        USocketIOLobbyManager* RawMgr = NewObject<USocketIOLobbyManager>(Outer);
        if (RawMgr == nullptr)
        {
            UE_LOG(LogTemp, Error, TEXT("ConnectToLobbyServer: NewObject<USocketIOLobbyManager>() failed."));
            return;
        }
        RawMgr->AddToRoot();

        // Wrap in a shared pointer (no deleter – GC will handle destruction)
//        SocketIOLobbyManager = MakeShareable(RawMgr);
        UE_LOG(LogTemp, Verbose, TEXT("ConnectToLobbyServer: Lobby manager created and rooted."));
    }

    // 3) If for some reason creation still failed, stop here
    if (!SocketIOLobbyManager)
    {
        UE_LOG(LogTemp, Error, TEXT("ConnectToLobbyServer: Unable to create or retrieve lobby manager."));
        return;
    }

    // 4) Only now do we call into the manager
    if (SocketIOLobbyManager)
    {
        if (!SocketIOLobbyManager->IsConnected())
        {
            SocketIOLobbyManager->Initialize(World);
            UE_LOG(LogTemp, Log, TEXT("ConnectToLobbyServer: Attempting Socket.IO connect to NodeJS server..."));
        }
        else
        {
            UE_LOG(LogTemp, Log, TEXT("ConnectToLobbyServer: Already connected; skipping."));
        }
    }

#endif // WITH_SOCKETIO
}





#if WITH_SOCKETIO
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeNetworkingWidget()
{
    return SNew(SVerticalBox)

    // 1) Header
    + SVerticalBox::Slot()
      .AutoHeight()
      .Padding(4)
    [
        SNew(STextBlock)
        .Font(FAppStyle::GetFontStyle("BoldFont"))
        .Text(FText::FromString("Multiplayer Lobby Setup Active"))
    ]

    // 2) Create New Lobby
    + SVerticalBox::Slot()
      .AutoHeight()
      .Padding(4)
    [
        SNew(SHorizontalBox)

        + SHorizontalBox::Slot()
          .AutoWidth()
          .VAlign(VAlign_Center)
          .Padding(0,0,6,0)
        [
            SNew(STextBlock)
            .Text(FText::FromString("New Lobby Name:"))
        ]

        + SHorizontalBox::Slot()
          .FillWidth(1.0f)
        [
            SAssignNew(LobbyNameTextBox, SEditableTextBox)
            .HintText(FText::FromString("Enter lobby name…"))
        ]

        + SHorizontalBox::Slot()
          .AutoWidth()
          .VAlign(VAlign_Center)
          .Padding(6,0,0,0)
        [
            // Create button
            SNew(SButton)
            .Text(FText::FromString("Create"))
            .OnClicked(this, &FDiggerEdModeToolkit::OnCreateLobbyClicked)
            .IsEnabled(this, &FDiggerEdModeToolkit::IsCreateLobbyEnabled)
        ]
    ]

    // 3) Join Existing Lobby
    + SVerticalBox::Slot()
      .AutoHeight()
      .Padding(4)
    [
        SNew(SHorizontalBox)

        + SHorizontalBox::Slot()
          .AutoWidth()
          .VAlign(VAlign_Center)
          .Padding(0,0,6,0)
        [
            SNew(STextBlock)
            .Text(FText::FromString("Lobby ID to Join:"))
        ]

        + SHorizontalBox::Slot()
          .FillWidth(1.0f)
        [
            SAssignNew(LobbyIdTextBox, SEditableTextBox)
            .HintText(FText::FromString("Enter lobby ID…"))
        ]

        + SHorizontalBox::Slot()
          .AutoWidth()
          .VAlign(VAlign_Center)
          .Padding(6,0,0,0)
        [
            // Join button
            SNew(SButton)
            .Text(FText::FromString("Join"))
            .OnClicked(this, &FDiggerEdModeToolkit::OnJoinLobbyClicked)
            .IsEnabled(this, &FDiggerEdModeToolkit::IsJoinLobbyEnabled)
        ]
    ]

    // 4) Active Lobbies List (placeholder)
    + SVerticalBox::Slot()
      .FillHeight(1.0f)
      .Padding(4)
    [
        SNew(SBox)
        .MinDesiredHeight(200)
        [
            SNew(SScrollBox)

            // Example static entries — replace with dynamic list later
            + SScrollBox::Slot()
            [
                SNew(STextBlock)
                .Text(FText::FromString("Lobby: AlphaRoom (ID: 1234)"))
            ]
            + SScrollBox::Slot()
            [
                SNew(STextBlock)
                .Text(FText::FromString("Lobby: BetaTeam (ID: abcd)"))
            ]
        ]
    ];

    // TODO: hook the scroll box to your USocketIOLobbyManager’s lobby list
}

FReply FDiggerEdModeToolkit::OnCreateLobbyClicked()
{
    if (LobbyNameTextBox.IsValid() && SocketIOLobbyManager)
    {
        FString Name = LobbyNameTextBox->GetText().ToString();
        SocketIOLobbyManager->CreateLobby(Name);
    }
    return FReply::Handled();
}

FReply FDiggerEdModeToolkit::OnJoinLobbyClicked()
{
    if (LobbyIdTextBox.IsValid() && SocketIOLobbyManager)
    {
        FString Id = LobbyIdTextBox->GetText().ToString();
        SocketIOLobbyManager->JoinLobby(Id);
    }
    return FReply::Handled();
}



#endif // WITH_SOCKETIO


ECheckBoxState FDiggerEdModeToolkit::IsBrushDebugEnabled()
{
    
    if (Manager && Manager->ActiveBrush)
    {
        return Manager->ActiveBrush->bEnableDebugDrawing ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
    }
    return ECheckBoxState::Unchecked;
}

void FDiggerEdModeToolkit::OnBrushDebugCheckChanged(ECheckBoxState NewState)
{
    if (Manager == GetDiggerManager() && Manager->ActiveBrush)
    {
        const bool bNewEnabled = (NewState == ECheckBoxState::Checked);
        Manager->ActiveBrush->bEnableDebugDrawing = bNewEnabled;
        UE_LOG(LogTemp, Warning, TEXT("Brush debug drawing manually set to %s"), bNewEnabled ? TEXT("ENABLED") : TEXT("DISABLED"));
    }
}


//Other Brush Settings Sliders
        /*+ SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 4)
        [
            MakeLabeledSliderRow(
                FText::FromString("Strength"),
                [this]() { return BrushStrength; },
                [this](float NewValue) { BrushStrength = FMath::Clamp(NewValue, 0.0f, 1.0f); },
                0.0f, 1.0f, {0.1f, 0.5f, 1.0f}, 1.0f, 0.01f
            )
        ]*/
       /* + SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 8)
        [
            MakeLabeledSliderRow(
                FText::FromString("Falloff"),
                [this]() { return BrushFalloff; },
                [this](float NewValue) { BrushFalloff = FMath::Clamp(NewValue, 0.0f, 1.0f); },
                0.0f, 1.0f, {0.0f, 0.5f, 1.0f}, 0.0f, 0.01f
            )
        ]*/;



// Section for Add/Subtract operation/*
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeOperationSection()
{
    return SNew(SVerticalBox)
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
         .IsChecked_Lambda([this]() {
             const bool bEffectiveDig = bUseBrushDigPreviewOverride ? bBrushDigPreviewOverride : bBrushDig;
             return !bEffectiveDig ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
         })
         .OnCheckStateChanged_Lambda([this](ECheckBoxState State) {
             if (State == ECheckBoxState::Checked)
             {
                 bBrushDig = false;
                 ClearBrushDigPreviewOverride();
                 if (Manager == GetDiggerManager())
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
         .IsChecked_Lambda([this]() {
             const bool bEffectiveDig = bUseBrushDigPreviewOverride ? bBrushDigPreviewOverride : bBrushDig;
             return bEffectiveDig ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
         })
         .OnCheckStateChanged_Lambda([this](ECheckBoxState State) {
             if (State == ECheckBoxState::Checked)
             {
                 bBrushDig = true;
                 ClearBrushDigPreviewOverride();
                 if ( Manager == GetDiggerManager())
                 {
                     Manager->EditorBrushDig = true;
                 }
             }
         })
         [
             SNew(STextBlock).Text(FText::FromString("Subtract (Dig)"))
         ]
     ]
];

}



//Rotation Row
// For double
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeRotationRow(const FText& Label, double& Value)
{
    static float DummyFloat = 0.0f;
    return MakeLabeledSliderRow(
        Label,
        [&Value]() { return float(Value); },
        [&Value](float NewValue) { Value = double(NewValue); }, // raw set
        0.0f, 360.0f,
        {45.f, 90.f, 180.f},
        0.0f, 1.0f,
        true, // bIsAngle
        &DummyFloat
    );
}


// For float
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeRotationRow(const FText& Label, float& Value)
{
    return MakeLabeledSliderRow(
        Label,
        [&Value]() { return Value; },
        [&Value](float NewValue) { Value = FMath::Fmod(NewValue, 360.0f); },
        0.0f, 360.0f,
        TArray<float>({45.f, 90.f, 180.f}),
        0.0f, 1.0f,
        true, // bIsAngle
        &Value
    );
}

//Offset Row
// For double
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeOffsetRow(const FText& Label, double& Value)
{
    static float DummyFloat = 0.0f;
    return MakeLabeledSliderRow(
        Label,
        [&Value]() { return float(Value); },
        [&Value](float NewValue) { Value = double(NewValue); },
        -1000.0f, 1000.0f,
        TArray<float>({-100.f, 0.f, 100.f}),
        0.0f, 1.0f,
        false, &DummyFloat
    );
}

// For float
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeOffsetRow(const FText& Label, float& Value)
{
    static float DummyFloat = 0.0f;
    return MakeLabeledSliderRow(
        Label,
        [&Value]() { return Value; },
        [&Value](float NewValue) { Value = NewValue; },
        -1000.0f, 1000.0f,
        TArray<float>({-100.f, 0.f, 100.f}),
        0.0f, 1.0f,
        false, &DummyFloat
    );
}

void FDiggerEdModeToolkit::OnConvertToPhysicsActorClicked()
{
    UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Add Physics to Selected Island button pressed."));
    
    if (SelectedIslandIndex != INDEX_NONE && Islands.IsValidIndex(SelectedIslandIndex))
    {
        const FIslandData& Island = Islands[SelectedIslandIndex];
        
        // Check if there's actually an island at this position
        if (Manager == GetDiggerManager())
        {
            // Use the reference voxel if available
            if (Island.ReferenceVoxel != FIntVector::ZeroValue)
            {
                Manager->ConvertIslandAtPositionToActor(Island.Location, true, Island.ReferenceVoxel);
                if (DiggerDebug::Islands)
                    UE_LOG(LogTemp, Log, TEXT("Island Location: %s, Reference Voxel: %s"), 
                    *Island.Location.ToString(), *Island.ReferenceVoxel.ToString());
            }
            else
            {
                //Manager->ConvertIslandAtPositionToActor(Island.Location, true);
                UE_LOG(LogTemp, Log, TEXT("Island Location: %s (no reference voxel)"), *Island.Location.ToString());
            }
        }
    }
}



//Make Island Section
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeIslandsSection()
{
    return SNew(SExpandableArea)
        .AreaTitle(FText::FromString("Islands"))
        .InitiallyCollapsed(true)
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
                    .Text(FText::FromString("Convert To Physics Actor"))
                    .IsEnabled_Lambda([this]() { return SelectedIslandIndex != INDEX_NONE; })
                    .OnClicked_Lambda([this]()
                    {
                        UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Add Physics to Selected Island button pressed."));
                        /* Convert logic here */
                        if (SelectedIslandIndex != INDEX_NONE && Islands.IsValidIndex(SelectedIslandIndex))
                        {
                            const FIslandData& Island = Islands[SelectedIslandIndex];
                            // Check if there's actually an island at this position
                            if (Manager == GetDiggerManager())
                            {
                                // Use the reference voxel if available
                                if (Island.ReferenceVoxel != FIntVector::ZeroValue)
                                {
                                    Manager->ConvertIslandAtPositionToActor(
                                        Island.Location, true, Island.ReferenceVoxel);
                                    UE_LOG(LogTemp, Log, TEXT("Island Location: %s, Reference Voxel: %s"),
                                           *Island.Location.ToString(), *Island.ReferenceVoxel.ToString());
                                }
                                else
                                {
                                    //Manager->ConvertIslandAtPositionToActor(Island.Location, true);
                                    UE_LOG(LogTemp, Error, TEXT("Island Location: %s (no reference voxel)"),
                                           *Island.Location.ToString());
                                }
                            }
                        }
                        return FReply::Handled();
                    })
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(2)
                [
                    SNew(SButton)
                    .Text(FText::FromString("Convert/Save as Scene Actor"))
                    .IsEnabled_Lambda([this]() { return SelectedIslandIndex != INDEX_NONE; })
                    .OnClicked_Lambda([this]()
                    {
                        UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Create Static Mesh from Island button pressed."));
                        /* Convert logic here */
                        if (SelectedIslandIndex != INDEX_NONE && Islands.IsValidIndex(SelectedIslandIndex))
                        {
                            const FIslandData& Island = Islands[SelectedIslandIndex];
                            // Check if there's actually an island at this position
                            if (Manager == GetDiggerManager())
                            {
                                // Use the reference voxel if available
                                if (Island.ReferenceVoxel != FIntVector::ZeroValue)
                                {
                                    Manager->ConvertIslandAtPositionToActor(
                                        Island.Location, false, Island.ReferenceVoxel);
                                    UE_LOG(LogTemp, Log, TEXT("Island Location: %s, Reference Voxel: %s"),
                                           *Island.Location.ToString(), *Island.ReferenceVoxel.ToString());
                                }
                                else
                                {
                                    //Manager->ConvertIslandAtPositionToActor(Island.Location, true);
                                    UE_LOG(LogTemp, Error, TEXT("Island Location: %s (no reference voxel)"),
                                           *Island.Location.ToString());
                                }
                            }
                        }
                        return FReply::Handled();
                    })
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(2)
                [
                    SNew(SButton)
                    .Text(FText::FromString("Remove"))
                    .IsEnabled_Lambda([this]() { return SelectedIslandIndex != INDEX_NONE; })
                    .OnClicked_Lambda([this]() { /* Remove logic here */ return FReply::Handled(); })
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(2)
                [
                    SNew(SButton)
                    .Text(FText::FromString("Duplicate"))
                    .IsEnabled_Lambda([this]() { return SelectedIslandIndex != INDEX_NONE; })
                    .OnClicked_Lambda([this]() { /* Duplicate logic here */ return FReply::Handled(); })
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
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
                    [
                        MakeRotationRow(FText::FromString("Pitch"), IslandRotation.Pitch)
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
                    [
                        MakeRotationRow(FText::FromString("Yaw"), IslandRotation.Yaw)
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
                    [
                        MakeRotationRow(FText::FromString("Roll"), IslandRotation.Roll)
                    ]
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
                ]
            ]
            // --- Island Grid Section ---
            + SVerticalBox::Slot().AutoHeight().Padding(4)
            [
                SAssignNew(IslandGridContainer, SBox)
                [
                    MakeIslandGridWidget()
                ]
            ]
        ];
}


// Section for rotation (X, Y, Z), collapsible
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeRotationSection(float& RotX, float& RotY, float& RotZ)
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(8, 8, 8, 4)
        [
            SNew(SButton)
            .Text(FText::FromString("Rotate"))
            .OnClicked_Lambda([this]() { bShowRotation = !bShowRotation; return FReply::Handled(); })
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 8)
        [
            SNew(SVerticalBox)
            .Visibility_Lambda([this]() { return bShowRotation ? EVisibility::Visible : EVisibility::Collapsed; })
            + SVerticalBox::Slot().AutoHeight().Padding(0,2)
            [
                MakeLabeledSliderRow(
                    FText::FromString("X"),
                    [&RotX]() { return RotX; },
                    [&RotX](float NewValue) { RotX = FMath::Fmod(NewValue, 360.0f); },
                    0.0f, 360.0f, {45.0f, 90.0f, 180.0f}, 0.0f
                )
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,2)
            [
                MakeLabeledSliderRow(
                    FText::FromString("Y"),
                    [&RotY]() { return RotY; },
                    [&RotY](float NewValue) { RotY = FMath::Fmod(NewValue, 360.0f); },
                    0.0f, 360.0f, {45.0f, 90.0f, 180.0f}, 0.0f
                )
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,2)
            [
                MakeLabeledSliderRow(
                    FText::FromString("Z"),
                    [&RotZ]() { return RotZ; },
                    [&RotZ](float NewValue) { RotZ = FMath::Fmod(NewValue, 360.0f); },
                    0.0f, 360.0f, {45.0f, 90.0f, 180.0f}, 0.0f
                )
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 4)
            [
                SNew(SButton)
                .Text(FText::FromString("Reset All Rotations"))
                .HAlign(HAlign_Fill)
                .ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
                .OnClicked_Lambda([&RotX, &RotY, &RotZ]() 
                { 
                    RotX = 0.0f;
                    RotY = 0.0f;
                    RotZ = 0.0f;
                    return FReply::Handled(); 
                })
                .Visibility_Lambda([this]()
                {
                    UE_LOG(LogTemp, Warning, TEXT("bShowRotation: %s"), bShowRotation ? TEXT("true") : TEXT("false"));
                    return bShowRotation ? EVisibility::Visible : EVisibility::Collapsed;
                })
            ]
        ];
}

// Section for offset (X, Y, Z), collapsible
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeOffsetSection(FVector& Offset)
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(8, 8, 8, 4)
        [
            SNew(SButton)
            .Text(FText::FromString("Offset"))
            .OnClicked_Lambda([this]() { bShowOffset = !bShowOffset; return FReply::Handled(); })
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 8)
        [
            SNew(SVerticalBox)
            .Visibility_Lambda([this]() { return bShowOffset ? EVisibility::Visible : EVisibility::Collapsed; })
            + SVerticalBox::Slot().AutoHeight().Padding(0,2)
            [
                MakeLabeledSliderRow(
                    FText::FromString("X"),
                    [&Offset]() { return Offset.X; },
                    [&Offset](float NewValue) { Offset.X = NewValue; },
                    -100.0f, 100.0f, {-10.0f, 0.0f, 10.0f}, 0.0f
                )
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,2)
            [
                MakeLabeledSliderRow(
                    FText::FromString("Y"),
                    [&Offset]() { return Offset.Y; },
                    [&Offset](float NewValue) { Offset.Y = NewValue; },
                    -100.0f, 100.0f, {-10.0f, 0.0f, 10.0f}, 0.0f
                )
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,2)
            [
                MakeLabeledSliderRow(
                    FText::FromString("Z"),
                    [&Offset]() { return Offset.Z; },
                    [&Offset](float NewValue) { Offset.Z = NewValue; },
                    -100.0f, 100.0f, {-10.0f, 0.0f, 10.0f}, 0.0f
                )
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 4)
            [
                SNew(SButton)
                .Text(FText::FromString("Reset All Offsets"))
                .HAlign(HAlign_Fill)
                .ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
                .OnClicked_Lambda([&Offset]() 
                { 
                    Offset.X = 0.0f;
                    Offset.Y = 0.0f;
                    Offset.Z = 0.0f;
                    return FReply::Handled(); 
                })
                .Visibility_Lambda([this]()
                {
                    UE_LOG(LogTemp, Warning, TEXT("bShowRotation: %s"), bShowRotation ? TEXT("true") : TEXT("false"));
                    return bShowOffset ? EVisibility::Visible : EVisibility::Collapsed;
                })
            ]
        ];
}



//Make Island Widget <Here>

// DiggerEdModeToolkit.cpp

#if WITH_SOCKETIO
//------------------------------------------------------------------------------
// Popup login window
//------------------------------------------------------------------------------
FReply FDiggerEdModeToolkit::ShowLoginModal()
{
    if (LoginWindow.IsValid())
    {
        // already open
        return FReply::Handled();
    }

    // build simple username prompt
    UsernameTextBox = SNew(SEditableTextBox)
        .HintText(LOCTEXT("UsernameHint", "Enter username…"));

    LoginWindow = SNew(SWindow)
        .Title(LOCTEXT("LoginWindowTitle", "Login"))
        .SupportsMinimize(false)
        .SupportsMaximize(false)
        .ClientSize(FVector2D(300, 100))
        .Content()
        [
            SNew(SBorder)
            .Padding(8)
            [
                SNew(SVerticalBox)

                + SVerticalBox::Slot()
                  .AutoHeight()
                  .Padding(0,0,0,8)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("LoginPrompt", "Please choose a username:"))
                ]

                + SVerticalBox::Slot()
                  .AutoHeight()
                [
                    UsernameTextBox.ToSharedRef()
                ]

                + SVerticalBox::Slot()
                  .AutoHeight()
                  .HAlign(HAlign_Right)
                  .Padding(0,8,0,0)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("LoginOk", "OK"))
                    .OnClicked(this, &FDiggerEdModeToolkit::OnConnectClicked)
                ]
            ]
        ];

    FSlateApplication::Get().AddWindow(LoginWindow.ToSharedRef());
    return FReply::Handled();
}
#endif


/*
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeNetworkingHelpWidget()
{
    TSharedPtr<SProgressBar> DownloadProgress;

    TSharedPtr<IPlugin> ZipPlugin = IPluginManager::Get().FindPlugin(TEXT("ZipUtility"));
    const bool bZipPluginValid = ZipPlugin.IsValid();
    const bool bZipPluginEnabled = bZipPluginValid && ZipPlugin->IsEnabled();
    const bool bZipModuleLoaded = FModuleManager::Get().IsModuleLoaded("ZipFile");

    return SNew(SVerticalBox)

        + SVerticalBox::Slot().AutoHeight().Padding(4)
        [
            SNew(STextBlock)
            .Text(FText::FromString("Multiplayer features require the SocketIO Plugin"))
        ]

        + SVerticalBox::Slot().AutoHeight().Padding(4)
        [
            SNew(SButton)
            .OnClicked_Lambda([DownloadProgress, bZipPluginEnabled, bZipModuleLoaded]() -> FReply
            {
                const FString PluginURL = TEXT("https://github.com/getnamo/socketio-client-ue4/archive/refs/heads/main.zip");
                const FString ZipPath = FPaths::ProjectPluginsDir() / TEXT("SocketIOClient.zip");
                const FString ExtractPath = FPaths::ProjectPluginsDir();

                FHttpModule* Http = &FHttpModule::Get();
                TSharedRef<IHttpRequest> Request = Http->CreateRequest();

                Request->OnProcessRequestComplete().BindLambda([ZipPath, ExtractPath, DownloadProgress, bZipPluginEnabled, bZipModuleLoaded](FHttpRequestPtr RequestPtr, FHttpResponsePtr Response, bool bWasSuccessful)
                {
                    if (bWasSuccessful && Response.IsValid())
                    {
                        FFileHelper::SaveArrayToFile(Response->GetContent(), *ZipPath);
                        UE_LOG(LogTemp, Log, TEXT("SocketIO Plugin downloaded to: %s"), *ZipPath);

                        if (DownloadProgress.IsValid())
                        {
                            DownloadProgress->SetPercent(1.0f);
                        }

#if WITH_ZIPFILE
                        if (bZipPluginEnabled)
                        {
                            if (!bZipModuleLoaded)
                            {
                                FModuleManager::Get().LoadModule("ZipFile");
                            }

                            UZipFileFunctionLibrary::UnzipFile(ZipPath, ExtractPath, true);
                            IFileManager::Get().Delete(*ZipPath);
                            UE_LOG(LogTemp, Log, TEXT("SocketIO Plugin extracted to: %s"), *ExtractPath);
                        }
                        else
#endif
                        {
                            UE_LOG(LogTemp, Warning, TEXT("ZipUtility plugin is not enabled. Please enable it and restart the editor."));
                        }
                    }
                    else
                    {
                        UE_LOG(LogTemp, Error, TEXT("Failed to download SocketIO plugin."));
                        if (DownloadProgress.IsValid())
                        {
                            DownloadProgress->SetPercent(0.0f);
                        }
                    }
                });

                Request->SetURL(PluginURL);
                Request->SetVerb("GET");
                Request->OnRequestProgress().BindLambda([DownloadProgress](FHttpRequestPtr Req, int32 BytesSent, int32 BytesReceived)
                {
                    if (DownloadProgress.IsValid() && Req->GetContentLength() > 0)
                    {
                        const float Progress = static_cast<float>(BytesReceived) / static_cast<float>(Req->GetContentLength());
                        DownloadProgress->SetPercent(Progress);
                    }
                });

                Request->ProcessRequest();

                return FReply::Handled();
            })
            [
                SNew(STextBlock).Text(FText::FromString("Auto-Install SocketIO Plugin"))
            ]
        ]

        + SVerticalBox::Slot().AutoHeight().Padding(4)
        [
            SAssignNew(DownloadProgress, SProgressBar)
            .Percent(0.0f)
        ]

        + SVerticalBox::Slot().AutoHeight().Padding(4)
        [
            SNew(STextBlock).Text(FText::FromString("Note: The ZipUtility plugin must be enabled and compiled for automatic installation to work."))
        ]

        + SVerticalBox::Slot().AutoHeight().Padding(4)
        [
            SNew(SButton)
            .IsEnabled(!bZipPluginEnabled)
            .OnClicked_Lambda([]() -> FReply
            {
                FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("DiggerPlugin", "ManualZipEnable", 
                    "To use automatic installation, please enable the ZipUtility plugin in the editor and restart."));
                return FReply::Handled();
            })
            [
                SNew(STextBlock).Text(FText::FromString("Enable ZipUtility Plugin"))
            ]
        ];
}
*/





void FDiggerEdModeToolkit::ShutdownNetworking()
{
  /*  if (WebSocket.IsValid())
    {
        UE_LOG(LogTemp, Log, TEXT("🔌 Disconnecting from Digger Network Server"));
        WebSocket->Close();
        WebSocket.Reset();
    }

    // Optional: Clear user/session info
    CurrentLobbyName.Empty();
    CurrentUserName.Empty();
    AvailableLobbies.Empty();

    // Optional: Clean up any dynamic widgets if necessary
    if (LobbyListBox.IsValid())
    {
        LobbyListBox->ClearChildren();
    }*/
}




void FDiggerEdModeToolkit::ClearIslands()
{
    UE_LOG(LogTemp, Warning, TEXT("ClearIslands called on toolkit: %p"), this);
    Islands.Empty();
    SelectedIslandIndex = INDEX_NONE;
    RebuildIslandGrid();
}


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
    for (int32 i = 0; i < CustomBrushEntries.Num(); ++i)
    {
        auto& Entry = CustomBrushEntries[i];
        TSharedPtr<SWidget> ThumbWidget;

        if (Entry.Thumbnail.IsValid())
        {
            ThumbWidget = SNew(SBox)
                .WidthOverride(64)
                .HeightOverride(64)
                [
                    Entry.Thumbnail->MakeThumbnailWidget()
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
                // Optionally: update UI to reflect selection
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
