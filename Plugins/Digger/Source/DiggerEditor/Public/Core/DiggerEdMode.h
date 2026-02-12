// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DiggerEdMode.h"
#include "EdMode.h"
#include "EditorModeManager.h"
#include "DiggerModeTypes.h"
#include "VoxelBrushTypes.h"
#include "FLightBrushTypes.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/Canvas.h"

#include "ScopedTransaction.h" 

class UPointLightComponent;
class ABrushPreviewActor;
class ADiggerManager;

// FORWARD DECLARATION (Fixes Circular Dependency)
class FDiggerEdModeToolkit;

// Delegate Definition
DECLARE_MULTICAST_DELEGATE_OneParam(FOnDiggerModeChanged, bool);

// Brush Hud Message System
struct FBrushHUDMessage
{
    FString Text;
    FLinearColor Color = FLinearColor::White;

    float TimeRemaining = 0.f;
    float Alpha = 1.f;

    float YOffset = 20.f;     // slide-in offset
    float SlideSpeed = 120.f; // px/sec

    bool IsAlive() const { return Alpha > 0.f; }
};


struct FBrushHUDState
{
    TArray<FBrushHUDMessage> Messages;
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

        float Force = 1.0f; // 0–1 normalized
        EDiggerPushMode PushMode = EDiggerPushMode::Ray; // default
        
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

    struct FContinuousClickSettings
    {
        bool bFinalBrushDig = false;
        FRotator FinalRotation = FRotator::ZeroRotator;
        bool bCtrlPressed = false;
        bool bRightClick = false;
        bool bIsValid = false;
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

        // NEW: Force + Push Mode
        float Force = 0.0f; // 0–1 scalar
        EDiggerPushMode PushMode = EDiggerPushMode::Ray;
    };
    
    // Preview Light
    TWeakObjectPtr<AActor> PreviewLightActor;
    
    UPointLightComponent* PreviewLightComponent = nullptr;
    
    
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
private:
    void OnLevelActorAdded(AActor* InActor);
    
    // Handles for delegates
    FDelegateHandle OnLevelActorAddedHandle;
public:
    
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
    void EnsurePreviewExists(FEditorViewportClient* ViewportClient);
    void DestroyPreview();
    void UpdatePreviewAtCursor(FEditorViewportClient* InViewportClient);
    bool TraceUnderCursor(FEditorViewportClient* InViewportClient, FHitResult& OutHit);
    void SpawnOrUpdatePreviewLight();
    void DestroyPreviewLight();
    void UpdatePreviewModeIndicator();
    FBrushUIParams GetCurrentBrushUI() const;

    // --- Brush UX Mode State ---
    EDiggerMainMode CurrentMode = EDiggerMainMode::Sculpt;
    EDiggerAxisMode CurrentAxis = EDiggerAxisMode::None;
    

    // --- Brush Hud Message system ---
    FBrushHUDState BrushHUD;
    void ShowBrushHUDMessage(const FString& Msg, const FLinearColor& Color = FLinearColor::White);
    void HandleModifierBlocked(bool bBlocked);


    // --- Brush Pressure Sensitivity ---
    bool bTabletPressureActive = true;

    // --- State Variables ---
    bool bPaintingEnabled = true; 
    bool bIsPainting = false;
    bool bIsDragging = false;
    bool bMouseButtonDown = false;
    bool bIsContinuouslyApplying = false;
    bool bRotationModeLatched = false;
    bool bOffsetModeLatched   = false;
    bool bIsSamplingNormal = false;
    bool bHasLastStrokeSample = false;


    // --- Scroll Velocity Variables ---
    float ScrollVelocity = 0.0f;
    float ScrollDecayRate = 12.0f;   // how fast velocity fades
    float ScrollImpulseScale = 150.0f; // how strong each wheel tick is

    
    float ContinuousApplicationTimer = 0.0f;
    FVector LastStrokeHitLocation = FVector::ZeroVector;
    FVector LastStrokePreviewCenter = FVector::ZeroVector;
    FVector2D LastPaintLocation = FVector2D::ZeroVector;
    float ContinuousApplicationInterval = 5.0f;

    FBrushCache BrushCache;
    FContinuousClickSettings ContinuousSettings;

    // Preview Actor Reference
    TWeakObjectPtr<ABrushPreviewActor> Preview;

    // 1. Prevents clicking actors in the viewport from selecting them
    virtual bool IsSelectionAllowed(AActor* InActor, bool bInSelection) const override;
    
    // 2. We need to restore the logic that forces deselection
    virtual bool Select(AActor* InActor, bool bInSelection) override;
    
    // --- UNDO / REDO ---
    TUniquePtr<FScopedTransaction> ActiveTransaction;
};