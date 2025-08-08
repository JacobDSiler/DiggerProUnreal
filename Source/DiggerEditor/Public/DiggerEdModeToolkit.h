// DiggerEdModeToolkit.h
#pragma once

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "ProcgenArcanaCaveImporter.h"
#include "SparseVoxelGrid.h" // <-- Make sure this is included!
#include "EditorModeManager.h"
#include "Toolkits/BaseToolkit.h"        
#include "VoxelBrushTypes.h"
#include "Toolkits/BaseToolkit.h"
#include "FCustomBrushEntry.h"
#include "SocketIOLobbyManager.h"   
#include "Widgets/Layout/SSeparator.h"
#include "DiggerEdModeToolkit.generated.h" // This MUST be the last include


class USocketIOLobbyManager;
class IWebSocket;
struct FIslandData;
class ADiggerManager;
class SUniformGridPanel;



UENUM(BlueprintType)
enum class EIslandOriginMode : uint8
{
	Center UMETA(DisplayName = "Center"),
	Base UMETA(DisplayName = "Base"),
	Top UMETA(DisplayName = "Top")
};



// Simple struct for holding lobby information
struct FLobbyInfo
{
	FString LobbyName;
	int32 PlayerCount;

	FLobbyInfo() = default;

	FLobbyInfo(const FString& InName, int32 InPlayers)
		: LobbyName(InName), PlayerCount(InPlayers)
	{}

	FString ToDisplayString() const
	{
		return FString::Printf(TEXT("%s (%d players)"), *LobbyName, PlayerCount);
	}
};
USTRUCT(BlueprintType)
struct DIGGEREDITOR_API FDiggerUIFeatureFlags
{
	GENERATED_USTRUCT_BODY()

	// Cave Importer Features
	UPROPERTY(EditAnywhere, Category = "Cave Importer")
	bool bEnableCaveImporter = true;
    
	UPROPERTY(EditAnywhere, Category = "Cave Importer")
	bool bEnableHeightModes = true;
    
	UPROPERTY(EditAnywhere, Category = "Cave Importer") 
	bool bEnableAdvancedSettings = false;
    
	UPROPERTY(EditAnywhere, Category = "Cave Importer")
	bool bEnableMultipleEntrances = false;
    
	UPROPERTY(EditAnywhere, Category = "Cave Importer")
	bool bEnableMeshGeneration = false;

	// Other Plugin Features
	UPROPERTY(EditAnywhere, Category = "Spline Brush")
	bool bEnableSplineBrush = false;
    
	UPROPERTY(EditAnywhere, Category = "Advanced")
	bool bEnableProcgenIntegration = false;
    
	FDiggerUIFeatureFlags()
	{
		bEnableCaveImporter = true;
		bEnableHeightModes = true;
		bEnableAdvancedSettings = false;
		bEnableMultipleEntrances = false;
		bEnableMeshGeneration = false;
		bEnableSplineBrush = false;
		bEnableProcgenIntegration = false;
	}
};


class FDiggerEdModeToolkit : public FModeToolkit
{
public:
	// Declare this so you can define it in the .cpp
	FDiggerEdModeToolkit();
	
	void SetUseAdvancedCubeBrush(bool bInUse)
	{
		bUseAdvancedCubeBrush = bInUse;
	}
	
	[[nodiscard]] float GetAdvancedCubeHalfExtentY() const
	{
		return AdvancedCubeHalfExtentY;
	}

	[[nodiscard]] float GetAdvancedCubeHalfExtentX() const
	{
		return AdvancedCubeHalfExtentX;
	}

	[[nodiscard]] float GetAdvancedCubeHalfExtentZ() const
	{
		return AdvancedCubeHalfExtentZ;
	}

	void ClearIslands();
	void RebuildIslandGrid();
	
	void AddIsland(const FIslandData& Island);
	void BindIslandDelegates();
	virtual void Init(const TSharedPtr<IToolkitHost>& InitToolkitHost) override;
	bool CanPaintWithCustomBrush() const;
	void ScanCustomBrushFolder();
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual class FEdMode* GetEditorMode() const override;
	virtual TSharedPtr<SWidget> GetInlineContent() const override;

	// In your class declaration
	FDiggerUIFeatureFlags UIFeatureFlags;

	virtual ~FDiggerEdModeToolkit() override;
	void OnIslandDetectedHandler(const FIslandData& NewIslandData);
	


	[[nodiscard]]bool IsDigMode() const
	{
		if (TemporaryDigOverride.IsSet())
		{
			return TemporaryDigOverride.GetValue();
		}

		return bBrushDig;
	}

	[[nodiscard]] FVector GetBrushOffset() const
	{
		return BrushOffset;
	}
	
	void SetBrushOffset(FVector NewOffset)
	{
		BrushOffset = NewOffset;
	}
	
	[[nodiscard]] float GetBrushAngle() const
	{
		return ConeAngle;
	}
	

    void SetIslands(const TArray<FIslandData>& InIslands)
    {
        Islands = InIslands;
        RebuildIslandGrid();
    }

	void SetBrushRotation(const FRotator& SurfaceNormal)
	{
		BrushRotX = SurfaceNormal.Pitch; // X axis tilt (vertical tilt)
		BrushRotY = SurfaceNormal.Yaw;   // Y axis turn (left/right)
		BrushRotZ = SurfaceNormal.Roll;  // Z axis spin (rare for normals)

		ToolkitWidget->Invalidate(EInvalidateWidget::LayoutAndVolatility);

		UE_LOG(LogTemp, Log, TEXT("SetBrushRotation -> Pitch: %f, Yaw: %f, Roll: %f"), BrushRotX, BrushRotY, BrushRotZ);
	}

	bool GetBrushIsFilled();

private:
	FDiggerUIFeatureFlags FeatureFlags;

	// New members for save file management
	TSharedPtr<SEditableTextBox> SaveFileNameWidget;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> SaveFileComboBox;
	TArray<TSharedPtr<FString>> AvailableSaveFiles;
	// Helper methods
	void RefreshSaveFilesList();

	
#if WITH_SOCKETIO
	USocketIOLobbyManager* SocketIOLobbyManager = nullptr;
#endif
	
	FLinearColor CurrentLightColor = FLinearColor::White;




public:
	FLinearColor GetCurrentLightColor() const { return CurrentLightColor; }
	void OnLightColorChanged(FLinearColor NewColor);

	void ShutdownNetworking();


private:
	ELightBrushType CurrentLightType = ELightBrushType::Spot;
    
public:
	ELightBrushType GetCurrentLightType() const { return CurrentLightType; }
	
	// Azgar Importer Methods
	void ImportProcgenArcanaCave();
	void PreviewProcgenArcanaCave();
	void ClearCavePreview();
	bool ValidateImportSettings();
	AActor* CreateCaveSplineActor(USplineComponent* SplineComponent, const TArray<FVector>& OriginalPoints, const TArray<FVector>& EntrancePoints, const
	                              TArray<FVector>& ExitPoints);

private:
	// Manual multi-spline creation (fallback)
	AActor* CreateManualMultiSplineActor(const FMultiSplineCaveData& CaveData, UWorld* World, const FVector& PivotOffset);
	FLinearColor GetSplineColorForType(ECavePassageType Type);
	void ImportMultiSplineCave();

public:
	/**
	 * Multi-spline import function - creates multiple connected splines from SVG
	 */
	UFUNCTION(BlueprintCallable, Category = "ProcgenArcana Import")
	FMultiSplineCaveData ImportMultiSplineCaveFromSVG(const FProcgenArcanaImportSettings& Settings);
    
	/**
	 * Create actor with multiple spline components from cave data
	 */
	UFUNCTION(BlueprintCallable, Category = "ProcgenArcana Import") 
	AActor* CreateMultiSplineCaveActor(const FMultiSplineCaveData& CaveData, UWorld* World, const FVector& PivotOffset = FVector::ZeroVector);

private:
	// Multi-spline processing methods
	FMultiSplineCaveData ParseSVGToMultiSplineData(const FString& SVGContent, const FProcgenArcanaImportSettings& Settings);
	TArray<FSVGPath> ExtractAllCavePaths(const FString& SVGContent);
	TArray<FSVGPathNode> BuildPathGraph(const TArray<FSVGPath>& Paths, float JunctionTolerance = 50.0f);
	void ClassifyPassageTypes(TArray<FSVGPath>& Paths, const TArray<FSVGPathNode>& Nodes);
	void EstimatePassageWidths(TArray<FSVGPath>& Paths, const FProcgenArcanaImportSettings& Settings);
	FCaveSplineData ConvertPathToSplineData(const FSVGPath& Path, const FProcgenArcanaImportSettings& Settings);
	USplineComponent* CreateSplineComponentFromData(const FCaveSplineData& SplineData, UObject* Owner);
    
	// Safe multi-spline processing methods with limits (the ones we just added)
	FMultiSplineCaveData ParseSVGToMultiSplineDataSafe(const FString& SVGContent, const FProcgenArcanaImportSettings& Settings);
	TArray<FSVGPath> ExtractAllCavePathsSafe(const FString& SVGContent, int32 MaxPaths = 50);
	TArray<FSVGPathNode> BuildPathGraphSafe(const TArray<FSVGPath>& Paths, float JunctionTolerance, int32 MaxNodes = 100);
	TArray<FVector2D> ParseSVGPathDataSafe(const FString& PathData, int32 MaxPoints = 500);
    
	// Helper method for SVG path parsing (you may already have this)
	TArray<FVector2D> ParseSVGPathData(const FString& PathData);
	
private:

	TSharedPtr<SVerticalBox> ToolkitWidget;
	EVoxelBrushType CurrentBrushType = EVoxelBrushType::Sphere; // First option;
	float ConeAngle=45;
	float MinConeAngle=5;
	float MaxConeAngle=60;
	float MinBrushLength=10;
	float MaxBrushLength=256.0;
	// ReSharper disable once CppUninitializedNonStaticDataMember
	int16 SmoothIterations; // or short SmoothIterations;
	FVector BrushOffset = FVector(0.f,0.f,0.f);
	
	// Network account Google Icon
	TSharedPtr<FSlateStyleSet> DiggerStyleSet;



private:
	bool bShowOffset=false;
	bool bRotateToSurfaceNormal = false;
	bool bUseSurfaceNormalRotation = false;
	float TorusInnerRadius=.2f;
	float MinTorusInnerRadius=1.f;
	float MaxTorusInnerRadius=5.f;
	bool bIsFilled=true;
	float MinCubeExtent=20.f;
	float MaxCubeExtent=600.f;
	float AdvancedCubeHalfExtentY=50.f;
	float AdvancedCubeHalfExtentX=50.f;
	float AdvancedCubeHalfExtentZ=60.f;
	bool bUseAdvancedCubeBrush = false;

public:
	[[nodiscard]] bool IsUsingAdvancedCubeBrush() const
	{
		return bUseAdvancedCubeBrush;
	}

private:
	ADiggerManager* Manager;

public:
	[[nodiscard]] bool UseSurfaceNormalRotation() const
	{
		return bUseSurfaceNormalRotation;
	}

	void SetUseSurfaceNormalRotation(bool InbUseSurfaceNormalRotation)
	{
		this->bUseSurfaceNormalRotation = InbUseSurfaceNormalRotation;
	}

	[[nodiscard]] bool RotateToSurfaceNormal() const
	{
		return bRotateToSurfaceNormal;
	}

private:
	// Output format selection
	int32 OutputFormat = 1; // 0=Single Spline, 1=Multi-Spline, 2=Proc Mesh, 3=SDF Brush
	bool bDetailedDebug = false;
	

	// Function to check the current state of the checkbox
	ECheckBoxState IsDetailedDebugChecked() const
	{
		return bDetailedDebug ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}

	//Helpers
	ADiggerManager* GetDiggerManager();

	TSharedRef<SWidget> MakeIslandGridWidget();
	TSharedRef<SWidget> MakeDebugCheckbox(const FString& Label, bool* FlagPtr);
	TSharedRef<SWidget> MakeAngleButton(float Angle, float& Target, const FString& Label);
    TSharedRef<SWidget> MakeAngleButton(double Angle, double& Target, const FString& Label);
    TSharedRef<SWidget> MakeMirrorButton(float& Target, const FString& Label);
	TSharedRef<SWidget> MakeMirrorButton(double& Target, const FString& Label);
	TSharedRef<SWidget> MakeRotationSection(float& RotX, float& RotY, float& RotZ);
	TSharedRef<SWidget> MakeOperationSection();
	TSharedRef<SWidget> MakeBrushShapeSection();
	TSharedRef<SWidget> MakeOffsetSection(FVector& Offset);
	TSharedRef<SWidget> MakeRotationRow(const FText& Label, double& Value);
	TSharedRef<SWidget> MakeRotationRow(const FText& Label, float& Value);
	TSharedRef<SWidget> MakeOffsetRow(const FText& Label, double& Value);
	TSharedRef<SWidget> MakeOffsetRow(const FText& Label, float& Value);

	TSharedRef<SWidget> MakeProcgenArcanaImporterWidget();

	FVector CalculatePivotOffset(const TArray<FVector>& Points, 
												   const TArray<FVector>& Entrances, 
												   const TArray<FVector>& Exits);
	
	void OnConvertToPhysicsActorClicked();
	void OnRemoveIslandClicked();
	TSharedRef<SWidget> MakeIslandsSection();

	TSharedRef<SWidget> MakeLabeledSliderRow(
		const FText& Label,
		TFunction<float()> Getter,
		TFunction<void(float)> Setter,
		float MinValue,
		float MaxValue,
		const TArray<float>& QuickSetValues,
		float ResetValue = 0.0f,
		float Step = 1.0f,
		bool bIsAngle = false,
		float* TargetForMirror = nullptr // pointer, not reference!
	);

	TSharedRef<SWidget> MakeQuickSetButtons(
		const TArray<float>& QuickSetValues,
		TFunction<void(float)> Setter,
		float* TargetForMirror = nullptr,
		bool bIsAngle = false
	);

	// Multiplayer Menu (Always Available)
	TSharedRef<SWidget> MakeLobbySection();
	TSharedRef<SWidget> MakeNetworkingWidget();
	TSharedRef<SWidget> MakeNetworkingHelpWidget();
	void CreateLobbyManager(UWorld* WorldContext);
	TSharedPtr<IWebSocket> WebSocket;
	TSharedPtr<SEditableTextBox> TemporaryPasswordBox;


	// UI Elements
	TSharedPtr<SEditableTextBox> CreateLobbyTextBox;
	TSharedPtr<SButton> CreateLobbyButton;
	TSharedPtr<SButton> JoinLobbyButton;
	TSharedPtr<SListView<TSharedPtr<FLobbyInfo>>> LobbyListView;
	
#if WITH_SOCKETIO
	
	// Socket.IO Networking
	TWeakObjectPtr<USocketIOLobbyManager> SocketIOClient;
	
	/** Popup login window (Google or temp username) */
	FReply ShowLoginModal();
	void ConnectToLobbyServer();

	/** Create a new lobby on the server */
	void CreateLobby(const FString& LobbyName);

	/** Join an existing lobby by name/ID */
	void JoinLobby(const FString& LobbyName);
	
	/** Username entry in the login modal */
	TSharedPtr<SEditableTextBox> UsernameTextBox;

	/** The Slate modal window itself */
	TSharedPtr<SWindow> LoginWindow;
	bool IsConnectButtonEnabled() const;
	FReply OnConnectClicked();

	// For Gating the create/join lobby buttons
#if WITH_SOCKETIO
	bool IsCreateLobbyEnabled() const;
	bool IsJoinLobbyEnabled() const;
#endif

	
	// In your class definition
	TSharedPtr<SEditableTextBox> LobbyNameTextBox;
	TSharedPtr<SEditableTextBox> LobbyIdTextBox;

	FReply OnCreateLobbyClicked();
	FReply OnJoinLobbyClicked();

	

	// Lobby State
	TArray<TSharedPtr<FLobbyInfo>> LobbyList;
	TSharedPtr<FLobbyInfo> SelectedLobby;
	FString PendingLobbyName;
	FString LoggedInUser;

#endif



	
public:
	[[nodiscard]] float GetBrushRadius() const
	{
		return BrushRadius;
	}
	
	[[nodiscard]] FRotator GetBrushRotation() const
	{
		return FRotator(BrushRotX,BrushRotY,BrushRotZ);
	}
	

	// Override to return the toolkit as IToolkit (for base class compatibility)
	virtual TSharedPtr<IToolkit> GetToolkit() { return AsShared(); }
	
	float GetBrushLength() const { return BrushLength; }
	void SetBrushLength(float NewLength) { BrushLength = NewLength; }
	EVoxelBrushType GetCurrentBrushType() const {return CurrentBrushType;}
	

	const TArray<FIslandData>& GetIslands() const
	{
	    return Islands;
	}

	void SetTemporaryDigOverride(TOptional<bool> Override);
	
	

	// Make sure this declaration matches exactly
	void OnLightTypeChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);

private:
	// Interactive preview positioning
	FVector PreviewPositionOffset = FVector::ZeroVector;
    
	// Method to update preview position in real-time
	void UpdatePreviewPosition();

	TSharedPtr<SUniformGridPanel> CustomBrushGrid;
	TArray<FCustomBrushEntry> CustomBrushEntries;
	int32 SelectedBrushIndex = -1;
	
	float BrushRadius = 50.0f;
	float BrushStrength = 0.8f;
	float BrushFalloff = 0.2f;
	float BrushRotX = 0.0f;
	float BrushRotY = 0.0f;
	float BrushRotZ = 0.0f;
	float BrushOffSetX = 0.0f;
	float BrushOffSetY = 0.0f;
	float BrushOffSetZ = 0.0f;
	bool bShowBuildSettings = false;
	bool bEnableCollision = true;
	bool bEnableNanite = false;
	float BakeDetail = 1.0f;
	bool bShowRotation = false;
	float BrushLength = 200.0f; // Default value, adjust as needed
	bool bDebugVoxels = false;

	TArray<FIslandData> Islands; // Your detected islands
	int32 SelectedIslandIndex = INDEX_NONE; // Currently selected island
	FRotator IslandRotation;
	
	
	bool bUseBrushDigPreviewOverride = false;
	bool bBrushDigPreviewOverride = false;

	void SetBrushDigPreviewOverride(bool bInDig);
	void ClearBrushDigPreviewOverride();

	TSharedPtr<SComboBox<TSharedPtr<ELightBrushType>>> LightTypeComboBox;
	TArray<TSharedPtr<ELightBrushType>> LightTypeOptions;
	
	void PopulateLightTypeOptions();
	TSharedRef<SWidget> MakeLightTypeComboWidget(TSharedPtr<ELightBrushType> InItem);
	void OnLightTypeChanged(TSharedPtr<ELightBrushType> NewSelection, ESelectInfo::Type);
	
	TOptional<bool> TemporaryDigOverride;

private:
	// ProcgenArcana Importer variables
	
	// World-scale positioning system
	bool bUseWorldScalePositioning = false;
	FVector WorldScalePosition = FVector::ZeroVector;
	bool bSnapToGrid = true;
	float SnapIncrement = 100.0f;
    
	// World scale indicator
	TWeakObjectPtr<AActor> WorldScaleIndicatorActor;
    
	// World scale methods
	void PlaceWorldScaleIndicator();
	void ClearWorldScaleIndicator();
	void HandleWorldScaleClick(const FVector& ClickLocation);

	

	bool bShowProcgenArcanaImporter = false;
	FString SelectedSVGFilePath;
	float SimplificationLevel = 0.5f;
	int32 HeightMode = 0;
	float CaveScale = 100.0f;
	float HeightVariation = 50.0f;
	float DescentRate = 0.3f;
	bool bAutoDetectEntrance = true;
	FVector2D ManualEntrancePoint = FVector2D::ZeroVector;
	int32 MaxSplinePoints = 200;
	bool bPreviewMode = false;
	
	TArray<TSharedPtr<FString>> HeightModeOptions;

	UPROPERTY()
	UProcgenArcanaCaveImporter* CaveImporter = nullptr;
	
	// Pivot mode settings
	int32 PivotMode = 0; // 0=Center, 1=First Entrance, 2=First Exit, 3=Start, 4=End
	TArray<TSharedPtr<FString>> PivotModeOptions;
    
	// Preview data storage
	TArray<FVector> StoredPreviewPoints;
	TArray<FVector> StoredEntrancePoints;
	TArray<FVector> StoredExitPoints;
	bool bHasActivePreview = false;
    
	// Helper methods
	FString GetPivotModeDisplayName(int32 Mode);
	
	TArray<TSoftObjectPtr<UStaticMesh>> CustomBrushMeshes;
	TSharedPtr<SBox> IslandGridContainer; // Add this!
	TSharedPtr<SUniformGridPanel> IslandGrid;
	TSharedPtr<FAssetThumbnailPool> AssetThumbnailPool;
	TSharedRef<SWidget> MakeSaveLoadSection();
	bool IsSocketIOPluginAvailable() const;
	ECheckBoxState IsBrushDebugEnabled();
	void OnBrushDebugCheckChanged(ECheckBoxState NewState);
	bool bBrushDig = false;
	void RebuildCustomBrushGrid();
};
