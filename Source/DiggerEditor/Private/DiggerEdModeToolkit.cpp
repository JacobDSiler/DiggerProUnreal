// DiggerEdModeToolkit.cpp

#include "DiggerEdModeToolkit.h"
#include "DiggerEdMode.h"
#include "DiggerManager.h"
#include "FCustomSDFBrush.h"

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
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSlider.h" // <- You noted this should be added
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "FDiggerEdModeToolkit"



// FDiggerEdModeToolkit::Init

void FDiggerEdModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost)
{
    BindIslandDelegates();

    Manager = GetDiggerManager();

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

        // --- Debug Voxels Checkbox: Only visible for DebugChunk brush ---
    /*+ SVerticalBox::Slot().AutoHeight().Padding(4)
    [
        SNew(SBox)
        .Visibility_Lambda([this]()
        {
            return (GetCurrentBrushType() == EVoxelBrushType::Debug) ? EVisibility::Visible : EVisibility::Collapsed;
        })
        [
            SNew(SCheckBox)
            .IsChecked_Lambda([this]() { return bDebugVoxels; })
            .OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
            {
                bDebugVoxels = (NewState == ECheckBoxState::Checked);
            })
            [
                SNew(STextBlock).Text(FText::FromString("Debug Voxels"))
            ]
        ]
    ]
    + SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 4)
    [
        SNew(SCheckBox)
        .OnCheckStateChanged(this, &FDiggerEdModeToolkit::OnDetailedDebugCheckChanged)
        .IsChecked(this, &FDiggerEdModeToolkit::IsDetailedDebugChecked)
        [
            SNew(STextBlock)
            .Text(FText::FromString("Enable Detailed Debug"))
        ]
    ]*/



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

        
        

    // --- Cone Angle UI: Only visible for Cone brush ---
    + SVerticalBox::Slot().AutoHeight().Padding(4)
    [
        SNew(SBox)
        .Visibility_Lambda([this]() {
            return (GetCurrentBrushType() == EVoxelBrushType::Cone) ? EVisibility::Visible : EVisibility::Collapsed;
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
  /*  + SVerticalBox::Slot().AutoHeight().Padding(8, 16, 8, 4)
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
    ]*/

    // --- Rotate Brush Section (roll-down) ---
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
        
    // --- Save/Load Section ( Todo: roll-down) ---
    + SVerticalBox::Slot().AutoHeight().Padding(8, 12, 8, 4)
    [
    MakeSaveLoadSection()
    ]

    // --- Islands Section (roll-down) ---
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
}


void FDiggerEdModeToolkit::OnIslandDetectedHandler(const FIslandData& NewIslandData)
{
    // Only add if not already present (e.g., by IslandID)
    if (!Islands.ContainsByPredicate([&](const FIslandData& Island) { return Island.IslandID == NewIslandData.IslandID; }))
    {
        Islands.Add(NewIslandData);
        RebuildIslandGrid();
    }
}

void FDiggerEdModeToolkit::AddIsland(const FIslandData& Island)
{
    UE_LOG(LogTemp, Error, TEXT("AddIsland called on toolkit: %p, IslandID: %d"), this, Island.IslandID);
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
    if (Manager == GetDiggerManager())
    {
        UE_LOG(LogTemp, Warning, TEXT("Binding delegates for toolkit: %p, manager: %p"), this, Manager);

        Manager->OnIslandsDetectionStarted.RemoveAll(this);
        Manager->OnIslandDetected.RemoveAll(this);

        Manager->OnIslandsDetectionStarted.AddRaw(this, &FDiggerEdModeToolkit::ClearIslands);
        Manager->OnIslandDetected.AddRaw(this, &FDiggerEdModeToolkit::AddIsland);
    }
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
        // Slider (conditionally visible)
        /*+ SHorizontalBox::Slot()
        .FillWidth(1.0f)
        .Padding(0, 0, 8, 0)
        [
            SNew(SBox)
            .Visibility_Lambda([bExpanded]() { return *bExpanded ? EVisibility::Visible : EVisibility::Collapsed; })
            [
                SNew(SSlider)
                .Value_Lambda([=]() {
                    float Value = Getter();
                    return (Value - MinValue) / (MaxValue - MinValue);
                })
                .OnValueChanged_Lambda([=](float NewValue) {
                    Setter(MinValue + NewValue * (MaxValue - MinValue));
                })
            ]
        ]*/
        // Numeric entry (conditionally visible)
        + SHorizontalBox::Slot()
        .AutoWidth()
        [
            SNew(SBox)
            .Visibility_Lambda([bExpanded]() { return *bExpanded ? EVisibility::Visible : EVisibility::Collapsed; })
            [
                SNew(SNumericEntryBox<float>)
                .Value_Lambda([=]() { return Getter(); })
                .OnValueChanged_Lambda([=](float NewValue) {
                    Setter(FMath::Clamp(NewValue, MinValue, MaxValue));
                })
                .MinValue(MinValue)
                .MaxValue(MaxValue)
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
        + SVerticalBox::Slot().AutoHeight().Padding(8, 16, 8, 4)
        [
            MakeLabeledSliderRow(
                FText::FromString("Radius"),
                [this]() { return BrushRadius; },
                [this](float NewValue) { BrushRadius = FMath::Clamp(NewValue, 10.0f, 256.0f); },
                10.0f, 256.0f, {10.0f, 64.0f, 128.0f, 256.0f}, 10.0f
            )
        ];
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
        [&Value](float NewValue) { Value = double(FMath::Fmod(NewValue, 360.0f)); },
        0.0f, 360.0f,
        TArray<float>({45.f, 90.f, 180.f}),
        0.0f, 1.0f,
        true, // bIsAngle
        &DummyFloat // Mirror button not supported for double
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
        -100.0f, 100.0f,
        TArray<float>({-10.f, 0.f, 10.f}),
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
        -100.0f, 100.0f,
        TArray<float>({-10.f, 0.f, 10.f}),
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
        ];
}



//Make Island Widget <Here>

// DiggerEdModeToolkit.cpp

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
