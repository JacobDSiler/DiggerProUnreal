// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdMode.h"
#include "EditorModeManager.h"
#include "DiggerEdModeToolkit.h"

class ADiggerManager;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnDiggerModeChanged, bool);

class DIGGEREDITOR_API FDiggerEdMode final : public FEdMode
{
public:
    FDiggerEdMode();
    virtual ~FDiggerEdMode() override;
    void DeselectAllSceneActors();

    // FEdMode interface overrides
    virtual void Enter() override;
    virtual void Exit() override;
    virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) override;
    virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;
    virtual bool HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click) override;
    virtual bool InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event) override;
    virtual bool InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale) override;
    virtual bool StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override;
    virtual bool EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override;
    virtual bool CapturedMouseMove(FEditorViewportClient* InViewportClient, FViewport* InViewport, int32 InMouseX, int32 InMouseY) override;
    virtual bool UsesToolkits() const override;
    virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

    // Custom methods
    bool GetMouseWorldHit(FEditorViewportClient* ViewportClient, FVector& OutHitLocation, FHitResult& OutHit);
    void SetPaintMode(bool bEnabled) { bPaintingEnabled = bEnabled; }
    bool IsPaintModeEnabled() const { return bPaintingEnabled; }
    bool HandleClickSimple(const FVector& RayOrigin, const FVector& RayDirection);
    void StartContinuousApplication(const FViewportClick& Click);
    void StopContinuousApplication();
    void ApplyContinuousBrush(FEditorViewportClient* InViewportClient);
    bool ShouldApplyContinuously() const;
    ADiggerManager* FindDiggerManager();

    // Toolkit helper
    TSharedPtr<FDiggerEdModeToolkit> GetDiggerToolkit()
    {
        return StaticCastSharedPtr<FDiggerEdModeToolkit>(Toolkit);
    }

    // Static helpers and members
    static bool IsDiggerModeActive();
    static FOnDiggerModeChanged OnDiggerModeChanged;
    static const FEditorModeID EM_DiggerEdModeId;

private:
    // Paint/continuous application state
    bool bIsPainting = false;
    bool bPaintingEnabled = false;
    FVector2D LastPaintLocation;
    bool bIsContinuouslyApplying = false;
    bool bMouseButtonDown = false;
    FVector2D LastMousePosition;
    float ContinuousApplicationTimer = 0.0f;
    float ContinuousApplicationInterval = 0.05f;

    struct FContinuousClickSettings
    {
        bool bFinalBrushDig = false;
        FRotator FinalRotation = FRotator::ZeroRotator;
        bool bCtrlPressed = false;
        bool bRightClick = false;
        bool bIsValid = false;
    } ContinuousSettings;

    TSharedPtr<FModeToolkit> Toolkit;

    // Static state
    static bool bIsDiggerModeCurrentlyActive;
};
