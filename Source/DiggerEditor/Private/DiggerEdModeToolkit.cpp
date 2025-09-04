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
#include "SocketIOClient.h"
#include "Json.h"
#include "JsonUtilities.h"
#include "Interfaces/IPluginManager.h"

// ProcgenArcana Cave Importer Includes
#include "ProcgenArcanaCaveImporter.h"
#include "Engine/World.h"
#include "Editor.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "DrawDebugHelpers.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"  // For FAppStyle instead of FEditorStyle




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
#include "DesktopPlatformModule.h"
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
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FDiggerEdModeToolkit"


FDiggerEdModeToolkit::FDiggerEdModeToolkit()
    : FModeToolkit()
    , SocketIOLobbyManager(nullptr)
{
#if WITH_SOCKETIO
    // Create the LobbyManager UObject once, root it so GC wonâ€™t kill it
    SocketIOLobbyManager = NewObject<USocketIOLobbyManager>(GetTransientPackage());
    SocketIOLobbyManager->AddToRoot();
#endif

    // Initialize the cave importer
    CaveImporter = NewObject<UProcgenArcanaCaveImporter>();
    
    // Initialize height mode options
    HeightModeOptions.Add(MakeShareable(new FString("Flat Cave")));
    HeightModeOptions.Add(MakeShareable(new FString("Descending Cave")));
    HeightModeOptions.Add(MakeShareable(new FString("Rolling Hills")));
    HeightModeOptions.Add(MakeShareable(new FString("Mountainous")));
    HeightModeOptions.Add(MakeShareable(new FString("Custom")));

    // Initialize pivot mode options
    PivotModeOptions.Add(MakeShareable(new FString(TEXT("Spline Center"))));
    PivotModeOptions.Add(MakeShareable(new FString(TEXT("First Entrance"))));
    PivotModeOptions.Add(MakeShareable(new FString(TEXT("First Exit"))));
    PivotModeOptions.Add(MakeShareable(new FString(TEXT("Spline Start"))));
    PivotModeOptions.Add(MakeShareable(new FString(TEXT("Spline End"))));

    // Clean up any invalid entries on startup
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
    
    // Call rebuild to update UI
    RebuildCustomBrushGrid();
}

// FDiggerEdModeToolkit::Init


void FDiggerEdModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost)
{
    BindIslandDelegates();
    Manager = GetDiggerManager();

    CustomBrushEntries.Empty();
    RefreshSaveFilesList();

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

    if (!FSlateStyleRegistry::FindSlateStyle("DiggerEditorStyle"))
    {
        DiggerStyleSet = MakeShareable(new FSlateStyleSet("DiggerEditorStyle"));
        DiggerStyleSet->SetContentRoot(FPaths::ProjectContentDir());
        DiggerStyleSet->Set("DiggerEditor.GoogleIcon", new FSlateImageBrush(
            FSlateImageBrush(
                DiggerStyleSet->RootToContentDir(TEXT("DiggerEditor/Resources/Icons/google_icon"), TEXT(".png")),
                FVector2D(16, 16)
            )
        ));
        FSlateStyleRegistry::RegisterSlateStyle(*DiggerStyleSet);
    }

    AssetThumbnailPool = MakeShareable(new FAssetThumbnailPool(32, true));
    IslandGrid = SNew(SUniformGridPanel).SlotPadding(2.0f);

    static float DummyFloat = 0.0f;

    ToolkitWidget = SNew(SVerticalBox)

    // â”€â”€ Brush Tools â”€â”€ (collapsible)
    + SVerticalBox::Slot().AutoHeight().Padding(8, 8, 8, 4)
    [
        SNew(SExpandableArea)
        .AreaTitle(FText::FromString(TEXT("â”€â”€ Brush Tools â”€â”€")))
        .InitiallyCollapsed(false)
        .BodyContent()
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(8, 8, 8, 4)
            [
                MakeBrushShapeSection()
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(8)
            [
                MakeCustomBrushSection()
            ]
        ]
    ]

    // â”€â”€ Environment â”€â”€ (collapsible)
    + SVerticalBox::Slot().AutoHeight().Padding(8, 12, 8, 4)
    [
        SNew(SExpandableArea)
        .AreaTitle(FText::FromString(TEXT("â”€â”€ Environment â”€â”€")))
        .InitiallyCollapsed(false)
        .BodyContent()
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(8, 12, 8, 4)
            [
                MakeIslandsSection()
            ]
        ]
    ]

    // â”€â”€ Additional Tools â”€â”€ (collapsible)
    + SVerticalBox::Slot().AutoHeight().Padding(8, 12, 8, 4)
    [
        SNew(SExpandableArea)
        .AreaTitle(FText::FromString(TEXT("â”€â”€ Additional Tools â”€â”€")))
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
                MakeLobbySection() // DiggerConnect
            ]
        ]
    ]

    // â”€â”€ Export & Data â”€â”€ (collapsible)
    + SVerticalBox::Slot().AutoHeight().Padding(8, 12, 8, 4)
    [
        SNew(SExpandableArea)
        .AreaTitle(FText::FromString(TEXT("â”€â”€ Export & Data â”€â”€")))
        .InitiallyCollapsed(false)
        .BodyContent()
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(8, 12, 8, 4)
            [
                MakeBuildExportSection()
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(8, 12, 8, 4)
            [
                MakeSaveLoadSection()
            ]
        ]
    ]

#if WITH_EDITOR && !UE_BUILD_SHIPPING
    + SVerticalBox::Slot().AutoHeight().Padding(4)
    [
        SNew(SExpandableArea)
        .AreaTitle(FText::FromString("ðŸ”§ Developer Settings"))
        .InitiallyCollapsed(true)
        .BodyContent()
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(2)
            [
                SNew(SCheckBox)
                .IsChecked_Lambda([this]() { return FeatureFlags.bEnableSplineBrush ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { FeatureFlags.bEnableSplineBrush = (NewState == ECheckBoxState::Checked); })
                [
                    SNew(STextBlock).Text(FText::FromString("Enable Spline Brush (Dev)"))
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(2)
            [
                SNew(SCheckBox)
                .IsChecked_Lambda([this]() { return FeatureFlags.bEnableMultipleEntrances ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { FeatureFlags.bEnableMultipleEntrances = (NewState == ECheckBoxState::Checked); })
                [
                    SNew(STextBlock).Text(FText::FromString("Enable Multiple Entrances (Dev)"))
                ]
            ]
        ]
    ]
#endif
    ;

    ScanCustomBrushFolder();
    if (CustomBrushGrid.IsValid())
    {
        RebuildCustomBrushGrid();
    }

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


bool FDiggerEdModeToolkit::GetBrushIsFilled()
{
    return bIsFilled;
}

void FDiggerEdModeToolkit::AddIsland(const FIslandData& Island)
{
    if (Island.IslandID != NAME_None)
    {
        IslandIDs.Add(Island.IslandID);
    }
    else
    if (DiggerDebug::Islands)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Attempted to add island with invalid ID."));
    }

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

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeCustomBrushSection()
{
    return SNew(SVerticalBox)

    // Header Button (Fold/Unfold)
    + SVerticalBox::Slot()
    .AutoHeight()
    .Padding(4)
    [
        SNew(SButton)
        .OnClicked_Lambda([this]() -> FReply
        {
            bShowCustomBrushSection = !bShowCustomBrushSection;
            return FReply::Handled();
        })
        [
            SNew(STextBlock)
            .Text_Lambda([this]()
            {
                return FText::FromString(
                    bShowCustomBrushSection
                    ? TEXT("â–¼ Custom Brushes")
                    : TEXT("â–º Custom Brushes")
                );
            })
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
        ]
    ]

    // Collapsible Section
    + SVerticalBox::Slot()
    .AutoHeight()
    .Padding(4)
    [
        SNew(SVerticalBox)
        .Visibility_Lambda([this]()
        {
            return bShowCustomBrushSection ? EVisibility::Visible : EVisibility::Collapsed;
        })

        // Empty State Message (when no brushes)
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8, 4, 8, 4)
        [
            SNew(SHorizontalBox)
            .Visibility_Lambda([this]()
            {
                return CustomBrushEntries.Num() == 0 ? EVisibility::Visible : EVisibility::Collapsed;
            })
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(0, 0, 8, 0)
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("âš ")))
                .Font(FAppStyle::GetFontStyle("PropertyWindow.LargeFont"))
                .ColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.8f, 0.2f)))
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("No custom brushes have been added yet.")))
                .Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
                .ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
            ]
        ]

        // The grid container (only when brushes exist)
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8, 4, 8, 4)
        [
            SAssignNew(CustomBrushGridContainer, SVerticalBox)
            .Visibility_Lambda([this]()
            {
                return CustomBrushEntries.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
            })
            
            // The actual grid inside the container
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SAssignNew(CustomBrushGrid, SUniformGridPanel)
            ]
        ]

        // The "Add Custom Brush" button
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8, 4, 8, 4)
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
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8, 4, 8, 8)
        [
            SNew(SButton)
            .Text(FText::FromString("Convert to SDF Brush"))
            .IsEnabled_Lambda([this]()
            {
                return SelectedBrushIndex >= 0 &&
                       CustomBrushEntries.IsValidIndex(SelectedBrushIndex) &&
                       CustomBrushEntries[SelectedBrushIndex].IsMesh() &&
                       !CustomBrushEntries[SelectedBrushIndex].IsSDF();
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
    ];
}

// || Rebuild Custom Brush Grid ||
void FDiggerEdModeToolkit::RebuildCustomBrushGrid()
{
    if (!CustomBrushGrid.IsValid())
        return;

    CustomBrushGrid->ClearChildren();

    const int32 Cols = 4;

    // Clamp selection
    if (CustomBrushEntries.Num() == 0 || SelectedBrushIndex >= CustomBrushEntries.Num())
    {
        SelectedBrushIndex = INDEX_NONE;
    }

    // Don't add anything to the grid when empty
    if (CustomBrushEntries.Num() == 0)
    {
        if (CustomBrushGridContainer.IsValid())
        {
            CustomBrushGridContainer->Invalidate(EInvalidateWidgetReason::Layout);
        }
        return;
    }

    // Add valid brushes to the grid
    for (int32 i = 0; i < CustomBrushEntries.Num(); ++i)
    {
        const FCustomBrushEntry& Entry = CustomBrushEntries[i];

        TSharedRef<SWidget> ThumbWidget =
            (Entry.Thumbnail.IsValid() && Entry.Thumbnail->GetViewportRenderTargetTexture())
            ? SNew(SBox)
                .WidthOverride(64)
                .HeightOverride(64)
                [
                    Entry.Thumbnail->MakeThumbnailWidget()
                ]
            : SNew(SBox)
                .WidthOverride(64)
                .HeightOverride(64)
                [
                    SNew(SBorder)
                    .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                    .HAlign(HAlign_Center)
                    .VAlign(VAlign_Center)
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(Entry.IsSDF() ? TEXT("SDF") : TEXT("Mesh")))
                        .Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
                    ]
                ];

        CustomBrushGrid->AddSlot(i % Cols, i / Cols)
        [
            SNew(SBox)
            .WidthOverride(70)
            .HeightOverride(70)
            [
                SNew(SOverlay)
                
                // Main brush button
                + SOverlay::Slot()
                [
                    SNew(SBorder)
                    .BorderBackgroundColor_Lambda([this, i]()
                    {
                        return (i == SelectedBrushIndex)
                            ? FLinearColor(0.f, 0.5f, 1.f)
                            : FLinearColor::Transparent;
                    })
                    .Padding(1)
                    .OnMouseButtonDown(FPointerEventHandler::CreateLambda(
                        [this, i](const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) -> FReply
                        {
                            if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
                            {
                                SelectedBrushIndex = i;
                                SetCurrentBrushType(EVoxelBrushType::Custom);
                                return FReply::Handled();
                            }
                            return FReply::Unhandled();
                        }))
                    [
                        ThumbWidget
                    ]
                ]
                
                // Delete X button overlay (top-right corner)
                + SOverlay::Slot()
                .HAlign(HAlign_Right)
                .VAlign(VAlign_Top)
                .Padding(0, 0, 2, 0)
                [
                    SNew(SBox)
                    .WidthOverride(16)
                    .HeightOverride(16)
                    [
                        SNew(SButton)
                        .ButtonStyle(FAppStyle::Get(), "NoBorder")
                        .ContentPadding(0)
                        .ToolTipText(FText::FromString("Delete Brush"))
                        .OnClicked_Lambda([this, i]() -> FReply
                        {
                            // Show confirmation dialog
                            FText Title = FText::FromString("Delete Custom Brush");
                            FText Message = FText::FromString("Are you sure you want to delete this brush?");
                            
                            EAppReturnType::Type Result = FMessageDialog::Open(
                                EAppMsgType::YesNo,
                                Message,
                                &Title
                            );
                            
                            if (Result == EAppReturnType::Yes)
                            {
                                DeleteCustomBrush(i);
                            }
                            
                            return FReply::Handled();
                        })
                        [
                            SNew(SBorder)
                            .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                            .BorderBackgroundColor(FLinearColor(0.8f, 0.2f, 0.2f, 0.9f))
                            .HAlign(HAlign_Center)
                            .VAlign(VAlign_Center)
                            [
                                SNew(STextBlock)
                                .Text(FText::FromString(TEXT("âœ•")))
                                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
                                .ColorAndOpacity(FLinearColor::White)
                            ]
                        ]
                    ]
                ]
            ]
        ];
    }

    // Force the parent container to refresh
    if (CustomBrushGridContainer.IsValid())
    {
        CustomBrushGridContainer->Invalidate(EInvalidateWidgetReason::Layout);
    }
}

// Helper method for brush deletion.
void FDiggerEdModeToolkit::DeleteCustomBrush(int32 Index)
{
    if (!CustomBrushEntries.IsValidIndex(Index))
        return;
    
    FCustomBrushEntry& Entry = CustomBrushEntries[Index];
    
    // If it's an SDF brush, delete the file
    if (Entry.IsSDF() && !Entry.SDFBrushFilePath.IsEmpty())
    {
        IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
        if (PlatformFile.FileExists(*Entry.SDFBrushFilePath))
        {
            PlatformFile.DeleteFile(*Entry.SDFBrushFilePath);
        }
    }
    
    // Remove from array
    CustomBrushEntries.RemoveAt(Index);
    
    // Adjust selected index if needed
    if (SelectedBrushIndex >= CustomBrushEntries.Num())
    {
        SelectedBrushIndex = CustomBrushEntries.Num() - 1;
    }
    if (CustomBrushEntries.Num() == 0)
    {
        SelectedBrushIndex = INDEX_NONE;
    }
    
    // Rebuild the grid
    RebuildCustomBrushGrid();
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
            ? FText::FromString(FString::Printf(TEXT("%.0fÂ°"), Value))
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
            .ToolTipText(FText::FromString("Mirror (add 180Â°)"))
            [
                SNew(STextBlock)
                .Font(DefaultFont)
                .Text(FText::FromString("Mirror"))
            ]
        ];
    }

    return Box;
}

ECheckBoxState FDiggerEdModeToolkit::GetHiddenSeamCheckState() const
{
    return bHiddenSeam ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FDiggerEdModeToolkit::OnHiddenSeamChanged(ECheckBoxState NewState)
{
    bHiddenSeam = (NewState == ECheckBoxState::Checked);
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

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeDebugCheckbox(const FString& Label, bool* FlagPtr)
{
    return SNew(SCheckBox)
    .IsChecked_Lambda([FlagPtr, Label]() -> ECheckBoxState
    {
        const bool bChecked = *FlagPtr;
        UE_LOG(LogTemp, Verbose, TEXT("[DebugFlags] Checkbox for %s is currently: %s"),
            *Label,
            bChecked ? TEXT("Checked") : TEXT("Unchecked"));
        return bChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
    })
    .OnCheckStateChanged_Lambda([FlagPtr, Label](ECheckBoxState NewState)
    {
        const bool bIsChecked = (NewState == ECheckBoxState::Checked);
        *FlagPtr = bIsChecked;
        UE_LOG(LogTemp, Warning, TEXT("[DebugFlags] %s was %s"), *Label, bIsChecked ? TEXT("Enabled") : TEXT("Disabled"));
    })
    .Content()
    [
        SNew(STextBlock)
        .Text(FText::FromString(Label))
    ];
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
// - bIsAngle: true for angle (adds Mirror, Â°), false for number.
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

// ||| Brush Shape Section |||
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
        { EVoxelBrushType::Capsule,      TEXT("Capsule"),       TEXT("Capsule Brush") },
        { EVoxelBrushType::Cone,         TEXT("Cone"),          TEXT("Cone Brush") },
        { EVoxelBrushType::Torus,        TEXT("Torus"),         TEXT("Torus Brush") },
        { EVoxelBrushType::Pyramid,      TEXT("Pyramid"),       TEXT("Pyramid Brush") },
        { EVoxelBrushType::Icosphere,    TEXT("Icosphere"),     TEXT("Icosphere Brush") },
        { EVoxelBrushType::Stairs,       TEXT("Stairs"),        TEXT("Stairs Brush") },
        { EVoxelBrushType::Custom,       TEXT("Custom"),        TEXT("Custom Mesh Brush") },
        { EVoxelBrushType::Smooth,       TEXT("Smooth"),        TEXT("Smooth Brush") },
        { EVoxelBrushType::Noise,        TEXT("Noise"),         TEXT("Noise Brush") },
        { EVoxelBrushType::Light,        TEXT("Light"),         TEXT("Light Brush") },
        { EVoxelBrushType::Debug,        TEXT("Debug"),         TEXT("Debug Clicked Chunk Brush") }
    };

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
            .IsChecked_Lambda([this, Info]() {
                return (CurrentBrushType == Info.Type) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
            })
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

    float DummyFloat;
    return SNew(SVerticalBox)

        // Header Button: Brush Shape
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(4)
        [
            SNew(SButton)
            .OnClicked_Lambda([this]() -> FReply
            {
                bShowBrushShapeSection = !bShowBrushShapeSection;
                return FReply::Handled();
            })
            [
                SNew(STextBlock)
                .Text_Lambda([this]()
                {
                    return FText::FromString(
                        bShowBrushShapeSection
                        ? TEXT("â–¼ Brush Shape & Parameters")
                        : TEXT("â–º Brush Shape & Parameters")
                    );
                })
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
            ]
        ]

        // Collapsible Content
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(4)
        [
            SNew(SVerticalBox)
            .Visibility_Lambda([this]() {
                return bShowBrushShapeSection ? EVisibility::Visible : EVisibility::Collapsed;
            })

            // Brush Shape Grid
            + SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 8)
            [
                ButtonGrid
            ]
            
            // === BRUSH-SPECIFIC PARAMETERS ===
            
            // --- Debug Brush Settings (only for Debug brush) ---
            + SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 4)
            [
                SNew(SBox)
                .Visibility_Lambda([this]() {
                    return GetCurrentBrushType() == EVoxelBrushType::Debug ? EVisibility::Visible : EVisibility::Collapsed;
                })
                [
                    SNew(SExpandableArea)
                    .InitiallyCollapsed(false)
                    .HeaderContent()
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString("Debug Brush Options"))
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
                    ]
                    .BodyContent()
                    [
                        SNew(SVerticalBox)
                        
                        // Chunk Debugging
                        + SVerticalBox::Slot().AutoHeight().Padding(2)
                        [
                            SNew(SExpandableArea)
                            .InitiallyCollapsed(true)
                            .HeaderContent()
                            [
                                SNew(STextBlock).Text(FText::FromString("Chunk"))
                            ]
                            .BodyContent()
                            [
                                SNew(SVerticalBox)
                                + SVerticalBox::Slot().AutoHeight().Padding(1)
                                [
                                    this->MakeDebugCheckbox(TEXT("Draw Chunk"), &DiggerDebug::Chunks)
                                ]
                                + SVerticalBox::Slot().AutoHeight().Padding(1)
                                [
                                    this->MakeDebugCheckbox(TEXT("Log Chunk Data"), &DiggerDebug::Cache)
                                ]
                                + SVerticalBox::Slot().AutoHeight().Padding(1)
                                [
                                    this->MakeDebugCheckbox(TEXT("Log Marching Cubes"), &DiggerDebug::Mesh)
                                ]
                                + SVerticalBox::Slot().AutoHeight().Padding(1)
                                [
                                    this->MakeDebugCheckbox(TEXT("Log Grid Ownership"), &DiggerDebug::Manager)
                                ]
                            ]
                        ]

                        // Voxels Debugging
                        + SVerticalBox::Slot().AutoHeight().Padding(2)
                        [
                            SNew(SExpandableArea)
                            .InitiallyCollapsed(true)
                            .HeaderContent()
                            [
                                SNew(STextBlock).Text(FText::FromString("Voxels"))
                            ]
                            .BodyContent()
                            [
                                SNew(SVerticalBox)
                                + SVerticalBox::Slot().AutoHeight().Padding(1)
                                [
                                    this->MakeDebugCheckbox(TEXT("Visualize Grid"), &DiggerDebug::Space)
                                ]
                                + SVerticalBox::Slot().AutoHeight().Padding(1)
                                [
                                    this->MakeDebugCheckbox(TEXT("Draw All Voxels"), &DiggerDebug::Voxels)
                                ]
                                + SVerticalBox::Slot().AutoHeight().Padding(1)
                                [
                                    this->MakeDebugCheckbox(TEXT("Log Voxel Data"), &DiggerDebug::IO)
                                ]
                                + SVerticalBox::Slot().AutoHeight().Padding(1)
                                [
                                    this->MakeDebugCheckbox(TEXT("Log Grid Contents"), &DiggerDebug::UserConv)
                                ]
                            ]
                        ]

                        // Manager Debugging
                        + SVerticalBox::Slot().AutoHeight().Padding(2)
                        [
                            SNew(SExpandableArea)
                            .InitiallyCollapsed(true)
                            .HeaderContent()
                            [
                                SNew(STextBlock).Text(FText::FromString("Digger Manager"))
                            ]
                            .BodyContent()
                            [
                                SNew(SVerticalBox)
                                + SVerticalBox::Slot().AutoHeight().Padding(1)
                                [
                                    this->MakeDebugCheckbox(TEXT("Log All Chunk Data"), &DiggerDebug::Chunks)
                                ]
                                + SVerticalBox::Slot().AutoHeight().Padding(1)
                                [
                                    this->MakeDebugCheckbox(TEXT("Log All Grid Data"), &DiggerDebug::UserConv)
                                ]
                                + SVerticalBox::Slot().AutoHeight().Padding(1)
                                [
                                    this->MakeDebugCheckbox(TEXT("Draw All Chunks"), &DiggerDebug::Brush)
                                ]
                                + SVerticalBox::Slot().AutoHeight().Padding(1)
                                [
                                    this->MakeDebugCheckbox(TEXT("Draw All Grids"), &DiggerDebug::Context)
                                ]
                                + SVerticalBox::Slot().AutoHeight().Padding(1)
                                [
                                    this->MakeDebugCheckbox(TEXT("Log Context States"), &DiggerDebug::Context)
                                ]
                            ]
                        ]
                    ]
                ]
            ]

            // --- Light Brush Settings (only for Light brush) ---
            + SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 4)
            [
                SNew(SBox)
                .Visibility_Lambda([this]()
                {
                    return (GetCurrentBrushType() == EVoxelBrushType::Light) ? EVisibility::Visible : EVisibility::Collapsed;
                })
                [
                    SNew(SVerticalBox)
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
                                    CurrentLightType = *NewSelection;
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

            // --- Height Parameter (Cylinder, Cube, Capsule, Cone) ---
            + SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 4)
            [
                SNew(SBox)
                .Visibility_Lambda([this]() {
                    EVoxelBrushType BrushType = GetCurrentBrushType();
                    return (BrushType == EVoxelBrushType::Cylinder || BrushType == EVoxelBrushType::Cube || 
                            BrushType == EVoxelBrushType::Capsule || BrushType == EVoxelBrushType::Cone) 
                            ? EVisibility::Visible : EVisibility::Collapsed;
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

            // --- Filled/Hollow Checkbox (Cone, Cylinder) ---
            + SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 4)
            [
                SNew(SBox)
                .Visibility_Lambda([this]()
                {
                    EVoxelBrushType BrushType = GetCurrentBrushType();
                    return (BrushType == EVoxelBrushType::Cone || BrushType == EVoxelBrushType::Cylinder)
                           ? EVisibility::Visible : EVisibility::Collapsed;
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

            // --- Operation (Add/Subtract) Section ---
            + SVerticalBox::Slot().AutoHeight().Padding(4)
            [
                MakeOperationSection()
            ]

            // --- Advanced Cube Settings (only for Cube) ---
            + SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 4)
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
                        ]
                    ]
                ]
            ]

            // --- Inner Radius (Torus only) ---
            + SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 4)
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

            // --- Cone Angle (Cone, Cylinder, Light) ---
            + SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 4)
            [
                SNew(SBox)
                .Visibility_Lambda([this]() {
                    EVoxelBrushType BrushType = GetCurrentBrushType();
                    return (BrushType == EVoxelBrushType::Cone || BrushType == EVoxelBrushType::Cylinder || 
                            BrushType == EVoxelBrushType::Light) ? EVisibility::Visible : EVisibility::Collapsed;
                })
                [
                    MakeLabeledSliderRow(
                        FText::FromString("Cone Angle"),
                        [this]() { return float(ConeAngle); },
                        [this](float NewValue) { ConeAngle = FMath::Clamp(double(NewValue), MinConeAngle, MaxConeAngle); },
                        float(MinConeAngle), float(MaxConeAngle),
                        TArray<float>({float(MinConeAngle), float((MinConeAngle+MaxConeAngle)/2.f), float(MaxConeAngle)}),
                        float(MinConeAngle), 1.0f,
                        false, &DummyFloat
                    )
                ]
            ]

            // --- Smooth Iterations (Smooth only) ---
            + SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 4)
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

            // === GENERAL BRUSH PARAMETERS (always visible when section is expanded) ===
            
            // Divider
            + SVerticalBox::Slot().AutoHeight().Padding(8, 8, 8, 4)
            [
                SNew(SSeparator)
                .Orientation(Orient_Horizontal)
            ]

            // Radius
            + SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 4)
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

            // Strength
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

            // Falloff
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
        ];
}




        
// Add this method to your FDiggerEdModeToolkit class

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeSaveLoadSection()
{
    return SNew(SVerticalBox)

    // Header Button (Fold/Unfold)
    + SVerticalBox::Slot()
    .AutoHeight()
    .Padding(4)
    [
        SNew(SButton)
        .OnClicked_Lambda([this]() -> FReply
        {
            bShowSaveLoadSection = !bShowSaveLoadSection;
            return FReply::Handled();
        })
        [
            SNew(STextBlock)
            .Text_Lambda([this]()
            {
                return FText::FromString(
                    bShowSaveLoadSection
                    ? TEXT("â–¼ Chunk Serialization")
                    : TEXT("â–º Chunk Serialization")
                );
            })
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
        ]
    ]

    // Collapsible Section
    + SVerticalBox::Slot()
    .AutoHeight()
    .Padding(4)
    [
        SNew(SVerticalBox)
        .Visibility_Lambda([this]()
        {
            return bShowSaveLoadSection ? EVisibility::Visible : EVisibility::Collapsed;
        })

        // Save File Name Input
        + SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 4)
        [
            SNew(SHorizontalBox)
            
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0).VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(FText::FromString("Save File:"))
                .Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
            ]
            
            + SHorizontalBox::Slot().FillWidth(1.0f)
            [
                SAssignNew(SaveFileNameWidget, SEditableTextBox)
                .Text(FText::FromString("Default"))
                .HintText(FText::FromString("Enter save file name..."))
                .Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
            ]
        ]

        // Save File Selection Dropdown
        + SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 4)
        [
            SNew(SHorizontalBox)
            
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0).VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(FText::FromString("Load File:"))
                .Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
            ]
            
            + SHorizontalBox::Slot().FillWidth(1.0f)
            [
                SAssignNew(SaveFileComboBox, SComboBox<TSharedPtr<FString>>)
                .OptionsSource(&AvailableSaveFiles)
                .OnGenerateWidget_Lambda([](TSharedPtr<FString> InOption)
                {
                    return SNew(STextBlock).Text(FText::FromString(*InOption));
                })
                .OnSelectionChanged_Lambda([this](TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
                {
                    if (NewSelection.IsValid())
                    {
                        SaveFileNameWidget->SetText(FText::FromString(*NewSelection));
                    }
                })
                [
                    SNew(STextBlock)
                    .Text_Lambda([this]() -> FText
                    {
                        TSharedPtr<FString> Selected = SaveFileComboBox->GetSelectedItem();
                        return Selected.IsValid() ? FText::FromString(*Selected) : FText::FromString("Select save file...");
                    })
                ]
            ]
            
            + SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0)
            [
                SNew(SButton)
                .Text(FText::FromString("Refresh"))
                .ToolTipText(FText::FromString("Refresh the list of available save files"))
                .OnClicked_Lambda([this]() -> FReply {
                    RefreshSaveFilesList();
                    return FReply::Handled();
                })
            ]
        ]

        // Save/Load Buttons Row
        + SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 4)
        [
            SNew(SHorizontalBox)

            // Save All Chunks Button
            + SHorizontalBox::Slot().FillWidth(0.33f).Padding(0, 0, 4, 0)
            [
                SNew(SButton)
                .Text(FText::FromString("Save All Chunks"))
                .ToolTipText(FText::FromString("Save all currently loaded chunks to the specified save file"))
                .HAlign(HAlign_Center)
                .VAlign(VAlign_Center)
                .OnClicked_Lambda([this]() -> FReply {
                    if (Manager == GetDiggerManager())
                    {
                        FString SaveFileName = SaveFileNameWidget->GetText().ToString().TrimStartAndEnd();
                        if (SaveFileName.IsEmpty())
                        {
                            SaveFileName = "Default";
                        }
                        
                        bool bSuccess = Manager->SaveAllChunks(SaveFileName);
                        if (bSuccess)
                        {
                            // Show success notification
                            FNotificationInfo Info(FText::FromString(FString::Printf(TEXT("Successfully saved all chunks to '%s'"), *SaveFileName)));
                            Info.ExpireDuration = 3.0f;
                            Info.bUseSuccessFailIcons = true;
                            FSlateNotificationManager::Get().AddNotification(Info);
                            
                            UE_LOG(LogTemp, Log, TEXT("Save All Chunks to '%s': Success"), *SaveFileName);
                            
                            // Refresh the save files list
                            RefreshSaveFilesList();
                        }
                        else
                        {
                            // Show error notification
                            FNotificationInfo Info(FText::FromString(FString::Printf(TEXT("Failed to save chunks to '%s' - check log"), *SaveFileName)));
                            Info.ExpireDuration = 5.0f;
                            Info.bUseSuccessFailIcons = true;
                            FSlateNotificationManager::Get().AddNotification(Info);
                            
                            UE_LOG(LogTemp, Error, TEXT("Save All Chunks to '%s': Failed"), *SaveFileName);
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
            + SHorizontalBox::Slot().FillWidth(0.33f).Padding(2, 0, 2, 0)
            [
                SNew(SButton)
                .Text(FText::FromString("Load All Chunks"))
                .ToolTipText(FText::FromString("Load all saved chunks from the specified save file"))
                .HAlign(HAlign_Center)
                .VAlign(VAlign_Center)
                .OnClicked_Lambda([this]() -> FReply {
                    if (Manager == GetDiggerManager())
                    {
                        FString SaveFileName = SaveFileNameWidget->GetText().ToString().TrimStartAndEnd();
                        if (SaveFileName.IsEmpty())
                        {
                            SaveFileName = "Default";
                        }
                        
                        bool bSuccess = Manager->LoadAllChunks(SaveFileName);
                        if (bSuccess)
                        {
                            // Show success notification
                            FNotificationInfo Info(FText::FromString(FString::Printf(TEXT("Successfully loaded all chunks from '%s'"), *SaveFileName)));
                            Info.ExpireDuration = 3.0f;
                            Info.bUseSuccessFailIcons = true;
                            FSlateNotificationManager::Get().AddNotification(Info);
                            
                            UE_LOG(LogTemp, Log, TEXT("Load All Chunks from '%s': Success"), *SaveFileName);
                        }
                        else
                        {
                            // Show error notification
                            FNotificationInfo Info(FText::FromString(FString::Printf(TEXT("Failed to load chunks from '%s' - check log"), *SaveFileName)));
                            Info.ExpireDuration = 5.0f;
                            Info.bUseSuccessFailIcons = true;
                            FSlateNotificationManager::Get().AddNotification(Info);
                            
                            UE_LOG(LogTemp, Error, TEXT("Load All Chunks from '%s': Failed"), *SaveFileName);
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
                    
                    FString SaveFileName = SaveFileNameWidget->GetText().ToString().TrimStartAndEnd();
                    if (SaveFileName.IsEmpty())
                    {
                        SaveFileName = "Default";
                    }
                    
                    // Check if there are any saved chunks to load for this save file
                    TArray<FIntVector> SavedChunks = Manager->GetAllSavedChunkCoordinates(SaveFileName);
                    return SavedChunks.Num() > 0;
                })
            ]

            // Delete Save File Button
            + SHorizontalBox::Slot().FillWidth(0.33f).Padding(4, 0, 0, 0)
            [
                SNew(SButton)
                .Text(FText::FromString("Delete Save File"))
                .ToolTipText(FText::FromString("Delete the specified save file and all its chunks"))
                .HAlign(HAlign_Center)
                .VAlign(VAlign_Center)
                .ButtonColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.2f, 0.2f, 1.0f)))
                .OnClicked_Lambda([this]() -> FReply {
                    if (Manager == GetDiggerManager())
                    {
                        FString SaveFileName = SaveFileNameWidget->GetText().ToString().TrimStartAndEnd();
                        if (SaveFileName.IsEmpty())
                        {
                            SaveFileName = "Default";
                        }
                        
                        // Show confirmation dialog
                        FText DialogTitle = FText::FromString("Delete Save File");
                        FText DialogText = FText::FromString(FString::Printf(TEXT("Are you sure you want to delete save file '%s'? This action cannot be undone."), *SaveFileName));
                        
                        EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::YesNo, DialogText, &DialogTitle);
                        
                        if (Result == EAppReturnType::Yes)
                        {
                            bool bSuccess = Manager->DeleteSaveFile(SaveFileName);
                            if (bSuccess)
                            {
                                // Show success notification
                                FNotificationInfo Info(FText::FromString(FString::Printf(TEXT("Successfully deleted save file '%s'"), *SaveFileName)));
                                Info.ExpireDuration = 3.0f;
                                Info.bUseSuccessFailIcons = true;
                                FSlateNotificationManager::Get().AddNotification(Info);
                                
                                UE_LOG(LogTemp, Log, TEXT("Delete Save File '%s': Success"), *SaveFileName);
                                
                                // Refresh the save files list and clear the text box
                                RefreshSaveFilesList();
                                SaveFileNameWidget->SetText(FText::FromString("Default"));
                            }
                            else
                            {
                                // Show error notification
                                FNotificationInfo Info(FText::FromString(FString::Printf(TEXT("Failed to delete save file '%s' - check log"), *SaveFileName)));
                                Info.ExpireDuration = 5.0f;
                                Info.bUseSuccessFailIcons = true;
                                FSlateNotificationManager::Get().AddNotification(Info);
                                
                                UE_LOG(LogTemp, Error, TEXT("Delete Save File '%s': Failed"), *SaveFileName);
                            }
                        }
                    }
                    else
                    {
                        UE_LOG(LogTemp, Error, TEXT("Delete Save File: No DiggerManager found"));
                    }
                    return FReply::Handled();
                })
                .IsEnabled_Lambda([this]() -> bool {
                    Manager = GetDiggerManager();
                    if (!Manager) return false;
                    
                    FString SaveFileName = SaveFileNameWidget->GetText().ToString().TrimStartAndEnd();
                    if (SaveFileName.IsEmpty())
                    {
                        SaveFileName = "Default";
                    }
                    
                    // Only enable if the save file exists
                    return Manager->DoesSaveFileExist(SaveFileName);
                })
            ]
        ]

        // Statistics Row
        + SVerticalBox::Slot().AutoHeight().Padding(8, 8, 8, 8)
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

            // Saved Chunks Count for current save file
            + SHorizontalBox::Slot().FillWidth(0.5f).Padding(4, 0, 0, 0)
            [
                SNew(STextBlock)
                .Text_Lambda([this]() -> FText {
                    if (Manager == GetDiggerManager())
                    {
                        FString SaveFileName = SaveFileNameWidget->GetText().ToString().TrimStartAndEnd();
                        if (SaveFileName.IsEmpty())
                        {
                            SaveFileName = "Default";
                        }
                        
                        TArray<FIntVector> SavedChunks = Manager->GetAllSavedChunkCoordinates(SaveFileName);
                        return FText::FromString(FString::Printf(TEXT("Saved (%s): %d"), *SaveFileName, SavedChunks.Num()));
                    }
                    return FText::FromString("Saved: 0");
                })
                .Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
                .ToolTipText(FText::FromString("Number of chunks saved to disk for the current save file"))
            ]
        ]
    ];
}

// Build/Export Section
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeBuildExportSection()
{
    float DummyFloat;
    return SNew(SVerticalBox)

    // Header Button (Fold/Unfold)
    + SVerticalBox::Slot()
    .AutoHeight()
    .Padding(4)
    [
        SNew(SButton)
        .OnClicked_Lambda([this]() -> FReply
        {
            bShowBuildSettings = !bShowBuildSettings;
            return FReply::Handled();
        })
        [
            SNew(STextBlock)
            .Text_Lambda([this]()
            {
                return FText::FromString(
                    bShowBuildSettings
                    ? TEXT("â–¼ Build/Export Settings")
                    : TEXT("â–º Build/Export Settings")
                );
            })
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
        ]
    ]

    // Collapsible Section
    + SVerticalBox::Slot()
    .AutoHeight()
    .Padding(4)
    [
        SNew(SVerticalBox)
        .Visibility_Lambda([this]()
        {
            return bShowBuildSettings ? EVisibility::Visible : EVisibility::Collapsed;
        })

        // Enable Collision Checkbox
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8, 4, 8, 4)
        [
            SNew(SCheckBox)
            .IsChecked_Lambda([this]()
            {
                return bEnableCollision ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
            })
            .OnCheckStateChanged_Lambda([this](ECheckBoxState State)
            {
                bEnableCollision = (State == ECheckBoxState::Checked);
            })
            [
                SNew(STextBlock)
                .Text(FText::FromString("Enable Collision"))
                .Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
            ]
        ]

        // Enable Nanite Checkbox
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8, 4, 8, 4)
        [
            SNew(SCheckBox)
            .IsChecked_Lambda([this]()
            {
                return bEnableNanite ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
            })
            .OnCheckStateChanged_Lambda([this](ECheckBoxState State)
            {
                bEnableNanite = (State == ECheckBoxState::Checked);
            })
            [
                SNew(STextBlock)
                .Text(FText::FromString("Enable Nanite"))
                .Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
            ]
        ]

        // Detail Reduction Slider
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8, 4, 8, 4)
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

        // Bake to Static Mesh Button
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8, 8, 8, 8)
        [
            SNew(SButton)
            .Text(FText::FromString("Bake to Static Mesh"))
            .HAlign(HAlign_Center)
            .VAlign(VAlign_Center)
            .ToolTipText(FText::FromString("Convert the current voxel terrain to a static mesh asset"))
            .OnClicked_Lambda([this]() -> FReply
            {
                if (Manager == GetDiggerManager())
                {
                    Manager->BakeToStaticMesh(bEnableCollision, bEnableNanite, BakeDetail);
                    
                    // Show notification
                    FNotificationInfo Info(FText::FromString("Baking terrain to static mesh..."));
                    Info.ExpireDuration = 3.0f;
                    Info.bUseSuccessFailIcons = true;
                    FSlateNotificationManager::Get().AddNotification(Info);
                }
                else
                {
                    UE_LOG(LogTemp, Error, TEXT("Bake to Static Mesh: No DiggerManager found"));
                }
                return FReply::Handled();
            })
            .IsEnabled_Lambda([this]() -> bool
            {
                Manager = GetDiggerManager();
                return Manager != nullptr && Manager->ChunkMap.Num() > 0;
            })
        ]
    ];
}

void FDiggerEdModeToolkit::RefreshSaveFilesList()
{
    UE_LOG(LogTemp, Log, TEXT("RefreshSaveFilesList: Starting refresh..."));
    
    AvailableSaveFiles.Empty();
    
    Manager = GetDiggerManager(); // Make sure Manager is up to date
    if (Manager)
    {
        TArray<FString> SaveFileNames = Manager->GetAllSaveFileNames();
        UE_LOG(LogTemp, Log, TEXT("RefreshSaveFilesList: Found %d save files from manager"), SaveFileNames.Num());
        
        for (const FString& SaveFileName : SaveFileNames)
        {
            UE_LOG(LogTemp, Log, TEXT("Adding save file to list: '%s'"), *SaveFileName);
            AvailableSaveFiles.Add(MakeShareable(new FString(SaveFileName)));
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("RefreshSaveFilesList: No DiggerManager found"));
    }
    
    // Always add "Default" if it's not already there
    bool bHasDefault = false;
    for (const auto& SaveFile : AvailableSaveFiles)
    {
        if (SaveFile.IsValid() && *SaveFile == "Default")
        {
            bHasDefault = true;
            break;
        }
    }
    
    if (!bHasDefault)
    {
        AvailableSaveFiles.Insert(MakeShareable(new FString("Default")), 0);
        UE_LOG(LogTemp, Log, TEXT("Added 'Default' to save files list"));
    }
    
    UE_LOG(LogTemp, Log, TEXT("Total save files in dropdown: %d"), AvailableSaveFiles.Num());
    
    if (SaveFileComboBox.IsValid())
    {
        // Clear current selection first
        SaveFileComboBox->ClearSelection();
        
        // Refresh the options
        SaveFileComboBox->RefreshOptions();
        
        // Set default selection if available
        if (AvailableSaveFiles.Num() > 0)
        {
            // Try to find "Default" and select it
            for (const auto& SaveFile : AvailableSaveFiles)
            {
                if (SaveFile.IsValid() && *SaveFile == "Default")
                {
                    SaveFileComboBox->SetSelectedItem(SaveFile);
                    break;
                }
            }
            
            // If no default found, select the first item
            if (!SaveFileComboBox->GetSelectedItem().IsValid())
            {
                SaveFileComboBox->SetSelectedItem(AvailableSaveFiles[0]);
            }
        }
        
        UE_LOG(LogTemp, Log, TEXT("Refreshed combo box options and set selection"));
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("SaveFileComboBox is not valid"));
    }
}

bool FDiggerEdModeToolkit::IsSocketIOPluginAvailable() const
{
    return IPluginManager::Get().FindPlugin("SocketIOClient").IsValid();
}

// || DiggerConnect Main Section || 
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeLobbySection()
{
    const bool bHasPlugin = IsSocketIOPluginAvailable();

    return SNew(SVerticalBox)
        
        // Header Button: DiggerConnect
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(4)
        [
            SNew(SButton)
            .OnClicked_Lambda([this]() -> FReply
            {
                bShowLobbySection = !bShowLobbySection;
                return FReply::Handled();
            })
            [
                SNew(STextBlock)
                .Text_Lambda([this]()
                {
                    return FText::FromString(
                        bShowLobbySection
                        ? TEXT("â–¼ DiggerConnect")
                        : TEXT("â–º DiggerConnect")
                    );
                })
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
            ]
        ]

        // Collapsible Lobby Content
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(4)
        [
            SNew(SVerticalBox)
            .Visibility_Lambda([this]() {
                return bShowLobbySection ? EVisibility::Visible : EVisibility::Collapsed;
            })

            + SVerticalBox::Slot().AutoHeight().Padding(8, 12, 8, 4)
            [
                MakeDiggerConnectLicensingPanel()
            ]

            // 1) Google/Username login
            + SVerticalBox::Slot().AutoHeight().Padding(8)
            [
                SNew(SButton)
                .IsEnabled(bHasPlugin)
                .OnClicked_Lambda([this, bHasPlugin]() {
                    return bHasPlugin ? ShowLoginModal() : FReply::Handled();
                })
                [
                    SNew(STextBlock)
                    .Text(FText::FromString("Sign in with Google"))
                ]
            ]

            // 2) Connect button
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
            ]
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
                // â€¦existing HTTP + ZipUtility download logicâ€¦
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
    // Get the editor world
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("ConnectToLobbyServer: No valid EditorWorldContext."));
        return;
    }

    // Create or reuse the lobby manager with proper outer
    if (!SocketIOLobbyManager)
    {
        // Use the world as outer to ensure proper world context
        SocketIOLobbyManager = NewObject<USocketIOLobbyManager>(World);
        if (!SocketIOLobbyManager)
        {
            UE_LOG(LogTemp, Error, TEXT("ConnectToLobbyServer: Failed to create lobby manager."));
            return;
        }

        UE_LOG(LogTemp, Log, TEXT("ConnectToLobbyServer: Lobby manager created."));
    }

    // Initialize and connect
    if (SocketIOLobbyManager)
    {
        UE_LOG(LogTemp, Log, TEXT("ConnectToLobbyServer: Initializing connection..."));
        SocketIOLobbyManager->Initialize(World);
    }
#endif // WITH_SOCKETIO
}



#if WITH_SOCKETIO
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeNetworkingWidget()
{
    return SNew(SExpandableArea)
        .AreaTitle(FText::FromString("Lobby Setup"))
        .InitiallyCollapsed(true)
        .BodyContent()
        [
            SNew(SVerticalBox)

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
                    .HintText(FText::FromString("Enter lobby nameâ€¦"))
                ]

                + SHorizontalBox::Slot()
                  .AutoWidth()
                  .VAlign(VAlign_Center)
                  .Padding(6,0,0,0)
                [
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
                    .HintText(FText::FromString("Enter lobby IDâ€¦"))
                ]

                + SHorizontalBox::Slot()
                  .AutoWidth()
                  .VAlign(VAlign_Center)
                  .Padding(6,0,0,0)
                [
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
            ]
        ];
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

// DiggerEdModeToolkit.cpp

void FDiggerEdModeToolkit::LoadLicenseFromConfig()
{
    GConfig->GetString(TEXT("/Script/Digger.DiggerConnect"), TEXT("LicenseEmail"), LicenseEmail, GGameIni);
    GConfig->GetString(TEXT("/Script/Digger.DiggerConnect"), TEXT("LicenseKey"),   LicenseKey,   GGameIni);

    // Minimal default; you will replace with real validation on startup if desired.
    ApplyTierCapsFromLicense();
}

void FDiggerEdModeToolkit::SaveLicenseToConfig() const
{
    GConfig->SetString(TEXT("/Script/Digger.DiggerConnect"), TEXT("LicenseEmail"), *LicenseEmail, GGameIni);
    GConfig->SetString(TEXT("/Script/Digger.DiggerConnect"), TEXT("LicenseKey"),   *LicenseKey,   GGameIni);
    GConfig->Flush(false, GGameIni);
}

void FDiggerEdModeToolkit::ApplyTierCapsFromLicense()
{
    // Placeholder logic â€” replace with your real verification result
    // Keep Free defaults if key is empty
    if (LicenseKey.IsEmpty())
    {
        CurrentTier = EDiggerConnectTier::Free;
        ConcurrentUsersCap = 2;
        return;
    }

    // Example: decode tier from key prefix
    if (LicenseKey.StartsWith(TEXT("INDI-PERP-")))
    {
        CurrentTier = EDiggerConnectTier::Indie;
        ConcurrentUsersCap = 5;
    }
    else if (LicenseKey.StartsWith(TEXT("ENTR-PERP-")))
    {
        CurrentTier = EDiggerConnectTier::Enterprise;
        ConcurrentUsersCap = 9999;
    }
    else if (LicenseKey.StartsWith(TEXT("INDI-SUB-")))
    {
        CurrentTier = EDiggerConnectTier::IndieSub;
        ConcurrentUsersCap = 5;
    }
    else if (LicenseKey.StartsWith(TEXT("ENTR-SUB-")))
    {
        CurrentTier = EDiggerConnectTier::EnterpriseSub;
        ConcurrentUsersCap = 9999;
    }
    else
    {
        CurrentTier = EDiggerConnectTier::Free;
        ConcurrentUsersCap = 2;
    }
}

FText FDiggerEdModeToolkit::GetTierDisplayText() const
{
    switch (CurrentTier)
    {
        case EDiggerConnectTier::Free:            return FText::FromString(TEXT("Free (Non-Commercial)"));
        case EDiggerConnectTier::Indie:           return FText::FromString(TEXT("Indie (Perpetual)"));
        case EDiggerConnectTier::Enterprise:      return FText::FromString(TEXT("Enterprise (Perpetual)"));
        case EDiggerConnectTier::IndieSub:        return FText::FromString(TEXT("Indie (Subscription)"));
        case EDiggerConnectTier::EnterpriseSub:   return FText::FromString(TEXT("Enterprise (Subscription)"));
        default:                                   return FText::FromString(TEXT("Unknown"));
    }
}

FText FDiggerEdModeToolkit::GetConcurrentText() const
{
    const FString Cap = (ConcurrentUsersCap >= 9999) ? TEXT("Unlimited") : FString::FromInt(ConcurrentUsersCap);
    return FText::FromString(FString::Printf(TEXT("Concurrent Users: %d / %s"), CurrentActiveUsers, *Cap));
}

bool FDiggerEdModeToolkit::IsUpgradeVisible(EDiggerConnectTier TargetTier) const
{
    // Show upgrades if target is strictly "more" than current
    auto Rank = [](EDiggerConnectTier T)->int32 {
        switch (T) {
            case EDiggerConnectTier::Free:          return 0;
            case EDiggerConnectTier::IndieSub:      return 1;
            case EDiggerConnectTier::Indie:         return 2;
            case EDiggerConnectTier::EnterpriseSub: return 3;
            case EDiggerConnectTier::Enterprise:    return 4;
            default:                                return 0;
        }
    };
    return Rank(TargetTier) > Rank(CurrentTier);
}

FReply FDiggerEdModeToolkit::OnValidateLicenseClicked()
{
    SaveLicenseToConfig();

    // TODO: Call your online validator (HTTP) or offline signature check here.
    // For now, just re-apply the placeholder mapping:
    ApplyTierCapsFromLicense();

    // Optional: toast/log
    UE_LOG(LogTemp, Log, TEXT("DiggerConnect license validated. Tier: %s"), *GetTierDisplayText().ToString());
    return FReply::Handled();
}

FReply FDiggerEdModeToolkit::OnUpgradeTierClicked(EDiggerConnectTier TargetTier)
{
    // TODO: Open URL to your store with querystring (email/UE version/sku)
    // Example:
    // FPlatformProcess::LaunchURL(TEXT("https://digger.jacobsiler.com/upgrade?sku=indie-perp"), nullptr, nullptr);
    return FReply::Handled();
}

TSharedRef<SWidget> FDiggerEdModeToolkit::MakeDiggerConnectLicensingPanel()
{
    LoadLicenseFromConfig();

    return SNew(SBorder)
        .Padding(8)
        .BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("DiggerConnect Licensing")))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,4)
            [
                SNew(STextBlock)
                .Text(this, &FDiggerEdModeToolkit::GetTierDisplayText)
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
            [
                SNew(STextBlock)
                .Text(this, &FDiggerEdModeToolkit::GetConcurrentText)
                .ColorAndOpacity(FSlateColor(FLinearColor(0.8f,0.8f,0.8f)))
            ]

            // License email/key entry
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,4)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,8,0)
                [
                    SNew(STextBlock).Text(FText::FromString(TEXT("Email")))
                ]
                + SHorizontalBox::Slot().FillWidth(1.f)
                [
                    SNew(SEditableTextBox)
                    .Text_Lambda([this]{ return FText::FromString(LicenseEmail); })
                    .OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type){ LicenseEmail = T.ToString(); })
                ]
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,8,0)
                [
                    SNew(STextBlock).Text(FText::FromString(TEXT("License Key")))
                ]
                + SHorizontalBox::Slot().FillWidth(1.f)
                [
                    SNew(SEditableTextBox)
                    .IsPassword(true)
                    .Text_Lambda([this]{ return FText::FromString(LicenseKey); })
                    .OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type){ LicenseKey = T.ToString(); })
                ]
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)
            [
                SNew(SButton)
                .Text(FText::FromString(TEXT("Validate / Activate")))
                .HAlign(HAlign_Center)
                .OnClicked(this, &FDiggerEdModeToolkit::OnValidateLicenseClicked)
            ]

            // Upgrade buttons row
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SWrapBox).UseAllottedWidth(true)
                + SWrapBox::Slot().Padding(0,4)
                [
                    SNew(SBox)
                    .Visibility_Lambda([this]{ return IsUpgradeVisible(EDiggerConnectTier::Indie) ? EVisibility::Visible : EVisibility::Collapsed; })
                    [
                        SNew(SButton)
                        .Text(FText::FromString(TEXT("Upgrade: Indie (Perpetual)")))
                        .OnClicked(this, &FDiggerEdModeToolkit::OnUpgradeTierClicked, EDiggerConnectTier::Indie)
                    ]
                ]
                + SWrapBox::Slot().Padding(6,4)
                [
                    SNew(SBox)
                    .Visibility_Lambda([this]{ return IsUpgradeVisible(EDiggerConnectTier::Enterprise) ? EVisibility::Visible : EVisibility::Collapsed; })
                    [
                        SNew(SButton)
                        .Text(FText::FromString(TEXT("Upgrade: Enterprise (Perpetual)")))
                        .OnClicked(this, &FDiggerEdModeToolkit::OnUpgradeTierClicked, EDiggerConnectTier::Enterprise)
                    ]
                ]
                + SWrapBox::Slot().Padding(6,4)
                [
                    SNew(SBox)
                    .Visibility_Lambda([this]{ return IsUpgradeVisible(EDiggerConnectTier::IndieSub) ? EVisibility::Visible : EVisibility::Collapsed; })
                    [
                        SNew(SButton)
                        .Text(FText::FromString(TEXT("Subscribe: Indie Monthly")))
                        .OnClicked(this, &FDiggerEdModeToolkit::OnUpgradeTierClicked, EDiggerConnectTier::IndieSub)
                    ]
                ]
                + SWrapBox::Slot().Padding(6,4)
                [
                    SNew(SBox)
                    .Visibility_Lambda([this]{ return IsUpgradeVisible(EDiggerConnectTier::EnterpriseSub) ? EVisibility::Visible : EVisibility::Collapsed; })
                    [
                        SNew(SButton)
                        .Text(FText::FromString(TEXT("Subscribe: Enterprise Monthly")))
                        .OnClicked(this, &FDiggerEdModeToolkit::OnUpgradeTierClicked, EDiggerConnectTier::EnterpriseSub)
                    ]
                ]
            ]
        ];
}


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
};

//------------------------------------------------------------------------------
// ProcgenArcana Cave Importer Section
//------------------------------------------------------------------------------
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeProcgenArcanaImporterWidget()
{
    return SNew(SVerticalBox)

        // Header Button (Fold/Unfold)
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(4)
        [
            SNew(SButton)
            .OnClicked_Lambda([this]() -> FReply
            {
                bShowProcgenArcanaImporter = !bShowProcgenArcanaImporter;
                return FReply::Handled();
            })
            [
                SNew(STextBlock)
                .Text_Lambda([this]()
                {
                    return FText::FromString(
                        bShowProcgenArcanaImporter
                        ? TEXT("â–¼ Cave Import (ProcgenArcana)")
                        : TEXT("â–º Cave Import (ProcgenArcana)")
                    );
                })
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
            ]
        ]

        // Collapsible Section
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(4)
        [
            SNew(SVerticalBox)
            .Visibility_Lambda([this]()
            {
                return bShowProcgenArcanaImporter ? EVisibility::Visible : EVisibility::Collapsed;
            })
            .IsEnabled_Lambda([this]()
            {
                return UIFeatureFlags.bEnableCaveImporter; // Use the feature flags struct
            })

            // File Selection Section
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(2)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                [
                    SNew(SEditableTextBox)
                    .Text_Lambda([this]() 
                    { 
                        return FText::FromString(SelectedSVGFilePath.IsEmpty() ? TEXT("No file selected...") : SelectedSVGFilePath); 
                    })
                    .IsReadOnly(true)
                    .HintText(FText::FromString(TEXT("Select an SVG file from ProcgenArcana's map generator")))
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(4, 0, 0, 0)
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("Browse...")))
                    .OnClicked_Lambda([this]() -> FReply
                    {
                        TArray<FString> OutFileNames;
                        bool bFileSelected = FDesktopPlatformModule::Get()->OpenFileDialog(
                            FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
                            TEXT("Select ProcgenArcana Cave SVG File"),
                            TEXT(""),
                            TEXT(""),
                            TEXT("SVG Files (*.svg)|*.svg"),
                            EFileDialogFlags::None,
                            OutFileNames
                        );

                        if (bFileSelected && OutFileNames.Num() > 0)
                        {
                            SelectedSVGFilePath = OutFileNames[0];
                            UE_LOG(LogTemp, Log, TEXT("[ProcgenArcana Importer] Selected file: %s"), *SelectedSVGFilePath);
                        }
                        return FReply::Handled();
                    })
                ]
            ]

            // Enable/Disable Toggle
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(2, 8, 2, 2)
            [
                SNew(SCheckBox)
                .IsChecked_Lambda([this]() 
                { 
                    return UIFeatureFlags.bEnableCaveImporter ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; 
                })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
                {
                    UIFeatureFlags.bEnableCaveImporter = (NewState == ECheckBoxState::Checked);
                    UE_LOG(LogTemp, Log, TEXT("[ProcgenArcana Importer] Enabled: %s"), UIFeatureFlags.bEnableCaveImporter ? TEXT("true") : TEXT("false"));
                })
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("Enable ProcgenArcana Cave Importer")))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
                ]
            ]

            // Import Settings Header
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(2, 8, 2, 2)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("Import Settings")))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
            ]

            // Output Format Selection
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(2)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0, 0, 0, 2)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("Output Format:")))
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(SHorizontalBox)
                    
                    // Single Spline (Current)
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    .Padding(1)
                    [
                        SNew(SButton)
                        .ButtonColorAndOpacity_Lambda([this]() 
                        { 
                            return OutputFormat == 0 ? FLinearColor(0.1f, 0.5f, 1.0f, 1.0f) : FLinearColor::White;
                        })
                        .Text(FText::FromString(TEXT("Single\nSpline")))
                        .OnClicked_Lambda([this]() -> FReply
                        {
                            OutputFormat = 0;
                            UE_LOG(LogTemp, Log, TEXT("[ProcgenArcana Importer] Output Format: Single Spline"));
                            return FReply::Handled();
                        })
                    ]
                    
                    // Multi-Spline (Now Available!)
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    .Padding(1)
                    [
                        SNew(SButton)
                        .ButtonColorAndOpacity_Lambda([this]() 
                        { 
                            return OutputFormat == 1 ? FLinearColor(0.1f, 0.5f, 1.0f, 1.0f) : FLinearColor::White;
                        })
                        .Text(FText::FromString(TEXT("Multi\nSpline")))
                        .OnClicked_Lambda([this]() -> FReply
                        {
                            OutputFormat = 1;
                            UE_LOG(LogTemp, Log, TEXT("[ProcgenArcana Importer] Output Format: Multi-Spline"));
                            return FReply::Handled();
                        })
                    ]
                    
                    // Procedural Mesh (Future)
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    .Padding(1)
                    [
                        SNew(SButton)
                        .IsEnabled(false) // Greyed out
                        .ButtonColorAndOpacity(FLinearColor(0.3f, 0.3f, 0.3f, 1.0f))
                        .Text(FText::FromString(TEXT("Proc\nMesh")))
                        .OnClicked_Lambda([this]() -> FReply
                        {
                            // Show "coming soon" notification
                            FNotificationInfo Info(FText::FromString(TEXT("Procedural Mesh generation is coming soon!")));
                            Info.ExpireDuration = 3.0f;
                            Info.bFireAndForget = true;
                            FSlateNotificationManager::Get().AddNotification(Info);
                            return FReply::Handled();
                        })
                    ]
                    
                    // SDF Brush (Future)
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    .Padding(1)
                    [
                        SNew(SButton)
                        .IsEnabled(false) // Greyed out
                        .ButtonColorAndOpacity(FLinearColor(0.3f, 0.3f, 0.3f, 1.0f))
                        .Text(FText::FromString(TEXT("SDF\nBrush")))
                        .OnClicked_Lambda([this]() -> FReply
                        {
                            // Show "coming soon" notification
                            FNotificationInfo Info(FText::FromString(TEXT("SDF Brush generation is coming soon!")));
                            Info.ExpireDuration = 3.0f;
                            Info.bFireAndForget = true;
                            FSlateNotificationManager::Get().AddNotification(Info);
                            return FReply::Handled();
                        })
                    ]
                ]
            ]

            // Pivot Mode Selection with Interactive Preview Controls
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(2)
            [
                SNew(SVerticalBox)
                
                // Origin Point Dropdown
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0, 0, 0, 4)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .FillWidth(0.4f)
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(TEXT("Origin Point:")))
                    ]
                    + SHorizontalBox::Slot()
                    .FillWidth(0.6f)
                    [
                        SNew(SComboBox<TSharedPtr<FString>>)
                        .OptionsSource(&PivotModeOptions)
                        .OnGenerateWidget_Lambda([](TSharedPtr<FString> InOption)
                        {
                            return SNew(STextBlock).Text(FText::FromString(*InOption));
                        })
                        .OnSelectionChanged_Lambda([this](TSharedPtr<FString> NewSelection, ESelectInfo::Type)
                        {
                            if (NewSelection.IsValid())
                            {
                                PivotMode = PivotModeOptions.IndexOfByPredicate([&](const TSharedPtr<FString>& Item)
                                {
                                    return Item.Get() == NewSelection.Get();
                                });
                                UE_LOG(LogTemp, Log, TEXT("[ProcgenArcana Importer] Pivot Mode: %s"), **NewSelection);
                                
                                // Update preview in real-time if active
                                if (bHasActivePreview)
                                {
                                    PreviewProcgenArcanaCave();
                                }
                            }
                        })
                        [
                            SNew(STextBlock)
                            .Text_Lambda([this]()
                            {
                                if (PivotModeOptions.IsValidIndex(PivotMode))
                                {
                                    return FText::FromString(*PivotModeOptions[PivotMode]);
                                }
                                return FText::FromString(TEXT("Spline Center"));
                            })
                        ]
                    ]
                ]
                
                                    // Manual Position Offset Controls (when preview is active)
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0, 4, 0, 0)
                [
                    SNew(SVerticalBox)
                    .Visibility_Lambda([this]() { return bHasActivePreview ? EVisibility::Visible : EVisibility::Collapsed; })
                    
                    // Header with World-Scale Positioning Option
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0, 0, 0, 2)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot()
                        .FillWidth(0.6f)
                        [
                            SNew(STextBlock)
                            .Text(FText::FromString(TEXT("Preview Position:")))
                            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
                        ]
                        + SHorizontalBox::Slot()
                        .FillWidth(0.4f)
                        [
                            SNew(SCheckBox)
                            .IsChecked_Lambda([this]() { return bUseWorldScalePositioning ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                            .OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
                            {
                                bUseWorldScalePositioning = (NewState == ECheckBoxState::Checked);
                                if (bUseWorldScalePositioning)
                                {
                                    // Place placement indicator at camera location
                                    PlaceWorldScaleIndicator();
                                }
                                else
                                {
                                    // Clear placement indicator
                                    ClearWorldScaleIndicator();
                                }
                            })
                            [
                                SNew(STextBlock)
                                .Text(FText::FromString(TEXT("World Scale")))
                                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
                            ]
                        ]
                    ]
                    
                    // World Scale Controls (when enabled)
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0, 2)
                    [
                        SNew(SVerticalBox)
                        .Visibility_Lambda([this]() { return bUseWorldScalePositioning ? EVisibility::Visible : EVisibility::Collapsed; })
                        
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0, 1)
                        [
                            SNew(STextBlock)
                            .Text(FText::FromString(TEXT("Ctrl+Click in viewport to place. Indicator shows placement location.")))
                            .Font(FCoreStyle::GetDefaultFontStyle("Italic", 7))
                            .ColorAndOpacity(FSlateColor(FLinearColor::Gray))
                        ]
                        
                        // Snap Settings
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0, 2)
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot()
                            .FillWidth(0.3f)
                            [
                                SNew(SCheckBox)
                                .IsChecked_Lambda([this]() { return bSnapToGrid ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                                .OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
                                {
                                    bSnapToGrid = (NewState == ECheckBoxState::Checked);
                                })
                                [
                                    SNew(STextBlock)
                                    .Text(FText::FromString(TEXT("Snap")))
                                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
                                ]
                            ]
                            + SHorizontalBox::Slot()
                            .FillWidth(0.7f)
                            [
                                SNew(SHorizontalBox)
                                .IsEnabled_Lambda([this]() { return bSnapToGrid; })
                                + SHorizontalBox::Slot()
                                .FillWidth(0.7f)
                                [
                                    SNew(SSlider)
                                    .Value_Lambda([this]() { return FMath::Clamp((SnapIncrement - 10.0f) / 990.0f, 0.0f, 1.0f); })
                                    .OnValueChanged_Lambda([this](float NewValue)
                                    {
                                        SnapIncrement = 10.0f + (NewValue * 990.0f); // 10 to 1000
                                    })
                                ]
                                + SHorizontalBox::Slot()
                                .FillWidth(0.3f)
                                [
                                    SNew(STextBlock)
                                    .Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%.0fu"), SnapIncrement)); })
                                    .Justification(ETextJustify::Center)
                                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
                                ]
                            ]
                        ]
                    ]
                    
                    // Local Offset Controls (when NOT using world scale)
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0, 2)
                    [
                        SNew(SVerticalBox)
                        .Visibility_Lambda([this]() { return !bUseWorldScalePositioning ? EVisibility::Visible : EVisibility::Collapsed; })
                        
                        // X Offset
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0, 1)
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot()
                            .FillWidth(0.2f)
                            [
                                SNew(STextBlock)
                                .Text(FText::FromString(TEXT("X:")))
                            ]
                            + SHorizontalBox::Slot()
                            .FillWidth(0.6f)
                            [
                                SNew(SSlider)
                                .Value_Lambda([this]() { return (PreviewPositionOffset.X + 1000.0f) / 2000.0f; })
                                .OnValueChanged_Lambda([this](float NewValue)
                                {
                                    PreviewPositionOffset.X = (NewValue * 2000.0f) - 1000.0f;
                                    if (bHasActivePreview)
                                    {
                                        UpdatePreviewPosition();
                                    }
                                })
                            ]
                            + SHorizontalBox::Slot()
                            .FillWidth(0.2f)
                            [
                                SNew(STextBlock)
                                .Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%.0f"), PreviewPositionOffset.X)); })
                                .Justification(ETextJustify::Center)
                            ]
                        ]
                        
                        // Y Offset
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0, 1)
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot()
                            .FillWidth(0.2f)
                            [
                                SNew(STextBlock)
                                .Text(FText::FromString(TEXT("Y:")))
                            ]
                            + SHorizontalBox::Slot()
                            .FillWidth(0.6f)
                            [
                                SNew(SSlider)
                                .Value_Lambda([this]() { return (PreviewPositionOffset.Y + 1000.0f) / 2000.0f; })
                                .OnValueChanged_Lambda([this](float NewValue)
                                {
                                    PreviewPositionOffset.Y = (NewValue * 2000.0f) - 1000.0f;
                                    if (bHasActivePreview)
                                    {
                                        UpdatePreviewPosition();
                                    }
                                })
                            ]
                            + SHorizontalBox::Slot()
                            .FillWidth(0.2f)
                            [
                                SNew(STextBlock)
                                .Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%.0f"), PreviewPositionOffset.Y)); })
                                .Justification(ETextJustify::Center)
                            ]
                        ]
                        
                        // Z Offset
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0, 1)
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot()
                            .FillWidth(0.2f)
                            [
                                SNew(STextBlock)
                                .Text(FText::FromString(TEXT("Z:")))
                            ]
                            + SHorizontalBox::Slot()
                            .FillWidth(0.6f)
                            [
                                SNew(SSlider)
                                .Value_Lambda([this]() { return (PreviewPositionOffset.Z + 500.0f) / 1000.0f; })
                                .OnValueChanged_Lambda([this](float NewValue)
                                {
                                    PreviewPositionOffset.Z = (NewValue * 1000.0f) - 500.0f;
                                    if (bHasActivePreview)
                                    {
                                        UpdatePreviewPosition();
                                    }
                                })
                            ]
                            + SHorizontalBox::Slot()
                            .FillWidth(0.2f)
                            [
                                SNew(STextBlock)
                                .Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%.0f"), PreviewPositionOffset.Z)); })
                                .Justification(ETextJustify::Center)
                            ]
                        ]
                    ]
                    
                    // Reset Button
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0, 4, 0, 0)
                    [
                        SNew(SButton)
                        .Text(FText::FromString(TEXT("Reset Position")))
                        .OnClicked_Lambda([this]() -> FReply
                        {
                            PreviewPositionOffset = FVector::ZeroVector;
                            WorldScalePosition = FVector::ZeroVector;
                            if (bHasActivePreview)
                            {
                                if (bUseWorldScalePositioning)
                                {
                                    PlaceWorldScaleIndicator(); // Reset to camera position
                                }
                                PreviewProcgenArcanaCave(); // Full refresh
                            }
                            return FReply::Handled();
                        })
                    ]
                ]
            ]

            // Cave Scale Setting
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(2)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .FillWidth(0.4f)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("Cave Scale:")))
                ]
                + SHorizontalBox::Slot()
                .FillWidth(0.6f)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    [
                        SNew(SSlider)
                        .Value_Lambda([this]() { return CaveScale / 500.0f; })
                        .OnValueChanged_Lambda([this](float NewValue)
                        {
                            CaveScale = NewValue * 500.0f;
                            UE_LOG(LogTemp, Log, TEXT("[ProcgenArcana Importer] Cave Scale: %f"), CaveScale);
                        })
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .Padding(4, 0, 0, 0)
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%.0f"), CaveScale)); })
                        .MinDesiredWidth(30)
                    ]
                ]
            ]

            // Simplification Level
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(2)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .FillWidth(0.4f)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("Simplification:")))
                ]
                + SHorizontalBox::Slot()
                .FillWidth(0.6f)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    [
                        SNew(SSlider)
                        .Value_Lambda([this]() { return SimplificationLevel; })
                        .OnValueChanged_Lambda([this](float NewValue)
                        {
                            SimplificationLevel = NewValue;
                            UE_LOG(LogTemp, Log, TEXT("[ProcgenArcana Importer] Simplification: %f"), SimplificationLevel);
                        })
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .Padding(4, 0, 0, 0)
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]() 
                        { 
                            FString Level = SimplificationLevel < 0.25f ? TEXT("High Detail") :
                                          SimplificationLevel < 0.5f ? TEXT("Medium") :
                                          SimplificationLevel < 0.75f ? TEXT("Low Detail") : TEXT("Simplified");
                            return FText::FromString(Level);
                        })
                        .MinDesiredWidth(60)
                    ]
                ]
            ]

            // Max Spline Points
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(2)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .FillWidth(0.4f)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("Max Points:")))
                ]
                + SHorizontalBox::Slot()
                .FillWidth(0.6f)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    [
                        SNew(SSlider)
                        .Value_Lambda([this]() { return (MaxSplinePoints - 50) / 450.0f; })
                        .OnValueChanged_Lambda([this](float NewValue)
                        {
                            MaxSplinePoints = 50 + (NewValue * 450.0f);
                            UE_LOG(LogTemp, Log, TEXT("[ProcgenArcana Importer] Max Points: %d"), MaxSplinePoints);
                        })
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .Padding(4, 0, 0, 0)
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%d"), MaxSplinePoints)); })
                        .MinDesiredWidth(30)
                    ]
                ]
            ]

            // Height Settings Header
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(2, 8, 2, 2)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("Height Settings")))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
            ]

            // Height Mode Selection (Using Buttons)
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(2)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0, 0, 0, 2)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("Height Mode:")))
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(SHorizontalBox)
                    
                    // Flat button
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    .Padding(1)
                    [
                        SNew(SButton)
                        .ButtonColorAndOpacity_Lambda([this]() 
                        { 
                            return HeightMode == 0 ? FLinearColor(0.1f, 0.5f, 1.0f, 1.0f) : FLinearColor::White;
                        })
                        .Text(FText::FromString(TEXT("Flat")))
                        .OnClicked_Lambda([this]() -> FReply
                        {
                            HeightMode = 0;
                            UE_LOG(LogTemp, Log, TEXT("[ProcgenArcana Importer] Height Mode: Flat"));
                            return FReply::Handled();
                        })
                    ]
                    
                    // Descending button
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    .Padding(1)
                    [
                        SNew(SButton)
                        .ButtonColorAndOpacity_Lambda([this]() 
                        { 
                            return HeightMode == 1 ? FLinearColor(0.1f, 0.5f, 1.0f, 1.0f) : FLinearColor::White;
                        })
                        .Text(FText::FromString(TEXT("Desc")))
                        .OnClicked_Lambda([this]() -> FReply
                        {
                            HeightMode = 1;
                            UE_LOG(LogTemp, Log, TEXT("[ProcgenArcana Importer] Height Mode: Descending"));
                            return FReply::Handled();
                        })
                    ]
                    
                    // Rolling button
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    .Padding(1)
                    [
                        SNew(SButton)
                        .ButtonColorAndOpacity_Lambda([this]() 
                        { 
                            return HeightMode == 2 ? FLinearColor(0.1f, 0.5f, 1.0f, 1.0f) : FLinearColor::White;
                        })
                        .Text(FText::FromString(TEXT("Roll")))
                        .OnClicked_Lambda([this]() -> FReply
                        {
                            HeightMode = 2;
                            UE_LOG(LogTemp, Log, TEXT("[ProcgenArcana Importer] Height Mode: Rolling"));
                            return FReply::Handled();
                        })
                    ]
                    
                    // Mountainous button
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    .Padding(1)
                    [
                        SNew(SButton)
                        .ButtonColorAndOpacity_Lambda([this]() 
                        { 
                            return HeightMode == 3 ? FLinearColor(0.1f, 0.5f, 1.0f, 1.0f) : FLinearColor::White;
                        })
                        .Text(FText::FromString(TEXT("Mount")))
                        .OnClicked_Lambda([this]() -> FReply
                        {
                            HeightMode = 3;
                            UE_LOG(LogTemp, Log, TEXT("[ProcgenArcana Importer] Height Mode: Mountainous"));
                            return FReply::Handled();
                        })
                    ]
                ]
            ]

            // Height Variation (conditional visibility)
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(2)
            [
                SNew(SHorizontalBox)
                .Visibility_Lambda([this]() { return HeightMode != 0 ? EVisibility::Visible : EVisibility::Collapsed; })
                + SHorizontalBox::Slot()
                .FillWidth(0.4f)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("Height Variation:")))
                ]
                + SHorizontalBox::Slot()
                .FillWidth(0.6f)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    [
                        SNew(SSlider)
                        .Value_Lambda([this]() { return HeightVariation / 200.0f; })
                        .OnValueChanged_Lambda([this](float NewValue)
                        {
                            HeightVariation = NewValue * 200.0f;
                            UE_LOG(LogTemp, Log, TEXT("[ProcgenArcana Importer] Height Variation: %f"), HeightVariation);
                        })
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .Padding(4, 0, 0, 0)
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%.0f"), HeightVariation)); })
                        .MinDesiredWidth(30)
                    ]
                ]
            ]

            // Descent Rate (only for Descending Cave mode)
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(2)
            [
                SNew(SHorizontalBox)
                .Visibility_Lambda([this]() { return HeightMode == 1 ? EVisibility::Visible : EVisibility::Collapsed; })
                + SHorizontalBox::Slot()
                .FillWidth(0.4f)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("Descent Rate:")))
                ]
                + SHorizontalBox::Slot()
                .FillWidth(0.6f)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    [
                        SNew(SSlider)
                        .Value_Lambda([this]() { return DescentRate; })
                        .OnValueChanged_Lambda([this](float NewValue)
                        {
                            DescentRate = NewValue;
                            UE_LOG(LogTemp, Log, TEXT("[ProcgenArcana Importer] Descent Rate: %f"), DescentRate);
                        })
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .Padding(4, 0, 0, 0)
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]() 
                        { 
                            FString Rate = DescentRate < 0.2f ? TEXT("Gentle") :
                                         DescentRate < 0.6f ? TEXT("Moderate") : TEXT("Steep");
                            return FText::FromString(Rate);
                        })
                        .MinDesiredWidth(50)
                    ]
                ]
            ]

            // Entrance Detection Settings
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(2, 8, 2, 2)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("Entrance Detection")))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
            ]

            // Auto-detect entrance checkbox
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(2)
            [
                SNew(SCheckBox)
                .IsChecked_Lambda([this]() { return bAutoDetectEntrance ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
                {
                    bAutoDetectEntrance = (NewState == ECheckBoxState::Checked);
                    UE_LOG(LogTemp, Log, TEXT("[ProcgenArcana Importer] Auto-detect entrance: %s"), bAutoDetectEntrance ? TEXT("true") : TEXT("false"));
                })
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("Auto-detect entrance from SVG")))
                ]
            ]

            // Action Buttons
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(2, 8, 2, 2)
            [
                SNew(SVerticalBox)
                
                // Preview Controls Row
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0, 0, 0, 4)
                [
                    SNew(SHorizontalBox)
                    
                    // Preview Button
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    .Padding(0, 0, 2, 0)
                    [
                        SNew(SButton)
                        .IsEnabled_Lambda([this]() { return !SelectedSVGFilePath.IsEmpty() && UIFeatureFlags.bEnableCaveImporter; })
                        .OnClicked_Lambda([this]() -> FReply
                        {
                            PreviewProcgenArcanaCave();
                            return FReply::Handled();
                        })
                        [
                            SNew(STextBlock)
                            .Text(FText::FromString(TEXT("Preview")))
                            .Justification(ETextJustify::Center)
                        ]
                    ]
                    
                    // Clear Preview Button
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    .Padding(2, 0, 0, 0)
                    [
                        SNew(SButton)
                        .IsEnabled_Lambda([this]() { return bHasActivePreview; })
                        .OnClicked_Lambda([this]() -> FReply
                        {
                            ClearCavePreview();
                            return FReply::Handled();
                        })
                        [
                            SNew(STextBlock)
                            .Text(FText::FromString(TEXT("Clear")))
                            .Justification(ETextJustify::Center)
                        ]
                    ]
                ]
                
                // Import Button Row
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(SButton)
                    .IsEnabled_Lambda([this]() { return !SelectedSVGFilePath.IsEmpty() && UIFeatureFlags.bEnableCaveImporter; })
                    .ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
                    .OnClicked_Lambda([this]() -> FReply
                    {
                        ImportProcgenArcanaCave();
                        return FReply::Handled();
                    })
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(TEXT("Import Cave")))
                        .Justification(ETextJustify::Center)
                    ]
                ]
            ]

            // Status/Info Text
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(2)
            [
                SNew(STextBlock)
                .Text_Lambda([this]()
                {
                    if (!UIFeatureFlags.bEnableCaveImporter)
                    {
                        return FText::FromString(TEXT("ProcgenArcana Importer is disabled"));
                    }
                    else if (SelectedSVGFilePath.IsEmpty())
                    {
                        return FText::FromString(TEXT("Select an SVG file to begin..."));
                    }
                    else
                    {
                        FString FileName = FPaths::GetCleanFilename(SelectedSVGFilePath);
                        return FText::FromString(FString::Printf(TEXT("Ready to import: %s"), *FileName));
                    }
                })
                .ColorAndOpacity_Lambda([this]()
                {
                    if (!UIFeatureFlags.bEnableCaveImporter)
                    {
                        return FSlateColor(FLinearColor::Red);
                    }
                    return SelectedSVGFilePath.IsEmpty() ? 
                        FSlateColor(FLinearColor::Gray) : 
                        FSlateColor(FLinearColor::Green);
                })
                .Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
            ]
        ];
}

// Add these methods to your .cpp file:

void FDiggerEdModeToolkit::PlaceWorldScaleIndicator()
{
    UWorld* World = nullptr;
    if (GEditor && GEditor->GetEditorWorldContext(false).World())
    {
        World = GEditor->GetEditorWorldContext(false).World();
    }
    
    if (!World)
    {
        return;
    }
    
    // Clear existing indicator
    ClearWorldScaleIndicator();
    
    // Get camera location as starting point
    FVector CameraLocation = FVector::ZeroVector;
    if (GEditor && GEditor->GetActiveViewport())
    {
        FEditorViewportClient* ViewportClient = static_cast<FEditorViewportClient*>(GEditor->GetActiveViewport()->GetClient());
        if (ViewportClient)
        {
            CameraLocation = ViewportClient->GetViewLocation();
        }
    }
    
    // If no world scale position set, use camera location
    if (WorldScalePosition == FVector::ZeroVector)
    {
        WorldScalePosition = CameraLocation;
    }
    
    // Apply snapping if enabled
    if (bSnapToGrid)
    {
        WorldScalePosition.X = FMath::RoundToFloat(WorldScalePosition.X / SnapIncrement) * SnapIncrement;
        WorldScalePosition.Y = FMath::RoundToFloat(WorldScalePosition.Y / SnapIncrement) * SnapIncrement;
        WorldScalePosition.Z = FMath::RoundToFloat(WorldScalePosition.Z / SnapIncrement) * SnapIncrement;
    }
    
    // Create visual indicator actor
    FActorSpawnParameters SpawnParams;
    SpawnParams.Name = MakeUniqueObjectName(World, AActor::StaticClass(), TEXT("ProcgenArcana_PlacementIndicator"));
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    
    AActor* IndicatorActor = World->SpawnActor<AActor>(AActor::StaticClass(), SpawnParams);
    if (IndicatorActor)
    {
        IndicatorActor->SetActorLocation(WorldScalePosition);
        IndicatorActor->Tags.Add(TEXT("ProcgenArcanaIndicator"));
        IndicatorActor->SetFolderPath(TEXT("_Temporary"));
        
        // Create visual component (simple scene component with debug visualization)
        USceneComponent* RootComp = NewObject<USceneComponent>(IndicatorActor);
        IndicatorActor->SetRootComponent(RootComp);
        RootComp->RegisterComponentWithWorld(World);
        
        WorldScaleIndicatorActor = IndicatorActor;
        
        // Draw debug indicator
        DrawDebugSphere(World, WorldScalePosition, 200.0f, 16, FColor::Magenta, true, 3600.0f, 0, 10.0f);
        DrawDebugString(World, WorldScalePosition + FVector(0, 0, 250), 
                       TEXT("PLACEMENT INDICATOR\nCtrl+Click to Move"), nullptr, FColor::Magenta, 3600.0f);
        
        UE_LOG(LogTemp, Log, TEXT("[WorldScale] Placed indicator at: %s"), *WorldScalePosition.ToString());
        
        // Update preview if active
        if (bHasActivePreview)
        {
            UpdatePreviewPosition();
        }
    }
}

void FDiggerEdModeToolkit::ClearWorldScaleIndicator()
{
    if (WorldScaleIndicatorActor.IsValid())
    {
        WorldScaleIndicatorActor->Destroy();
        WorldScaleIndicatorActor = nullptr;
    }
    
    // Clear debug lines
    UWorld* World = nullptr;
    if (GEditor && GEditor->GetEditorWorldContext(false).World())
    {
        World = GEditor->GetEditorWorldContext(false).World();
        if (World)
        {
            FlushPersistentDebugLines(World);
        }
    }
}

void FDiggerEdModeToolkit::HandleWorldScaleClick(const FVector& ClickLocation)
{
    if (!bUseWorldScalePositioning)
    {
        return;
    }
    
    WorldScalePosition = ClickLocation;
    
    // Apply snapping if enabled
    if (bSnapToGrid)
    {
        WorldScalePosition.X = FMath::RoundToFloat(WorldScalePosition.X / SnapIncrement) * SnapIncrement;
        WorldScalePosition.Y = FMath::RoundToFloat(WorldScalePosition.Y / SnapIncrement) * SnapIncrement;
        WorldScalePosition.Z = FMath::RoundToFloat(WorldScalePosition.Z / SnapIncrement) * SnapIncrement;
    }
    
    UE_LOG(LogTemp, Log, TEXT("[WorldScale] Updated position to: %s"), *WorldScalePosition.ToString());
    
    // Update the indicator
    PlaceWorldScaleIndicator();
    
    // Show notification
    FNotificationInfo Info(FText::FromString(FString::Printf(
        TEXT("Placement updated: X=%.0f, Y=%.0f, Z=%.0f"), 
        WorldScalePosition.X, WorldScalePosition.Y, WorldScalePosition.Z)));
    Info.ExpireDuration = 2.0f;
    Info.bFireAndForget = true;
    FSlateNotificationManager::Get().AddNotification(Info);
}

// Update your UpdatePreviewPosition method to handle world-scale positioning:
void FDiggerEdModeToolkit::UpdatePreviewPosition()
{
    if (!bHasActivePreview || StoredPreviewPoints.Num() == 0)
    {
        return;
    }
    
    UWorld* World = nullptr;
    if (GEditor && GEditor->GetEditorWorldContext(false).World())
    {
        World = GEditor->GetEditorWorldContext(false).World();
    }
    
    if (!World)
    {
        return;
    }
    
    // Clear existing debug lines
    FlushPersistentDebugLines(World);
    FlushDebugStrings(World);
    
    // Calculate the base pivot offset (without manual adjustment)
    FVector BasePivotOffset = CalculatePivotOffset(StoredPreviewPoints, StoredEntrancePoints, StoredExitPoints);
    
    // Apply either world-scale positioning or local offset
    FVector TotalOffset;
    if (bUseWorldScalePositioning)
    {
        // Use world-scale position as the final location
        TotalOffset = BasePivotOffset - WorldScalePosition;
    }
    else
    {
        // Use local offset system
        TotalOffset = BasePivotOffset - PreviewPositionOffset;
    }
    
    // Adjust points for display
    TArray<FVector> DisplayPoints = StoredPreviewPoints;
    TArray<FVector> DisplayEntrances = StoredEntrancePoints;
    TArray<FVector> DisplayExits = StoredExitPoints;
    
    for (FVector& Point : DisplayPoints)
    {
        Point -= TotalOffset;
    }
    for (FVector& Point : DisplayEntrances)
    {
        Point -= TotalOffset;
    }
    for (FVector& Point : DisplayExits)
    {
        Point -= TotalOffset;
    }
    
    // Draw the updated preview
    // Draw the main cave path with thick green lines
    for (int32 i = 0; i < DisplayPoints.Num() - 1; i++)
    {
        DrawDebugLine(World, DisplayPoints[i], DisplayPoints[i + 1], 
                     FColor::Green, true, 60.0f, 0, 8.0f);
    }
    
    // Draw spline points as small spheres
    for (int32 i = 0; i < DisplayPoints.Num(); i++)
    {
        FColor PointColor = (i == 0) ? FColor::Yellow : 
                           (i == DisplayPoints.Num() - 1) ? FColor::Orange : FColor::Cyan;
        DrawDebugSphere(World, DisplayPoints[i], 25.0f, 8, 
                       PointColor, true, 60.0f, 0, 2.0f);
    }
    
    // Draw entrance points
    for (const FVector& Entrance : DisplayEntrances)
    {
        DrawDebugSphere(World, Entrance, 75.0f, 12, 
                       FColor::Red, true, 60.0f, 0, 5.0f);
        DrawDebugString(World, Entrance + FVector(0, 0, 100), 
                       TEXT("ENTRANCE"), nullptr, FColor::Red, 60.0f);
    }
    
    // Draw exit points
    for (const FVector& Exit : DisplayExits)
    {
        DrawDebugSphere(World, Exit, 75.0f, 12, 
                       FColor::Blue, true, 60.0f, 0, 5.0f);
        DrawDebugString(World, Exit + FVector(0, 0, 100), 
                       TEXT("EXIT"), nullptr, FColor::Blue, 60.0f);
    }
    
    // Draw origin point
    FVector OriginPoint = -TotalOffset;
    DrawDebugSphere(World, OriginPoint, 50.0f, 12, 
                   FColor::Magenta, true, 60.0f, 0, 3.0f);
    DrawDebugString(World, OriginPoint + FVector(0, 0, 80), 
                   TEXT("ORIGIN"), nullptr, FColor::Magenta, 60.0f);
    
    // Force viewport refresh
    if (GEditor)
    {
        GEditor->RedrawLevelEditingViewports();
    }
}




// Section for Add/Subtract operation/*
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeOperationSection()
{
    return SNew(SExpandableArea)
        .AreaTitle(FText::FromString("Operation"))
        .InitiallyCollapsed(true)
        .BodyContent()
        [
            SNew(SVerticalBox)
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
                            if (Manager == GetDiggerManager())
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
            // Hidden Seam Section
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 4)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                [
                    SAssignNew(HiddenSeamCheckbox, SCheckBox)
                    .IsChecked(this, &FDiggerEdModeToolkit::GetHiddenSeamCheckState)
                    .OnCheckStateChanged(this, &FDiggerEdModeToolkit::OnHiddenSeamChanged)
                    .ToolTipText(LOCTEXT("HiddenSeamTooltip", "Hidden Seam: Creates flush cuts with no raised rim. Unchecked creates natural excavation with realistic disturbed earth."))
                ]
                + SHorizontalBox::Slot()
                .Padding(8, 0, 0, 0)
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("HiddenSeam", "Hidden Seam"))
                    .Font(IDetailLayoutBuilder::GetDetailFont())
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

// Island Methods

FName FDiggerEdModeToolkit::GetSelectedIslandID() const
{
    if (SelectedIslandIndex != INDEX_NONE && IslandIDs.IsValidIndex(SelectedIslandIndex))
    {
        return IslandIDs[SelectedIslandIndex];
    }

    return NAME_None;
}


void FDiggerEdModeToolkit::OnConvertToPhysicsActorClicked()
{
    const FName IslandID = GetSelectedIslandID();
    if (IslandID == NAME_None) return;

    if (ADiggerManager* LocalManager = GetDiggerManager())
    {
        LocalManager->ConvertIslandToStaticMesh(IslandID, true);

        if (DiggerDebug::Islands)
        {
            UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Conversion triggered for IslandID: %s"), *IslandID.ToString());
        }
    }
}


void FDiggerEdModeToolkit::OnConvertToSceneActorClicked()
{
    const FName IslandID = GetSelectedIslandID();
    if (IslandID == NAME_None) return;

    if (ADiggerManager* LocalManager = GetDiggerManager())
    {
        LocalManager->ConvertIslandToStaticMesh(IslandID, false);

        if (DiggerDebug::Islands)
        {
            UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Static mesh conversion triggered for IslandID: %s"), *IslandID.ToString());
        }
    }
}


// For removing the selected island from the voxel grid and the island list in the UI
void FDiggerEdModeToolkit::OnRemoveIslandClicked()
{
    const FName IslandID = GetSelectedIslandID();
    if (IslandID == NAME_None) return;

    if (ADiggerManager* LocalManager = GetDiggerManager())
    {
        LocalManager->RemoveIslandByID(IslandID);
    }

    IslandIDs.Remove(IslandID);
    SelectedIslandIndex = INDEX_NONE;
    RebuildIslandGrid();

    if (DiggerDebug::Islands)
    {
        UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Removed island %s and rebuilt grid."), *IslandID.ToString());
    }
}

FIslandData* FDiggerEdModeToolkit::GetSelectedIsland()
{
    const FName IslandID = GetSelectedIslandID();
    Manager = GetDiggerManager();
    return (Manager && IslandID != NAME_None) ? Manager->FindIsland(IslandID) : nullptr;
}


//Make Island Section
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeIslandsSection()
{
    return SNew(SVerticalBox)

    // Header Button (Fold/Unfold)
    + SVerticalBox::Slot()
    .AutoHeight()
    .Padding(4)
    [
        SNew(SButton)
        .OnClicked_Lambda([this]() -> FReply
        {
            bShowIslandsSection = !bShowIslandsSection;
            return FReply::Handled();
        })
        [
            SNew(STextBlock)
            .Text_Lambda([this]()
            {
                return FText::FromString(
                    bShowIslandsSection
                    ? TEXT("â–¼ Islands")
                    : TEXT("â–º Islands")
                );
            })
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
        ]
    ]

    // Collapsible Section
    + SVerticalBox::Slot()
    .AutoHeight()
    .Padding(4)
    [
        SNew(SVerticalBox)
        .Visibility_Lambda([this]()
        {
            return bShowIslandsSection ? EVisibility::Visible : EVisibility::Collapsed;
        })

        // Island Operations Buttons
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8, 4, 8, 4)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(2)
            [
                SNew(SButton)
                .Text(FText::FromString("Convert To Physics Actor"))
                .IsEnabled_Lambda([this]() { return SelectedIslandIndex != INDEX_NONE; })
                .OnClicked_Lambda([this]() -> FReply
                {
                    OnConvertToPhysicsActorClicked();
                    return FReply::Handled();
                })
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(2)
            [
                SNew(SButton)
                .Text(FText::FromString("Convert to Static Scene Actor"))
                .IsEnabled_Lambda([this]() { return SelectedIslandIndex != INDEX_NONE; })
                .OnClicked_Lambda([this]() -> FReply
                {
                    OnConvertToSceneActorClicked();
                    return FReply::Handled();
                })
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(2)
            [
                SNew(SButton)
                .Text(FText::FromString("Remove"))
                .IsEnabled_Lambda([this]() { return SelectedIslandIndex != INDEX_NONE; })
                .OnClicked_Lambda([this]() -> FReply
                {
                    OnRemoveIslandClicked();
                    return FReply::Handled();
                })
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(2)
            [
                SNew(SButton)
                .Text(FText::FromString("Duplicate"))
                .IsEnabled_Lambda([this]() { return SelectedIslandIndex != INDEX_NONE; })
                .OnClicked_Lambda([this]() -> FReply
                {
                    GhostIslandID = GetSelectedIslandID();
                    bIsGhostPreviewActive = true;
                    UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Ghost preview activated for island %s"), *GhostIslandID.ToString());
                    return FReply::Handled();
                })

            ]
        ]

        // Island Details Menu (Stats / Rename / Rotate / Duplicate)
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8, 4, 8, 4)
        [
            SNew(SVerticalBox)
            .Visibility_Lambda([this]() {
                return SelectedIslandIndex != INDEX_NONE ? EVisibility::Visible : EVisibility::Collapsed;
            })

            + SVerticalBox::Slot().AutoHeight().Padding(2)
            [
                SNew(SExpandableArea)
                .AreaTitle(FText::FromString("Island Details"))
                .InitiallyCollapsed(false)
                .BodyContent()
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(2)
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]() {
                            const FIslandData* Island = GetSelectedIsland();
                            return Island ? FText::FromString(FString::Printf(TEXT("Island ID: %s"), *Island->IslandID.ToString())) : FText::FromString("Island ID: None");
                        })
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(2)
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]() {
                            const FIslandData* Island = GetSelectedIsland();
                            return Island ? FText::FromString(FString::Printf(TEXT("Voxel Count: %d"), Island->VoxelCount)) : FText::FromString("Voxel Count: 0");
                        })
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(2)
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]() {
                            const FIslandData* Island = GetSelectedIsland();
                            return Island ? FText::FromString(FString::Printf(TEXT("Center: %s"), *Island->Location.ToString())) : FText::FromString("Center: N/A");
                        })
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(2)
                    [
                        SNew(SEditableTextBox)
                        .Text_Lambda([this]() {
                            const FIslandData* Island = GetSelectedIsland();
                            return Island ? FText::FromString(Island->IslandName) : FText::FromString("");
                        })
                        .OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type CommitType)
                        {
                            FIslandData* Island = GetSelectedIsland();
                            if (Island)
                            {
                                Island->IslandName = NewText.ToString();
                                UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Island renamed to: %s"), *Island->IslandName);
                            }
                            // TODO: make this update the name in the actual manager's map data for the island! It isn't actually being renamed until this is done.
                        })
                    ]
                ]
            ]
        ]

        // Change Rotation Subsection
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8, 4, 8, 4)
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
                    .OnClicked_Lambda([this]() -> FReply
                    {
                        UE_LOG(LogTemp, Warning, TEXT("Island Rotation Applied: Pitch=%f, Yaw=%f, Roll=%f"),
                            IslandRotation.Pitch, IslandRotation.Yaw, IslandRotation.Roll);
                        // comment: TODO: implement rotation application that actually reads the island voxels, rotated them, and places them correctly in the source grid.
                        return FReply::Handled();
                    })
                ]
            ]
        ]

        // Island Grid Section
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(8, 4, 8, 8)
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
        .HintText(LOCTEXT("UsernameHint", "Enter usernameâ€¦"));

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



void FDiggerEdModeToolkit::ShutdownNetworking()
{
  /*  if (WebSocket.IsValid())
    {
        UE_LOG(LogTemp, Log, TEXT("ðŸ”Œ Disconnecting from Digger Network Server"));
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


// Helper function to create disabled sections:
TSharedRef<SWidget> CreateComingSoonSection(const FString& FeatureName, const FString& Description = TEXT(""))
{
    return SNew(SBorder)
        .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
        .Padding(4)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.0f)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(FeatureName))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
                    .ColorAndOpacity(FSlateColor(FLinearColor::Gray))
                ]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(STextBlock)
                    .Text(FText::FromString("Coming Soon"))
                    .Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
                    .ColorAndOpacity(FSlateColor(FLinearColor::Yellow))
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
            [
                SNew(STextBlock)
                .Text(FText::FromString(Description.IsEmpty() ? TEXT("This feature is in development.") : Description))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                .ColorAndOpacity(FSlateColor(FLinearColor::Gray))
                .AutoWrapText(true)
            ]
        ];
}


void FDiggerEdModeToolkit::ClearIslands()
{
    if (DiggerDebug::Islands)
        UE_LOG(LogTemp, Warning, TEXT("ClearIslands called on toolkit: %p"), this);

    IslandIDs.Empty();
    SelectedIslandIndex = INDEX_NONE;
    RebuildIslandGrid();
}




void FDiggerEdModeToolkit::RebuildIslandGrid()
{
    if (!IslandGridContainer.IsValid())
    {
        if (DiggerDebug::Islands)
            UE_LOG(LogTemp, Warning, TEXT("RebuildIslandGrid: IslandGridContainer is invalid."));
        return;
    }

    TSharedRef<SWidget> NewWidget = MakeIslandGridWidget(); // Make sure this returns a valid widget (not SWidget)

    IslandGridContainer->SetContent(NewWidget);

    if (DiggerDebug::Islands)
        UE_LOG(LogTemp, Warning, TEXT("RebuildIslandGrid: Grid rebuilt successfully."));
}



// In DiggerEdModeToolkit.cpp
TSharedRef<SWidget> FDiggerEdModeToolkit::MakeIslandGridWidget()
{
    TSharedRef<SVerticalBox> GridBox = SNew(SVerticalBox);

    for (int32 Index = 0; Index < IslandIDs.Num(); ++Index)
    {
        const FName& IslandID = IslandIDs[Index];

        GridBox->AddSlot().AutoHeight().Padding(2)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(2)
            [
                SNew(SButton)
                .Text(FText::FromString(IslandID.ToString()))
                .OnClicked_Lambda([this, Index]() -> FReply
                {
                    SelectedIslandIndex = Index;

                    const FName& IslandID = IslandIDs[Index];
                    ADiggerManager* Manager = GetDiggerManager();
                    if (Manager)
                    {
                        Manager->HighlightIslandByID(IslandID);
                    }

                    if (DiggerDebug::Islands)
                    {
                        UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Island button clicked: Index %d, IslandID %s"),
                               Index, *IslandID.ToString());
                    }

                    return FReply::Handled();
                })
            ]
        ];
    }

    if (DiggerDebug::Islands)
    {
        UE_LOG(LogTemp, Log, TEXT("[DiggerPro] MakeIslandGridWidget: Built grid with %d islands"),
               IslandIDs.Num());
    }

    return GridBox;
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
        .ToolTipText(FText::FromString("Mirror (add 180Â°)"))
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
        .ToolTipText(FText::FromString("Mirror (add 180Â°)"))
        .ContentPadding(FMargin(2,0));
}

/// ProcgenArcana Cave Methods ///

// Preview Azgar cave import method
void FDiggerEdModeToolkit::PreviewProcgenArcanaCave()
{
    if (DiggerDebug::Caves)
    UE_LOG(LogTemp, Warning, TEXT("[DEBUG PREVIEW] Starting preview process..."));

    if (!CaveImporter)
    {
        if (DiggerDebug::Caves)
        UE_LOG(LogTemp, Error, TEXT("[DEBUG PREVIEW] Cave importer not initialized"));
        return;
    }

    if (SelectedSVGFilePath.IsEmpty())
    {
        if (DiggerDebug::Caves)
        UE_LOG(LogTemp, Warning, TEXT("[DEBUG PREVIEW] No SVG file selected"));
        return;
    }
    if (DiggerDebug::Caves)
    UE_LOG(LogTemp, Warning, TEXT("[DEBUG PREVIEW] Preview file: %s"), *SelectedSVGFilePath);

    // Get the correct world context - FIXED
    UWorld* World = nullptr;
    if (GEditor && GEditor->GetEditorWorldContext(false).World())
    {
        World = GEditor->GetEditorWorldContext(false).World();
    }
    else if (GEditor && GEditor->GetPIEWorldContext())
    {
        World = GEditor->GetPIEWorldContext()->World();
    }
    
    if (!World)
    {
        if (DiggerDebug::Caves || DiggerDebug::Context)
        UE_LOG(LogTemp, Error, TEXT("[DEBUG PREVIEW] Could not get valid world context for preview"));
        return;
    }

    if (DiggerDebug::Caves || DiggerDebug::Context)
    UE_LOG(LogTemp, Warning, TEXT("[DEBUG PREVIEW] Got world context"));

    // Clear any existing preview
    ClearCavePreview();

    // Prepare preview settings
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

    if (DiggerDebug::Caves)
    UE_LOG(LogTemp, Warning, TEXT("[DEBUG PREVIEW] Settings prepared, calling PreviewCaveFromSVG..."));

    // Get preview points with timeout protection
    TArray<FVector> PreviewPoints;
    TArray<FVector> EntrancePoints; 
    TArray<FVector> ExitPoints;     
    
    try
    {
        // Add some basic error checking
        if (!FPaths::FileExists(PreviewSettings.SVGFilePath))
        {
            if (DiggerDebug::Caves || DiggerDebug::IO)
            UE_LOG(LogTemp, Error, TEXT("[DEBUG PREVIEW] SVG file does not exist: %s"), *PreviewSettings.SVGFilePath);
            return;
        }
        
        if (DiggerDebug::Caves || DiggerDebug::IO)
        UE_LOG(LogTemp, Warning, TEXT("[DEBUG PREVIEW] File exists, calling importer..."));
        
        PreviewPoints = CaveImporter->PreviewCaveFromSVG(PreviewSettings);
        if (DiggerDebug::Caves)
        UE_LOG(LogTemp, Warning, TEXT("[DEBUG PREVIEW] PreviewCaveFromSVG returned %d points"), PreviewPoints.Num());
    }
    catch (...)
    {
        if (DiggerDebug::Caves || DiggerDebug::IO || DiggerDebug::Error)
        UE_LOG(LogTemp, Error, TEXT("[DEBUG PREVIEW] Exception during preview generation"));
        return;
    }
    
    if (PreviewPoints.Num() > 0)
    {
        if (DiggerDebug::Caves)
        UE_LOG(LogTemp, Log, TEXT("[DEBUG PREVIEW] Preview generated with %d points"), PreviewPoints.Num());
        
        // Add basic entrance/exit points
        EntrancePoints.Add(PreviewPoints[0]);
        if (PreviewPoints.Num() > 1)
        {
            ExitPoints.Add(PreviewPoints.Last());
        }
        
        // Apply pivot offset based on PivotMode
        FVector PivotOffset = CalculatePivotOffset(PreviewPoints, EntrancePoints, ExitPoints);

        if (DiggerDebug::Caves)
        UE_LOG(LogTemp, Warning, TEXT("[DEBUG PREVIEW] Calculated pivot offset: %s"), *PivotOffset.ToString());
        
        // Adjust all points by pivot offset
        for (FVector& Point : PreviewPoints)
        {
            Point -= PivotOffset;
        }
        for (FVector& Point : EntrancePoints)
        {
            Point -= PivotOffset;
        }
        for (FVector& Point : ExitPoints)
        {
            Point -= PivotOffset;
        }
        
        // Store preview data for clearing later
        StoredPreviewPoints = PreviewPoints;
        StoredEntrancePoints = EntrancePoints;
        StoredExitPoints = ExitPoints;
        bHasActivePreview = true;

        if (DiggerDebug::Caves)
        UE_LOG(LogTemp, Warning, TEXT("[DEBUG PREVIEW] About to draw debug lines..."));
        
        // Draw the main cave path with thick green lines
        for (int32 i = 0; i < PreviewPoints.Num() - 1; i++)
        {
            DrawDebugLine(World, PreviewPoints[i], PreviewPoints[i + 1], 
                         FColor::Green, true, 60.0f, 0, 8.0f);
        }
        
        // Draw spline points as small spheres
        for (int32 i = 0; i < PreviewPoints.Num(); i++)
        {
            FColor PointColor = (i == 0) ? FColor::Yellow : 
                               (i == PreviewPoints.Num() - 1) ? FColor::Orange : FColor::Cyan;
            DrawDebugSphere(World, PreviewPoints[i], 25.0f, 8, 
                           PointColor, true, 60.0f, 0, 2.0f);
        }
        
        // Draw entrance points as RED spheres
        for (const FVector& Entrance : EntrancePoints)
        {
            DrawDebugSphere(World, Entrance, 75.0f, 12, 
                           FColor::Red, true, 60.0f, 0, 5.0f);
            // Add entrance label
            DrawDebugString(World, Entrance + FVector(0, 0, 100), 
                           TEXT("ENTRANCE"), nullptr, FColor::Red, 60.0f);
        }
        
        // Draw exit points as BLUE spheres  
        for (const FVector& Exit : ExitPoints)
        {
            DrawDebugSphere(World, Exit, 75.0f, 12, 
                           FColor::Blue, true, 60.0f, 0, 5.0f);
            // Add exit label
            DrawDebugString(World, Exit + FVector(0, 0, 100), 
                           TEXT("EXIT"), nullptr, FColor::Blue, 60.0f);
        }
        
        // Draw pivot point
        DrawDebugSphere(World, -PivotOffset, 50.0f, 12, 
                       FColor::Magenta, true, 60.0f, 0, 3.0f);
        DrawDebugString(World, -PivotOffset + FVector(0, 0, 80), 
                       TEXT("ORIGIN"), nullptr, FColor::Magenta, 60.0f);
        
        UE_LOG(LogTemp, Warning, TEXT("[DEBUG PREVIEW] Debug lines drawn"));
        
        // Force viewport refresh
        if (GEditor)
        {
            GEditor->RedrawLevelEditingViewports();
        }
        
        // Show preview notification with more details
        FString PivotModeText = GetPivotModeDisplayName(PivotMode);
        FNotificationInfo Info(FText::FromString(FString::Printf(
            TEXT("Preview: %d points, %s mode, %s pivot\nEntrances: %d, Exits: %d"), 
            PreviewPoints.Num(), 
            *UEnum::GetValueAsString(static_cast<EHeightMode>(HeightMode)),
            *PivotModeText,
            EntrancePoints.Num(),
            ExitPoints.Num())));
        Info.ExpireDuration = 8.0f;
        Info.bFireAndForget = true;
        Info.Image = FAppStyle::GetBrush(TEXT("LevelEditor.Tabs.Viewports"));
        FSlateNotificationManager::Get().AddNotification(Info);

        if (DiggerDebug::Caves)
        UE_LOG(LogTemp, Warning, TEXT("[DEBUG PREVIEW] Preview completed successfully"));
    }
    else
    {
        if (DiggerDebug::Caves)
        UE_LOG(LogTemp, Warning, TEXT("[DEBUG PREVIEW] Preview failed - no points generated"));
        
        FNotificationInfo Info(FText::FromString(TEXT("Preview failed - no points generated from SVG")));
        Info.ExpireDuration = 5.0f;
        Info.bFireAndForget = true;
        FSlateNotificationManager::Get().AddNotification(Info);
    }
}


AActor* FDiggerEdModeToolkit::CreateCaveSplineActor(USplineComponent* SplineComponent, 
                                                    const TArray<FVector>& OriginalPoints,
                                                    const TArray<FVector>& EntrancePoints,
                                                    const TArray<FVector>& ExitPoints)
{
    if (!SplineComponent)
    {
        return nullptr;
    }

    UWorld* World = nullptr;
    if (GEditor && GEditor->GetEditorWorldContext(false).World())
    {
        World = GEditor->GetEditorWorldContext(false).World();
    }
    
    if (!World)
    {
        if (DiggerDebug::Caves || DiggerDebug::Context)
        UE_LOG(LogTemp, Error, TEXT("[Digger] Could not get valid world context"));
        return nullptr;
    }

    // Calculate the same pivot offset as used in preview INCLUDING manual adjustment
    FVector TotalPivotOffset;
    if (bUseWorldScalePositioning)
    {
        // Use world-scale positioning - place at the indicator location
        TotalPivotOffset = CalculatePivotOffset(OriginalPoints, EntrancePoints, ExitPoints) - WorldScalePosition;
    }
    else
    {
        // Use local offset system
        FVector BasePivotOffset = CalculatePivotOffset(OriginalPoints, EntrancePoints, ExitPoints);
        TotalPivotOffset = BasePivotOffset - PreviewPositionOffset;
    }

    if (DiggerDebug::Caves)
    UE_LOG(LogTemp, Warning, TEXT("[DEBUG] Creating spline actor with total pivot offset: %s"), *TotalPivotOffset.ToString());

    // Create a new actor properly
    FActorSpawnParameters SpawnParams;
    SpawnParams.Name = MakeUniqueObjectName(World, AActor::StaticClass(), 
                                           *FString::Printf(TEXT("CaveSpline_%s"), 
                                           *FPaths::GetBaseFilename(SelectedSVGFilePath)));
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AActor* CaveActor = World->SpawnActor<AActor>(AActor::StaticClass(), SpawnParams);
    if (!CaveActor)
    {
        if (DiggerDebug::Caves)
        UE_LOG(LogTemp, Error, TEXT("[Digger] Failed to spawn cave actor"));
        return nullptr;
    }

    // CRITICAL FIX: Create spline component properly for runtime creation
    USplineComponent* EditableSplineComp = NewObject<USplineComponent>(CaveActor, USplineComponent::StaticClass(), TEXT("CaveSpline"));
    
    // IMPORTANT: Set creation method and ownership flags BEFORE copying data
    EditableSplineComp->CreationMethod = EComponentCreationMethod::UserConstructionScript;
    EditableSplineComp->SetFlags(RF_Transactional | RF_DefaultSubObject);
    
    // Copy all the data from the imported spline to the new editable one
    EditableSplineComp->ClearSplinePoints();
    
    int32 NumPoints = SplineComponent->GetNumberOfSplinePoints();
    for (int32 i = 0; i < NumPoints; i++)
    {
        FVector PointLocation = SplineComponent->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World);
        FVector PointTangent = SplineComponent->GetTangentAtSplinePoint(i, ESplineCoordinateSpace::World);
        
        // Apply pivot offset to match preview (including manual adjustment)
        PointLocation -= TotalPivotOffset;
        
        EditableSplineComp->AddSplinePoint(PointLocation, ESplineCoordinateSpace::Local);
        EditableSplineComp->SetTangentAtSplinePoint(i, PointTangent, ESplineCoordinateSpace::Local);
        EditableSplineComp->SetSplinePointType(i, SplineComponent->GetSplinePointType(i));
    }
    
    // Copy other spline properties
    EditableSplineComp->SetClosedLoop(SplineComponent->IsClosedLoop());
    EditableSplineComp->UpdateSpline();

    // Set as root component BEFORE registering
    CaveActor->SetRootComponent(EditableSplineComp);
    
    // CRITICAL: Register and set up for editing
    EditableSplineComp->RegisterComponentWithWorld(World);
    
    // IMPORTANT: Add as instance component so it shows in details panel
    CaveActor->AddInstanceComponent(EditableSplineComp);
    
    // Alternative approach: Try to force it to be recognized as editable
    EditableSplineComp->bEditableWhenInherited = true;
    //EditableSplineComp->bCreatedByConstructionScript = false;
    
    // Set component tags for identification
    EditableSplineComp->ComponentTags.Add(TEXT("Editable"));
    EditableSplineComp->ComponentTags.Add(TEXT("ProcgenArcana"));
    
    // Set up the actor properties
    CaveActor->SetActorLabel(SpawnParams.Name.ToString());
    CaveActor->Tags.Add(TEXT("ProgenCave"));
    CaveActor->Tags.Add(TEXT("ProcgenArcanaImport"));
    
    // CRITICAL: Set the actor location to ZERO since points already have correct positions
    CaveActor->SetActorLocation(FVector::ZeroVector);
    
    // Set up spline properties for editability
    EditableSplineComp->SetMobility(EComponentMobility::Movable);
    EditableSplineComp->bInputSplinePointsToConstructionScript = false;
    EditableSplineComp->bModifiedByConstructionScript = false;
    EditableSplineComp->bDrawDebug = true;
    EditableSplineComp->SetVisibility(true);
    EditableSplineComp->bHiddenInGame = false;
    
    // Make sure the actor can be properly selected and edited
    CaveActor->SetCanBeDamaged(false);
    // Note: bCanBeInCluster is protected, so we skip this setting
    
    // Add metadata for easier identification
    CaveActor->SetFolderPath(TEXT("Procgen Caves"));
    
    // Add custom metadata about the import
    FString PivotModeText = GetPivotModeDisplayName(PivotMode);
    CaveActor->Tags.Add(*FString::Printf(TEXT("Pivot_%s"), *PivotModeText.Replace(TEXT(" "), TEXT("_"))));
    CaveActor->Tags.Add(*FString::Printf(TEXT("Points_%d"), NumPoints));
    
    // IMPORTANT: Mark components as editable
    EditableSplineComp->SetFlags(RF_Transactional); // Allows undo/redo
    CaveActor->SetFlags(RF_Transactional);
    
    // Mark the actor as dirty so it saves properly
    CaveActor->MarkPackageDirty();
    
    // Clear any active preview since we've created the actual spline
    ClearCavePreview();
    
    // Select the newly created actor
    if (GEditor)
    {
        GEditor->SelectNone(false, true);
        GEditor->SelectActor(CaveActor, true, true);
        
        // Force refresh the details panel
        GEditor->RedrawLevelEditingViewports();
        FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
        PropertyModule.NotifyCustomizationModuleChanged();
    }

    if (DiggerDebug::Caves)
    UE_LOG(LogTemp, Log, TEXT("[Digger] Created editable cave actor '%s' with %d spline points, pivot mode: %s"), 
           *CaveActor->GetName(), NumPoints, *PivotModeText);
    
    // Show success notification
    FNotificationInfo Info(FText::FromString(FString::Printf(
        TEXT("Created editable cave spline: %d points, %s origin"), 
        NumPoints, *PivotModeText)));
    Info.ExpireDuration = 5.0f;
    Info.bFireAndForget = true;
    Info.Image = FAppStyle::GetBrush(TEXT("LevelEditor.Tabs.Viewports"));
    FSlateNotificationManager::Get().AddNotification(Info);
    
    return CaveActor;
}


// Helper method to get display name for brush
FString FDiggerEdModeToolkit::GetBrushDisplayName(const FCustomBrushEntry& Entry) const
{
    if (Entry.IsSDF())
    {
        // Extract name from file path
        FString FileName = FPaths::GetBaseFilename(Entry.SDFBrushFilePath);
        return FileName.IsEmpty() ? TEXT("SDF Brush") : FileName;
    }
    else if (Entry.Mesh.IsValid())
    {
        return Entry.Mesh->GetName();
    }
    
    return TEXT("Unknown");
}

// Helper method to calculate pivot offset based on mode
FVector FDiggerEdModeToolkit::CalculatePivotOffset(const TArray<FVector>& Points, 
                                                   const TArray<FVector>& Entrances, 
                                                   const TArray<FVector>& Exits)
{
    if (Points.Num() == 0) return FVector::ZeroVector;
    
    switch (PivotMode)
    {
    case 0: // Spline Center
        {
            FVector Sum = FVector::ZeroVector;
            for (const FVector& Point : Points)
            {
                Sum += Point;
            }
            return Sum / Points.Num();
        }
        
    case 1: // First Entrance
        {
            if (Entrances.Num() > 0)
            {
                return Entrances[0];
            }
            return Points[0]; // Fallback to first point
        }
        
    case 2: // First Exit
        {
            if (Exits.Num() > 0)
            {
                return Exits[0];
            }
            return Points.Last(); // Fallback to last point
        }
        
    case 3: // Spline Start
        {
            return Points[0];
        }
        
    case 4: // Spline End
        {
            return Points.Last();
        }
        
    default:
        return FVector::ZeroVector;
    }
}

// Helper to get display name for pivot mode
FString FDiggerEdModeToolkit::GetPivotModeDisplayName(int32 Mode)
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

void FDiggerEdModeToolkit::ClearCavePreview()
{
    UWorld* World = nullptr;
    if (GEditor && GEditor->GetEditorWorldContext(false).World())
    {
        World = GEditor->GetEditorWorldContext(false).World();
    }
    
    if (World)
    {
        FlushPersistentDebugLines(World);
        FlushDebugStrings(World);
        
        if (GEditor)
        {
            GEditor->RedrawLevelEditingViewports();
        }
    }
    
    // Clear stored preview data
    StoredPreviewPoints.Empty();
    StoredEntrancePoints.Empty();
    StoredExitPoints.Empty();
    bHasActivePreview = false;
}

// Optional: Add validation function
bool FDiggerEdModeToolkit::ValidateImportSettings()
{
    if (SelectedSVGFilePath.IsEmpty())
    {
        if (DiggerDebug::Caves || DiggerDebug::IO)
        UE_LOG(LogTemp, Warning, TEXT("[Digger] No SVG file selected"));
        return false;
    }

    if (!FPaths::FileExists(SelectedSVGFilePath))
    {
        if (DiggerDebug::Caves || DiggerDebug::IO)
        UE_LOG(LogTemp, Error, TEXT("[Digger] SVG file does not exist: %s"), *SelectedSVGFilePath);
        return false;
    }

    if (CaveScale <= 0.0f)
    {
        if (DiggerDebug::Caves || DiggerDebug::IO)
        UE_LOG(LogTemp, Warning, TEXT("[Digger] Invalid cave scale: %f"), CaveScale);
        return false;
    }

    if (MaxSplinePoints < 3)
    {
        if (DiggerDebug::Caves || DiggerDebug::IO)
        UE_LOG(LogTemp, Warning, TEXT("[Digger] Max spline points too low: %d"), MaxSplinePoints);
        return false;
    }

    return true;
}

// Enhanced Import function with validation:
void FDiggerEdModeToolkit::ImportProcgenArcanaCave()
{
    UE_LOG(LogTemp, Warning, TEXT("[DEBUG] Starting cave import process..."));
    
    // Check if multi-spline is selected and handle appropriately
    if (OutputFormat == 1)
    {
        if (DiggerDebug::Caves)
        UE_LOG(LogTemp, Warning, TEXT("[DEBUG] Multi-spline import selected"));
        ImportMultiSplineCave();
        return;
    }
    else if (OutputFormat != 0)
    {
        FString FormatName;
        switch (OutputFormat)
        {
            case 2: FormatName = TEXT("Procedural Mesh"); break;
            case 3: FormatName = TEXT("SDF Brush"); break;
            default: FormatName = TEXT("Unknown"); break;
        }

        // if (DiggerDebug::Caves)
        UE_LOG(LogTemp, Warning, TEXT("[DEBUG] Non-implemented format selected: %s"), *FormatName);
        
        FNotificationInfo Info(FText::FromString(FString::Printf(
            TEXT("%s import is coming soon! Currently using Single Spline mode."), *FormatName)));
        Info.ExpireDuration = 5.0f;
        Info.bFireAndForget = true;
        FSlateNotificationManager::Get().AddNotification(Info);

        //if (DiggerDebug::Caves)
        UE_LOG(LogTemp, Warning, TEXT("[ProcgenArcana Importer] %s format not yet implemented, falling back to Single Spline"), *FormatName);
    }

    if (!CaveImporter)
    {
        if (DiggerDebug::Caves || DiggerDebug::IO)
        UE_LOG(LogTemp, Error, TEXT("[DEBUG] Cave importer not initialized"));
        return;
    }

    if (SelectedSVGFilePath.IsEmpty())
    {
        if (DiggerDebug::Caves || DiggerDebug::IO)
        UE_LOG(LogTemp, Warning, TEXT("[DEBUG] No SVG file selected"));
        return;
    }

    if (DiggerDebug::Caves || DiggerDebug::IO)
    UE_LOG(LogTemp, Warning, TEXT("[DEBUG] Importing from file: %s"), *SelectedSVGFilePath);

    // Get the world context
    UWorld* World = nullptr;
    if (GEditor && GEditor->GetEditorWorldContext(false).World())
    {
        World = GEditor->GetEditorWorldContext(false).World();
    }
    
    if (!World)
    {
        if (DiggerDebug::Caves || DiggerDebug::Context || DiggerDebug::IO)
        UE_LOG(LogTemp, Error, TEXT("[DEBUG] Could not get valid world context for import"));
        return;
    }

    if (DiggerDebug::Caves || DiggerDebug::Context || DiggerDebug::IO)
    UE_LOG(LogTemp, Warning, TEXT("[DEBUG] Got world context successfully"));

    // Prepare import settings
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
    ImportSettings.bPreviewMode = false; // This is the real import

    if (DiggerDebug::Caves || DiggerDebug::Context || DiggerDebug::IO)
    UE_LOG(LogTemp, Warning, TEXT("[DEBUG] Import settings prepared"));

    // Get preview data for pivot calculation
    TArray<FVector> OriginalPoints;
    TArray<FVector> EntrancePoints; 
    TArray<FVector> ExitPoints;

    if (DiggerDebug::Caves || DiggerDebug::Context || DiggerDebug::IO)
    UE_LOG(LogTemp, Warning, TEXT("[DEBUG] About to get preview data..."));
    
    // Try to get preview data first
    if (StoredPreviewPoints.Num() > 0)
    {
        if (DiggerDebug::Caves || DiggerDebug::Context || DiggerDebug::IO)
        UE_LOG(LogTemp, Warning, TEXT("[DEBUG] Using cached preview data: %d points"), StoredPreviewPoints.Num());
        // Use cached preview data if available
        OriginalPoints = StoredPreviewPoints;
        EntrancePoints = StoredEntrancePoints;
        ExitPoints = StoredExitPoints;
    }
    else
    {
        if (DiggerDebug::Caves || DiggerDebug::Context || DiggerDebug::IO)
        UE_LOG(LogTemp, Warning, TEXT("[DEBUG] No cached data, generating preview data..."));
        
        // IMPORTANT: Make sure this doesn't freeze - add timeout or simplify
        try
        {
            OriginalPoints = CaveImporter->PreviewCaveFromSVG(ImportSettings);
            if (DiggerDebug::Caves || DiggerDebug::Context || DiggerDebug::IO)
            UE_LOG(LogTemp, Warning, TEXT("[DEBUG] Preview data generated: %d points"), OriginalPoints.Num());
        }
        catch (...)
        {
            if (DiggerDebug::Caves || DiggerDebug::Context || DiggerDebug::IO)
            UE_LOG(LogTemp, Error, TEXT("[DEBUG] Exception during preview generation"));
            return;
        }
        
        if (OriginalPoints.Num() == 0)
        {
            if (DiggerDebug::Caves || DiggerDebug::Context || DiggerDebug::IO)
            UE_LOG(LogTemp, Error, TEXT("[DEBUG] No preview points generated"));
            return;
        }
        
        // Add some default entrance/exit points
        EntrancePoints.Add(OriginalPoints[0]);
        if (OriginalPoints.Num() > 1)
        {
            ExitPoints.Add(OriginalPoints.Last());
        }
        
        UE_LOG(LogTemp, Warning, TEXT("[DEBUG] Added default entrance/exit points"));
    }

    UE_LOG(LogTemp, Warning, TEXT("[DEBUG] About to call ImportCaveFromSVG..."));
    
    // FORCE SINGLE SPLINE MODE FOR NOW TO AVOID FREEZE
    USplineComponent* SplineComponent = nullptr;
    
    try 
    {
        // Call the original single-spline import
        SplineComponent = CaveImporter->ImportCaveFromSVG(ImportSettings, World, nullptr);
        UE_LOG(LogTemp, Warning, TEXT("[DEBUG] ImportCaveFromSVG completed"));
    }
    catch (...)
    {
        UE_LOG(LogTemp, Error, TEXT("[DEBUG] Exception during ImportCaveFromSVG"));
        return;
    }
    
    if (SplineComponent)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DEBUG] Spline component created successfully"));
        
        // Create the actor with proper pivot handling
        AActor* CaveActor = CreateCaveSplineActor(SplineComponent, OriginalPoints, EntrancePoints, ExitPoints);
        
        if (CaveActor)
        {
            UE_LOG(LogTemp, Log, TEXT("[DEBUG] Successfully imported cave from: %s"), *SelectedSVGFilePath);
            
            // Success notification
            FNotificationInfo Info(FText::FromString(TEXT("Cave imported successfully!")));
            Info.ExpireDuration = 3.0f;
            Info.bFireAndForget = true;
            Info.Image = FAppStyle::GetBrush(TEXT("LevelEditor.Tabs.Viewports"));
            FSlateNotificationManager::Get().AddNotification(Info);
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[DEBUG] Failed to create cave actor"));
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[DEBUG] Failed to import cave from SVG - spline component is null"));
        
        FNotificationInfo Info(FText::FromString(TEXT("Cave import failed - check the SVG file format")));
        Info.ExpireDuration = 5.0f;
        Info.bFireAndForget = true;
        FSlateNotificationManager::Get().AddNotification(Info);
    }
    
    UE_LOG(LogTemp, Warning, TEXT("[DEBUG] Import process completed"));
}

// Add this new method for multi-spline import with extensive debugging
void FDiggerEdModeToolkit::ImportMultiSplineCave()
{
    UE_LOG(LogTemp, Warning, TEXT("[DEBUG MULTI] Starting multi-spline cave import..."));
    
    if (!CaveImporter)
    {
        UE_LOG(LogTemp, Error, TEXT("[DEBUG MULTI] Cave importer not initialized"));
        return;
    }
    
    if (SelectedSVGFilePath.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("[DEBUG MULTI] No SVG file selected"));
        return;
    }
    
    // Get world context
    UWorld* World = nullptr;
    if (GEditor && GEditor->GetEditorWorldContext(false).World())
    {
        World = GEditor->GetEditorWorldContext(false).World();
    }
    
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("[DEBUG MULTI] Could not get valid world context"));
        return;
    }
    
    UE_LOG(LogTemp, Warning, TEXT("[DEBUG MULTI] Got world context, preparing settings..."));
    
    // Prepare import settings
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
    
    UE_LOG(LogTemp, Warning, TEXT("[DEBUG MULTI] Settings prepared, about to call ImportMultiSplineCaveFromSVG..."));
    
    // CRITICAL: Add try-catch and timeout protection
    FMultiSplineCaveData CaveData;
    try
    {
        // Check if the method exists and is accessible
        if (!CaveImporter->FindFunction(TEXT("ImportMultiSplineCaveFromSVG")))
        {
            UE_LOG(LogTemp, Error, TEXT("[DEBUG MULTI] ImportMultiSplineCaveFromSVG method not found! Falling back to single spline."));
            
            // Fallback to single spline
            OutputFormat = 0; // Reset to single spline
            ImportProcgenArcanaCave();
            return;
        }
        
        UE_LOG(LogTemp, Warning, TEXT("[DEBUG MULTI] Calling ImportMultiSplineCaveFromSVG..."));
        
        // Call the multi-spline import with timeout protection
        CaveData = CaveImporter->ImportMultiSplineCaveFromSVG(ImportSettings);
        
        UE_LOG(LogTemp, Warning, TEXT("[DEBUG MULTI] ImportMultiSplineCaveFromSVG returned with %d splines"), CaveData.Splines.Num());
    }
    catch (...)
    {
        UE_LOG(LogTemp, Error, TEXT("[DEBUG MULTI] Exception during ImportMultiSplineCaveFromSVG! Falling back to single spline."));
        
        // Fallback to single spline
        OutputFormat = 0;
        ImportProcgenArcanaCave();
        return;
    }
    
    if (CaveData.Splines.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DEBUG MULTI] No splines generated from multi-spline import, falling back to single spline"));
        
        // Show notification
        FNotificationInfo Info(FText::FromString(TEXT("Multi-spline import found no branches. Using single spline instead.")));
        Info.ExpireDuration = 5.0f;
        Info.bFireAndForget = true;
        FSlateNotificationManager::Get().AddNotification(Info);
        
        // Fallback to single spline
        OutputFormat = 0;
        ImportProcgenArcanaCave();
        return;
    }
    
    UE_LOG(LogTemp, Warning, TEXT("[DEBUG MULTI] Multi-spline data validated, calculating positioning..."));
    
    // Calculate positioning
    FVector PivotOffset = FVector::ZeroVector;
    if (bUseWorldScalePositioning)
    {
        PivotOffset = -WorldScalePosition; // Position at world-scale location
        UE_LOG(LogTemp, Warning, TEXT("[DEBUG MULTI] Using world-scale positioning: %s"), *WorldScalePosition.ToString());
    }
    else
    {
        // Use standard pivot calculation with manual offset
        TArray<FVector> AllPoints;
        for (const FCaveSplineData& Spline : CaveData.Splines)
        {
            AllPoints.Append(Spline.Points);
        }
        
        if (AllPoints.Num() > 0)
        {
            FVector BasePivotOffset = CalculatePivotOffset(AllPoints, CaveData.EntrancePoints, CaveData.ExitPoints);
            PivotOffset = BasePivotOffset - PreviewPositionOffset;
            UE_LOG(LogTemp, Warning, TEXT("[DEBUG MULTI] Using local positioning: Base=%s, Manual=%s, Total=%s"), 
                   *BasePivotOffset.ToString(), *PreviewPositionOffset.ToString(), *PivotOffset.ToString());
        }
    }
    
    UE_LOG(LogTemp, Warning, TEXT("[DEBUG MULTI] About to create multi-spline actor..."));
    
    // Create multi-spline actor with timeout protection
    AActor* CaveActor = nullptr;
    try
    {
        // Check if the method exists
        if (!CaveImporter->FindFunction(TEXT("CreateMultiSplineCaveActor")))
        {
            UE_LOG(LogTemp, Error, TEXT("[DEBUG MULTI] CreateMultiSplineCaveActor method not found! Creating manual actor."));
            CaveActor = CreateManualMultiSplineActor(CaveData, World, PivotOffset);
        }
        else
        {
            CaveActor = CaveImporter->CreateMultiSplineCaveActor(CaveData, World, PivotOffset);
        }
        
        UE_LOG(LogTemp, Warning, TEXT("[DEBUG MULTI] Multi-spline actor creation completed"));
    }
    catch (...)
    {
        UE_LOG(LogTemp, Error, TEXT("[DEBUG MULTI] Exception during CreateMultiSplineCaveActor!"));
        return;
    }
    
    if (CaveActor)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DEBUG MULTI] Multi-spline actor created successfully"));
        
        // Clear preview
        ClearCavePreview();
        
        // Select the new actor
        if (GEditor)
        {
            GEditor->SelectNone(false, true);
            GEditor->SelectActor(CaveActor, true, true);
        }
        
        UE_LOG(LogTemp, Log, TEXT("[DEBUG MULTI] Successfully created multi-spline cave with %d splines"), CaveData.Splines.Num());
        
        FNotificationInfo Info(FText::FromString(FString::Printf(
            TEXT("Multi-spline cave created: %d passages, %d junctions"), 
            CaveData.Splines.Num(), CaveData.Junctions.Num())));
        Info.ExpireDuration = 5.0f;
        Info.bFireAndForget = true;
        Info.Image = FAppStyle::GetBrush(TEXT("LevelEditor.Tabs.Viewports"));
        FSlateNotificationManager::Get().AddNotification(Info);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[DEBUG MULTI] Failed to create multi-spline cave actor"));
        
        FNotificationInfo Info(FText::FromString(TEXT("Multi-spline cave creation failed")));
        Info.ExpireDuration = 5.0f;
        Info.bFireAndForget = true;
        FSlateNotificationManager::Get().AddNotification(Info);
    }
}

// Add this method to your .cpp file as a fallback for multi-spline creation:


AActor* FDiggerEdModeToolkit::CreateManualMultiSplineActor(const FMultiSplineCaveData& CaveData, UWorld* World, const FVector& PivotOffset)
{
    if (!World || CaveData.Splines.Num() == 0)
    {
        return nullptr;
    }
    
    UE_LOG(LogTemp, Warning, TEXT("[DEBUG MANUAL] Creating manual multi-spline actor with %d splines"), CaveData.Splines.Num());
    
    // Create the main actor
    FActorSpawnParameters SpawnParams;
    SpawnParams.Name = MakeUniqueObjectName(World, AActor::StaticClass(), 
                                           *FString::Printf(TEXT("MultiSplineCave_%s"), 
                                           *FPaths::GetBaseFilename(CaveData.SourceSVGPath.IsEmpty() ? SelectedSVGFilePath : CaveData.SourceSVGPath)));
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    
    AActor* CaveActor = World->SpawnActor<AActor>(AActor::StaticClass(), SpawnParams);
    if (!CaveActor)
    {
        UE_LOG(LogTemp, Error, TEXT("[DEBUG MANUAL] Failed to spawn cave actor"));
        return nullptr;
    }
    
    UE_LOG(LogTemp, Warning, TEXT("[DEBUG MANUAL] Cave actor spawned, creating root component..."));
    
    // Create root component
    USceneComponent* RootComp = NewObject<USceneComponent>(CaveActor, USceneComponent::StaticClass(), TEXT("RootComponent"));
    CaveActor->SetRootComponent(RootComp);
    RootComp->RegisterComponentWithWorld(World);
    
    UE_LOG(LogTemp, Warning, TEXT("[DEBUG MANUAL] Root component created, creating %d spline components..."), CaveData.Splines.Num());
    
    // Create spline components for each passage
    for (int32 i = 0; i < CaveData.Splines.Num(); i++)
    {
        const FCaveSplineData& SplineData = CaveData.Splines[i];
        
        UE_LOG(LogTemp, Warning, TEXT("[DEBUG MANUAL] Creating spline %d with %d points"), i, SplineData.Points.Num());
        
        if (SplineData.Points.Num() < 2)
        {
            UE_LOG(LogTemp, Warning, TEXT("[DEBUG MANUAL] Skipping spline %d - insufficient points"), i);
            continue;
        }
        
        // Create spline component
        FString SplineName = FString::Printf(TEXT("Spline_%s_%d"), 
                                           *UEnum::GetValueAsString(SplineData.Type), i);
        
        USplineComponent* SplineComp = NewObject<USplineComponent>(CaveActor, USplineComponent::StaticClass(), *SplineName);
        if (!SplineComp)
        {
            UE_LOG(LogTemp, Error, TEXT("[DEBUG MANUAL] Failed to create spline component %d"), i);
            continue;
        }
        
        // Set up spline properties for editability
        SplineComp->CreationMethod = EComponentCreationMethod::UserConstructionScript;
        SplineComp->SetFlags(RF_Transactional);
        SplineComp->bEditableWhenInherited = true;

        
        // Setup spline points
        SplineComp->ClearSplinePoints();
        for (int32 j = 0; j < SplineData.Points.Num(); j++)
        {
            FVector PointLocation = SplineData.Points[j] - PivotOffset; // Apply pivot offset
            SplineComp->AddSplinePoint(PointLocation, ESplineCoordinateSpace::Local);
            SplineComp->SetSplinePointType(j, ESplinePointType::Curve);
        }
        
        // Update and configure
        SplineComp->UpdateSpline();
        SplineComp->SetMobility(EComponentMobility::Movable);
        SplineComp->bDrawDebug = true;
        SplineComp->SetVisibility(true);
        SplineComp->bHiddenInGame = false;
        
        // Color-code splines by type (if possible - this may not work in all UE versions)
        // SplineComp->SetSplineColor(GetSplineColorForType(SplineData.Type));
        
        // Add component tags for identification
        SplineComp->ComponentTags.Add(TEXT("ProcgenArcana"));
        SplineComp->ComponentTags.Add(*UEnum::GetValueAsString(SplineData.Type));
        SplineComp->ComponentTags.Add(*FString::Printf(TEXT("Width_%.0f"), SplineData.Width));
        
        // Attach to root component
        SplineComp->AttachToComponent(RootComp, FAttachmentTransformRules::KeepWorldTransform);
        SplineComp->RegisterComponentWithWorld(World);
        
        // Add to actor's component list
        CaveActor->AddInstanceComponent(SplineComp);
        
        UE_LOG(LogTemp, Warning, TEXT("[DEBUG MANUAL] Spline component %d created and attached"), i);
    }
    
    UE_LOG(LogTemp, Warning, TEXT("[DEBUG MANUAL] Setting up actor properties..."));
    
    // Set up actor properties
    CaveActor->SetActorLabel(SpawnParams.Name.ToString());
    CaveActor->Tags.Add(TEXT("ProgenCave"));
    CaveActor->Tags.Add(TEXT("MultiSpline"));
    CaveActor->Tags.Add(TEXT("ProcgenArcanaImport"));
    CaveActor->Tags.Add(*FString::Printf(TEXT("Splines_%d"), CaveData.Splines.Num()));
    CaveActor->Tags.Add(*FString::Printf(TEXT("Junctions_%d"), CaveData.Junctions.Num()));
    CaveActor->SetFolderPath(TEXT("Procgen Caves"));
    
    // Apply final positioning
    CaveActor->SetActorLocation(FVector::ZeroVector); // Points already have offset applied
    
    // Mark as editable and dirty
    CaveActor->SetFlags(RF_Transactional);
    CaveActor->MarkPackageDirty();
    
    TArray<USplineComponent*> SplineComponents;
    CaveActor->GetComponents<USplineComponent>(SplineComponents);

    UE_LOG(LogTemp, Log, TEXT("[DEBUG MANUAL] Successfully created manual multi-spline cave actor with %d spline components"),
           SplineComponents.Num());

    
    return CaveActor;
}


// Helper method to get spline color based on passage type
FLinearColor FDiggerEdModeToolkit::GetSplineColorForType(ECavePassageType Type)
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


FName FDiggerEdModeToolkit::GetToolkitFName() const { return FName("DiggerEdMode"); }
FText FDiggerEdModeToolkit::GetBaseToolkitName() const { return LOCTEXT("ToolkitName", "Digger Toolkit"); }
FEdMode* FDiggerEdModeToolkit::GetEditorMode() const { return GLevelEditorModeTools().GetActiveMode(FDiggerEdMode::EM_DiggerEdModeId); }
TSharedPtr<SWidget> FDiggerEdModeToolkit::GetInlineContent() const { return ToolkitWidget; }

#undef LOCTEXT_NAMESPACE

