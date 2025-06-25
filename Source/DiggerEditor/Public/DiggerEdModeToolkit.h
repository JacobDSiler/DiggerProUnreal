// DiggerEdModeToolkit.h
#pragma once

#include "SparseVoxelGrid.h" // <-- Make sure this is included!
#include "EditorModeManager.h"
#include "Toolkits/BaseToolkit.h"        
#include "VoxelBrushTypes.h"
#include "Toolkits/BaseToolkit.h"
#include "FCustomBrushEntry.h"

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


class FDiggerEdModeToolkit : public FModeToolkit
{
public:
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


	~FDiggerEdModeToolkit();
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
	float AdvancedCubeHalfExtentY=2.f;
	float AdvancedCubeHalfExtentX=.5f;
	float AdvancedCubeHalfExtentZ=1.f;
	bool bUseAdvancedCubeBrush = true;

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
	bool bDetailedDebug = false;

	// Function to handle state change
	/*void OnDetailedDebugCheckChanged(ECheckBoxState NewState)
	{
		bDetailedDebug = (NewState == ECheckBoxState::Checked);

		if (ADiggerManager* Manager = GetDiggerManager())
		{
			if (UVoxelChunk* Chunk = Manager->GetOrCreateChunkAtChunk(ChunkPosition);)
			{
				if (bDetailedDebug)
				{
					Chunk->DebugPrintVoxelData();  // Show detailed debug
				}
				else
				{
					Chunk->DebugDrawChunk();  // Show basic chunk debug
				}
			}
		}
	}*/

	// Function to check the current state of the checkbox
	ECheckBoxState IsDetailedDebugChecked() const
	{
		return bDetailedDebug ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}

	//Helpers
	ADiggerManager* GetDiggerManager();

	TSharedRef<SWidget> MakeIslandGridWidget();

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
	void OnConvertToPhysicsActorClicked();
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


	/*void RemoveIsland(int32 IslandIndex)
	{
	    if (Islands.IsValidIndex(IslandIndex))
	    {
	        Islands.RemoveAt(IslandIndex);
	        if (SelectedIslandIndex == IslandIndex)
	            SelectedIslandIndex = INDEX_NONE;
	        else if (SelectedIslandIndex > IslandIndex)
	            --SelectedIslandIndex;
	        RebuildIslandGrid();
	    }
	}*/

	const TArray<FIslandData>& GetIslands() const
	{
	    return Islands;
	}

	void SetTemporaryDigOverride(TOptional<bool> Override);
	


private:

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
	TOptional<bool> TemporaryDigOverride;


	
	TArray<TSoftObjectPtr<UStaticMesh>> CustomBrushMeshes;
	TSharedPtr<SBox> IslandGridContainer; // Add this!
	TSharedPtr<SUniformGridPanel> IslandGrid;
	TSharedPtr<FAssetThumbnailPool> AssetThumbnailPool;
	TSharedRef<SWidget> MakeSaveLoadSection();
	ECheckBoxState IsBrushDebugEnabled();
	void OnBrushDebugCheckChanged(ECheckBoxState NewState);
	bool bBrushDig = false;
	void RebuildCustomBrushGrid();
};
