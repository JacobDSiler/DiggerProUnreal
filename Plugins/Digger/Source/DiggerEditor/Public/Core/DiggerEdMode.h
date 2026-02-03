// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DiggerEdMode.h"
#include "EdMode.h"
#include "EditorModeManager.h"
#include "VoxelBrushTypes.h"
#include "FLightBrushTypes.h" 

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/Canvas.h"

#include "ScopedTransaction.h" 

class ABrushPreviewActor;
class ADiggerManager;

// FORWARD DECLARATION (Fixes Circular Dependency)
class FDiggerEdModeToolkit;

// Delegate Definition
DECLARE_MULTICAST_DELEGATE_OneParam(FOnDiggerModeChanged, bool);

// Brush Hud Message System
struct FBrushHUDState
{
    FString Text;
    float TimeRemaining = 0.f;

    bool bPaintingPaused = false;   // ⭐ NEW
};

class DIGGEREDITOR_API FDiggerEdMode final : public FEdMode
{
public:
    const static FEditorModeID EM_DiggerEdModeId;

    FDiggerEdMode();
    virtual ~FDiggerEdMode() override;

    // --- Structures ---
    struct FBrushCache
    {
        //Brush Preview Members
        FVector CachedBrushPreviewCenter = FVector::ZeroVector;
        FVector CachedPreviewOffset = FVector::ZeroVector;
        float Radius;
        float Falloff;
        float Strength;
        bool bFinalBrushDig;
        FRotator Rotation;
        FVector Offset;
        
        // Advanced
        bool bIsFilled;
        float Angle;
        bool bHiddenSeam;
        
        // Cube
        bool bUseAdvancedCube;
        float CubeHalfExtentX;
        float CubeHalfExtentY;
        float CubeHalfExtentZ;
        
        EVoxelBrushType BrushType;
        ELightBrushType LightType; 
    };

    struct FContinuousSettings
    {
        bool bIsValid = false;
        bool bRightClick = false;
        bool bCtrlPressed = false;
    };

    struct FBrushUIParams
    {
        FVector RadiusXYZ;
        float Falloff;
        bool bAdd;
        FRotator Rotation;
        FVector Offset;
        float CellSize;
        uint8 ShapeType;
    };
    


    // --- FEdMode Interface ---
    virtual void Enter() override;
    virtual void Exit() override;
    virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) override;
    virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;
    
    // Input Handling
    virtual bool InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event) override;
    virtual bool MouseEnter(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y) override;
    virtual bool CapturedMouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 InMouseX, int32 InMouseY) override;
    virtual bool StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override;
    virtual bool EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override;
    virtual bool InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale) override;
    virtual bool InputAxis(FEditorViewportClient* InViewportClient, FViewport* InViewport, int32 ControllerId, FKey Key, float Delta, float DeltaTime) override;
    
    // Explicit override for the simple click handler
    virtual bool HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click) override;

    virtual bool UsesToolkits() const override;
    virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

    // --- Custom Methods ---
    bool GetMouseWorldHit(FEditorViewportClient* ViewportClient, FVector& OutHitLocation, FHitResult& OutHit);
    
    // Legacy/Helper methods
    void SetPaintMode(bool bEnabled) { bPaintingEnabled = bEnabled; }
    bool IsPaintModeEnabled() const { return bPaintingEnabled; }
    bool HandleClickSimple(const FVector& RayOrigin, const FVector& RayDirection);

    // Continuous Application
    void StartContinuousApplication(const FViewportClick& Click);
    void StopContinuousApplication();
    void ApplyContinuousBrush(FEditorViewportClient* InViewportClient);
    bool ShouldApplyContinuously() const;
    
    // Helpers
    ADiggerManager* FindDiggerManager();
    void DeselectAllSceneActors();

    // Toolkit helpers
    TSharedPtr<FDiggerEdModeToolkit> GetDiggerToolkit();
    TSharedPtr<FDiggerEdModeToolkit> GetDiggerToolkit() const;
    
    // Events
    static FOnDiggerModeChanged OnDiggerModeChanged;
    static bool bIsDiggerModeCurrentlyActive;

private:
    // Helper to update BrushCache
    void UpdateBrushSettingsFromUI(const FHitResult& TraceHit, bool bRightClick);
    void ApplyBrushWithSettings(ADiggerManager* Digger, const FVector& HitLocation, const FHitResult& Hit, const FBrushCache& Settings);

    // Preview
    void EnsurePreviewExists();
    void DestroyPreview();
    void UpdatePreviewAtCursor(FEditorViewportClient* InViewportClient);
    bool TraceUnderCursor(FEditorViewportClient* InViewportClient, FHitResult& OutHit);
    FBrushUIParams GetCurrentBrushUI() const;

    // --- Brush Hud Message system ---
    FBrushHUDState BrushHUD;
    void ShowBrushHUDMessage(const FString& Msg);
    void HandleModifierBlocked(bool bBlocked);


    // --- Brush Pressure Sensitivity ---
    bool bTabletPressureActive = true;

    // --- State Variables ---
    bool bPaintingEnabled = true; 
    bool bIsPainting = false;
    bool bIsDragging = false;
    bool bMouseButtonDown = false;
    bool bIsContinuouslyApplying = false;

    // --- Scroll Velocity Variables ---
    float ScrollVelocity = 0.0f;
    float ScrollDecayRate = 12.0f;   // how fast velocity fades
    float ScrollImpulseScale = 150.0f; // how strong each wheel tick is

    
    float ContinuousApplicationTimer = 0.0f;
    FVector LastStrokeHitLocation = FVector::ZeroVector;
    FVector LastStrokePreviewCenter = FVector::ZeroVector;
    FVector2D LastPaintLocation = FVector2D::ZeroVector;
    float StrokeSpacing = 5.0f;

    FBrushCache BrushCache;
    FContinuousSettings ContinuousSettings;

    // Preview Actor Reference
    TWeakObjectPtr<ABrushPreviewActor> Preview;

    // 1. Prevents clicking actors in the viewport from selecting them
    virtual bool IsSelectionAllowed(AActor* InActor, bool bInSelection) const override;
    
    // 2. We need to restore the logic that forces deselection
    virtual bool Select(AActor* InActor, bool bInSelection) override;
    
    // --- UNDO / REDO ---
    TUniquePtr<FScopedTransaction> ActiveTransaction;
};