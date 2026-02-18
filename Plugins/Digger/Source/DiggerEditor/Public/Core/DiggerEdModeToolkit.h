#pragma once

#include "CoreMinimal.h"
#include "Toolkits/BaseToolkit.h"
#include "DiggerManager.h"
#include "Materials/DiggerMaterialTypes.h"
#include "VoxelBrushTypes.h"
#include "FLightBrushTypes.h"
#include "FCustomBrushEntry.h"

// --- NEW WIDGET INCLUDES ---
#include "DiggerEdMode.h"
#include "DiggerFeatureFlags.h"
#include "SDiggerCaveImporterWidget.h"
#include "SDiggerLobbyWidget.h"

// Forward Declarations
class FEditorViewportClient;
class SUniformGridPanel;
class ADiggerManager;


// Digger Feature Flags Enabled/Disabled Section.
static bool IsBrushEnabled(EVoxelBrushType Type)
{
    switch (Type)
    {
    case EVoxelBrushType::Sphere:   return FDiggerFeatureFlags::bEnableBrush_Sphere;
    case EVoxelBrushType::Cube:     return FDiggerFeatureFlags::bEnableBrush_Cube;
    case EVoxelBrushType::Cylinder: return FDiggerFeatureFlags::bEnableBrush_Cylinder;
    case EVoxelBrushType::Capsule:  return FDiggerFeatureFlags::bEnableBrush_Capsule;
    case EVoxelBrushType::Cone:     return FDiggerFeatureFlags::bEnableBrush_Cone;
    case EVoxelBrushType::Torus:    return FDiggerFeatureFlags::bEnableBrush_Torus;
    case EVoxelBrushType::Pyramid:  return FDiggerFeatureFlags::bEnableBrush_Pyramid;
    case EVoxelBrushType::Icosphere:return FDiggerFeatureFlags::bEnableBrush_Icosphere;
    case EVoxelBrushType::Stairs:   return FDiggerFeatureFlags::bEnableBrush_Stairs;
    case EVoxelBrushType::Custom:   return FDiggerFeatureFlags::bEnableBrush_Custom;
    case EVoxelBrushType::Smooth:   return FDiggerFeatureFlags::bEnableBrush_Smooth;
    case EVoxelBrushType::Noise:    return FDiggerFeatureFlags::bEnableBrush_Noise;
    case EVoxelBrushType::Light:    return FDiggerFeatureFlags::bEnableBrush_Light;
    case EVoxelBrushType::Debug:    return FDiggerFeatureFlags::bEnableBrush_Debug;
    default: return false;
    }
}


/**
 * Main Toolkit for the Digger Editor Mode.
 */
class FDiggerEdModeToolkit : public FModeToolkit
{
public:
    FDiggerEdModeToolkit();
    virtual ~FDiggerEdModeToolkit() override;

    // --- FModeToolkit Interface ---
    virtual void Init(const TSharedPtr<IToolkitHost>& InitToolkitHost) override;
    virtual FName GetToolkitFName() const override;
    virtual FText GetBaseToolkitName() const override;
    virtual class FEdMode* GetEditorMode() const override;
    virtual TSharedPtr<class SWidget> GetInlineContent() const override;

    // --- Public API (Used by EdMode) ---
    void RequestBrushUIRefresh();
    void SetTemporaryDigOverride(TOptional<bool> Override);
    
    // Accessors
    float GetBrushRadius() const { return BrushRadius; }
    void SetBrushRadius(float InRadius) { BrushRadius = InRadius; }
    
    float GetBrushStrength() const { return BrushStrength; }
    void SetBrushStrength(float InStrength) { BrushStrength = InStrength; }
    
    float GetBrushFalloff() const { return BrushFalloff; }
    void SetBrushFalloff(float InFalloff) { BrushFalloff = InFalloff; }

    float GetBrushForce() const { return BrushForce; }
    void SetBrushForce(float InForce) { BrushForce = InForce; }

    float GetBrushLength() const { return BrushLength; }
    void SetBrushLength(float InLength) { BrushLength = InLength; }

    FVector GetBrushOffset() const { return BrushOffset; }
    void SetBrushOffset(FVector InOffset) { BrushOffset = InOffset; }

    FRotator GetBrushRotation() const { return FRotator(BrushRotX, BrushRotY, BrushRotZ); }
    void SetBrushRotation(const FRotator& InRot);

    float GetBrushAngle() const { return ConeAngle; }
    bool GetBrushIsFilled() const { return bIsFilled; }
    
    bool IsDigMode() const;
    
    EVoxelBrushType GetCurrentBrushType() const { return CurrentBrushType; }
    void SetCurrentBrushType(EVoxelBrushType NewType);

    EDiggerPushMode GetBrushPushMode() const { return CurrentPushMode; }
    void SetBrushPushMode(EDiggerPushMode Mode) { CurrentPushMode = Mode; }
    // Options array for the dropdown
    TArray<TSharedPtr<EDiggerPushMode>> PushModeOptions;

    // Light Brush Specifics (Fixed: This was missing)
    ELightBrushType GetCurrentLightType() const { return CurrentLightType; }
    FLinearColor GetBrushLightColor() const { return BrushLightColor; }

    // Advanced Cube
    bool IsUsingAdvancedCubeBrush() const { return bUseAdvancedCubeBrush; }
    float GetAdvancedCubeHalfExtentX() const { return AdvancedCubeHalfExtentX; }
    float GetAdvancedCubeHalfExtentY() const { return AdvancedCubeHalfExtentY; }
    float GetAdvancedCubeHalfExtentZ() const { return AdvancedCubeHalfExtentZ; }
    
    // Seams (Fixed: These were missing)
    bool GetHiddenSeam() const { return bHiddenSeam; }
    ECheckBoxState GetHiddenSeamCheckState() const;
    void OnHiddenSeamChanged(ECheckBoxState NewState);

    // Orientation
    bool UseSurfaceNormalRotation() const { return bUseSurfaceNormalRotation; }
    bool RotateToSurfaceNormal() const { return bRotateToSurfaceNormal; }

    // Debugging (Fixed: These were missing)
    ECheckBoxState IsBrushDebugEnabled() const;
    void OnBrushDebugCheckChanged(ECheckBoxState NewState);

    // Custom Brushes
    bool CanPaintWithCustomBrush() const;
    FString GetBrushDisplayName(const FCustomBrushEntry& Entry) const; // Fixed: Missing declaration

    // --- Sub-Widget Access ---
    TSharedPtr<SDiggerCaveImporterWidget> GetCaveImporterWidget() const { return CaveImporterWidget; }

    // --- Environment / Worklight API ---
    void SetViewportClient(FEditorViewportClient* InClient) { CachedViewportClient = InClient; }
    void SpawnOrUpdateWorklight(FEditorViewportClient* ViewportClient);
    void DestroyWorklight();
    void ToggleWorklight(bool bEnable);
    bool GetWorklightEnabled() const { return bWorklightEnabled; }
    bool GetAutoUnderLandscape() const { return bAutoUnderLandscape; }
    void GetElevationInfo(float& AbsoluteOut, float& RelativeOut) const;

    // --- LIGHTING TOOLS ---
    
    // Worklight (Scene)
    bool bWorklightEnabled = true;
    float WorklightIntensity = 50000.0f; // Default high for visibility
    float WorklightAttenuation = 5000.0f;
    FLinearColor WorklightColor = FLinearColor::White;

    
    // Brush Light (Preview)
    float BrushLightIntensity = 1500.0f;
    float BrushLightAttenuation = 1000.0f;
    bool bMatchBrushLightColor = true;

    // Helper to save
    void SaveLightingSettings();
    
    // --- Save System ---
    FString GetCurrentSaveFileName() const;

    // --- Material Manager API ---
    UDiggerMaterialProfile* GetMutableProfile() const;
    void EnsureActiveProfile();

protected:
    ADiggerManager* GetDiggerManager() const;

private:
    // The Root Widget
    TSharedPtr<SVerticalBox> ToolkitWidget;
    
    // Cached Reference
    ADiggerManager* Manager = nullptr; 

    // --- SUB-WIDGET REFERENCES ---
    TSharedPtr<SDiggerCaveImporterWidget> CaveImporterWidget;
    TSharedPtr<SDiggerLobbyWidget> LobbyWidget;
    

    // -------------------------------------------------------------------------
    // UI GENERATION
    // -------------------------------------------------------------------------
    TSharedRef<SWidget> MakeBrushShapeSection();
    TSharedRef<SWidget> MakeCustomBrushSection();
    TSharedRef<SWidget> MakeGenerationSection();
    TSharedRef<SWidget> MakeOperationSection();
    TSharedRef<SWidget> MakeBuildExportSection();
    TSharedRef<SWidget> MakeSaveLoadSection();
    TSharedRef<SWidget> MakeResetDiggerDataWidget();

    // Environment
    TSharedRef<SWidget> MakeNavigationSection();
    TSharedRef<SWidget> MakeWorklightSection();
    TSharedRef<SWidget> MakeIslandsSection();
    
    // Material Manager
    TSharedRef<SWidget> MakeMaterialManagerSection();

    // New Modular Sections
    TSharedRef<SWidget> MakeAdditionalToolsSection();
    TSharedRef<SWidget> MakeProcgenArcanaImporterWidget();
    TSharedRef<SWidget> MakeLobbySection();
    
    // UI Helpers
    TSharedRef<SWidget> MakeRollDownHeader(const FString& Label, bool& bToggleFlag);
    TSharedRef<SWidget> MakeBrushParameterSection();

    // -------------------------------------------------------------------------
    // BRUSH STATE
    // -------------------------------------------------------------------------
    float BrushRadius = 50.0f;
    float BrushFalloff = 0.2f;
    float BrushStrength = 0.8f;
    float BrushForce = 0.5f;
    float BrushLength = 200.0f;
    
    // Rotation & Offset
    float BrushRotX = 0.0f;
    float BrushRotY = 0.0f;
    float BrushRotZ = 0.0f;
    FVector BrushOffset = FVector::ZeroVector;

    // Brush Settings
    EVoxelBrushType CurrentBrushType = EVoxelBrushType::Sphere;
    bool bBrushDig = false;
    TOptional<bool> TemporaryDigOverride;

    EDiggerPushMode CurrentPushMode = EDiggerPushMode::Ray;
    
    float ConeAngle = 45.0f;
    float MinConeAngle = 5.0f;
    float MaxConeAngle = 60.0f;
    float MinBrushLength = 10.0f;
    float MaxBrushLength = 256.0f;
    int16 SmoothIterations = 1;

    // Shape Specifics
    float TorusInnerRadius = 0.2f;
    float MinTorusInnerRadius = 1.0f;
    float MaxTorusInnerRadius = 5.0f;
    bool bIsFilled = true;
    bool bHiddenSeam = false;

    // Advanced Cube
    bool bUseAdvancedCubeBrush = false;
    float MinCubeExtent = 20.0f;
    float MaxCubeExtent = 600.0f;
    float AdvancedCubeHalfExtentX = 50.0f;
    float AdvancedCubeHalfExtentY = 50.0f;
    float AdvancedCubeHalfExtentZ = 60.0f;

    // Orientation Logic
    bool bRotateToSurfaceNormal = false;
    bool bUseSurfaceNormalRotation = false;

    // Light Brush (Fixed: Renamed member to match EdMode)
    ELightBrushType CurrentLightType = ELightBrushType::Point;
    FLinearColor BrushLightColor = FLinearColor::White;
    TArray<TSharedPtr<ELightBrushType>> LightTypeOptions;
    void PopulateLightTypeOptions();
    void OnLightColorChanged(FLinearColor NewColor);
    void OnLightTypeChanged(TSharedPtr<ELightBrushType> NewSelection, ESelectInfo::Type);
    TSharedRef<SWidget> MakeLightTypeComboWidget(TSharedPtr<ELightBrushType> InItem);

    // Preview Overrides
    bool bUseBrushDigPreviewOverride = false;
    bool bBrushDigPreviewOverride = false;
    void SetBrushDigPreviewOverride(bool bInDig);
    void ClearBrushDigPreviewOverride();

    // Hole Members
    bool bShowDynamicHoles = true;


    // -------------------------------------------------------------------------
    // CUSTOM BRUSHES
    // -------------------------------------------------------------------------
    TArray<FCustomBrushEntry> CustomBrushEntries;
    TSharedPtr<SUniformGridPanel> CustomBrushGrid;
    TSharedPtr<SVerticalBox> CustomBrushGridContainer;
    TSharedPtr<FAssetThumbnailPool> AssetThumbnailPool;
    int32 SelectedBrushIndex = -1;
    
    void ScanCustomBrushFolder();
    void RebuildCustomBrushGrid();
    void DeleteCustomBrush(int32 Index);
    
    // -------------------------------------------------------------------------
    // ENVIRONMENT & WORKLIGHT
    // -------------------------------------------------------------------------
    FEditorViewportClient* CachedViewportClient = nullptr;
    TWeakObjectPtr<AActor> WorklightActor = nullptr;
    class ULightComponent* DiggerWorklightComponent = nullptr;
    
    FString SelectedWorkLightType = "Point";
    TArray<TSharedPtr<FString>> WorklightTypeOptions;
    TSharedPtr<FString> SelectedWorklightTypeItem;
    
    bool bAutoUnderLandscape = false;
    bool bSmoothLandscapeAware = false;

    void UpdateWorklightType(const FString& NewType);
    void UpdateWorklightIntensity(float NewIntensity);
    void UpdateWorklightAttenuation(float NewRadius);
    void UpdateWorklightColor(const FLinearColor& NewColor);

    // -------------------------------------------------------------------------
    // ISLAND MANAGEMENT
    // -------------------------------------------------------------------------
    TSharedPtr<SBox> IslandGridContainer;
    TSharedPtr<SUniformGridPanel> IslandGrid;
    TArray<FIslandData> Islands;
    int32 SelectedIslandIndex = INDEX_NONE;
    FRotator IslandRotation;

    void BindIslandDelegates();
    void OnIslandDetectedHandler(const FIslandData& NewIslandData);
    void AddIsland(const FIslandData& Island);
    void ClearIslands();
    void RebuildIslandGrid();
    TSharedRef<SWidget> MakeIslandGridWidget();
    
    void OnConvertToPhysicsActorClicked();
    void OnConvertToSceneActorClicked();
    void OnRemoveIslandClicked();

    // -------------------------------------------------------------------------
    // SAVE / EXPORT
    // -------------------------------------------------------------------------
    TSharedPtr<SEditableTextBox> SaveFileNameWidget;
    TSharedPtr<SComboBox<TSharedPtr<FString>>> SaveFileComboBox;
    TArray<TSharedPtr<FString>> AvailableSaveFiles;
    
    void RefreshSaveFilesList();
    FReply OnClearAllClicked();

    // Build Settings
    bool bEnableCollision = true;
    bool bEnableNanite = false;
    float BakeDetail = 1.0f;
    bool bShowBuildSettings = false;

    // Mesh Generation Options
    TArray<TSharedPtr<FString>> MeshGenerationOptions = {
        MakeShared<FString>(TEXT("Cubic")),
        MakeShared<FString>(TEXT("Marching Cubes")),
        MakeShared<FString>(TEXT("Dual Contouring"))
    };
    TSharedPtr<FString> SelectedMeshGenerationMethod = MeshGenerationOptions[0];

    // -------------------------------------------------------------------------
    // MATERIAL MANAGER (DMM)
    // -------------------------------------------------------------------------
    enum class EDMMPanelMode : uint8 { Sediment, Landscape, Utilities };
    
    EDMMPanelMode CurrentDMMMode = EDMMPanelMode::Sediment;
    TWeakObjectPtr<UDiggerMaterialProfile> ActiveMaterialProfile;
    TSharedPtr<class SBox> SedimentBodyBox;
    bool bShowMaterialManagerSection = true;

    void LoadDMMState();
    void SaveDMMState() const;
    void TouchProfile(UDiggerMaterialProfile* Profile);
    static FString GetProfileConfigKey() { return TEXT("DMM.ActiveProfilePath"); }
    static FString GetModeConfigKey()    { return TEXT("DMM.Mode"); }

    TSharedRef<SWidget> MakeDMMHeaderRow();
    TSharedRef<SWidget> MakeDMMToolbar();
    TSharedRef<SWidget> MakeDMMBody();
    TSharedRef<SWidget> MakeLayerListHeader();
    TSharedRef<SWidget> MakeLayerListBody();
    TSharedRef<SWidget> MakeSedimentBody();
    TSharedRef<SWidget> MakeSedimentLayerRow(int32 Index);
    TSharedRef<SWidget> MakeLandscapeBody();
    TSharedRef<SWidget> MakeUtilitiesBody();

    FReply OnDMMHeaderClicked();
    FReply OnQuickApplyClicked();
    FReply OnAddLayerClicked();
    FReply OnRemoveLayerClicked(int32 LayerIndex);
    FReply OnMoveLayerUpClicked(int32 LayerIndex);
    FReply OnMoveLayerDownClicked(int32 LayerIndex);

    // -------------------------------------------------------------------------
    // UI HELPERS & STATE
    // -------------------------------------------------------------------------
    TSharedPtr<FSlateStyleSet> DiggerStyleSet;
    FDelegateHandle PreBeginPIEHandle;

    // UI Folding State
    bool bShowBrushShapeSection = true;
    bool bShowCustomBrushSection = false;
    bool bShowWorklightSection = false;
    bool bNavigationSection = false;
    bool bShowIslandsSection = false;
    bool bShowSaveLoadSection = false;
    bool bShowRotation = false;
    bool bShowOffset = false;
    bool bShowBrushParameters = true;

    // Outliner Helpers
    // Helper to set the protected bListedInSceneOutliner property
public:
    // Make this public and static so the Spawner can use it
    static void SetActorListedInOutliner(AActor* Actor, bool bListed);

    static void SetDynamicHolesFolderVisible(bool bVisible);

private:
    

    // Helper Factory Methods
    TSharedRef<SWidget> MakeLabeledSliderRow(const FText& Label, TFunction<float()> Getter, TFunction<void(float)> Setter, float Min, float Max, const TArray<float>& QuickSet, float Reset = 0.0f, float Step = 1.0f, bool bIsAngle = false, float* MirrorTarget = nullptr);
    TSharedRef<SWidget> MakeQuickSetButtons(const TArray<float>& Values, TFunction<void(float)> Setter, float* MirrorTarget = nullptr, bool bIsAngle = false);
    TSharedRef<SWidget> MakeDebugCheckbox(const FString& Label, bool* FlagPtr);
    TSharedRef<SWidget> MakeDebugCheckbox(const struct FDiggerDebug::FFlagEntry& FlagEntry);
    
    // Overloads for convenience
    TSharedRef<SWidget> MakeRotationRow(const FText& Label, float& Value);
    TSharedRef<SWidget> MakeRotationRow(const FText& Label, double& Value);
    TSharedRef<SWidget> MakeOffsetRow(const FText& Label, float& Value);
    TSharedRef<SWidget> MakeOffsetRow(const FText& Label, double& Value);
    TSharedRef<SWidget> MakeRotationSection(float& RotX, float& RotY, float& RotZ);
    TSharedRef<SWidget> MakeOffsetSection(FVector& Offset);
    
    TSharedRef<SWidget> MakeAngleButton(float Angle, float& Target, const FString& Label);
    TSharedRef<SWidget> MakeAngleButton(double Angle, double& Target, const FString& Label);
    TSharedRef<SWidget> MakeMirrorButton(float& Target, const FString& Label);
    TSharedRef<SWidget> MakeMirrorButton(double& Target, const FString& Label);
};