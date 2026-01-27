#include "DiggerEdModeToolkit.h"
#include "DiggerEdMode.h"
#include "DiggerManager.h"
#include "DiggerFeatureFlags.h"
#include "DiggerDebug.h"
#include "BrushAssetEditorUtils.h"
#include "FCustomSDFBrush.h"

// Unreal Engine - Editor & Engine
#include "EditorModeManager.h"
#include "EditorStyleSet.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "Editor.h"

// Content Browser
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "PropertyCustomizationHelpers.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/DataAssetFactory.h"

// Slate UI - Widgets
#include "DesktopPlatformModule.h"
#include "DetailLayoutBuilder.h"
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
#include "Misc/ConfigCacheIni.h"

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
    if (Manager == GetDiggerManager())
    {
        Manager->OnIslandDetected.RemoveAll(this);
    }

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
    BindIslandDelegates();
    Manager = GetDiggerManager();

    // Style Init... (Keep your existing Style code here)
    if (!FSlateStyleRegistry::FindSlateStyle("DiggerEditorStyle"))
    {
        DiggerStyleSet = MakeShareable(new FSlateStyleSet("DiggerEditorStyle"));
        DiggerStyleSet->SetContentRoot(FPaths::ProjectContentDir());
        DiggerStyleSet->Set("DiggerEditor.GoogleIcon", new FSlateImageBrush(DiggerStyleSet->RootToContentDir(TEXT("DiggerEditor/Resources/Icons/google_icon"), TEXT(".png")), FVector2D(16, 16)));
        FSlateStyleRegistry::RegisterSlateStyle(*DiggerStyleSet);
    }
    
    AssetThumbnailPool = MakeShareable(new FAssetThumbnailPool(32, true));
    IslandGrid = SNew(SUniformGridPanel).SlotPadding(2.0f);
    TSharedPtr<SVerticalBox> DebugFlagListContainer;

    // --- BUILD MAIN WIDGET ---
    TSharedRef<SVerticalBox> MainBox = SNew(SVerticalBox);

    // 1. BRUSH TOOLS (Always Visible)
    MainBox->AddSlot().AutoHeight().Padding(8, 8, 8, 4)
    [
        SNew(SExpandableArea).AreaTitle(FText::FromString(TEXT("── Brush Tools ──")))
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
            .OnClicked_Lambda([this](){ if(Manager) Manager->RefreshLandscapeCache(); return FReply::Handled(); })
        ];

        MainBox->AddSlot().AutoHeight().Padding(8, 12, 8, 4)
        [
            SNew(SExpandableArea).AreaTitle(FText::FromString(TEXT("── Environment ──"))).BodyContent()[ EnvContent ]
        ];
    }

    // 3. ADDITIONAL TOOLS (Gated)
    if (FDiggerFeatureFlags::bEnableAdditionalTools)
    {
        MainBox->AddSlot().AutoHeight().Padding(8, 12, 8, 4)
        [
            MakeAdditionalToolsSection()
        ];
    }

    // 4. EXPORT & DATA (Gated)
    if (FDiggerFeatureFlags::bEnableExportData)
    {
        MainBox->AddSlot().AutoHeight().Padding(8, 12, 8, 4)
        [
            SNew(SExpandableArea).AreaTitle(FText::FromString(TEXT("── Export & Data ──"))).BodyContent()
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(8)[ MakeBuildExportSection() ]
                + SVerticalBox::Slot().AutoHeight().Padding(8)[ MakeSaveLoadSection() ]
                + SVerticalBox::Slot().AutoHeight().Padding(8)[ MakeResetDiggerDataWidget() ]
            ]
        ];
    }

    // 5. DEVELOPER SETTINGS (Editor Only)
    #if WITH_EDITOR && !UE_BUILD_SHIPPING
    if (FDiggerFeatureFlags::bEnableDeveloperSettings)
    {
        MainBox->AddSlot().AutoHeight().Padding(4)
        [
            SNew(SExpandableArea).AreaTitle(FText::FromString("Developer Settings")).InitiallyCollapsed(true).BodyContent()
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(2)[ SNew(SCheckBox).IsChecked_Lambda([](){ return FDiggerFeatureFlags::bEnableSplineBrush ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }).OnCheckStateChanged_Lambda([](ECheckBoxState S){ FDiggerFeatureFlags::bEnableSplineBrush = (S==ECheckBoxState::Checked); })[ SNew(STextBlock).Text(FText::FromString("Enable Spline Brush")) ] ]
                // Add other toggles here...
            ]
        ];
    }
    #endif

    // 6. DEBUG FLAGS
    MainBox->AddSlot().AutoHeight().Padding(4)
    [
        SNew(SExpandableArea).InitiallyCollapsed(true).HeaderContent()[ SNew(STextBlock).Text(FText::FromString("Debug Flags")) ]
        .BodyContent()[ SAssignNew(DebugFlagListContainer, SVerticalBox) ]
    ];

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
    const TArray<FBrushTypeInfo> EnabledBrushes = {
        { EVoxelBrushType::Sphere, TEXT("Sphere") },
        { EVoxelBrushType::Cube, TEXT("Cube") },
        { EVoxelBrushType::Light, TEXT("Light") },
        { EVoxelBrushType::Debug, TEXT("Debug") }
    };

    TSharedRef<SUniformGridPanel> ButtonGrid = SNew(SUniformGridPanel).SlotPadding(FMargin(2.0f));
    for (int32 i = 0; i < EnabledBrushes.Num(); ++i)
    {
        const auto& Info = EnabledBrushes[i];
        ButtonGrid->AddSlot(i % 3, i / 3)
        [
            SNew(SCheckBox)
            .Style(FAppStyle::Get(), "RadioButton")
            .IsChecked_Lambda([this, Info]() { return (CurrentBrushType == Info.Type) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this, Info](ECheckBoxState State) {
                if (State == ECheckBoxState::Checked) { CurrentBrushType = Info.Type; if(Manager) Manager->EditorBrushType = Info.Type; }
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
        SNew(SVerticalBox).Visibility_Lambda([this](){ return bShowBrushParameters ? EVisibility::Visible : EVisibility::Collapsed; })
        + SVerticalBox::Slot().AutoHeight()
        [
            MakeLabeledSliderRow(FText::FromString("Radius"), [this](){return BrushRadius;}, [this](float V){BrushRadius=V;}, 10.f, 1000.f, {50.f, 100.f, 500.f})
        ]
        + SVerticalBox::Slot().AutoHeight()
        [
            MakeLabeledSliderRow(FText::FromString("Strength"), [this](){return BrushStrength;}, [this](float V){BrushStrength=V;}, 0.f, 1.f, {0.1f, 0.5f, 1.f})
        ]
        + SVerticalBox::Slot().AutoHeight()
        [
            MakeLabeledSliderRow(FText::FromString("Falloff"), [this](){return BrushFalloff;}, [this](float V){BrushFalloff=V;}, 0.f, 1.f, {0.1f, 0.5f, 1.f})
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

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeWorklightSection()
{
    if (!FDiggerFeatureFlags::bEnableWorklight) return SNew(SBox).Visibility(EVisibility::Collapsed);

    return SNew(SVerticalBox)
    + SVerticalBox::Slot().AutoHeight().Padding(4)
    [
        MakeRollDownHeader("Worklight", bShowWorklightSection)
    ]
    + SVerticalBox::Slot().AutoHeight().Padding(4)
    [
        SNew(SVerticalBox).Visibility_Lambda([this](){ return bShowWorklightSection ? EVisibility::Visible : EVisibility::Collapsed; })
        + SVerticalBox::Slot().AutoHeight().Padding(4)
        [
            SNew(SComboBox<TSharedPtr<FString>>).OptionsSource(&WorklightTypeOptions)
            .OnGenerateWidget_Lambda([](TSharedPtr<FString> S){ return SNew(STextBlock).Text(FText::FromString(*S)); })
            .OnSelectionChanged_Lambda([this](TSharedPtr<FString> S, ESelectInfo::Type){ if(S){ SelectedWorkLightType = *S; UpdateWorklightType(SelectedWorkLightType); } })
            [ SNew(STextBlock).Text_Lambda([this](){ return FText::FromString(SelectedWorkLightType); }) ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(4)
        [
            SNew(SSlider).Value_Lambda([this](){ return WorklightIntensity/10000.f; }).OnValueChanged_Lambda([this](float V){ UpdateWorklightIntensity(V*10000.f); })
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(4)
        [
            SNew(SCheckBox).IsChecked_Lambda([this](){ return bWorklightEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this](ECheckBoxState S){ ToggleWorklight(S==ECheckBoxState::Checked); })
            [ SNew(STextBlock).Text(FText::FromString("Enabled")) ]
        ]
    ];
}

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeIslandsSection()
{
    if (!FDiggerFeatureFlags::bEnableIslands) return SNew(SBox).Visibility(EVisibility::Collapsed);

    return SNew(SVerticalBox)
    + SVerticalBox::Slot().AutoHeight().Padding(4)
    [
        MakeRollDownHeader("Islands", bShowIslandsSection)
    ]
    + SVerticalBox::Slot().AutoHeight().Padding(4)
    [
        SNew(SVerticalBox).Visibility_Lambda([this](){ return bShowIslandsSection ? EVisibility::Visible : EVisibility::Collapsed; })
        + SVerticalBox::Slot().AutoHeight()
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
           .AreaTitle(FText::FromString(TEXT("── Additional Tools ──")))
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
                         if (EAppReturnType::Yes == FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString("Delete save file: " + Name + "?"))) {
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

void FDiggerEdModeToolkit::BindIslandDelegates()
{
    Manager = GetDiggerManager();
    if (!IsValid(Manager)) return;
    Manager->OnIslandsDetectionStarted.RemoveAll(this);
    Manager->OnIslandDetected.RemoveAll(this);
    Manager->OnIslandsDetectionStarted.AddSP(this, &FDiggerEdModeToolkit::ClearIslands);
    Manager->OnIslandDetected.AddSP(this, &FDiggerEdModeToolkit::AddIsland);
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

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeRotationRow(const FText& Label, float& Value) { return MakeLabeledSliderRow(Label, [&](){return Value;}, [&](float V){Value=V;}, 0, 360, {}); }
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeRotationRow(const FText& Label, double& Value) { return MakeLabeledSliderRow(Label, [&](){return (float)Value;}, [&](float V){Value=V;}, 0, 360, {}); }
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeOffsetRow(const FText& Label, float& Value) { return MakeLabeledSliderRow(Label, [&](){return Value;}, [&](float V){Value=V;}, -1000, 1000, {}); }
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeOffsetRow(const FText& Label, double& Value) { return MakeLabeledSliderRow(Label, [&](){return (float)Value;}, [&](float V){Value=V;}, -1000, 1000, {}); }

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

// -----------------------------------------------------------------------------------
// 5. GENERATION SECTION
// -----------------------------------------------------------------------------------

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeGenerationSection()
{
    if (!FDiggerFeatureFlags::bEnableGenerationSection) return SNew(SBox).Visibility(EVisibility::Collapsed);

    return SNew(SExpandableArea)
        .AreaTitle(FText::FromString("Mesh Generation"))
        .BodyContent()
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(4)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4)
                [
                    SNew(STextBlock).Text(FText::FromString("Method:"))
                ]
                + SHorizontalBox::Slot().FillWidth(1.0f).Padding(4)
                [
                    SNew(SComboBox<TSharedPtr<FString>>)
                    .OptionsSource(&MeshGenerationOptions)
                    .OnGenerateWidget_Lambda([](TSharedPtr<FString> InOption) { return SNew(STextBlock).Text(FText::FromString(*InOption)); })
                    .OnSelectionChanged_Lambda([this](TSharedPtr<FString> NewSelection, ESelectInfo::Type) {
                        SelectedMeshGenerationMethod = NewSelection;
                        // Assuming Manager has a method to set this, otherwise just store UI state
                        // if(Manager) Manager->SetGenMethod(*NewSelection); 
                    })
                    .InitiallySelectedItem(SelectedMeshGenerationMethod)
                    [
                        SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(SelectedMeshGenerationMethod.IsValid() ? *SelectedMeshGenerationMethod : TEXT("Cubic")); })
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
    FText Msg = FText::FromString("WARNING: This will delete ALL voxel changes.\nAre you sure?");
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
    return SNew(SHorizontalBox)
    + SHorizontalBox::Slot().AutoWidth().Padding(2)
    [
        SNew(SButton).Text(FText::FromString("Sediment")).OnClicked_Lambda([this](){ CurrentDMMMode=EDMMPanelMode::Sediment; SaveDMMState(); return FReply::Handled(); })
    ]
    + SHorizontalBox::Slot().AutoWidth().Padding(2)
    [
        SNew(SButton).Text(FText::FromString("Landscape")).OnClicked_Lambda([this](){ CurrentDMMMode=EDMMPanelMode::Landscape; SaveDMMState(); return FReply::Handled(); })
    ]
    + SHorizontalBox::Slot().FillWidth(1.0f).Padding(4,0)
    [
        SNew(SObjectPropertyEntryBox)
        .AllowedClass(UDiggerMaterialProfile::StaticClass())
        .ObjectPath_Lambda([this](){ return ActiveMaterialProfile.IsValid() ? ActiveMaterialProfile->GetPathName() : ""; })
        .OnObjectChanged_Lambda([this](const FAssetData& Data){ ActiveMaterialProfile = Cast<UDiggerMaterialProfile>(Data.GetAsset()); SaveDMMState(); if(SedimentBodyBox.IsValid()) SedimentBodyBox->SetContent(MakeSedimentBody()); })
        .AllowClear(true)
    ];
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

// Stubs for other DMM tabs
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeLandscapeBody() { return SNew(STextBlock).Text(FText::FromString("Landscape Mode (Coming Soon)")); }
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeUtilitiesBody() { return SNew(STextBlock).Text(FText::FromString("Utilities (Coming Soon)")); }

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