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
    void UpdateGhostPreview(const FVector& CursorWorldLocation);
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

    // Cached brush data for continuous strokes
    struct FBrushCache
    {
        float Radius = 0.f;
        bool bFinalBrushDig = false;
        FRotator Rotation = FRotator::ZeroRotator;
        bool bIsFilled = false;
        float Angle = 0.f;
        EVoxelBrushType BrushType = EVoxelBrushType::Sphere;
        bool bHiddenSeam = false;
        bool bUseAdvancedCube = false;
        float CubeHalfExtentX = 0.f;
        float CubeHalfExtentY = 0.f;
        float CubeHalfExtentZ = 0.f;
        FVector Offset = FVector::ZeroVector;
    } BrushCache;

    FVector LastStrokeHitLocation = FVector::ZeroVector;
    float StrokeSpacing = 0.f; // min move before next stamp
    bool bIsDragging;

    // Internal helper
    void ApplyBrushWithSettings(ADiggerManager* Digger, const FVector& HitLocation, const FHitResult& Hit, const FBrushCache& Settings);

    TSharedPtr<FModeToolkit> Toolkit;

    // Static state
    static bool bIsDiggerModeCurrentlyActive;
};

