// DiggerEdModeToolkit.h
#pragma once

#include "SparseVoxelGrid.h" // <-- Make sure this is included!
#include "EditorModeManager.h"
#include "Toolkits/BaseToolkit.h"        
#include "VoxelBrushTypes.h"
#include "Toolkits/BaseToolkit.h"

struct FIsland;
class ADiggerManager;
class SUniformGridPanel;

class FDiggerEdModeToolkit : public FModeToolkit
{
public:
	virtual void Init(const TSharedPtr<IToolkitHost>& InitToolkitHost) override;
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual class FEdMode* GetEditorMode() const override;
	virtual TSharedPtr<SWidget> GetInlineContent() const override;
	bool IsDigMode() const
	{
		return bBrushDig;
	}

	float GetBrushAngle(){return ConeAngle;}

	
    void SetIslands(const TArray<FIsland>& InIslands)
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



private:

	TSharedPtr<SVerticalBox> ToolkitWidget;
	EVoxelBrushType CurrentBrushType = EVoxelBrushType::Sphere; // First option;
	int ConeAngle=45;
	int MinConeAngle=5;
	int MaxConeAngle=60;
	int MinBrushLength=10;
	double MaxBrushLength=256.0;
	signed int SmoothIterations=1;


	//Helpers
	ADiggerManager* GetDiggerManager();

	TSharedRef<SWidget> MakeIslandGridWidget();

	TSharedRef<SWidget> MakeAngleButton(float Angle, float& Target, const FString& Label);
    TSharedRef<SWidget> MakeAngleButton(double Angle, double& Target, const FString& Label);
    TSharedRef<SWidget> MakeMirrorButton(float& Target, const FString& Label);
	TSharedRef<SWidget> MakeMirrorButton(double& Target, const FString& Label);

	
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

	// In FDiggerEdModeToolkit.h (public section)
	void AddIsland(const FIsland& Island)
	{
	    Islands.Add(Island);
	    RebuildIslandGrid();
	}

	void RemoveIsland(int32 IslandIndex)
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
	}

	const TArray<FIsland>& GetIslands() const
	{
	    return Islands;
	}

	


private:
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
	int32 SelectedBrushIndex = 0;
	float BrushLength = 200.0f; // Default value, adjust as needed

	TArray<FIsland> Islands; // Your detected islands
	int32 SelectedIslandIndex = INDEX_NONE; // Currently selected island
	FRotator IslandRotation;
	

	TArray<TSoftObjectPtr<UStaticMesh>> CustomBrushMeshes;
	TSharedPtr<SUniformGridPanel> CustomBrushGrid;
	TSharedPtr<SBox> IslandGridContainer; // Add this!
	TSharedPtr<SUniformGridPanel> IslandGrid;
	TSharedPtr<FAssetThumbnailPool> AssetThumbnailPool;
	bool bBrushDig = false;
	void RebuildCustomBrushGrid();

public:
	void RebuildIslandGrid();
};
