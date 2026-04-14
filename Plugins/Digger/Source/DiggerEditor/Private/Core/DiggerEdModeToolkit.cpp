#include "DiggerEdModeToolkit.h"
#include "DiggerEdMode.h"
#include "DiggerManager.h"
#include "DiggerFeatureFlags.h"
#include "DiggerDebug.h"
#include "BrushAssetEditorUtils.h"
#include "FCustomSDFBrush.h"
#include "UDiggerEditorEventHub.h"
#include "DiggerIslandRuntimeSubsystem.h"

// Unreal Engine - Editor & Engine
#include "EditorModeManager.h"
#include "EditorStyleSet.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "Editor.h"
#include "UnrealEdMisc.h"          // FUnrealEdMisc::Get().RestartEditor()
#include "FileHelpers.h"           // FEditorFileUtils::SaveDirtyPackages()
#include "UObject/UnrealType.h" // Required for FBoolProperty and CastField

// Content Browser
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "PropertyCustomizationHelpers.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/DataAssetFactory.h"
#include "EditorActorFolders.h"

// Slate UI - Widgets
#include "DesktopPlatformModule.h"
#include "DetailLayoutBuilder.h"
#include "DiggerEditorSettings.h"
#include "Brushes/SlateImageBrush.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Widgets/Colors/SColorPicker.h"
#include "EditorViewportClient.h"
#include "VoxelChunk.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/DiggerTextureSet.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SSeparator.h"
#include "Misc/ConfigCacheIni.h"
#include "Subsystems/EditorActorSubsystem.h"

#define LOCTEXT_NAMESPACE "FDiggerEdModeToolkit"

// --- Helper Functions ---


static FString GetInitialProfileFolder()
{
    FString DefaultPath = TEXT("/Game/Digger/Materials/Profiles");
#if WITH_EDITOR
    FContentBrowserModule& CB = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
    TArray<FString> Paths;
    CB.Get().GetSelectedFolders(Paths);
    if (Paths.Num() > 0) DefaultPath = Paths[0];
#endif
    return DefaultPath;
}

static void NotifyProfileArrayChanged(UDiggerMaterialProfile* Profile, const FName MemberName)
{
    if (!Profile) return;
#if WITH_EDITOR
    const FScopedTransaction Tx(NSLOCTEXT("Digger", "Tx_ModifyMaterialProfile", "Modify Digger Material Profile"));
    Profile->Modify();
    if (FProperty* ChangedProp = Profile->GetClass()->FindPropertyByName(MemberName))
    {
        Profile->PreEditChange(ChangedProp);
        FPropertyChangedEvent Event(ChangedProp, EPropertyChangeType::ValueSet);
        Profile->PostEditChangeProperty(Event);
    }
    else
    {
        Profile->PostEditChange();
    }
    if (UPackage* Pkg = Profile->GetOutermost()) Pkg->MarkPackageDirty();
#endif
}



// -----------------------------------------------------------------------------------
// CONSTRUCTOR & DESTRUCTOR
// -----------------------------------------------------------------------------------

FDiggerEdModeToolkit::FDiggerEdModeToolkit() : FModeToolkit()
{
    // Read camera follow state from persisted editor config.
    // Falls back to true (follow on) if settings aren't loaded yet.
    if (const UDiggerEditorSettings* S = UDiggerEditorSettings::Get())
        bCameraFollowsBrush = S->bCameraFollowsBrush;
    else
        bCameraFollowsBrush = true;

    WorklightTypeOptions.Add(MakeShared<FString>("Point"));
    WorklightTypeOptions.Add(MakeShared<FString>("Spot"));
    SelectedWorklightTypeItem = WorklightTypeOptions[0];

    LightTypeOptions.Empty();
    for (int32 i = 0; i < StaticEnum<ELightBrushType>()->NumEnums() - 1; ++i)
    {
        LightTypeOptions.Add(MakeShared<ELightBrushType>((ELightBrushType)i));
    }
    PopulateLightTypeOptions();
    if (LightTypeOptions.Num() > 0)
    {
        CurrentLightType = *LightTypeOptions[0];
    }

    PushModeOptions.Add(MakeShared<EDiggerPushMode>(EDiggerPushMode::Ray));
    PushModeOptions.Add(MakeShared<EDiggerPushMode>(EDiggerPushMode::Normal));
    PushModeOptions.Add(MakeShared<EDiggerPushMode>(EDiggerPushMode::Blended));

    CustomBrushEntries.RemoveAll([](const FCustomBrushEntry& Entry) 
    {
        bool bHasValidMesh = Entry.Mesh.IsValid();
        bool bHasValidSDF = false;
        if (!Entry.SDFBrushFilePath.IsEmpty())
        {
            IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
            bHasValidSDF = PlatformFile.FileExists(*Entry.SDFBrushFilePath);
        }
        return !bHasValidMesh && !bHasValidSDF;
    });
}

FDiggerEdModeToolkit::~FDiggerEdModeToolkit()
{
    DisconnectIslandHub();

    if (DiggerStyleSet.IsValid())
    {
        FSlateStyleRegistry::UnRegisterSlateStyle(*DiggerStyleSet);
        DiggerStyleSet.Reset();
    }
}

// -----------------------------------------------------------------------------------
// INIT (The Main UI Assembly)
// -----------------------------------------------------------------------------------

void FDiggerEdModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost)
{
    Manager = GetDiggerManager();
    ConnectIslandHub();

    // Style Init... (Keep your existing Style code here)
    if (!FSlateStyleRegistry::FindSlateStyle("DiggerEditorStyle"))
    {
        DiggerStyleSet = MakeShareable(new FSlateStyleSet("DiggerEditorStyle"));
        DiggerStyleSet->SetContentRoot(FPaths::ProjectContentDir());
        DiggerStyleSet->Set("DiggerEditor.GoogleIcon", new FSlateImageBrush(DiggerStyleSet->RootToContentDir(TEXT("DiggerEditor/Resources/Icons/google_icon"), TEXT(".png")), FVector2D(16, 16)));
        FSlateStyleRegistry::RegisterSlateStyle(*DiggerStyleSet);
    }

    // --- INITIALIZE SETTINGS FROM CONFIG ---
    const UDiggerEditorSettings* Settings = UDiggerEditorSettings::Get();
    if (Settings)
    {
        // Worklight (Scene Light)
        // (If you want to save/load Scene Light settings to config, you'd load them here too,
        //  but currently your settings file mainly has Brush Light settings. 
        //  I will implement loading Brush Light settings here.)
        
        BrushLightIntensity     = Settings->BrushLightIntensity;
        BrushLightAttenuation   = Settings->BrushLightAttenuationRadius;
        BrushLightColor         = Settings->BrushLightColor;
        bMatchBrushLightColor   = Settings->bMatchLightColorToBrush;
        // Camera follow — persisted in editor config
        bCameraFollowsBrush = Settings->bCameraFollowsBrush;
        // Outliner Folders
        SetDynamicHolesFolderVisible(Settings->bShowDynamicHolesFolder);
    }
    
    AssetThumbnailPool = MakeShareable(new FAssetThumbnailPool(32, true));
    IslandGrid = SNew(SUniformGridPanel).SlotPadding(2.0f);
    TSharedPtr<SVerticalBox> DebugFlagListContainer;

    // --- BUILD MAIN WIDGET ---
    TSharedRef<SVerticalBox> MainBox = SNew(SVerticalBox);

    // 0. QUICK TOGGLES — always visible at the top, no expander needed.
    MainBox->AddSlot().AutoHeight().Padding(8, 6, 8, 2)
    [
        SNew(SHorizontalBox)

        // Camera Follows Brush [L]
        + SHorizontalBox::Slot().AutoWidth().Padding(0,0,16,0).VAlign(VAlign_Center)
        [
            SNew(SCheckBox)
            .IsChecked_Lambda([this]()
            {
                return bCameraFollowsBrush
                    ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
            })
            .OnCheckStateChanged_Lambda([this](ECheckBoxState S)
            {
                bCameraFollowsBrush = (S == ECheckBoxState::Checked);
                if (UDiggerEditorSettings* MS = GetMutableDefault<UDiggerEditorSettings>())
                {
                    MS->bCameraFollowsBrush = bCameraFollowsBrush;
                    MS->SaveConfig();
                }
            })
            .ToolTipText(FText::FromString(
                TEXT("Camera Follows Brush\nThe viewport camera smoothly tracks your brush while sculpting.\nToggle anytime with the L key.")))
            [ SNew(STextBlock).Text(FText::FromString("Camera  [L]")) ]
        ]

        // Landscape Fallback
        + SHorizontalBox::Slot().AutoWidth().Padding(0,0,4,0).VAlign(VAlign_Center)
        [
            SNew(SCheckBox)
            .IsChecked_Lambda([this]()
            {
                const UDiggerEditorSettings* S = UDiggerEditorSettings::Get();
                const bool bOn = !S || S->bFallbackToLandscapeWhenNoMeshHit;
                return bOn ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
            })
            .OnCheckStateChanged_Lambda([this](ECheckBoxState S)
            {
                if (UDiggerEditorSettings* MS = GetMutableDefault<UDiggerEditorSettings>())
                {
                    MS->bFallbackToLandscapeWhenNoMeshHit = (S == ECheckBoxState::Checked);
                    MS->SaveConfig();
                }
            })
            .ToolTipText(FText::FromString(
                TEXT("Landscape Fallback\nKeep ON (recommended): the brush snaps to the landscape surface when there is no voxel mesh underneath.\nTurn OFF only if your entire terrain is pre-baked with voxel collision.")))
            [ SNew(STextBlock).Text(FText::FromString("Landscape Fallback")) ]
        ]
    ];

    // 1. BRUSH TOOLS (Always Visible)
    MainBox->AddSlot().AutoHeight().Padding(8, 8, 8, 4)
    [
        SNew(SExpandableArea).AreaTitle(FText::FromString(TEXT("Brush Tools")))
        .InitiallyCollapsed(false)
        .BodyContent()
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(8)[ MakeBrushShapeSection() ]
            + SVerticalBox::Slot().AutoHeight().Padding(8,0)[ MakeBrushParameterSection() ]
            + SVerticalBox::Slot().AutoHeight().Padding(8,0)[ MakeRotationSection(BrushRotX, BrushRotY, BrushRotZ) ]
            + SVerticalBox::Slot().AutoHeight().Padding(8,0)[ MakeOffsetSection(BrushOffset) ]
            + SVerticalBox::Slot().AutoHeight().Padding(8,0)[ MakeCustomBrushSection() ]
        ]
    ];

    // 2. ENVIRONMENT (Gated Subsections)
    if (FDiggerFeatureFlags::bEnableEnvironment) // Master flag
    {
        TSharedRef<SVerticalBox> EnvContent = SNew(SVerticalBox);
        
        // Add Subsections only if enabled
        if (FDiggerFeatureFlags::bEnableNavigation) EnvContent->AddSlot().AutoHeight().Padding(8)[ MakeNavigationSection() ];
        if (FDiggerFeatureFlags::bEnableWorklight) EnvContent->AddSlot().AutoHeight().Padding(8)[ MakeWorklightSection() ];
        if (FDiggerFeatureFlags::bEnableIslands) EnvContent->AddSlot().AutoHeight().Padding(8)[ MakeIslandsSection() ];
        if (FDiggerFeatureFlags::bEnableMaterialManager) EnvContent->AddSlot().AutoHeight().Padding(8)[ MakeMaterialManagerSection() ];

        
        // Always add Refresh button
        EnvContent->AddSlot().AutoHeight().Padding(4)
        [
            SNew(SButton).Text(FText::FromString("Refresh Landscape Cache"))
            .ToolTipText(FText::FromString(TEXT("Rebuilds the cached landscape height data.\nUse this after modifying the landscape outside of Digger.")))
            .OnClicked_Lambda([this](){ if(Manager) Manager->RefreshLandscapeCache(); return FReply::Handled(); })
        ];

        MainBox->AddSlot().AutoHeight().Padding(8, 12, 8, 4)
        [
            SNew(SExpandableArea).AreaTitle(FText::FromString(TEXT("Environment"))).BodyContent()[ EnvContent ]
        ];
    }

    // 3. MESH GENERATION (Gated by bEnableGenerationSection)
    if (FDiggerFeatureFlags::bEnableGenerationSection)
    {
        MainBox->AddSlot().AutoHeight().Padding(8, 12, 8, 4)
        [
            MakeGenerationSection()
        ];
    }

    // 4. ADDITIONAL TOOLS (Gated)
    if (FDiggerFeatureFlags::bEnableAdditionalTools)
    {
        MainBox->AddSlot().AutoHeight().Padding(8, 12, 8, 4)
        [
            MakeAdditionalToolsSection()
        ];
    }

    // 5. EXPORT & DATA (Gated)
    if (FDiggerFeatureFlags::bEnableExportData)
    {
        MainBox->AddSlot().AutoHeight().Padding(8, 12, 8, 4)
        [
            SNew(SExpandableArea).AreaTitle(FText::FromString(TEXT("Export & Data"))).BodyContent()
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(8)[ MakeBuildExportSection() ]
                + SVerticalBox::Slot().AutoHeight().Padding(8)[ MakeSaveLoadSection() ]
                + SVerticalBox::Slot().AutoHeight().Padding(8)[ MakeResetDiggerDataWidget() ]
            ]
        ];
        MainBox->AddSlot().AutoHeight().Padding(8, 12, 8, 4)
        [
            SNew(SExpandableArea)
            .AreaTitle(FText::FromString(TEXT("Hole Optimization")))
            .InitiallyCollapsed(true)
            .BodyContent()[ MakeHoleOptimizationSection() ]
        ];
    }

    // 6. DEVELOPER SETTINGS (Editor Only)
    #if WITH_EDITOR && !UE_BUILD_SHIPPING
    if (FDiggerFeatureFlags::bEnableDeveloperSettings)
    {
        MainBox->AddSlot().AutoHeight().Padding(4)
        [
            SNew(SExpandableArea).AreaTitle(FText::FromString("Developer Settings")).InitiallyCollapsed(true).BodyContent()
            [
                SNew(SVerticalBox)
                //+ SVerticalBox::Slot().AutoHeight().Padding(2)[ SNew(SCheckBox).IsChecked_Lambda([](){ return FDiggerFeatureFlags::bEnableSplineBrush ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }).OnCheckStateChanged_Lambda([](ECheckBoxState S){ FDiggerFeatureFlags::bEnableSplineBrush = (S==ECheckBoxState::Checked); })[ SNew(STextBlock).Text(FText::FromString("Enable Spline Brush")) ] ]

                + SVerticalBox::Slot().AutoHeight().Padding(2)
                [
                    SNew(SCheckBox)
                    .IsChecked_Lambda([]()
                    {
                        // READ from Settings
                        const UDiggerEditorSettings* Settings = UDiggerEditorSettings::Get();
                        return (Settings && Settings->bShowDynamicHolesFolder)
                                   ? ECheckBoxState::Checked
                                   : ECheckBoxState::Unchecked;
                    })
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState State)
                    {
                        // 1. Calculate new boolean
                        bool bNewVisible = (State == ECheckBoxState::Checked);

                        // 2. WRITE to Settings & Save Config
                        UDiggerEditorSettings* Settings = GetMutableDefault<UDiggerEditorSettings>();
                        if (Settings)
                        {
                            Settings->bShowDynamicHolesFolder = bNewVisible;
                            Settings->SaveConfig(); // <--- This writes to DefaultEditor.ini / User settings
                        }

                        // 3. Apply the logic immediately
                        SetDynamicHolesFolderVisible(bNewVisible);
                    })
                    [
                        SNew(STextBlock).Text(FText::FromString("Show Dynamic Holes Folder"))
                    ]
                ]

            ]
        ];
    }
    #endif

    // 7. DEBUG FLAGS (developer-only)
    if (FDiggerFeatureFlags::bEnableDeveloperSettings)
    {
        MainBox->AddSlot().AutoHeight().Padding(4)
        [
            SNew(SExpandableArea).InitiallyCollapsed(true).HeaderContent()[ SNew(STextBlock).Text(FText::FromString("Debug Flags")) ]
            .BodyContent()[ SAssignNew(DebugFlagListContainer, SVerticalBox) ]
        ];
    }

    ToolkitWidget = MainBox;

    // Final Init Logic
    ScanCustomBrushFolder();
    if (CustomBrushGrid.IsValid()) RebuildCustomBrushGrid();
    LoadDMMState();
    RefreshSaveFilesList();
    if (DebugFlagListContainer.IsValid())
    {
        const FDiggerDebug::FFlagList& Flags = DiggerDebug::GetAllFlags();
        for (const FDiggerDebug::FFlagEntry& FlagEntry : Flags)
        {
            DebugFlagListContainer->AddSlot().AutoHeight().Padding(1.0f)[ MakeDebugCheckbox(FlagEntry) ];
        }
    }

    FModeToolkit::Init(InitToolkitHost);
}

// -----------------------------------------------------------------------------------
// NEW UI HELPER FOR ROLL-DOWNS
// -----------------------------------------------------------------------------------

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeRollDownHeader(const FString& Label, bool& bToggleFlag)
{
    return SNew(SButton)
        .OnClicked_Lambda([&bToggleFlag]()
        { 
            bToggleFlag = !bToggleFlag; 
            return FReply::Handled(); 
        })
        [
            SNew(STextBlock)
            .Text_Lambda([&bToggleFlag, Label]()
            {
                return FText::FromString(bToggleFlag ? TEXT("▼ ") + Label : TEXT("► ") + Label);
            })
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
        ];
}

// -----------------------------------------------------------------------------------
// 1. BRUSH TOOLS IMPLEMENTATION
// -----------------------------------------------------------------------------------

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeBrushShapeSection()
{
    if (!FDiggerFeatureFlags::bEnableBrushShapes) return SNew(SBox).Visibility(EVisibility::Collapsed);

    struct FBrushTypeInfo { EVoxelBrushType Type; FString Label; };
    
    TArray<FBrushTypeInfo> EnabledBrushes;

    auto AddIfEnabled = [&](EVoxelBrushType Type, const FString& Label)
    {
        if (IsBrushEnabled(Type))
        {
            EnabledBrushes.Add({ Type, Label });
        }
    };

    // Add brushes in the order you want them to appear
    AddIfEnabled(EVoxelBrushType::Sphere,   TEXT("Sphere"));
    AddIfEnabled(EVoxelBrushType::Cube,     TEXT("Cube"));
    AddIfEnabled(EVoxelBrushType::Cylinder, TEXT("Cylinder"));
    AddIfEnabled(EVoxelBrushType::Capsule,  TEXT("Capsule"));
    AddIfEnabled(EVoxelBrushType::Cone,     TEXT("Cone"));
    AddIfEnabled(EVoxelBrushType::Torus,    TEXT("Torus"));
    AddIfEnabled(EVoxelBrushType::Pyramid,  TEXT("Pyramid"));
    AddIfEnabled(EVoxelBrushType::Icosphere,TEXT("Icosphere"));
    AddIfEnabled(EVoxelBrushType::Stairs,   TEXT("Stairs"));
    AddIfEnabled(EVoxelBrushType::Custom,   TEXT("Custom"));
    AddIfEnabled(EVoxelBrushType::Smooth,   TEXT("Smooth"));
    AddIfEnabled(EVoxelBrushType::Noise,    TEXT("Noise"));
    AddIfEnabled(EVoxelBrushType::Light,    TEXT("Light"));
    AddIfEnabled(EVoxelBrushType::Debug,    TEXT("Debug"));


    TSharedRef<SUniformGridPanel> ButtonGrid = SNew(SUniformGridPanel).SlotPadding(FMargin(2.0f));
    for (int32 i = 0; i < EnabledBrushes.Num(); ++i)
    {
        const auto& Info = EnabledBrushes[i];
        ButtonGrid->AddSlot(i % 3, i / 3)
        [
            SNew(SCheckBox)
            .Style(FAppStyle::Get(), "RadioButton")
            .IsChecked_Lambda([this, Info]() { return (CurrentBrushType == Info.Type) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this, Info](ECheckBoxState State)
            {
                if (State == ECheckBoxState::Checked)
                {
                    CurrentBrushType = Info.Type;
                    if (Manager)
                    {
                        Manager->EditorBrushType = Info.Type;
                    }

                    // ⬇️ Add this block right here
                    if (CurrentBrushType == EVoxelBrushType::Smooth)
                    {
                        const UDiggerEditorSettings* Settings = UDiggerEditorSettings::Get();
                        bSmoothLandscapeAware = Settings->bSmoothBrushLandscapeAware;

                        // Push to manager too
                        if (Manager)
                        {
                            Manager->bSmoothLandscapeAware = bSmoothLandscapeAware;
                        }
                    }
                }
            })
            [
                SNew(STextBlock).Text(FText::FromString(Info.Label))
            ]
        ];
    }

    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(4)
        [
            MakeRollDownHeader("Brush Shape", bShowBrushShapeSection)
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(4)
        [
            SNew(SVerticalBox).Visibility_Lambda([this](){ return bShowBrushShapeSection ? EVisibility::Visible : EVisibility::Collapsed; })
            + SVerticalBox::Slot().AutoHeight()[ ButtonGrid ]
            
            // Light Type Specifics
            + SVerticalBox::Slot().AutoHeight().Padding(4)
            [
                SNew(SBox).Visibility_Lambda([this](){ return CurrentBrushType == EVoxelBrushType::Light ? EVisibility::Visible : EVisibility::Collapsed; })
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(SComboBox<TSharedPtr<ELightBrushType>>)
                        .OptionsSource(&LightTypeOptions)
                        .OnGenerateWidget(this, &FDiggerEdModeToolkit::MakeLightTypeComboWidget)
                        .OnSelectionChanged(this, &FDiggerEdModeToolkit::OnLightTypeChanged)
                        .InitiallySelectedItem(LightTypeOptions[0])
                        [
                            SNew(STextBlock).Text_Lambda([this](){ return FText::FromString(StaticEnum<ELightBrushType>()->GetDisplayNameTextByValue((int64)CurrentLightType).ToString()); })
                        ]
                    ]
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(SColorBlock)
                        .Color_Lambda([this](){ return BrushLightColor; })
                        .OnMouseButtonDown_Lambda([this](const FGeometry&, const FPointerEvent&){
                            FColorPickerArgs Args; Args.bUseAlpha = false; Args.InitialColor = BrushLightColor;
                            Args.OnColorCommitted = FOnLinearColorValueChanged::CreateLambda([this](FLinearColor C){ BrushLightColor = C; OnLightColorChanged(C); });
                            OpenColorPicker(Args); return FReply::Handled();
                        })
                    ]
                ]
            ]
            // Smooth Brush Options
            + SVerticalBox::Slot().AutoHeight().Padding(4)
            [
                SNew(SBox)
                .Visibility_Lambda([this]()
                {
                    return CurrentBrushType == EVoxelBrushType::Smooth
                               ? EVisibility::Visible
                               : EVisibility::Collapsed;
                })
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(SCheckBox)
                        .OnCheckStateChanged_Lambda([this](ECheckBoxState State)
                        {
                            bSmoothLandscapeAware = (State == ECheckBoxState::Checked);

                            // Save to settings
                            UDiggerEditorSettings* Settings = GetMutableDefault<UDiggerEditorSettings>();
                            Settings->bSmoothBrushLandscapeAware = bSmoothLandscapeAware;
                            Settings->SaveConfig();

                            // Push to manager
                            if (Manager)
                            {
                                Manager->bSmoothLandscapeAware = bSmoothLandscapeAware;
                            }
                        })
                        .IsChecked_Lambda([this]()
                        {
                            return bSmoothLandscapeAware
                                       ? ECheckBoxState::Checked
                                       : ECheckBoxState::Unchecked;
                        })
                        [
                            SNew(STextBlock).Text(FText::FromString("Landscape Aware Smoothing"))
                        ]
                    ]
                ]
            ]
            + SVerticalBox::Slot().AutoHeight()[ MakeOperationSection() ]
        ];
}

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeBrushParameterSection()
{
    return SNew(SVerticalBox)
    + SVerticalBox::Slot().AutoHeight().Padding(4)
    [
        MakeRollDownHeader("Brush Parameters", bShowBrushParameters)
    ]
    + SVerticalBox::Slot().AutoHeight().Padding(4)
    [
        SNew(SVerticalBox)
.Visibility_Lambda([this](){ return bShowBrushParameters ? EVisibility::Visible : EVisibility::Collapsed; })

// --------------------
// RADIUS
// --------------------
+ SVerticalBox::Slot().AutoHeight()
[
    MakeLabeledSliderRow(
        FText::FromString("Radius"),
        [this](){ return BrushRadius; },
        [this](float V)
        {
            BrushRadius = V;

            if (FDiggerEdMode* Mode = (FDiggerEdMode*)GLevelEditorModeTools()
                    .GetActiveMode(FDiggerEdMode::EM_DiggerEdModeId))
            {
                Mode->BrushCache.Radius = V;
                Mode->UpdateBrushHUDPanel();
            }
        },
        10.f, 1000.f, {50.f, 100.f, 500.f}
    )
]

// --------------------
// STRENGTH
// --------------------
+ SVerticalBox::Slot().AutoHeight()
[
    MakeLabeledSliderRow(
        FText::FromString("Strength"),
        [this](){ return BrushStrength; },
        [this](float V)
        {
            BrushStrength = V;

            if (FDiggerEdMode* Mode = (FDiggerEdMode*)GLevelEditorModeTools()
                    .GetActiveMode(FDiggerEdMode::EM_DiggerEdModeId))
            {
                Mode->BrushCache.Strength = V;
                Mode->UpdateBrushHUDPanel();
            }
        },
        0.f, 1.f, {0.1f, 0.5f, 1.f}
    )
]

// --------------------
// FALLOFF
// --------------------
+ SVerticalBox::Slot().AutoHeight()
[
    MakeLabeledSliderRow(
        FText::FromString("Falloff"),
        [this](){ return BrushFalloff; },
        [this](float V)
        {
            BrushFalloff = V;

            if (FDiggerEdMode* Mode = (FDiggerEdMode*)GLevelEditorModeTools()
                    .GetActiveMode(FDiggerEdMode::EM_DiggerEdModeId))
            {
                Mode->BrushCache.Falloff = V;
                Mode->UpdateBrushHUDPanel();
            }
        },
        0.f, 1.f, {0.1f, 0.5f, 1.f}
    )
]

// --------------------
// FORCE
// --------------------
+ SVerticalBox::Slot().AutoHeight()
[
    MakeLabeledSliderRow(
        FText::FromString("Force"),
        [this](){ return BrushForce; },
        [this](float V)
        {
            BrushForce = V;

            if (FDiggerEdMode* Mode = (FDiggerEdMode*)GLevelEditorModeTools()
                    .GetActiveMode(FDiggerEdMode::EM_DiggerEdModeId))
            {
                Mode->BrushCache.Force = V;
                Mode->UpdateBrushHUDPanel();
            }
        },
        0.f, 1.f, {0.1f, 0.5f, 1.f}
    )
]
        // --------------------
        // FORCE MODE Dropdown
        // --------------------
        + SVerticalBox::Slot().AutoHeight().Padding(4)
        [
            SNew(SHorizontalBox)

            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(FText::FromString("Push Mode"))
            ]

            + SHorizontalBox::Slot().FillWidth(1.f).Padding(8,0)
            [
                SNew(SComboBox<TSharedPtr<EDiggerPushMode>>)
                .OptionsSource(&PushModeOptions)
                .OnGenerateWidget_Lambda([](TSharedPtr<EDiggerPushMode> Mode)
                {
                    return SNew(STextBlock)
                        .Text(FText::FromString(
                            StaticEnum<EDiggerPushMode>()->GetNameStringByValue((int64)*Mode)));
                })
                .OnSelectionChanged_Lambda([this](TSharedPtr<EDiggerPushMode> Mode, ESelectInfo::Type)
                {
                    if (Mode.IsValid())
                        CurrentPushMode = *Mode;
                })
                [
                    SNew(STextBlock)
                    .Text_Lambda([this]()
                    {
                        return FText::FromString(
                            StaticEnum<EDiggerPushMode>()->GetNameStringByValue((int64)CurrentPushMode));
                    })
                ]
            ]
        ]

    ];
}

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeCustomBrushSection()
{
    if (!FDiggerFeatureFlags::bEnableCustomBrushes) return SNew(SBox).Visibility(EVisibility::Collapsed);

    return SNew(SVerticalBox)
    + SVerticalBox::Slot().AutoHeight().Padding(4)
    [
        MakeRollDownHeader("Custom Brushes", bShowCustomBrushSection)
    ]
    + SVerticalBox::Slot().AutoHeight().Padding(4)
    [
        SNew(SVerticalBox).Visibility_Lambda([this](){ return bShowCustomBrushSection ? EVisibility::Visible : EVisibility::Collapsed; })
        + SVerticalBox::Slot().AutoHeight().Padding(4)[ SAssignNew(CustomBrushGridContainer, SVerticalBox) + SVerticalBox::Slot().AutoHeight()[ SAssignNew(CustomBrushGrid, SUniformGridPanel) ] ]
        + SVerticalBox::Slot().AutoHeight().Padding(4)
        [
            SNew(SButton).Text(FText::FromString("Add Custom Brush")).OnClicked_Lambda([this](){ 
                FContentBrowserModule& CB = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
                FOpenAssetDialogConfig Cfg; Cfg.DialogTitleOverride = FText::FromString("Select Mesh"); Cfg.AssetClassNames.Add(FTopLevelAssetPath("/Script/Engine", "StaticMesh")); Cfg.bAllowMultipleSelection = true;
                TArray<FAssetData> Assets = CB.Get().CreateModalOpenAssetDialog(Cfg);
                for(auto& A : Assets) { 
                    FCustomBrushEntry E; E.Mesh = Cast<UStaticMesh>(A.GetAsset()); 
                    E.Thumbnail = MakeShareable(new FAssetThumbnail(A, 64, 64, AssetThumbnailPool));
                    CustomBrushEntries.Add(E); 
                }
                RebuildCustomBrushGrid(); return FReply::Handled(); 
            })
        ]
    ];
}

// -----------------------------------------------------------------------------------
// 2. ENVIRONMENT SECTION IMPLEMENTATION (New Flags)
// -----------------------------------------------------------------------------------

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeNavigationSection()
{
    if (!FDiggerFeatureFlags::bEnableNavigation) return SNew(SBox).Visibility(EVisibility::Collapsed);

    return SNew(SVerticalBox)
    + SVerticalBox::Slot().AutoHeight().Padding(4)
    [
        MakeRollDownHeader("Navigation", bNavigationSection)
    ]
    + SVerticalBox::Slot().AutoHeight().Padding(4)
    [
        SNew(SVerticalBox).Visibility_Lambda([this](){ return bNavigationSection ? EVisibility::Visible : EVisibility::Collapsed; })
        + SVerticalBox::Slot().AutoHeight().Padding(2)
        [
            SNew(STextBlock).Text_Lambda([this](){ 
                FVector Loc = CachedViewportClient ? CachedViewportClient->GetViewLocation() : FVector::Zero(); 
                return FText::Format(FText::FromString("Abs Elev: {0}"), FText::AsNumber((int)Loc.Z)); 
            })
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(2)
        [
            SNew(STextBlock).Text_Lambda([this](){ 
                float Abs, Rel; GetElevationInfo(Abs, Rel);
                return FText::Format(FText::FromString("Rel Elev: {0}"), FText::AsNumber((int)Rel)); 
            })
        ]
    ];
}

// -----------------------------------------------------------------------------------
// WORKLIGHT & LIGHTING SECTION
// -----------------------------------------------------------------------------------

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeWorklightSection()
{
    if (!FDiggerFeatureFlags::bEnableWorklight) return SNew(SBox).Visibility(EVisibility::Collapsed);

    return SNew(SVerticalBox)
    + SVerticalBox::Slot().AutoHeight().Padding(4)
    [
        MakeRollDownHeader("Lighting Tools", bShowWorklightSection)
    ]
    + SVerticalBox::Slot().AutoHeight().Padding(4)
    [
        SNew(SVerticalBox).Visibility_Lambda([this](){ return bShowWorklightSection ? EVisibility::Visible : EVisibility::Collapsed; })
        
        // --- SCENE WORKLIGHT ---
        + SVerticalBox::Slot().AutoHeight().Padding(4)
        [
            SNew(STextBlock).Text(FText::FromString("Scene Worklight")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(4, 2)
        [
            SNew(SComboBox<TSharedPtr<FString>>).OptionsSource(&WorklightTypeOptions)
            .OnGenerateWidget_Lambda([](TSharedPtr<FString> S){ return SNew(STextBlock).Text(FText::FromString(*S)); })
            .OnSelectionChanged_Lambda([this](TSharedPtr<FString> S, ESelectInfo::Type){ if(S){ SelectedWorkLightType = *S; UpdateWorklightType(SelectedWorkLightType); } })
            [ SNew(STextBlock).Text_Lambda([this](){ return FText::FromString(SelectedWorkLightType); }) ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(4, 2)
        [
            MakeLabeledSliderRow(FText::FromString("Intensity"), 
                [this](){ return WorklightIntensity / 5000.f; }, 
                [this](float V){ UpdateWorklightIntensity(V * 5000.f); }, 
                0.f, 20.f, {1.f, 5.f, 10.f}) // Scaled for UI usability
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(4, 2)
        [
            MakeLabeledSliderRow(FText::FromString("Radius"), 
                [this](){ return WorklightAttenuation; }, 
                [this](float V){ UpdateWorklightAttenuation(V); }, 
                100.f, 10000.f, {1000.f, 3000.f, 5000.f})
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(4, 2)
        [
            SNew(SCheckBox).IsChecked_Lambda([this](){ return bWorklightEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this](ECheckBoxState S){ ToggleWorklight(S==ECheckBoxState::Checked); })
            [ SNew(STextBlock).Text(FText::FromString("Enable Scene Light")) ]
        ]

        // Camera Follows Brush — L key toggles.
        // When ON the viewport mouse capture tracks the brush during painting,
        // keeping the brush centred in view (useful for deep tunnel work).
        // When OFF the viewport stays stationary and only the brush preview moves,
        // matching the behaviour of ZBrush's Free Rotation mode.
        + SVerticalBox::Slot().AutoHeight().Padding(4, 2)
        [
            SNew(SCheckBox)
            .IsChecked_Lambda([this]()
            {
                return bCameraFollowsBrush
                    ? ECheckBoxState::Checked
                    : ECheckBoxState::Unchecked;
            })
            .OnCheckStateChanged_Lambda([this](ECheckBoxState S)
            {
                bCameraFollowsBrush = (S == ECheckBoxState::Checked);
                // Persist to editor config so the choice survives restarts.
                if (UDiggerEditorSettings* MutableSettings =
                        GetMutableDefault<UDiggerEditorSettings>())
                {
                    MutableSettings->bCameraFollowsBrush = bCameraFollowsBrush;
                    MutableSettings->SaveConfig();
                }
            })
            .ToolTipText(FText::FromString(
                "Camera Follows Brush\n"
                "When enabled the viewport tracks the brush during painting.\n"
                "Toggle with  L  (Lock/Unlock camera)."))
            [
                SNew(STextBlock)
                .Text(FText::FromString("Camera Follows Brush  [L]"))
            ]
        ]

        // --- SEPARATOR ---
        + SVerticalBox::Slot().AutoHeight().Padding(0, 8)[ SNew(SSeparator).Orientation(Orient_Horizontal) ]

        // --- BRUSH LIGHT ---
        + SVerticalBox::Slot().AutoHeight().Padding(4)
        [
            SNew(STextBlock).Text(FText::FromString("Brush Preview Light")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(4, 2)
        [
            MakeLabeledSliderRow(FText::FromString("Intensity"), 
                [this](){ return BrushLightIntensity; }, 
                [this](float V){ 
                    BrushLightIntensity = V; 
                    // Update Settings Object immediately for Live Preview
                    if (UDiggerEditorSettings* S = GetMutableDefault<UDiggerEditorSettings>()) S->BrushLightIntensity = V;
                }, 
                0.f, 10000.f, {1500.f, 3000.f, 5000.f})
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(4, 2)
        [
            MakeLabeledSliderRow(FText::FromString("Radius"), 
                [this](){ return BrushLightAttenuation; }, 
                [this](float V){ 
                    BrushLightAttenuation = V; 
                    if (UDiggerEditorSettings* S = GetMutableDefault<UDiggerEditorSettings>()) S->BrushLightAttenuationRadius = V;
                }, 
                100.f, 5000.f, {1000.f, 2000.f})
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(4, 2)
        [
            SNew(SCheckBox)
            .IsChecked_Lambda([this](){ return bMatchBrushLightColor ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this](ECheckBoxState S){ 
                bMatchBrushLightColor = (S == ECheckBoxState::Checked); 
                if (UDiggerEditorSettings* Set = GetMutableDefault<UDiggerEditorSettings>()) Set->bMatchLightColorToBrush = bMatchBrushLightColor;
            })
            [ SNew(STextBlock).Text(FText::FromString("Match Brush Mode Color (Red/Green)")) ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(4, 2)
        [
            SNew(SHorizontalBox)
            .Visibility_Lambda([this](){ return bMatchBrushLightColor ? EVisibility::Collapsed : EVisibility::Visible; })
            + SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0).VAlign(VAlign_Center)
            [ SNew(STextBlock).Text(FText::FromString("Custom Color")) ]
            + SHorizontalBox::Slot().AutoWidth()
            [
                SNew(SColorBlock)
                .Color_Lambda([this](){ return BrushLightColor; })
                .ShowBackgroundForAlpha(false)
                .OnMouseButtonDown_Lambda([this](const FGeometry&, const FPointerEvent&){
                    FColorPickerArgs Args; 
                    Args.bUseAlpha = false; 
                    Args.InitialColor = BrushLightColor;
                    Args.OnColorCommitted = FOnLinearColorValueChanged::CreateLambda([this](FLinearColor C){ 
                        BrushLightColor = C; 
                        if (UDiggerEditorSettings* S = GetMutableDefault<UDiggerEditorSettings>()) S->BrushLightColor = C;
                    });
                    OpenColorPicker(Args); 
                    return FReply::Handled();
                })
            ]
        ]

        // --- SAVE BUTTON ---
        + SVerticalBox::Slot().AutoHeight().Padding(4, 10, 4, 4)
        [
            SNew(SButton)
            .HAlign(HAlign_Center)
            .Text(FText::FromString("Update Settings (Save to Config)"))
            .ToolTipText(FText::FromString("Saves current Worklight and Brush Light settings to Project Settings so they persist."))
            .OnClicked_Lambda([this]() {
                SaveLightingSettings();
                return FReply::Handled();
            })
        ]
    ];
}

void FDiggerEdModeToolkit::SaveLightingSettings()
{
    UDiggerEditorSettings* Settings = GetMutableDefault<UDiggerEditorSettings>();
    if (Settings)
    {
        // 1. Save Brush Light Settings
        Settings->BrushLightIntensity = BrushLightIntensity;
        Settings->BrushLightAttenuationRadius = BrushLightAttenuation;
        Settings->BrushLightColor = BrushLightColor;
        Settings->bMatchLightColorToBrush = bMatchBrushLightColor;

        // 2. Save Scene Worklight Settings
        // (Assuming you add these fields to DiggerEditorSettings.h, if not, only Brush settings will save)
        // If you want Worklight settings to save, add them to UDiggerEditorSettings class first!
        // For now, we update config.
        
        Settings->SaveConfig();
        
        // Notify user
        FNotificationInfo Info(FText::FromString("Lighting Settings Saved"));
        Info.ExpireDuration = 2.0f;
        FSlateNotificationManager::Get().AddNotification(Info);
    }
}

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeIslandsSection()
{
    if (!FDiggerFeatureFlags::bEnableIslands) return SNew(SBox).Visibility(EVisibility::Collapsed);

    // Float policy display names for the combo box.
    static const TArray<TSharedPtr<FString>> FloatPolicyOptions = {
        MakeShared<FString>(TEXT("Ignore")),
        MakeShared<FString>(TEXT("Auto Remove")),
        MakeShared<FString>(TEXT("To Physics")),
        MakeShared<FString>(TEXT("To Static"))
    };

    return SNew(SVerticalBox)
    + SVerticalBox::Slot().AutoHeight().Padding(4)
    [
        MakeRollDownHeader("Islands", bShowIslandsSection)
    ]
    + SVerticalBox::Slot().AutoHeight().Padding(4)
    [
        SNew(SVerticalBox).Visibility_Lambda([this](){ return bShowIslandsSection ? EVisibility::Visible : EVisibility::Collapsed; })

        // --- Island config controls ---
        + SVerticalBox::Slot().AutoHeight().Padding(2)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2)
            [
                SNew(SButton)
                .Text(FText::FromString("Detect Islands"))
                .ToolTipText(FText::FromString(TEXT("Scans all loaded chunks for disconnected floating voxel clusters.\nResults appear in the island list below.")))
                .OnClicked_Lambda([this]()
                {
                    if (IsValid(Manager))
                    {
                        if (UWorld* W = Manager->GetWorld())
                        {
                            if (auto* Sys = W->GetSubsystem<UDiggerIslandRuntimeSubsystem>())
                            {
                                Sys->ScanIslands(Manager);
                            }
                        }
                    }
                    return FReply::Handled();
                })
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)
            [
                SNew(STextBlock).Text(FText::FromString("Min Voxels:"))
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [
                SNew(SSpinBox<int32>)
                .MinValue(0)
                .MaxValue(10000)
                .MinDesiredWidth(60)
                .Value_Lambda([this]() -> int32
                {
                    if (IsValid(Manager))
                        if (UWorld* W = Manager->GetWorld())
                            if (auto* Sys = W->GetSubsystem<UDiggerIslandRuntimeSubsystem>())
                                return Sys->AutoCleanupMinVoxels;
                    return 0;
                })
                .OnValueCommitted_Lambda([this](int32 NewVal, ETextCommit::Type)
                {
                    if (IsValid(Manager))
                        if (UWorld* W = Manager->GetWorld())
                            if (auto* Sys = W->GetSubsystem<UDiggerIslandRuntimeSubsystem>())
                                Sys->AutoCleanupMinVoxels = NewVal;
                })
            ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(2)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2)
            [
                SNew(STextBlock).Text(FText::FromString("Float Policy:"))
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [
                SNew(SComboBox<TSharedPtr<FString>>)
                .OptionsSource(&FloatPolicyOptions)
                .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
                {
                    return SNew(STextBlock).Text(FText::FromString(*Item));
                })
                .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type)
                {
                    if (!Item.IsValid() || !IsValid(Manager)) return;
                    if (UWorld* W = Manager->GetWorld())
                    {
                        if (auto* Sys = W->GetSubsystem<UDiggerIslandRuntimeSubsystem>())
                        {
                            if (*Item == TEXT("Ignore"))            Sys->FloatPolicy = EIslandFloatPolicy::Ignore;
                            else if (*Item == TEXT("Auto Remove"))  Sys->FloatPolicy = EIslandFloatPolicy::AutoRemove;
                            else if (*Item == TEXT("To Physics"))   Sys->FloatPolicy = EIslandFloatPolicy::ConvertToPhysics;
                            else if (*Item == TEXT("To Static"))    Sys->FloatPolicy = EIslandFloatPolicy::ConvertToStatic;
                        }
                    }
                })
                [
                    SNew(STextBlock).Text_Lambda([this]() -> FText
                    {
                        if (IsValid(Manager))
                            if (UWorld* W = Manager->GetWorld())
                                if (auto* Sys = W->GetSubsystem<UDiggerIslandRuntimeSubsystem>())
                                {
                                    switch (Sys->FloatPolicy)
                                    {
                                    case EIslandFloatPolicy::Ignore:          return FText::FromString("Ignore");
                                    case EIslandFloatPolicy::AutoRemove:      return FText::FromString("Auto Remove");
                                    case EIslandFloatPolicy::ConvertToPhysics:return FText::FromString("To Physics");
                                    case EIslandFloatPolicy::ConvertToStatic: return FText::FromString("To Static");
                                    }
                                }
                        return FText::FromString("Ignore");
                    })
                ]
            ]
        ]

        // --- Existing action buttons ---
        + SVerticalBox::Slot().AutoHeight().Padding(2)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth()[ SNew(SButton).Text(FText::FromString("Physics")).OnClicked_Lambda([this](){ OnConvertToPhysicsActorClicked(); return FReply::Handled(); }) ]
            + SHorizontalBox::Slot().AutoWidth()[ SNew(SButton).Text(FText::FromString("Static Mesh")).OnClicked_Lambda([this](){ OnConvertToSceneActorClicked(); return FReply::Handled(); }) ]
            + SHorizontalBox::Slot().AutoWidth()[ SNew(SButton).Text(FText::FromString("Remove")).OnClicked_Lambda([this](){ OnRemoveIslandClicked(); return FReply::Handled(); }) ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(4)
        [
            SAssignNew(IslandGridContainer, SBox)[ MakeIslandGridWidget() ]
        ]
    ];
}

// -----------------------------------------------------------------------------------
// 3. ADDITIONAL TOOLS
// -----------------------------------------------------------------------------------

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeAdditionalToolsSection()
{
    if (!FDiggerFeatureFlags::bEnableAdditionalTools) return SNew(SBox).Visibility(EVisibility::Collapsed);
    
    return SNew(SExpandableArea)
           .AreaTitle(FText::FromString(TEXT("Additional Tools")))
           .InitiallyCollapsed(true)
           .BodyContent()
           [
               SNew(SVerticalBox)
               + SVerticalBox::Slot().AutoHeight().Padding(8)
               [
                   MakeProcgenArcanaImporterWidget()
               ]
               + SVerticalBox::Slot().AutoHeight().Padding(8, 12, 8, 4)
               [
                   MakeLobbySection() 
               ]
           ];
}

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeProcgenArcanaImporterWidget()
{
    if (!FDiggerFeatureFlags::bEnableCaveImporter) return SNew(SBox).Visibility(EVisibility::Collapsed);
    ADiggerManager* Mgr = GetDiggerManager();
    SAssignNew(CaveImporterWidget, SDiggerCaveImporterWidget, Mgr);
    return SNew(SExpandableArea).AreaTitle(FText::FromString("Cave Import (ProcgenArcana)")).BodyContent()[ CaveImporterWidget.ToSharedRef() ];
}

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeLobbySection()
{
    if (!FDiggerFeatureFlags::bEnableAdditionalTools) return SNew(SBox).Visibility(EVisibility::Collapsed);
    SAssignNew(LobbyWidget, SDiggerLobbyWidget);
    return SNew(SExpandableArea).AreaTitle(FText::FromString("DiggerConnect")).BodyContent()[ LobbyWidget.ToSharedRef() ];
}

// -----------------------------------------------------------------------------------
// 4. EXPORT & DATA RESTORATION
// -----------------------------------------------------------------------------------

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeBuildExportSection()
{
    if (!FDiggerFeatureFlags::bEnableBuild) return SNew(SBox).Visibility(EVisibility::Collapsed);
    
    return SNew(SVerticalBox)
    + SVerticalBox::Slot().AutoHeight().Padding(4)
    [
        MakeRollDownHeader("Build Settings", bShowBuildSettings)
    ]
    + SVerticalBox::Slot().AutoHeight().Padding(4)
    [
        SNew(SVerticalBox).Visibility_Lambda([this](){ return bShowBuildSettings ? EVisibility::Visible : EVisibility::Collapsed; })
        + SVerticalBox::Slot().AutoHeight()[ SNew(SCheckBox).IsChecked_Lambda([this](){ return bEnableCollision ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }).OnCheckStateChanged_Lambda([this](ECheckBoxState S){ bEnableCollision = (S == ECheckBoxState::Checked); })[ SNew(STextBlock).Text(FText::FromString("Enable Collision")) ] ]
        + SVerticalBox::Slot().AutoHeight()[ SNew(SCheckBox).IsChecked_Lambda([this](){ return bEnableNanite ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }).OnCheckStateChanged_Lambda([this](ECheckBoxState S){ bEnableNanite = (S == ECheckBoxState::Checked); })[ SNew(STextBlock).Text(FText::FromString("Enable Nanite")) ] ]
        + SVerticalBox::Slot().AutoHeight()[ SNew(SButton).Text(FText::FromString("Bake Static Mesh")).OnClicked_Lambda([this](){ if(Manager) Manager->BakeToStaticMesh(bEnableCollision, bEnableNanite, BakeDetail); return FReply::Handled(); }) ]
    ];
}

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeSaveLoadSection()
{
    // Feature flag check handled in Init now
    
    return SNew(SVerticalBox)
    + SVerticalBox::Slot().AutoHeight().Padding(4)
    [
        MakeRollDownHeader("Chunk Serialization", bShowSaveLoadSection)
    ]
    + SVerticalBox::Slot().AutoHeight().Padding(4)
    [
        SNew(SVerticalBox).Visibility_Lambda([this](){ return bShowSaveLoadSection ? EVisibility::Visible : EVisibility::Collapsed; })
        
        // Filename Input
        + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,4,0)
            [ SNew(STextBlock).Text(FText::FromString("File:")) ]
            + SHorizontalBox::Slot().FillWidth(1.0f)
            [ 
                SAssignNew(SaveFileNameWidget, SEditableTextBox)
                .Text(FText::FromString("Default"))
                .HintText(FText::FromString("Save Name"))
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(4,0,0,0)
            [
                SNew(SButton).Text(FText::FromString("Refresh")).OnClicked_Lambda([this](){ RefreshSaveFilesList(); return FReply::Handled(); })
            ]
        ]
        
        // Dropdown List
        + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
        [
            SAssignNew(SaveFileComboBox, SComboBox<TSharedPtr<FString>>)
            .OptionsSource(&AvailableSaveFiles)
            .OnGenerateWidget_Lambda([](TSharedPtr<FString> S){ return SNew(STextBlock).Text(FText::FromString(*S)); })
            .OnSelectionChanged_Lambda([this](TSharedPtr<FString> S, ESelectInfo::Type){ if(S) SaveFileNameWidget->SetText(FText::FromString(*S)); })
            [ 
                SNew(STextBlock).Text(FText::FromString("Select Existing File...")) 
            ]
        ]

        // Action Buttons
        + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
            [ 
                SNew(SButton).HAlign(HAlign_Center).Text(FText::FromString("Save")).OnClicked_Lambda([this](){ 
                    if(Manager) { Manager->SaveAllChunks(SaveFileNameWidget->GetText().ToString()); RefreshSaveFilesList(); } 
                    return FReply::Handled(); 
                }) 
            ]
            + SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
            [ 
                SNew(SButton).HAlign(HAlign_Center).Text(FText::FromString("Load")).OnClicked_Lambda([this](){ 
                    if(Manager) Manager->LoadAllChunks(SaveFileNameWidget->GetText().ToString()); 
                    return FReply::Handled(); 
                }) 
            ]
            + SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
            [ 
                SNew(SButton).HAlign(HAlign_Center).Text(FText::FromString("Delete"))
                .ButtonColorAndOpacity(FLinearColor(0.7f, 0.1f, 0.1f)) // Red warning color
                .OnClicked_Lambda([this](){ 
                    FString Name = SaveFileNameWidget->GetText().ToString();
                    if(Manager && !Name.IsEmpty()) {
                         if (EAppReturnType::Yes == FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString("Permanently delete the save file \"" + Name + "\"?\n\nThis cannot be undone."))) {
                             Manager->DeleteSaveFile(Name); 
                             RefreshSaveFilesList();
                         }
                    }
                    return FReply::Handled(); 
                }) 
            ]
        ]
    ];
}

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeResetDiggerDataWidget()
{
    return SNew(SButton)
    .Text(FText::FromString("Clear All Data"))
    .OnClicked(this, &FDiggerEdModeToolkit::OnClearAllClicked)
    .ButtonColorAndOpacity(FLinearColor(0.8f, 0.1f, 0.1f, 1.0f));
}

// -----------------------------------------------------------------------------------
// IMPLEMENTATION HELPERS (Islands, Light, Rotation, etc.)
// -----------------------------------------------------------------------------------

ADiggerManager* FDiggerEdModeToolkit::GetDiggerManager() const
{
    if (Manager && IsValid(Manager)) return Manager;
    if (GEditor)
    {
        UWorld* World = GEditor->GetEditorWorldContext().World();
        if(World) return ADiggerManager::FindDiggerManager(World);
    }
    return nullptr;
}

void FDiggerEdModeToolkit::ConnectIslandHub()
{
    auto* Hub = GEditor->GetEditorSubsystem<UDiggerEditorEventHub>();
    if (!Hub) return;

    // Disconnect stale handles before re-binding.
    DisconnectIslandHub();

    // Connect the hub to the current manager and world subsystem.
    Hub->ConnectToManager(Manager);
    if (IsValid(Manager))
    {
        if (UWorld* W = Manager->GetWorld())
        {
            Hub->ConnectToIslandSubsystem(W->GetSubsystem<UDiggerIslandRuntimeSubsystem>());
        }
    }

    // Bind toolkit UI callbacks to hub's editor-side delegates.
    Handle_HubScanStarted = Hub->OnIslandScanStartedEditor.AddSP(
        this, &FDiggerEdModeToolkit::ClearIslands);
    Handle_HubIslandDetected = Hub->OnIslandDetectedEditor.AddSP(
        this, &FDiggerEdModeToolkit::AddIsland);
}

void FDiggerEdModeToolkit::DisconnectIslandHub()
{
    auto* Hub = GEditor->GetEditorSubsystem<UDiggerEditorEventHub>();
    if (!Hub) return;
    if (Handle_HubScanStarted.IsValid())
    {
        Hub->OnIslandScanStartedEditor.Remove(Handle_HubScanStarted);
        Handle_HubScanStarted.Reset();
    }
    if (Handle_HubIslandDetected.IsValid())
    {
        Hub->OnIslandDetectedEditor.Remove(Handle_HubIslandDetected);
        Handle_HubIslandDetected.Reset();
    }
}

void FDiggerEdModeToolkit::ClearIslands()
{
    if (!IsInGameThread()) return;
    Islands.Empty();
    SelectedIslandIndex = INDEX_NONE;
    RebuildIslandGrid();
}

void FDiggerEdModeToolkit::OnIslandDetectedHandler(const FIslandData& NewIslandData)
{
    // Pass-through to the main Add function
    AddIsland(NewIslandData);
}

void FDiggerEdModeToolkit::AddIsland(const FIslandData& Island)
{
    Islands.Add(Island);
    RebuildIslandGrid();
}

void FDiggerEdModeToolkit::RebuildIslandGrid()
{
    if (IslandGridContainer.IsValid()) IslandGridContainer->SetContent(MakeIslandGridWidget());
}

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeIslandGridWidget()
{
    if (Islands.Num() == 0) return SNew(STextBlock).Text(FText::FromString("No islands found."));
    TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel);
    for(int32 i=0; i<Islands.Num(); ++i)
    {
        Grid->AddSlot(i%4, i/4)
        [
            SNew(SButton).Text(FText::AsNumber(i)).OnClicked_Lambda([this, i](){ SelectedIslandIndex = i; return FReply::Handled(); })
        ];
    }
    return Grid;
}

// Rotation Helper
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeRotationSection(float& RotX, float& RotY, float& RotZ)
{
    return SNew(SVerticalBox)
    + SVerticalBox::Slot().AutoHeight()
    [
        MakeRollDownHeader("Rotation", bShowRotation)
    ]
    + SVerticalBox::Slot().AutoHeight()
    [
        SNew(SVerticalBox).Visibility_Lambda([this](){ return bShowRotation ? EVisibility::Visible : EVisibility::Collapsed; })
        + SVerticalBox::Slot().AutoHeight()[ MakeRotationRow(FText::FromString("X (Pitch)"), RotX) ]
        + SVerticalBox::Slot().AutoHeight()[ MakeRotationRow(FText::FromString("Y (Yaw)"), RotY) ]
        + SVerticalBox::Slot().AutoHeight()[ MakeRotationRow(FText::FromString("Z (Roll)"), RotZ) ]
        + SVerticalBox::Slot().AutoHeight()[ SNew(SButton).Text(FText::FromString("Reset")).OnClicked_Lambda([&](){ RotX=0; RotY=0; RotZ=0; return FReply::Handled(); }) ]
    ];
}

// Offset Helper
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeOffsetSection(FVector& Offset)
{
    return SNew(SVerticalBox)
    + SVerticalBox::Slot().AutoHeight()
    [
        MakeRollDownHeader("Offset", bShowOffset)
    ]
    + SVerticalBox::Slot().AutoHeight()
    [
        SNew(SVerticalBox).Visibility_Lambda([this](){ return bShowOffset ? EVisibility::Visible : EVisibility::Collapsed; })
        + SVerticalBox::Slot().AutoHeight()[ MakeOffsetRow(FText::FromString("X"), Offset.X) ]
        + SVerticalBox::Slot().AutoHeight()[ MakeOffsetRow(FText::FromString("Y"), Offset.Y) ]
        + SVerticalBox::Slot().AutoHeight()[ MakeOffsetRow(FText::FromString("Z"), Offset.Z) ]
        + SVerticalBox::Slot().AutoHeight()[ SNew(SButton).Text(FText::FromString("Reset")).OnClicked_Lambda([&](){ Offset=FVector::ZeroVector; return FReply::Handled(); }) ]
    ];
}

// Light Type Logic
void FDiggerEdModeToolkit::PopulateLightTypeOptions() { /* Done in Ctor */ }

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeLightTypeComboWidget(TSharedPtr<ELightBrushType> InItem)
{
    FString EnumName = StaticEnum<ELightBrushType>()->GetDisplayNameTextByValue((int64)*InItem).ToString();
    return SNew(STextBlock).Text(FText::FromString(EnumName));
}

void FDiggerEdModeToolkit::OnLightTypeChanged(TSharedPtr<ELightBrushType> NewSelection, ESelectInfo::Type)
{
    if (NewSelection.IsValid())
    {
        CurrentLightType = *NewSelection;
        if(Manager) Manager->EditorBrushLightType = CurrentLightType;
    }
}

void FDiggerEdModeToolkit::OnLightColorChanged(FLinearColor NewColor)
{
    BrushLightColor = NewColor;
    if (Manager) Manager->EditorBrushLightColor = NewColor;
}

// Custom Brush Logic
void FDiggerEdModeToolkit::RebuildCustomBrushGrid()
{
    if (!CustomBrushGrid.IsValid()) return;
    CustomBrushGrid->ClearChildren();
    for (int32 i = 0; i < CustomBrushEntries.Num(); ++i)
    {
        CustomBrushGrid->AddSlot(i % 4, i / 4)
        [
            SNew(SButton).OnClicked_Lambda([this, i](){ SelectedBrushIndex = i; return FReply::Handled(); })
            [ SNew(STextBlock).Text(FText::FromString(CustomBrushEntries[i].IsSDF() ? "SDF" : "Mesh")) ]
        ];
    }
}

void FDiggerEdModeToolkit::ScanCustomBrushFolder()
{
    // Logic restored in constructor/init
}

// Operation
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeOperationSection()
{
    return SNew(SHorizontalBox)
    + SHorizontalBox::Slot().AutoWidth().Padding(2)
    [
        SNew(SCheckBox).Style(FAppStyle::Get(), "RadioButton")
        .IsChecked_Lambda([this](){ return !bBrushDig ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
        .OnCheckStateChanged_Lambda([this](ECheckBoxState S){ if(S==ECheckBoxState::Checked) bBrushDig = false; })
        [ SNew(STextBlock).Text(FText::FromString("Add (Build)")) ]
    ]
    + SHorizontalBox::Slot().AutoWidth().Padding(2)
    [
        SNew(SCheckBox).Style(FAppStyle::Get(), "RadioButton")
        .IsChecked_Lambda([this](){ return bBrushDig ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
        .OnCheckStateChanged_Lambda([this](ECheckBoxState S){ if(S==ECheckBoxState::Checked) bBrushDig = true; })
        [ SNew(STextBlock).Text(FText::FromString("Subtract (Dig)")) ]
    ]
    + SHorizontalBox::Slot().AutoWidth().Padding(8,0,0,0)
    [
        SNew(SCheckBox)
        .IsChecked(this, &FDiggerEdModeToolkit::GetHiddenSeamCheckState)
        .OnCheckStateChanged(this, &FDiggerEdModeToolkit::OnHiddenSeamChanged)
        [ SNew(STextBlock).Text(FText::FromString("Hidden Seam")) ]
    ];
}

// Generic UI Helpers
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeLabeledSliderRow(const FText& Label, TFunction<float()> Getter, TFunction<void(float)> Setter, float Min, float Max, const TArray<float>& QuickSet, float Reset, float Step, bool bIsAngle, float* MirrorTarget)
{
    return SNew(SVerticalBox)
    + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
    [
        SNew(SHorizontalBox)
        // Label
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,8,0)
        [
            SNew(STextBlock).Text(Label).Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
            .MinDesiredWidth(60)
        ]
        // The Slider / Spinbox
        + SHorizontalBox::Slot().FillWidth(1.0f)
        [
            SNew(SSpinBox<float>)
            .Value_Lambda([Getter](){ return Getter(); })
            .OnValueChanged_Lambda([Setter](float V){ Setter(V); })
            .OnValueCommitted_Lambda([Setter](float V, ETextCommit::Type){ Setter(V); })
            .MinValue(Min).MaxValue(Max)
            .MinSliderValue(Min).MaxSliderValue(Max)
            .Delta(Step)
            .SliderExponent(1.0f) // Linear slider
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
        ]
        // Caret / Expand button (Optional, keeping simple for now)
    ]
    // Quick Set Buttons (Below the slider)
    + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
    [
        MakeQuickSetButtons(QuickSet, Setter, MirrorTarget, bIsAngle)
    ];
}

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeQuickSetButtons(
    const TArray<float>& QuickSetValues,
    TFunction<void(float)> Setter,
    float* MirrorTarget,
    bool bIsAngle
)
{
    TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);
    const FSlateFontInfo Font = FCoreStyle::Get().GetFontStyle("NormalFont");

    for (float Val : QuickSetValues)
    {
        FText Label = bIsAngle 
            ? FText::FromString(FString::Printf(TEXT("%.0f°"), Val)) 
            : FText::AsNumber(Val);

        Box->AddSlot().AutoWidth().Padding(2,0)
        [
            SNew(SButton)
            .OnClicked_Lambda([Setter, Val](){ Setter(Val); return FReply::Handled(); })
            .ContentPadding(FMargin(4, 1))
            [
                SNew(STextBlock).Font(Font).Text(Label)
            ]
        ];
    }

    if (bIsAngle && MirrorTarget)
    {
        Box->AddSlot().AutoWidth().Padding(2,0)
        [
            SNew(SButton)
            .OnClicked_Lambda([MirrorTarget]() {
                if(MirrorTarget) {
                    *MirrorTarget = FMath::Fmod(*MirrorTarget + 180.0f, 360.0f);
                    if (*MirrorTarget < 0) *MirrorTarget += 360.0f;
                }
                return FReply::Handled();
            })
            .ToolTipText(FText::FromString("Mirror +180°"))
            [
                SNew(STextBlock).Font(Font).Text(FText::FromString("Mirror"))
            ]
        ];
    }

    return Box;
}
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeAngleButton(float Angle, float& Target, const FString& Label)
{
    return SNew(SButton).Text(FText::FromString(Label)).OnClicked_Lambda([&Target, Angle](){ Target = Angle; return FReply::Handled(); });
}
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeAngleButton(double Angle, double& Target, const FString& Label)
{
    return SNew(SButton).Text(FText::FromString(Label)).OnClicked_Lambda([&Target, Angle](){ Target = Angle; return FReply::Handled(); });
}
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeMirrorButton(float& Target, const FString& Label)
{
    return SNew(SButton).Text(FText::FromString(Label)).OnClicked_Lambda([&Target](){ Target += 180.f; return FReply::Handled(); });
}
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeMirrorButton(double& Target, const FString& Label)
{
    return SNew(SButton).Text(FText::FromString(Label)).OnClicked_Lambda([&Target](){ Target += 180.0; return FReply::Handled(); });
}

// --- Rotation and offset rows ---
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeRotationRow(const FText& Label, float& Value)
{
    // Determine which rotation axis this row controls by checking the label prefix.
    // Labels arrive as "X (Pitch)", "Y (Yaw)", "Z (Roll)" so we check the first char.
    const FString LabelStr = Label.ToString();
    const bool bIsPitch = LabelStr.StartsWith(TEXT("X"));
    const bool bIsYaw   = LabelStr.StartsWith(TEXT("Y"));
    // bIsRoll = everything else (Z)

    return MakeLabeledSliderRow(
        Label,
        [&](){ return Value; },
        [&, bIsPitch, bIsYaw](float V)
        {
            Value = V;

            if (FDiggerEdMode* Mode = (FDiggerEdMode*)GLevelEditorModeTools()
                    .GetActiveMode(FDiggerEdMode::EM_DiggerEdModeId))
            {
                if (bIsPitch)
                    Mode->BrushCache.Rotation.Pitch = V;
                else if (bIsYaw)
                    Mode->BrushCache.Rotation.Yaw = V;
                else
                    Mode->BrushCache.Rotation.Roll = V;

                Mode->UpdateBrushHUDPanel();
            }
        },
        0.f, 360.f, {});
}

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeRotationRow(const FText& Label, double& Value)
{
    const FString LabelStr = Label.ToString();
    const bool bIsPitch = LabelStr.StartsWith(TEXT("X"));
    const bool bIsYaw   = LabelStr.StartsWith(TEXT("Y"));

    return MakeLabeledSliderRow(
        Label,
        [&](){ return (float)Value; },
        [&, bIsPitch, bIsYaw](float V)
        {
            Value = (double)V;

            if (FDiggerEdMode* Mode = (FDiggerEdMode*)GLevelEditorModeTools()
                    .GetActiveMode(FDiggerEdMode::EM_DiggerEdModeId))
            {
                if (bIsPitch)
                    Mode->BrushCache.Rotation.Pitch = V;
                else if (bIsYaw)
                    Mode->BrushCache.Rotation.Yaw = V;
                else
                    Mode->BrushCache.Rotation.Roll = V;

                Mode->UpdateBrushHUDPanel();
            }
        },
        0.f, 360.f, {});
}

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeOffsetRow(const FText& Label, float& Value)
{
    const FString LabelStr = Label.ToString();
    const bool bIsX = LabelStr.StartsWith(TEXT("X"));
    const bool bIsY = LabelStr.StartsWith(TEXT("Y"));

    return MakeLabeledSliderRow(
        Label,
        [&](){ return Value; },
        [&, bIsX, bIsY](float V)
        {
            Value = V;

            if (FDiggerEdMode* Mode = (FDiggerEdMode*)GLevelEditorModeTools()
                    .GetActiveMode(FDiggerEdMode::EM_DiggerEdModeId))
            {
                if (bIsX)
                    Mode->BrushCache.Offset.X = V;
                else if (bIsY)
                    Mode->BrushCache.Offset.Y = V;
                else
                    Mode->BrushCache.Offset.Z = V;

                Mode->UpdateBrushHUDPanel();
            }
        },
        -1000.f, 1000.f, {});
}

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeOffsetRow(const FText& Label, double& Value)
{
    const FString LabelStr = Label.ToString();
    const bool bIsX = LabelStr.StartsWith(TEXT("X"));
    const bool bIsY = LabelStr.StartsWith(TEXT("Y"));

    return MakeLabeledSliderRow(
        Label,
        [&](){ return (float)Value; },
        [&, bIsX, bIsY](float V)
        {
            Value = (double)V;

            if (FDiggerEdMode* Mode = (FDiggerEdMode*)GLevelEditorModeTools()
                    .GetActiveMode(FDiggerEdMode::EM_DiggerEdModeId))
            {
                if (bIsX)
                    Mode->BrushCache.Offset.X = V;
                else if (bIsY)
                    Mode->BrushCache.Offset.Y = V;
                else
                    Mode->BrushCache.Offset.Z = V;

                Mode->UpdateBrushHUDPanel();
            }
        },
        -1000.f, 1000.f, {});
}

ECheckBoxState FDiggerEdModeToolkit::IsBrushDebugEnabled() const
{
    return (Manager && Manager->ActiveBrush && Manager->ActiveBrush->bEnableDebugDrawing) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FDiggerEdModeToolkit::OnBrushDebugCheckChanged(ECheckBoxState NewState)
{
    if(Manager && Manager->ActiveBrush) Manager->ActiveBrush->bEnableDebugDrawing = (NewState == ECheckBoxState::Checked);
}

FString FDiggerEdModeToolkit::GetBrushDisplayName(const FCustomBrushEntry& Entry) const
{
    return Entry.IsSDF() ? FPaths::GetBaseFilename(Entry.SDFBrushFilePath) : (Entry.Mesh.IsValid() ? Entry.Mesh->GetName() : TEXT("Unknown"));
}

bool FDiggerEdModeToolkit::IsDigMode() const
{
    return TemporaryDigOverride.IsSet() ? TemporaryDigOverride.GetValue() : bBrushDig;
}

ECheckBoxState FDiggerEdModeToolkit::GetHiddenSeamCheckState() const
{
    return bHiddenSeam ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FDiggerEdModeToolkit::OnHiddenSeamChanged(ECheckBoxState NewState)
{
    bHiddenSeam = (NewState == ECheckBoxState::Checked);
}

bool FDiggerEdModeToolkit::CanPaintWithCustomBrush() const
{
    return CurrentBrushType == EVoxelBrushType::Custom
        && SelectedBrushIndex >= 0
        && CustomBrushEntries.IsValidIndex(SelectedBrushIndex)
        && (CustomBrushEntries[SelectedBrushIndex].IsMesh() || CustomBrushEntries[SelectedBrushIndex].IsSDF());
}

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeDebugCheckbox(const FString& Label, bool* FlagPtr)
{
    return SNew(SCheckBox)
        .IsChecked_Lambda([FlagPtr]() { return *FlagPtr ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
        .OnCheckStateChanged_Lambda([FlagPtr](ECheckBoxState S) { *FlagPtr = (S == ECheckBoxState::Checked); })
        [ SNew(STextBlock).Text(FText::FromString(Label)) ];
}

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeDebugCheckbox(const FDiggerDebug::FFlagEntry& FlagEntry)
{
    return MakeDebugCheckbox(FlagEntry.Key.ToString(), FlagEntry.Value);
}

// Outliner Helpers

// Helper to toggle the protected bListedInSceneOutliner
void FDiggerEdModeToolkit::SetActorListedInOutliner(AActor* Actor, bool bListed)
{
    if (!Actor) return;
    static FBoolProperty* ListedProp = CastField<FBoolProperty>(
        AActor::StaticClass()->FindPropertyByName(FName("bListedInSceneOutliner"))
    );
    
    // Only modify if the value is actually different to avoid dirtying the transaction buffer
    if (ListedProp && ListedProp->GetPropertyValue_InContainer(Actor) != bListed)
    {
        Actor->Modify();
        ListedProp->SetPropertyValue_InContainer(Actor, bListed);
    }
}

void FDiggerEdModeToolkit::SetDynamicHolesFolderVisible(bool bVisible)
{
    // --- 0. PERSISTENCE: Save to Config ---
    {
        UDiggerEditorSettings* Settings = GetMutableDefault<UDiggerEditorSettings>();
        if (Settings)
        {
            // Only save if it actually changed to prevent spamming config I/O
            if (Settings->bShowDynamicHolesFolder != bVisible)
            {
                Settings->bShowDynamicHolesFolder = bVisible;

                // This saves to DefaultEditor.ini (or User settings)
                Settings->SaveConfig();

                // Notify other systems that settings changed
                Settings->TryUpdateDefaultConfigFile();
            }
        }
    }

    if (!GEditor) return;

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World) return;

    // --- 1. Get Manager (Static Logic) ---
    ADiggerManager* Mgr = ADiggerManager::FindDiggerManager(World);
    if (!Mgr || !Mgr->DynamicHoleClass) return;

    // --- 2. Find Actors by Class ---
    TArray<AActor*> HoleActors;
    UGameplayStatics::GetAllActorsOfClass(World, Mgr->DynamicHoleClass, HoleActors);
    if (HoleActors.Num() == 0) return;

    const FName TargetPath = FName("Digger/DynamicHoles");
    bool bChanged = false;

    if (bVisible)
    {
        // --- SHOW LOGIC ---
        for (AActor* Hole : HoleActors)
        {
            if (Hole->GetFolderPath() != TargetPath)
            {
                Hole->SetFolderPath(TargetPath);
            }

            SetActorListedInOutliner(Hole, true);
        }
        bChanged = true;
    }
    else
    {
        // --- HIDE LOGIC ---
        for (AActor* Hole : HoleActors)
        {
            SetActorListedInOutliner(Hole, false);

            if (!Hole->GetFolderPath().IsNone())
            {
                Hole->SetFolderPath(NAME_None);
            }
        }

        // --- DELETE FOLDER (new API) ---
        if (FActorFolders::IsAvailable())
        {
            // Construct folder using the modern API
            FFolder FolderToDelete(World, TargetPath);

            FActorFolders::Get().DeleteFolder(*World, FolderToDelete);
        }

        bChanged = true;
    }

    // --- 3. Refresh UI ---
    if (bChanged)
    {
        GEditor->BroadcastLevelActorListChanged();
    }
}


// -----------------------------------------------------------------------------------
// 5. GENERATION SECTION
// -----------------------------------------------------------------------------------

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeGenerationSection()
{
    if (!FDiggerFeatureFlags::bEnableGenerationSection)
        return SNew(SBox).Visibility(EVisibility::Collapsed);

    return SNew(SExpandableArea)
        .AreaTitle(FText::FromString(TEXT("Mesh Generation")))
        .InitiallyCollapsed(false)
        .BodyContent()
        [
            SNew(SVerticalBox)

            // ── Method selector ────────────────────────────────────────────
            + SVerticalBox::Slot().AutoHeight().Padding(8, 6, 8, 2)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("Method:")))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
                ]
                + SHorizontalBox::Slot().FillWidth(1.0f)
                [
                    SNew(SComboBox<TSharedPtr<FString>>)
                    .OptionsSource(&MeshGenerationOptions)
                    .OnGenerateWidget_Lambda([](TSharedPtr<FString> Opt)
                    {
                        return SNew(STextBlock).Text(FText::FromString(*Opt));
                    })
                    .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Sel, ESelectInfo::Type)
                    {
                        SelectedMeshGenerationMethod = Sel;
                        // Reserved for future MC / DC / Cubic selector hookup.
                        // if (Manager && Sel) Manager->SetMeshGenerationMethod(*Sel);
                    })
                    .InitiallySelectedItem(SelectedMeshGenerationMethod)
                    [
                        SNew(STextBlock).Text_Lambda([this]()
                        {
                            return FText::FromString(
                                SelectedMeshGenerationMethod.IsValid()
                                    ? *SelectedMeshGenerationMethod
                                    : TEXT("Marching Cubes"));
                        })
                    ]
                ]
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(8, 2)
            [ SNew(SSeparator) ]

            // ── Bake full chunk volume checkbox ────────────────────────────
            + SVerticalBox::Slot().AutoHeight().Padding(8, 6, 8, 2)
            [
                SNew(SCheckBox)
                .IsChecked_Lambda([this]()
                {
                    ADiggerManager* Mgr = GetDiggerManager();
                    return (Mgr && Mgr->bBakeFullChunkVolume)
                        ? ECheckBoxState::Checked
                        : ECheckBoxState::Unchecked;
                })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState State)
                {
                    if (ADiggerManager* Mgr = GetDiggerManager())
                        Mgr->bBakeFullChunkVolume = (State == ECheckBoxState::Checked);
                })
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("Bake Full Chunk Volume")))
                    .ToolTipText(FText::FromString(TEXT(
                        "ON  — every chunk in the brush radius generates a full solid-volume mesh.\n"
                        "OFF — only sculpted chunks produce geometry (faster, recommended for editing).")))
                ]
            ]

            // ── Bake queue progress ────────────────────────────────────────
            + SVerticalBox::Slot().AutoHeight().Padding(8, 2, 8, 6)
            [
                SNew(STextBlock)
                .Text_Lambda([this]() -> FText
                {
                    ADiggerManager* Mgr = GetDiggerManager();
                    if (!Mgr) return FText::GetEmpty();
                    const int32 Q = Mgr->GetBakeQueueSize();
                    if (Q == 0) return FText::GetEmpty();
                    return FText::FromString(FString::Printf(
                        TEXT("Baking... %d chunks remaining"), Q));
                })
                .ColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.75f, 0.2f)))
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(8, 2)
            [ SNew(SSeparator) ]

            // ── Bake loaded chunks button + cancel ─────────────────────────
            + SVerticalBox::Slot().AutoHeight().Padding(8, 6, 8, 2)
            [
                SNew(SVerticalBox)

                // Description
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
                [
                    SNew(STextBlock)
                    .AutoWrapText(true)
                    .Text(FText::FromString(TEXT(
                        "Rebuilds all chunks currently loaded in the ChunkMap "
                        "(only chunks that have been visited via lazy loading). "
                        "Chunks are processed one per tick to keep the editor responsive.")))
                    .ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
                ]

                // Button row
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SHorizontalBox)

                    // Bake all loaded chunks
                    + SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 4, 0)
                    [
                        SNew(SButton)
                        .HAlign(HAlign_Center)
                        .ContentPadding(FMargin(0, 8))
                        .ButtonColorAndOpacity(FLinearColor(0.1f, 0.35f, 0.15f))
                        .ToolTipText(FText::FromString(TEXT(
                            "Enqueue all loaded chunks for a full-volume bake.\n"
                            "Respects 'Bake Full Chunk Volume' flag.\n"
                            "One chunk is processed per tick.")))
                        .OnClicked_Lambda([this]() -> FReply
                        {
                            if (ADiggerManager* Mgr = GetDiggerManager())
                                Mgr->EnqueueBakeAllLoadedChunks();
                            return FReply::Handled();
                        })
                        [
                            SNew(STextBlock)
                            .Justification(ETextJustify::Center)
                            .Text(FText::FromString(TEXT("Bake All Loaded Chunks")))
                            .Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
                        ]
                    ]

                    // Cancel
                    + SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)
                    [
                        SNew(SButton)
                        .HAlign(HAlign_Center)
                        .ContentPadding(FMargin(12, 8))
                        .ButtonColorAndOpacity(FLinearColor(0.35f, 0.1f, 0.1f))
                        .ToolTipText(FText::FromString(TEXT("Cancel the in-progress bake queue.")))
                        .IsEnabled_Lambda([this]()
                        {
                            ADiggerManager* Mgr = GetDiggerManager();
                            return Mgr && Mgr->GetBakeQueueSize() > 0;
                        })
                        .OnClicked_Lambda([this]() -> FReply
                        {
                            if (ADiggerManager* Mgr = GetDiggerManager())
                                Mgr->CancelBakeQueue();
                            return FReply::Handled();
                        })
                        [
                            SNew(STextBlock)
                            .Justification(ETextJustify::Center)
                            .Text(FText::FromString(TEXT("Cancel")))
                        ]
                    ]
                ]
            ]
        ];
}

// -----------------------------------------------------------------------------------
// 6. LOGIC IMPLEMENTATIONS (Worklight, Brushes, Etc)
// -----------------------------------------------------------------------------------

void FDiggerEdModeToolkit::DeleteCustomBrush(int32 Index)
{
    if (!CustomBrushEntries.IsValidIndex(Index)) return;

    const FCustomBrushEntry& Entry = CustomBrushEntries[Index];
    
    // If it's an SDF brush, delete the actual file
    if (Entry.IsSDF() && !Entry.SDFBrushFilePath.IsEmpty())
    {
        IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
        if (PlatformFile.FileExists(*Entry.SDFBrushFilePath))
        {
            PlatformFile.DeleteFile(*Entry.SDFBrushFilePath);
        }
    }

    CustomBrushEntries.RemoveAt(Index);
    
    // Reset selection if needed
    if (SelectedBrushIndex == Index) SelectedBrushIndex = -1;
    else if (SelectedBrushIndex > Index) SelectedBrushIndex--;

    RebuildCustomBrushGrid();
}

void FDiggerEdModeToolkit::SpawnOrUpdateWorklight(FEditorViewportClient* ViewportClient)
{
    if (!ViewportClient || !bWorklightEnabled) 
    {
        if(WorklightActor.IsValid()) WorklightActor->SetActorHiddenInGame(true);
        return; 
    }

    UWorld* World = ViewportClient->GetWorld();
    if (!World) return;

    // 1. Create if missing
    if (!WorklightActor.IsValid())
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        SpawnParams.ObjectFlags = RF_Transient; 
        SpawnParams.bHideFromSceneOutliner = true;

        AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
        if (Actor)
        {
            WorklightActor = Actor;
            Actor->SetActorLabel(TEXT("DiggerWorklight"));
            Actor->SetActorEnableCollision(false);
            
            // Create Component based on selection
            if (SelectedWorkLightType == "Spot")
                DiggerWorklightComponent = NewObject<USpotLightComponent>(Actor);
            else
                DiggerWorklightComponent = NewObject<UPointLightComponent>(Actor);

            if (DiggerWorklightComponent)
            {
                DiggerWorklightComponent->RegisterComponent();
                DiggerWorklightComponent->SetMobility(EComponentMobility::Movable);
                Actor->SetRootComponent(DiggerWorklightComponent);
                
                // Apply initial settings
                UpdateWorklightIntensity(WorklightIntensity);
                UpdateWorklightAttenuation(WorklightAttenuation);
                UpdateWorklightColor(WorklightColor);
            }
        }
    }

    // 2. Update Transform to match camera
    if (WorklightActor.IsValid())
    {
        WorklightActor->SetActorLocation(ViewportClient->GetViewLocation());
        
        // If spot light, match rotation too
        if (SelectedWorkLightType == "Spot")
        {
            WorklightActor->SetActorRotation(ViewportClient->GetViewRotation());
        }
    }
}

void FDiggerEdModeToolkit::DestroyWorklight()
{
    if (WorklightActor.IsValid())
    {
        WorklightActor->Destroy();
        WorklightActor.Reset();
        DiggerWorklightComponent = nullptr;
    }
}

bool FDiggerEdModeToolkit::GetCameraFollowsBrush() const
{
    return bCameraFollowsBrush;
}

void FDiggerEdModeToolkit::SetCameraFollowsBrush(bool bFollow)
{
    bCameraFollowsBrush = bFollow;
    // Persist to editor config so the L-key toggle survives restarts.
    if (UDiggerEditorSettings* MutableSettings =
            GetMutableDefault<UDiggerEditorSettings>())
    {
        MutableSettings->bCameraFollowsBrush = bFollow;
        MutableSettings->SaveConfig();
    }
}

void FDiggerEdModeToolkit::ToggleWorklight(bool bEnable)
{
    bWorklightEnabled = bEnable;
    if (!bEnable)
    {
        DestroyWorklight();
    }
}

void FDiggerEdModeToolkit::UpdateWorklightType(const FString& NewType)
{
    SelectedWorkLightType = NewType;
    DestroyWorklight(); // Destroy to respawn as new type next tick
}

void FDiggerEdModeToolkit::UpdateWorklightIntensity(float NewIntensity)
{
    WorklightIntensity = NewIntensity;
    if (DiggerWorklightComponent) DiggerWorklightComponent->SetIntensity(NewIntensity);
}

void FDiggerEdModeToolkit::UpdateWorklightAttenuation(float NewRadius)
{
    WorklightAttenuation = NewRadius;
    if (!DiggerWorklightComponent) return;

    // Try casting to Point Light
    if (UPointLightComponent* PointLight = Cast<UPointLightComponent>(DiggerWorklightComponent))
    {
        PointLight->SetAttenuationRadius(NewRadius);
    }
    // Try casting to Spot Light
    else if (USpotLightComponent* SpotLight = Cast<USpotLightComponent>(DiggerWorklightComponent))
    {
        SpotLight->SetAttenuationRadius(NewRadius);
    }
}

void FDiggerEdModeToolkit::UpdateWorklightColor(const FLinearColor& NewColor)
{
    WorklightColor = NewColor;
    if (DiggerWorklightComponent) DiggerWorklightComponent->SetLightColor(NewColor);
}

// =============================================================================
// DIGGER — Hole Optimization Panel
// =============================================================================
//
// HOW TO INTEGRATE
// ─────────────────
// 1. HEADER  (DiggerEdModeToolkit.h)
//    Add to the "Section Builders" region (wherever you declare your other
//    Make___Section() methods):
//
//       TSharedRef<SWidget> MakeHoleOptimizationSection();
//
//    Add to the private member variable block:
//
//       // Hole Optimization state
//       FIntVector OptimizationTargetChunk = FIntVector(0, 0, 0);
//       TSharedPtr<STextBlock> HoleOptStatusText;   // live feedback label
//
// 2. CPP  (DiggerEdModeToolkit.cpp)
//    a. Paste the MakeHoleOptimizationSection() implementation below into the
//       .cpp (near the other Make___Section() definitions is cleanest).
//
//    b. In Init(), inside the "ADDITIONAL TOOLS" expandable area (section 3),
//       or as its own top-level expandable area after section 3, add:
//
//          MainBox->AddSlot().AutoHeight().Padding(8, 12, 8, 4)
//          [
//              SNew(SExpandableArea)
//              .AreaTitle(FText::FromString(TEXT("Hole Optimization")))
//              .InitiallyCollapsed(true)
//              .BodyContent()[ MakeHoleOptimizationSection() ]
//          ];
//
// 3. INCLUDES  (top of DiggerEdModeToolkit.cpp, already present in your file)
//    All needed headers (#include "VoxelChunk.h", EngineUtils, etc.) are
//    already in your include list.  No new includes required.
//
// =============================================================================


// -----------------------------------------------------------------------------
// MakeHoleOptimizationSection
// Builds the Hole Optimization panel body.
// -----------------------------------------------------------------------------
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeHoleOptimizationSection()
{
    // ── Shared status label ───────────────────────────────────────────────────
    // Declared as a member (HoleOptStatusText) so both button lambdas can write
    // to it.  We construct it here and store the shared pointer.

    TSharedRef<STextBlock> StatusLabel =
        SNew(STextBlock)
        .Text(FText::FromString(TEXT("No operation run yet.")))
        .ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
        .AutoWrapText(true);

    HoleOptStatusText = StatusLabel; // store for lambda capture

    // ── Helpers ───────────────────────────────────────────────────────────────

    // Retrieves the VoxelChunk at OptimizationTargetChunk, or nullptr.
    // Logs an error and updates the status label when the chunk is missing.
    auto GetTargetChunk = [this]() -> UVoxelChunk*
    {
        ADiggerManager* Mgr = GetDiggerManager();
        if (!Mgr)
        {
            if (HoleOptStatusText.IsValid())
                HoleOptStatusText->SetText(
                    FText::FromString(TEXT("No Digger Manager found. Place one in your level to use this tool.")));
            return nullptr;
        }

        UVoxelChunk* Chunk = Mgr->GetChunkAtCoords(OptimizationTargetChunk);
        if (!Chunk)
        {
            if (HoleOptStatusText.IsValid())
                HoleOptStatusText->SetText(FText::FromString(FString::Printf(
                    TEXT("Chunk (%d, %d, %d) is not loaded. Try sculpting in that area first."),
                    OptimizationTargetChunk.X,
                    OptimizationTargetChunk.Y,
                    OptimizationTargetChunk.Z)));
            return nullptr;
        }

        return Chunk;
    };

    // Formats a concise summary after an optimization pass.
    auto MakeResultText = [](const FString& OpName, int32 Before, int32 After) -> FString
    {
        const int32 Removed = Before - After;
        return FString::Printf(
            TEXT("%s complete — %d holes before, %d after (%d removed)."),
            *OpName, Before, After, Removed);
    };

    // ── Layout ────────────────────────────────────────────────────────────────
    return SNew(SVerticalBox)

    // ── Section description ───────────────────────────────────────────────────
    + SVerticalBox::Slot().AutoHeight().Padding(8, 6, 8, 2)
    [
        SNew(STextBlock)
        .AutoWrapText(true)
        .Text(FText::FromString(
            TEXT("Manually reduce hole actor count on a chunk.\n"
                 "Dedup removes near-identical holes (same shape / position / scale). "
                 "Declutter removes holes whose volume is already covered by a larger hole.")))
        .ColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.65f, 0.65f)))
    ]

    + SVerticalBox::Slot().AutoHeight().Padding(8, 2)
    [ SNew(SSeparator) ]

    // ── Target chunk display (read-only for now, editable later) ─────────────
    + SVerticalBox::Slot().AutoHeight().Padding(8, 6, 8, 2)
    [
        SNew(SHorizontalBox)

        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("Target Chunk:")))
            .Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
        ]

        // X
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 2, 0)
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("X")))
            .ColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.3f, 0.3f)))
        ]
        + SHorizontalBox::Slot().MaxWidth(52.f).VAlign(VAlign_Center).Padding(0, 0, 6, 0)
        [
            SNew(SNumericEntryBox<int32>)
            .Value_Lambda([this]() { return TOptional<int32>(OptimizationTargetChunk.X); })
            .OnValueCommitted_Lambda([this](int32 Val, ETextCommit::Type)
            {
                OptimizationTargetChunk.X = Val;
            })
        ]

        // Y
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 2, 0)
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("Y")))
            .ColorAndOpacity(FSlateColor(FLinearColor(0.3f, 0.8f, 0.3f)))
        ]
        + SHorizontalBox::Slot().MaxWidth(52.f).VAlign(VAlign_Center).Padding(0, 0, 6, 0)
        [
            SNew(SNumericEntryBox<int32>)
            .Value_Lambda([this]() { return TOptional<int32>(OptimizationTargetChunk.Y); })
            .OnValueCommitted_Lambda([this](int32 Val, ETextCommit::Type)
            {
                OptimizationTargetChunk.Y = Val;
            })
        ]

        // Z
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 2, 0)
        [
            SNew(STextBlock)
            .Text(FText::FromString(TEXT("Z")))
            .ColorAndOpacity(FSlateColor(FLinearColor(0.3f, 0.5f, 0.9f)))
        ]
        + SHorizontalBox::Slot().MaxWidth(52.f).VAlign(VAlign_Center)
        [
            SNew(SNumericEntryBox<int32>)
            .Value_Lambda([this]() { return TOptional<int32>(OptimizationTargetChunk.Z); })
            .OnValueCommitted_Lambda([this](int32 Val, ETextCommit::Type)
            {
                OptimizationTargetChunk.Z = Val;
            })
        ]
    ]

    // ── Hole count readout ────────────────────────────────────────────────────
    + SVerticalBox::Slot().AutoHeight().Padding(8, 2, 8, 6)
    [
        SNew(STextBlock)
        // Re-evaluated every frame so it always reflects live state.
        .Text_Lambda([this]() -> FText
        {
            ADiggerManager* Mgr = GetDiggerManager();
            if (!Mgr) return FText::FromString(TEXT("Holes in chunk: —"));

            UVoxelChunk* Chunk = Mgr->GetChunkAtCoords(OptimizationTargetChunk);
            if (!Chunk) return FText::FromString(TEXT("Holes in chunk: (chunk not loaded)"));

            return FText::FromString(FString::Printf(
                TEXT("Holes in chunk: %d"), Chunk->GetSpawnedHoleCount()));
        })
        .ColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.75f, 0.45f)))
    ]

    + SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 2)
    [ SNew(SSeparator) ]

    // ── Buttons ───────────────────────────────────────────────────────────────
    + SVerticalBox::Slot().AutoHeight().Padding(8, 4)
    [
        SNew(SHorizontalBox)

        // ── Dedup ────────────────────────────────────────────────────────────
        + SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 4, 0)
        [
            SNew(SButton)
            .HAlign(HAlign_Center)
            .VAlign(VAlign_Center)
            .ContentPadding(FMargin(0, 8))
            .ButtonColorAndOpacity(FLinearColor(0.12f, 0.22f, 0.38f))
            .ToolTipText(FText::FromString(
                TEXT("Remove exact or near-identical duplicate holes "
                     "(same shape, snapped position, and scale).")))
            .OnClicked_Lambda([this, GetTargetChunk, MakeResultText]() -> FReply
            {
                UVoxelChunk* Chunk = GetTargetChunk();
                if (!Chunk) return FReply::Handled();

                const int32 Before = Chunk->GetSpawnedHoleCount();

                GEditor->BeginTransaction(FText::FromString(
                    TEXT("Digger: Dedup Holes")));
                Chunk->DedupHoles();
                GEditor->EndTransaction();

                const int32 After = Chunk->GetSpawnedHoleCount();

                if (HoleOptStatusText.IsValid())
                    HoleOptStatusText->SetText(FText::FromString(
                        MakeResultText(TEXT("Dedup"), Before, After)));

                GEditor->RedrawLevelEditingViewports();
                return FReply::Handled();
            })
            [
                SNew(STextBlock)
                .Justification(ETextJustify::Center)
                .Text(FText::FromString(TEXT("Dedup Holes")))
                .Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
            ]
        ]

        // ── Declutter ────────────────────────────────────────────────────────
        + SHorizontalBox::Slot().FillWidth(1.f).Padding(4, 0, 0, 0)
        [
            SNew(SButton)
            .HAlign(HAlign_Center)
            .VAlign(VAlign_Center)
            .ContentPadding(FMargin(0, 8))
            .ButtonColorAndOpacity(FLinearColor(0.22f, 0.15f, 0.38f))
            .ToolTipText(FText::FromString(
                TEXT("Remove holes whose volume is already covered "
                     "by a larger overlapping hole.")))
            .OnClicked_Lambda([this, GetTargetChunk, MakeResultText]() -> FReply
            {
                UVoxelChunk* Chunk = GetTargetChunk();
                if (!Chunk) return FReply::Handled();

                const int32 Before = Chunk->GetSpawnedHoleCount();

                GEditor->BeginTransaction(FText::FromString(
                    TEXT("Digger: Declutter Holes")));
                Chunk->DeclutterHoles();
                GEditor->EndTransaction();

                const int32 After = Chunk->GetSpawnedHoleCount();

                if (HoleOptStatusText.IsValid())
                    HoleOptStatusText->SetText(FText::FromString(
                        MakeResultText(TEXT("Declutter"), Before, After)));

                GEditor->RedrawLevelEditingViewports();
                return FReply::Handled();
            })
            [
                SNew(STextBlock)
                .Justification(ETextJustify::Center)
                .Text(FText::FromString(TEXT("Declutter Holes")))
                .Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
            ]
        ]
    ]

    // ── Run both in sequence ──────────────────────────────────────────────────
    + SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 4)
    [
        SNew(SButton)
        .HAlign(HAlign_Center)
        .VAlign(VAlign_Center)
        .ContentPadding(FMargin(0, 6))
        .ButtonColorAndOpacity(FLinearColor(0.18f, 0.32f, 0.18f))
        .ToolTipText(FText::FromString(
            TEXT("Runs Dedup first, then Declutter in a single transaction. "
                 "Recommended for the best reduction.")))
        .OnClicked_Lambda([this, GetTargetChunk, MakeResultText]() -> FReply
        {
            UVoxelChunk* Chunk = GetTargetChunk();
            if (!Chunk) return FReply::Handled();

            const int32 Before = Chunk->GetSpawnedHoleCount();

            GEditor->BeginTransaction(FText::FromString(
                TEXT("Digger: Optimize Holes (Dedup + Declutter)")));
            Chunk->DedupHoles();
            Chunk->DeclutterHoles();
            GEditor->EndTransaction();

            const int32 After = Chunk->GetSpawnedHoleCount();

            if (HoleOptStatusText.IsValid())
                HoleOptStatusText->SetText(FText::FromString(
                    MakeResultText(TEXT("Dedup + Declutter"), Before, After)));

            GEditor->RedrawLevelEditingViewports();
            return FReply::Handled();
        })
        [
            SNew(STextBlock)
            .Justification(ETextJustify::Center)
            .Text(FText::FromString(TEXT("Optimize (Dedup + Declutter)")))
        ]
    ]

    + SVerticalBox::Slot().AutoHeight().Padding(8, 2)
    [ SNew(SSeparator) ]

    // ── Status feedback ───────────────────────────────────────────────────────
    + SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 8)
    [
        StatusLabel
    ];
}


// =============================================================================
// REQUIRED: VoxelChunk::GetSpawnedHoleCount()
// =============================================================================
// Add this accessor to VoxelChunk.h (public section) so the UI can read the
// live count without exposing the full SpawnedHoles array:
//
//   int32 GetSpawnedHoleCount() const { return SpawnedHoles.Num(); }
//
// And add this to VoxelChunk.h if not already present (used by GetTargetChunk):
//
//   // In DiggerManager.h / .cpp — expose a lookup by chunk coordinates:
//   UVoxelChunk* GetChunkAtCoords(const FIntVector& ChunkCoords) const;
//
// If your manager already has GetOrCreateChunkAtCoords() you can alias it, or
// add a non-creating variant:
//
//   UVoxelChunk* ADiggerManager::GetChunkAtCoords(const FIntVector& Coords) const
//   {
//       // Assumes you store chunks in a TMap<FIntVector, UVoxelChunk*> called Chunks
//       UVoxelChunk* const* Found = Chunks.Find(Coords);
//       return Found ? *Found : nullptr;
//   }
//
// =============================================================================

void FDiggerEdModeToolkit::GetElevationInfo(float& AbsoluteOut, float& RelativeOut) const
{
    AbsoluteOut = 0.f;
    RelativeOut = 0.f;

    if (!CachedViewportClient || !Manager) return;

    const FVector ViewLocation = CachedViewportClient->GetViewLocation();
    AbsoluteOut = ViewLocation.Z;

    float TerrainHeight = Manager->GetLandscapeHeightAt(ViewLocation);
    
    // If raycast fails or is infinite, handle gracefully
    if(FMath::IsNearlyEqual(TerrainHeight, -FLT_MAX))
        RelativeOut = 0.0f;
    else
        RelativeOut = ViewLocation.Z - TerrainHeight;
}

// -----------------------------------------------------------------------------------
// 7. ISLAND & PHYSICS LOGIC
// -----------------------------------------------------------------------------------

void FDiggerEdModeToolkit::OnConvertToPhysicsActorClicked()
{
    if (SelectedIslandIndex != INDEX_NONE && Islands.IsValidIndex(SelectedIslandIndex))
    {
        const FIslandData& Island = Islands[SelectedIslandIndex];
        if (Manager)
        {
            // Use the reference voxel if available, otherwise pass ZeroValue
            if (Island.ReferenceVoxel != FIntVector::ZeroValue)
            {
                Manager->ConvertIslandAtPositionToActor(Island.Location, true, Island.ReferenceVoxel);
            }
            else
            {
                Manager->ConvertIslandAtPositionToActor(Island.Location, true, FIntVector::ZeroValue);
            }
        }
    }
}

void FDiggerEdModeToolkit::OnConvertToSceneActorClicked()
{
    if (SelectedIslandIndex != INDEX_NONE && Islands.IsValidIndex(SelectedIslandIndex))
    {
        const FIslandData& Island = Islands[SelectedIslandIndex];
        if (Manager)
        {
            // Use the reference voxel if available, otherwise pass ZeroValue
            if (Island.ReferenceVoxel != FIntVector::ZeroValue)
            {
                Manager->ConvertIslandAtPositionToActor(Island.Location, false, Island.ReferenceVoxel);
            }
            else
            {
                Manager->ConvertIslandAtPositionToActor(Island.Location, false, FIntVector::ZeroValue);
            }
        }
    }
}

void FDiggerEdModeToolkit::OnRemoveIslandClicked()
{
    if (SelectedIslandIndex != INDEX_NONE && Islands.IsValidIndex(SelectedIslandIndex))
    {
        const FIslandData Island = Islands[SelectedIslandIndex];
        if (Manager)
        {
            Manager->RemoveIslandVoxels(Island);
        }
        
        // Removing from local list immediately for UI responsiveness
        Islands.RemoveAt(SelectedIslandIndex);
        SelectedIslandIndex = INDEX_NONE;
        RebuildIslandGrid();
    }
}

// -----------------------------------------------------------------------------------
// 8. BRUSH STATE SETTERS
// -----------------------------------------------------------------------------------

void FDiggerEdModeToolkit::RequestBrushUIRefresh()
{
    if (ToolkitWidget.IsValid())
    {
        ToolkitWidget->Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
    }
}

void FDiggerEdModeToolkit::OnManagerRespawned()
{
    // 1. Drop the stale cached pointer and pick up the new instance.
    Manager = GetDiggerManager();

    if (!IsValid(Manager))
    {
        UE_LOG(LogTemp, Error,
            TEXT("Digger Toolkit: OnManagerRespawned called but no valid "
                 "DiggerManager was found in the world."));
        return;
    }

    // 2. Rebind island delegates — the old manager instance is gone, so any
    //    delegates registered on it are dangling. ConnectIslandHub() disconnects
    //    stale handles before re-binding, so it is safe to call directly.
    ConnectIslandHub();

    // 3. Re-push the current brush type so the new manager is in sync with the UI.
    Manager->EditorBrushType = CurrentBrushType;

    // 4. Refresh the toolkit panel so any manager-dependent widgets repopulate.
    RequestBrushUIRefresh();

    UE_LOG(LogTemp, Log,
        TEXT("Digger Toolkit: Rebound to new DiggerManager '%s'."),
        *Manager->GetName());
}

void FDiggerEdModeToolkit::SetTemporaryDigOverride(TOptional<bool> Override)
{
    TemporaryDigOverride = Override;
}

void FDiggerEdModeToolkit::SetBrushRotation(const FRotator& InRot)
{
    BrushRotX = InRot.Pitch;
    BrushRotY = InRot.Yaw;
    BrushRotZ = InRot.Roll;
}

void FDiggerEdModeToolkit::SetCurrentBrushType(EVoxelBrushType NewType)
{
    CurrentBrushType = NewType;
    if (Manager) Manager->EditorBrushType = NewType;
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

// -----------------------------------------------------------------------------------
// 9. SAVE / LOAD
// -----------------------------------------------------------------------------------

FString FDiggerEdModeToolkit::GetCurrentSaveFileName() const
{
    if (SaveFileNameWidget.IsValid())
    {
        FString Text = SaveFileNameWidget->GetText().ToString();
        return Text.IsEmpty() ? TEXT("Default") : Text;
    }
    return TEXT("Default");
}

void FDiggerEdModeToolkit::RefreshSaveFilesList()
{
    if (!SaveFileComboBox.IsValid()) return;

    AvailableSaveFiles.Empty();
    if (Manager)
    {
        TArray<FString> Names = Manager->GetAllSaveFileNames();
        for (const FString& Name : Names)
        {
            AvailableSaveFiles.Add(MakeShareable(new FString(Name)));
        }
    }

    // Ensure Default exists
    bool bFoundDefault = false;
    for(auto& Ptr : AvailableSaveFiles) { if(*Ptr == "Default") bFoundDefault = true; }
    if(!bFoundDefault) AvailableSaveFiles.Insert(MakeShareable(new FString("Default")), 0);

    SaveFileComboBox->RefreshOptions();
    if (AvailableSaveFiles.Num() > 0) SaveFileComboBox->SetSelectedItem(AvailableSaveFiles[0]);
}

FReply FDiggerEdModeToolkit::OnClearAllClicked()
{
    FText Msg = FText::FromString("This will permanently erase all voxel sculpting in the current level.\n\nThis action cannot be undone. Are you sure?");
    if (FMessageDialog::Open(EAppMsgType::YesNo, Msg) == EAppReturnType::Yes)
    {
        if (Manager)
        {
            // Transaction for Undo/Redo
            GEditor->BeginTransaction(FText::FromString("Clear All Voxel Data"));
            Manager->Modify();
            
            Manager->ClearAllVoxelData();
            ClearIslands(); // Update local UI list
            
            GEditor->EndTransaction();
            
            // Force Redraw
            GEditor->RedrawLevelEditingViewports();
        }
    }
    return FReply::Handled();
}

// -----------------------------------------------------------------------------------
// 10. MATERIAL MANAGER (DMM) IMPLEMENTATION
// -----------------------------------------------------------------------------------

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeDMMBody()
{
    // This aggregates the header and list body for the DMM section
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
        [
            MakeLayerListHeader()
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
        [
            MakeLayerListBody()
        ];
}

void FDiggerEdModeToolkit::LoadDMMState()
{
#if WITH_EDITOR
    FString ModeStr;
    if (GConfig->GetString(TEXT("/Script/DiggerEditor"), *GetModeConfigKey(), ModeStr, GEditorPerProjectIni))
    {
        if (ModeStr == "Landscape") CurrentDMMMode = EDMMPanelMode::Landscape;
        else if (ModeStr == "Utilities") CurrentDMMMode = EDMMPanelMode::Utilities;
        else CurrentDMMMode = EDMMPanelMode::Sediment;
    }

    FString ProfilePath;
    if (GConfig->GetString(TEXT("/Script/DiggerEditor"), *GetProfileConfigKey(), ProfilePath, GEditorPerProjectIni))
    {
        ActiveMaterialProfile = LoadObject<UDiggerMaterialProfile>(nullptr, *ProfilePath);
    }
#endif
}

void FDiggerEdModeToolkit::SaveDMMState() const
{
#if WITH_EDITOR
    FString Path = ActiveMaterialProfile.IsValid() ? ActiveMaterialProfile->GetPathName() : "";
    GConfig->SetString(TEXT("/Script/DiggerEditor"), *GetProfileConfigKey(), *Path, GEditorPerProjectIni);
    
    FString ModeStr = "Sediment";
    if (CurrentDMMMode == EDMMPanelMode::Landscape) ModeStr = "Landscape";
    if (CurrentDMMMode == EDMMPanelMode::Utilities) ModeStr = "Utilities";
    GConfig->SetString(TEXT("/Script/DiggerEditor"), *GetModeConfigKey(), *ModeStr, GEditorPerProjectIni);
    
    GConfig->Flush(false, GEditorPerProjectIni);
#endif
}

UDiggerMaterialProfile* FDiggerEdModeToolkit::GetMutableProfile() const
{
    return ActiveMaterialProfile.Get();
}

void FDiggerEdModeToolkit::EnsureActiveProfile()
{
    // Optional: Auto-create default if null logic here
}

void FDiggerEdModeToolkit::TouchProfile(UDiggerMaterialProfile* Profile)
{
    NotifyProfileArrayChanged(Profile, FName("Layers"));
}

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeMaterialManagerSection()
{
    if (!FDiggerFeatureFlags::bEnableMaterialManager) return SNew(SBox).Visibility(EVisibility::Collapsed);

    return SNew(SVerticalBox)
    + SVerticalBox::Slot().AutoHeight().Padding(4)
    [
        MakeDMMHeaderRow()
    ]
    + SVerticalBox::Slot().AutoHeight().Padding(4)
    [
        SNew(SVerticalBox).Visibility_Lambda([this](){ return bShowMaterialManagerSection ? EVisibility::Visible : EVisibility::Collapsed; })
        + SVerticalBox::Slot().AutoHeight()[ MakeDMMToolbar() ]
        + SVerticalBox::Slot().AutoHeight().Padding(4)
        [
            SNew(SWidgetSwitcher)
            .WidgetIndex_Lambda([this]() { return (int32)CurrentDMMMode; })
            + SWidgetSwitcher::Slot()[ SAssignNew(SedimentBodyBox, SBox)[ MakeSedimentBody() ] ]
            + SWidgetSwitcher::Slot()[ MakeLandscapeBody() ]
            + SWidgetSwitcher::Slot()[ MakeUtilitiesBody() ]
        ]
    ];
}

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeDMMHeaderRow()
{
    return MakeRollDownHeader("Material Manager", bShowMaterialManagerSection);
}

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeDMMToolbar()
{
    // Sediment and Landscape tabs are hidden for initial release.
    // Only Utilities is exposed until DMM is ready for public testing.
    return SNew(SHorizontalBox)
    + SHorizontalBox::Slot().AutoWidth().Padding(2)
    [
        SNew(SButton)
        .Text(FText::FromString("Utilities"))
        .OnClicked_Lambda([this]()
        {
            CurrentDMMMode = EDMMPanelMode::Utilities;
            SaveDMMState();
            return FReply::Handled();
        })
    ];
    // Profile picker intentionally omitted until Sediment/Landscape tabs are released.
}

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeSedimentBody()
{
    UDiggerMaterialProfile* Profile = GetMutableProfile();
    TSharedRef<SVerticalBox> VBox = SNew(SVerticalBox);

    // Toolbar
    VBox->AddSlot().AutoHeight().Padding(2)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth()[ SNew(SButton).Text(FText::FromString("Add Layer")).OnClicked(this, &FDiggerEdModeToolkit::OnAddLayerClicked) ]
        + SHorizontalBox::Slot().AutoWidth()[ SNew(SButton).Text(FText::FromString("Quick Apply")).OnClicked(this, &FDiggerEdModeToolkit::OnQuickApplyClicked) ]
    ];

    if (!Profile)
    {
        VBox->AddSlot().AutoHeight().Padding(4)[ SNew(STextBlock).Text(FText::FromString("Please select a Material Profile.")) ];
        return VBox;
    }

    for (int32 i = 0; i < Profile->Layers.Num(); ++i)
    {
        VBox->AddSlot().AutoHeight().Padding(2)[ MakeSedimentLayerRow(i) ];
    }

    return VBox;
}

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeSedimentLayerRow(int32 Index)
{
    UDiggerMaterialProfile* P = GetMutableProfile();
    if(!P || !P->Layers.IsValidIndex(Index)) return SNew(SBox);
    
    FDiggerLayerParams& L = P->Layers[Index];

    return SNew(SBorder).Padding(4).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth()
            [
                SNew(SCheckBox).IsChecked_Lambda([&L](){ return L.bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this, &L](ECheckBoxState S){ L.bEnabled = (S == ECheckBoxState::Checked); TouchProfile(GetMutableProfile()); })
            ]
            + SHorizontalBox::Slot().FillWidth(1.0f).Padding(4,0)
            [
                SNew(SEditableTextBox).Text_Lambda([&L](){ return FText::FromString(L.LayerName); })
                .OnTextCommitted_Lambda([this, &L](const FText& T, ETextCommit::Type){ L.LayerName = T.ToString(); TouchProfile(GetMutableProfile()); })
            ]
            + SHorizontalBox::Slot().AutoWidth()[ SNew(SButton).Text(FText::FromString("Up")).OnClicked(this, &FDiggerEdModeToolkit::OnMoveLayerUpClicked, Index) ]
            + SHorizontalBox::Slot().AutoWidth()[ SNew(SButton).Text(FText::FromString("Dn")).OnClicked(this, &FDiggerEdModeToolkit::OnMoveLayerDownClicked, Index) ]
            + SHorizontalBox::Slot().AutoWidth()[ SNew(SButton).Text(FText::FromString("X")).OnClicked(this, &FDiggerEdModeToolkit::OnRemoveLayerClicked, Index) ]
        ]
        // Texture Set Picker
        + SVerticalBox::Slot().AutoHeight().Padding(0,4)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth()[ SNew(STextBlock).Text(FText::FromString("Texture Set: ")) ]
            + SHorizontalBox::Slot().FillWidth(1.0f)
            [
                SNew(SObjectPropertyEntryBox)
                .AllowedClass(UDiggerTextureSet::StaticClass())
                .ObjectPath_Lambda([&L](){ return L.TextureSet.IsNull() ? "" : L.TextureSet.ToSoftObjectPath().ToString(); })
                .OnObjectChanged_Lambda([this, &L](const FAssetData& D){ L.TextureSet = Cast<UDiggerTextureSet>(D.GetAsset()); TouchProfile(GetMutableProfile()); })
            ]
        ]
        // Params
        + SVerticalBox::Slot().AutoHeight()
        [
            MakeLabeledSliderRow(FText::FromString("Tiling"), [&L](){return L.Tiling;}, [this,&L](float V){L.Tiling=V; TouchProfile(GetMutableProfile());}, 0.01f, 100.f, {})
        ]
    ];
}

// Other DMM Tabs
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeLandscapeBody()
{
    // Landscape tab — placeholder for future landscape layer painting tools.
    // For now, direct users to Utilities for the one-click setup.
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(8, 12)
        [
            SNew(STextBlock)
            .Text(FText::FromString(
                "Landscape layer painting tools are planned for a future update.\n"
                "Use the Utilities tab to set up your landscape for dynamic holes."))
            .AutoWrapText(true)
            .ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
        ];
}

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeUtilitiesBody()
{
    return SNew(SVerticalBox)

        // ── Section title ──────────────────────────────────────────────────
        + SVerticalBox::Slot().AutoHeight().Padding(4, 8, 4, 4)
        [
            SNew(STextBlock)
            .Text(FText::FromString("Dynamic Holes - Landscape Setup"))
            .Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
        ]

        // ── Preflight checklist (live, recomputed each frame via _Lambda) ──
        + SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 4)
        [
            SNew(SBorder)
            .BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
            .Padding(8)
            [
                SNew(SVerticalBox)

                + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString("Pre-flight Check"))
                    .Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
                    .ColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.9f, 0.9f)))
                ]

                // Landscape status
                + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]()
                        {
                            ADiggerManager* Mgr = GetDiggerManager();
                            if (!Mgr) return FText::FromString("[?]");
                            auto R = Mgr->RunSetupPreflight();
                            return FText::FromString(R.bHasLandscape ? "[OK]" : "[!!]");
                        })
                        .ColorAndOpacity_Lambda([this]()
                        {
                            ADiggerManager* Mgr = GetDiggerManager();
                            if (!Mgr) return FSlateColor(FLinearColor::Gray);
                            auto R = Mgr->RunSetupPreflight();
                            return FSlateColor(R.bHasLandscape
                                ? FLinearColor(0.2f, 0.9f, 0.2f)
                                : FLinearColor(0.9f, 0.2f, 0.2f));
                        })
                    ]
                    + SHorizontalBox::Slot().FillWidth(1.f)
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]()
                        {
                            ADiggerManager* Mgr = GetDiggerManager();
                            if (!Mgr) return FText::FromString("No DiggerManager in scene");
                            auto R = Mgr->RunSetupPreflight();
                            return R.bHasLandscape
                                ? FText::FromString(FString::Printf(
                                    TEXT("Landscape found (%d proxy/proxies)"),
                                    R.LandscapeProxyCount))
                                : FText::FromString("No landscape found in scene");
                        })
                        .AutoWrapText(true)
                        .ColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.75f, 0.75f)))
                    ]
                ]

                // RVT status
                + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]()
                        {
                            ADiggerManager* Mgr = GetDiggerManager();
                            if (!Mgr) return FText::FromString("[?]");
                            auto R = Mgr->RunSetupPreflight();
                            return FText::FromString(R.bHasRVT ? "[OK]" : "[--]");
                        })
                        .ColorAndOpacity_Lambda([this]()
                        {
                            ADiggerManager* Mgr = GetDiggerManager();
                            if (!Mgr) return FSlateColor(FLinearColor::Gray);
                            auto R = Mgr->RunSetupPreflight();
                            return FSlateColor(R.bHasRVT
                                ? FLinearColor(0.2f, 0.9f, 0.2f)
                                : FLinearColor(0.9f, 0.8f, 0.1f));
                        })
                    ]
                    + SHorizontalBox::Slot().FillWidth(1.f)
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]()
                        {
                            ADiggerManager* Mgr = GetDiggerManager();
                            if (!Mgr) return FText::FromString("");
                            auto R = Mgr->RunSetupPreflight();
                            return R.bHasRVT
                                ? FText::FromString("RVT found on landscape")
                                : FText::FromString(
                                    "No RVT found - will be created automatically");
                        })
                        .AutoWrapText(true)
                        .ColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.75f, 0.75f)))
                    ]
                ]

                // Material blend mode status
                + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]()
                        {
                            ADiggerManager* Mgr = GetDiggerManager();
                            if (!Mgr) return FText::FromString("[?]");
                            auto R = Mgr->RunSetupPreflight();
                            return FText::FromString(
                                R.bAllMaterialsMasked ? "[OK]" : "[--]");
                        })
                        .ColorAndOpacity_Lambda([this]()
                        {
                            ADiggerManager* Mgr = GetDiggerManager();
                            if (!Mgr) return FSlateColor(FLinearColor::Gray);
                            auto R = Mgr->RunSetupPreflight();
                            return FSlateColor(R.bAllMaterialsMasked
                                ? FLinearColor(0.2f, 0.9f, 0.2f)
                                : FLinearColor(0.9f, 0.8f, 0.1f));
                        })
                    ]
                    + SHorizontalBox::Slot().FillWidth(1.f)
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]()
                        {
                            ADiggerManager* Mgr = GetDiggerManager();
                            if (!Mgr) return FText::FromString("");
                            auto R = Mgr->RunSetupPreflight();
                            return R.bAllMaterialsMasked
                                ? FText::FromString(
                                    "Landscape material Blend Mode is Masked")
                                : FText::FromString(
                                    "Blend Mode not Masked - will be fixed automatically");
                        })
                        .AutoWrapText(true)
                        .ColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.75f, 0.75f)))
                    ]
                ]

                // MF_SetAttribs status
                + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]()
                        {
                            ADiggerManager* Mgr = GetDiggerManager();
                            if (!Mgr) return FText::FromString("[?]");
                            auto R = Mgr->RunSetupPreflight();
                            return FText::FromString(
                                R.bMFSetAttribsDetected ? "[OK]" : "[--]");
                        })
                        .ColorAndOpacity_Lambda([this]()
                        {
                            ADiggerManager* Mgr = GetDiggerManager();
                            if (!Mgr) return FSlateColor(FLinearColor::Gray);
                            auto R = Mgr->RunSetupPreflight();
                            return FSlateColor(R.bMFSetAttribsDetected
                                ? FLinearColor(0.2f, 0.9f, 0.2f)
                                : FLinearColor(0.9f, 0.8f, 0.1f));
                        })
                    ]
                    + SHorizontalBox::Slot().FillWidth(1.f)
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]()
                        {
                            ADiggerManager* Mgr = GetDiggerManager();
                            if (!Mgr) return FText::FromString("");
                            auto R = Mgr->RunSetupPreflight();
                            return R.bMFSetAttribsDetected
                                ? FText::FromString(
                                    "Opacity injection function detected")
                                : FText::FromString(
                                    "Opacity injection not found - "
                                    "will be injected automatically during setup");
                        })
                        .AutoWrapText(true)
                        .ColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.75f, 0.75f)))
                    ]
                ]
            ]
        ]

        + SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 8)
        [
            SNew(STextBlock)
            .AutoWrapText(true)
            .Text(FText::FromString(
                "Setup automatically configures everything above. "
                "Your landscape material will be modified directly - "
                "Digger injects opacity mask nodes into the material graph "
                "to enable dynamic holes. A backup of your original material "
                "is saved to /Game/Digger/Backups/ before any changes are made."))
            .ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
        ]

        // ── Main setup button ──────────────────────────────────────────────
        + SVerticalBox::Slot().AutoHeight().Padding(8, 4)
        [
            SNew(SButton)
            .HAlign(HAlign_Center)
            .VAlign(VAlign_Center)
            .ButtonColorAndOpacity_Lambda([this]()
            {
                ADiggerManager* Mgr = GetDiggerManager();
                if (!Mgr) return FLinearColor(0.3f, 0.3f, 0.3f);
                return Mgr->RunSetupPreflight().bHasLandscape
                    ? FLinearColor(0.1f, 0.4f, 0.15f)
                    : FLinearColor(0.3f, 0.3f, 0.3f);
            })
            .ContentPadding(FMargin(0, 10))
            .IsEnabled_Lambda([this]()
            {
                ADiggerManager* Mgr = GetDiggerManager();
                return Mgr != nullptr &&
                       Mgr->RunSetupPreflight().bHasLandscape;
            })
            .OnClicked_Lambda([this]() -> FReply
            {
                ADiggerManager* Mgr = GetDiggerManager();
                if (!Mgr) return FReply::Handled();

                const EAppReturnType::Type Confirm = FMessageDialog::Open(
                    EAppMsgType::YesNo,
                    FText::FromString(
                        "Digger will automatically configure your landscape "
                        "for dynamic holes. This includes:\n\n"
                        "- Creating an RVT asset if none exists\n"
                        "- Spawning an RVT Volume covering all landscapes\n"
                        "- Setting Blend Mode to Masked if needed\n"
                        "- Injecting opacity mask nodes into your landscape material\n"
                        "- Setting VirtualTextureRenderPassType to Always\n\n"
                        "A BACKUP of your original material will be saved to:\n"
                        "  /Game/Digger/Backups/\n\n"
                        "The editor will need to restart after setup for\n"
                        "changes to take effect.\n\n"
                        "Continue?"));

                if (Confirm != EAppReturnType::Yes)
                    return FReply::Handled();

                Mgr->AutoSetupLandscapeForDynamicHoles();

                // Save all dirty packages before restart
                FEditorFileUtils::SaveDirtyPackages(
                    /*bPromptUserToSave=*/ false,
                    /*bSaveMapPackages=*/ true,
                    /*bSaveContentPackages=*/ true);

                // Prompt user to restart
                const EAppReturnType::Type RestartConfirm = FMessageDialog::Open(
                    EAppMsgType::YesNo,
                    FText::FromString(
                        "Digger: Landscape setup complete!\n\n"
                        "The editor needs to restart for all material\n"
                        "changes to take effect properly.\n\n"
                        "Restart now?"));

                if (RestartConfirm == EAppReturnType::Yes)
                {
                    FUnrealEdMisc::Get().RestartEditor(/*bWarn=*/ false);
                }
                else
                {
                    FNotificationInfo Info(FText::FromString(
                        "Digger: Setup complete. Please restart the editor "
                        "for changes to take full effect."));
                    Info.ExpireDuration = 8.0f;
                    FSlateNotificationManager::Get().AddNotification(Info);
                }

                return FReply::Handled();
            })
            [
                SNew(STextBlock)
                .Justification(ETextJustify::Center)
                .Text(FText::FromString("Setup Landscape for Dynamic Holes"))
                .Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
            ]
        ]

        + SVerticalBox::Slot().AutoHeight().Padding(8, 2)
        [ SNew(SSeparator) ]

        // ── Refresh ────────────────────────────────────────────────────────
        + SVerticalBox::Slot().AutoHeight().Padding(4, 8, 4, 2)
        [
            SNew(STextBlock)
            .Text(FText::FromString("Repair / Refresh"))
            .Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 4)
        [
            SNew(STextBlock)
            .AutoWrapText(true)
            .Text(FText::FromString(
                "Re-run setup after sculpting your landscape or changing "
                "its material to update RVT Volume bounds and material assignments."))
            .ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(8, 4)
        [
            SNew(SButton)
            .HAlign(HAlign_Center)
            .ContentPadding(FMargin(0, 6))
            .OnClicked_Lambda([this]() -> FReply
            {
                if (ADiggerManager* Mgr = GetDiggerManager())
                    Mgr->AutoSetupLandscapeForDynamicHoles();
                return FReply::Handled();
            })
            [ SNew(STextBlock).Justification(ETextJustify::Center)
              .Text(FText::FromString("Refresh Landscape Setup")) ]
        ]

        + SVerticalBox::Slot().AutoHeight().Padding(8, 2)
        [ SNew(SSeparator) ]

        // ── Shadow flush ───────────────────────────────────────────────────
        + SVerticalBox::Slot().AutoHeight().Padding(4, 8, 4, 2)
        [
            SNew(STextBlock)
            .Text(FText::FromString("Shadow System"))
            .Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 8)
        [
            SNew(STextBlock)
            .AutoWrapText(true)
            .Text(FText::FromString(
                "Digger automatically invalidates shadow cache around each hole. "
                "Use the button below if you see persistent shadow artifacts."))
            .ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(8, 4)
        [
            SNew(SButton)
            .HAlign(HAlign_Center)
            .ContentPadding(FMargin(0, 6))
            .OnClicked_Lambda([this]() -> FReply
            {
                if (IConsoleVariable* CVar =
                    IConsoleManager::Get().FindConsoleVariable(
                        TEXT("r.Shadow.Virtual.Cache")))
                {
                    CVar->Set(0, ECVF_SetByCode);
                    if (GEditor)
                    {
                        GEditor->GetTimerManager()->SetTimerForNextTick(
                            FTimerDelegate::CreateLambda([CVar]()
                            {
                                CVar->Set(1, ECVF_SetByCode);
                            }));
                    }
                }
                if (GEditor) GEditor->RedrawLevelEditingViewports();
                return FReply::Handled();
            })
            [ SNew(STextBlock).Justification(ETextJustify::Center)
              .Text(FText::FromString("Force Flush Shadow Cache")) ]
        ];
}


// DMM Actions
FReply FDiggerEdModeToolkit::OnDMMHeaderClicked() { bShowMaterialManagerSection = !bShowMaterialManagerSection; return FReply::Handled(); }
FReply FDiggerEdModeToolkit::OnQuickApplyClicked() { if(Manager) Manager->ApplyMaterialProfile(GetMutableProfile()); return FReply::Handled(); }
FReply FDiggerEdModeToolkit::OnAddLayerClicked() { if(auto P = GetMutableProfile()) { P->Layers.AddDefaulted(); TouchProfile(P); if(SedimentBodyBox) SedimentBodyBox->SetContent(MakeSedimentBody()); } return FReply::Handled(); }

FReply FDiggerEdModeToolkit::OnRemoveLayerClicked(int32 LayerIndex)
{
    if(auto P = GetMutableProfile()) { if(P->Layers.IsValidIndex(LayerIndex)) { P->Layers.RemoveAt(LayerIndex); TouchProfile(P); if(SedimentBodyBox) SedimentBodyBox->SetContent(MakeSedimentBody()); } }
    return FReply::Handled();
}
FReply FDiggerEdModeToolkit::OnMoveLayerUpClicked(int32 LayerIndex)
{
    if(auto P = GetMutableProfile()) { if(LayerIndex > 0) { P->Layers.Swap(LayerIndex, LayerIndex-1); TouchProfile(P); if(SedimentBodyBox) SedimentBodyBox->SetContent(MakeSedimentBody()); } }
    return FReply::Handled();
}
FReply FDiggerEdModeToolkit::OnMoveLayerDownClicked(int32 LayerIndex)
{
    if(auto P = GetMutableProfile()) { if(LayerIndex < P->Layers.Num()-1) { P->Layers.Swap(LayerIndex, LayerIndex+1); TouchProfile(P); if(SedimentBodyBox) SedimentBodyBox->SetContent(MakeSedimentBody()); } }
    return FReply::Handled();
}

// Unused Legacy helpers (kept for VTable safety if declared in header)
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeLayerListHeader() { return SNew(SBox); }
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeLayerListBody() { return SNew(SBox); }

// Boilerplate
FName FDiggerEdModeToolkit::GetToolkitFName() const { return FName("DiggerEdMode"); }
FText FDiggerEdModeToolkit::GetBaseToolkitName() const { return LOCTEXT("ToolkitName", "Digger Toolkit"); }
FEdMode* FDiggerEdModeToolkit::GetEditorMode() const { return GLevelEditorModeTools().GetActiveMode(FDiggerEdMode::EM_DiggerEdModeId); }
TSharedPtr<SWidget> FDiggerEdModeToolkit::GetInlineContent() const { return ToolkitWidget; }

#undef LOCTEXT_NAMESPACE