#pragma once

#include "CoreMinimal.h"
#include "DiggerEdModeToolkit.h"
#include "EditorModeManager.h"
#include "DiggerEdModeToolkit.h"
#include "EdMode.h"

class FDiggerEdModeToolkit;
class ADiggerManager;


// SOLUTION 1: Override CapturedMouseMove (RECOMMENDED)
// This is the most professional and clean approach used by Epic's tools

class FDiggerEdMode : public FEdMode
{
private:
	bool bIsPainting = false;
	bool bPaintingEnabled = false; // Toggle for paint mode
	FVector2D LastPaintLocation;
    
public:
	// Override these key functions
	virtual bool CapturedMouseMove(FEditorViewportClient* InViewportClient, FViewport* InViewport, int32 InMouseX, int32 InMouseY) override;
	virtual bool StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override;
	virtual bool EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override;
	virtual bool InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event) override;
	virtual bool DisallowMouseDeltaTracking() const override { return bIsPainting; }
    
	// Add paint mode toggle
	void SetPaintMode(bool bEnabled) { bPaintingEnabled = bEnabled; }
	bool IsPaintModeEnabled() const { return bPaintingEnabled; }

	// Add Handle Click Simple
	bool HandleClickSimple(const FVector& RayOrigin, const FVector& RayDirection);

private:
	// Add these member variables for continuous clicking support
	bool bIsContinuouslyApplying = false;
	bool bMouseButtonDown = false;
	FVector2D LastMousePosition;
	float ContinuousApplicationTimer = 0.0f;
	float ContinuousApplicationInterval = 0.05f; // Apply every 50ms during drag
    
	// Store the click settings for continuous application
	struct FContinuousClickSettings
	{
		bool bFinalBrushDig = false;
		FRotator FinalRotation = FRotator::ZeroRotator;
		bool bCtrlPressed = false;
		bool bRightClick = false;
		bool bIsValid = false;
	} ContinuousSettings;

public:
	// Override these functions in your header
	virtual bool InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale) override;

private:
	// Helper functions
	void StartContinuousApplication(const FViewportClick& Click);
	void StopContinuousApplication();
	void ApplyContinuousBrush(FEditorViewportClient* InViewportClient);
	bool ShouldApplyContinuously() const;

public:
	const static FEditorModeID EM_DiggerEdModeId;

	FDiggerEdMode();  // ✅ Add this
	virtual ~FDiggerEdMode(); // ✅ And this

	virtual void Enter() override;
	bool GetMouseWorldHit(FEditorViewportClient* ViewportClient, FVector& OutHitLocation, FHitResult& OutHit);
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;
	bool HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click);
	virtual void Exit() override;
	virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual bool UsesToolkits() const override;
	// Your custom stuff
	ADiggerManager* FindDiggerManager(); // Add this for Step 2

	
	// Helper to get your custom toolkit
	TSharedPtr<FDiggerEdModeToolkit> GetDiggerToolkit()
	{
		return StaticCastSharedPtr<FDiggerEdModeToolkit>(GetToolkit());
	}

};
