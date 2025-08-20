// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DiggerEdModeToolkit.h"
#include "EditorModeManager.h"
#include "EdMode.h"
#include "VoxelBrushTypes.h"

class ABrushPreviewActor;
class FDiggerEdModeToolkit;
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

    // Toolkit helpers
    TSharedPtr<FDiggerEdModeToolkit> GetDiggerToolkit();

    TSharedPtr<FDiggerEdModeToolkit> GetDiggerToolkit() const;
    
    
    // Static helpers and members
    static bool IsDiggerModeActive();
    static FOnDiggerModeChanged OnDiggerModeChanged;
    static const FEditorModeID EM_DiggerEdModeId;

private:
    // Persistent Brush Actor Refernce
    TWeakObjectPtr<ABrushPreviewActor> Preview;

    // Helpers
    void EnsurePreviewExists();
    void DestroyPreview();

    // Recomputes preview transform + params from current cursor hit
    void UpdatePreviewAtCursor(FEditorViewportClient* InViewportClient);

    // Utility to trace under the mouse into the world
    bool TraceUnderCursor(FEditorViewportClient* InViewportClient, FHitResult& OutHit);

    // Pull current brush UI params (adapt to your actual accessors)
    struct FBrushUIParams
    {
        FVector RadiusXYZ = FVector(100.f);
        float Falloff = 0.25f;
        bool bAdd = true;
        float CellSize = 50.f;
        uint8 ShapeType = 0; // your mapping to EBrushPreviewShape
        FRotator Rotation = FRotator::ZeroRotator;
        FVector  Offset   = FVector::ZeroVector;
    };
    FBrushUIParams GetCurrentBrushUI() const;

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
        EVoxelBrushType BrushType;// = EVoxelBrushType::Sphere;
        bool bHiddenSeam = false;
        bool bUseAdvancedCube = false;
        float CubeHalfExtentX = 0.f;
        float CubeHalfExtentY = 0.f;
        float CubeHalfExtentZ = 0.f;
        FVector Offset = FVector::ZeroVector;
    } BrushCache;

    FVector LastStrokeHitLocation = FVector::ZeroVector;
    float StrokeSpacing = 1.f; // min move before next stamp
    bool bIsDragging;

    // Internal helper
    void ApplyBrushWithSettings(ADiggerManager* Digger, const FVector& HitLocation, const FHitResult& Hit, const FBrushCache& Settings);

    TSharedPtr<FModeToolkit> Toolkit;

    // Static state
    static bool bIsDiggerModeCurrentlyActive;
};
