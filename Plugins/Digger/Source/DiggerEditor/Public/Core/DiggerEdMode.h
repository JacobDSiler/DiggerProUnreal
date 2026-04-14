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
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Input/SSpinBox.h"

#include "ScopedTransaction.h" 
#include "Framework/Application/IInputProcessor.h"

class FDiggerEdMode;
class UPointLightComponent;
class ABrushPreviewActor;
class ADiggerManager;
struct FDiggerLoadProgress;

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

struct FBrushHUDPanel
{
    FString ModeText;

    // Sculpt
    float Radius = 0.f;
    float Strength = 0.f;
    float Falloff = 0.f;
    float Force = 0.f;
    EDiggerPushMode  PushMode = EDiggerPushMode::Ray; // default

    // Offset Mode
    FVector Offset = FVector::ZeroVector;

    // Rotation Mode
    FRotator Rotation = FRotator::ZeroRotator;

    // Display
    float TimeRemaining = 0.f;
    float Alpha = 1.f;
    bool  bVisible = false;
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
    FVector GetVisualLocation(const FVector& RawHitLocation, const FBrushCache& Settings) const;
    virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

    // --- Custom Methods ---
    bool GetMouseWorldHit(FEditorViewportClient* ViewportClient, FVector& OutHitLocation, FHitResult& OutHit);
private:
    void OnLevelActorAdded(AActor* InActor);
    void OnLevelActorDeleted(AActor* InActor);

    // Handles for delegates
    FDelegateHandle OnLevelActorAddedHandle;
    FDelegateHandle OnLevelActorDeletedHandle;

    // Digger Input Processor
    TSharedPtr<class IInputProcessor> DiggerInputProcessor;
    
public:

    struct FScopedBrushBusy
    {
        FDiggerEdMode* Mode;

        FScopedBrushBusy(FDiggerEdMode* InMode)
            : Mode(InMode)
        {
            if (Mode) Mode->bIsBrushBusy = true;
        }

        ~FScopedBrushBusy()
        {
            if (Mode) Mode->bIsBrushBusy = false;
        }
    };

    
    // Legacy/Helper methods
    void SetPaintMode(bool bEnabled) { bPaintingEnabled = bEnabled; }
    bool IsPaintModeEnabled() const { return bPaintingEnabled; }

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

    // Load progress notification (Slate lives in editor module, not runtime).
    TWeakPtr<SNotificationItem> LoadProgressNotification;
    double LoadNotifStartTime = 0.0;
    void OnDiggerLoadProgress(const FDiggerLoadProgress& Progress);

public:

    // -----------------------------------------------------------------------
    // SetCurrentMode: sets mode, resets axis, updates HUD + indicator.
    // Prefer ToggleRotationMode/ToggleOffsetMode for interactive hotkey use.
    // -----------------------------------------------------------------------
    void SetCurrentMode(EDiggerMainMode NewMode)
    {
        CurrentMode = NewMode;
        CurrentAxis = EDiggerAxisMode::None;
        bRotationModeLatched = (NewMode == EDiggerMainMode::Rotate);
        bOffsetModeLatched   = (NewMode == EDiggerMainMode::Offset);
        UpdateBrushHUDPanel();
        UpdatePreviewModeIndicator();
        OnDiggerModeChanged.Broadcast(true);
    }

    // -----------------------------------------------------------------------
    // Atomic toggle helpers — safe to call from FDiggerInputProcessor
    // (external class) without touching private booleans directly.
    // Each owns the full latch + mode + axis + HUD + indicator update.
    // -----------------------------------------------------------------------

    // Toggle Rotation mode on/off. Pressing R again returns to Sculpt.
    void ToggleRotationMode()
    {
        bRotationModeLatched = !bRotationModeLatched;
        bOffsetModeLatched   = false;
        CurrentMode = bRotationModeLatched ? EDiggerMainMode::Rotate : EDiggerMainMode::Sculpt;
        CurrentAxis = EDiggerAxisMode::None;
        UpdateBrushHUDPanel();
        UpdatePreviewModeIndicator();
    }

    // Toggle Offset mode on/off. Pressing O again returns to Sculpt.
    void ToggleOffsetMode()
    {
        bOffsetModeLatched   = !bOffsetModeLatched;
        bRotationModeLatched = false;
        CurrentMode = bOffsetModeLatched ? EDiggerMainMode::Offset : EDiggerMainMode::Sculpt;
        CurrentAxis = EDiggerAxisMode::None;
        UpdateBrushHUDPanel();
        UpdatePreviewModeIndicator();
    }

    // -----------------------------------------------------------------------
    // Latch state read-only accessors
    // -----------------------------------------------------------------------
    [[nodiscard]] bool IsRotationModeLatched() const { return bRotationModeLatched; }
    [[nodiscard]] bool IsOffsetModeLatched()   const { return bOffsetModeLatched;   }

    void CycleAxisMode()
    {
        switch (CurrentAxis)
        {
        case EDiggerAxisMode::None: CurrentAxis = EDiggerAxisMode::X;    break;
        case EDiggerAxisMode::X:    CurrentAxis = EDiggerAxisMode::Y;    break;
        case EDiggerAxisMode::Y:    CurrentAxis = EDiggerAxisMode::Z;    break;
        case EDiggerAxisMode::Z:    CurrentAxis = EDiggerAxisMode::None; break;
        }
        UpdateBrushHUDPanel();
    }

    // Modifier-blocked relay (public so the input processor can reach it)
    void TriggerModifierState(bool bBlocked)
    {
        HandleModifierBlocked(bBlocked);
    }

    // -----------------------------------------------------------------------
    // Axis reset helpers — sync both BrushCache AND the toolkit spinboxes.
    // -----------------------------------------------------------------------
    // -----------------------------------------------------------------------
    // Axis / transform reset helpers (all implemented in .cpp)
    //
    //  ResetCurrentModeAxis()   — clear the selected axis on the CURRENT mode only
    //  ResetCurrentModeAxes()   — clear ALL axes on the CURRENT mode only
    //  ResetAllTransforms()     — wipe both rotation AND offset (Alt+C action)
    //
    //  ResetActiveAxis / ResetAllAxes kept for back-compat (delegate to above).
    // -----------------------------------------------------------------------
    void ResetCurrentModeAxis();   // C   — current mode, selected axis only
    void ResetCurrentModeAxes();   // CC  — current mode, all axes
    void ResetAllTransforms();     // Alt+C — wipe rotation + offset entirely
    void ResetActiveAxis();        // legacy alias → ResetCurrentModeAxis
    void ResetAllAxes();           // legacy alias → ResetCurrentModeAxes

    double LastCPressTime = 0.0;


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

public:
    [[nodiscard]] EDiggerMainMode GetCurrentMode() const
    {
        return CurrentMode;
    }

    [[nodiscard]] EDiggerAxisMode GetCurrentAxis() const
    {
        return CurrentAxis;
    }

    void SetCurrentAxis(EDiggerAxisMode InCurrentAxis)
    {
        this->CurrentAxis = InCurrentAxis;
    }


    EDiggerAxisMode CurrentAxis = EDiggerAxisMode::None;
    
private:
    // --- Brush Hud Message system ---
    FBrushHUDPanel BrushHUD;
public:
    void UpdateBrushHUDPanel();
    void HandleModifierBlocked(bool bBlocked);

    // -----------------------------------------------------------------------
    // Focus-independent scroll and axis-select entry points.
    // Called directly from FDiggerInputProcessor so they work regardless of
    // whether the viewport has OS focus — no viewport input routing required.
    // -----------------------------------------------------------------------
    bool ProcessScrollDelta(float Delta, float DeltaTime);
    void SelectAxis(EDiggerAxisMode Axis);
    
private:
    void SyncBrushSettingsToManager(ADiggerManager* Digger, const FBrushCache& Settings);


    // --- Brush Pressure Sensitivity ---
    bool bTabletPressureActive = true;

    // --- State Variables ---
    bool bPaintingEnabled = true;

public:
    [[nodiscard]] bool IsPaintingEnabled() const
    {
        return bPaintingEnabled;
    }

    void SetPaintingEnabled(bool InbPaintingEnabled)
    {
        this->bPaintingEnabled = InbPaintingEnabled;
    }

private:
    bool bIsPainting = false;
    bool bIsDragging = false;
    bool bMouseButtonDown = false;
    bool bIsContinuouslyApplying = false;
    bool bRotationModeLatched = false;
    bool bOffsetModeLatched   = false;
    bool bIsSamplingNormal = false;
    bool bHasLastStrokeSample = false;
    // Set by StopContinuousApplication() when it commits the action itself,
    // so EndTracking knows not to call CommitPendingAction() a second time.
    bool bStrokeCommittedByContinuous = false;

    // Viewport refresh — set true whenever visuals change, consumed by Tick
    bool bNeedsViewportRefresh = false;
    void MarkViewportDirty() { bNeedsViewportRefresh = true; }

    // Loading Indication
    float LoadingSpriteRotation = 0.f;
    bool bIsBrushBusy = false;
    

    // Worklight position cache — only update when camera moves significantly
    FVector LastWorklightCameraPos = FVector(FLT_MAX);

    // --- Scroll Velocity Variables ---
    float ScrollVelocity = 0.0f;
    float ScrollDecayRate = 12.0f;   // how fast velocity fades
    float ScrollImpulseScale = 150.0f; // how strong each wheel tick is

    
    float ContinuousApplicationTimer = 0.0f;
    FVector LastStrokeHitLocation = FVector::ZeroVector;
    FVector LastStrokePreviewCenter = FVector::ZeroVector;
    FVector2D LastPaintLocation = FVector2D::ZeroVector;
    // XY position of the last successfully spawned hole BP.
    // Initialised to FLT_MAX sentinel so the first application of any stroke
    // always passes the distance gate.  Reset at the start of each stroke.
    FVector LastHoleSpawnPos = FVector(FLT_MAX, FLT_MAX, 0.f);
    float ContinuousApplicationInterval = 5.0f;
public:
    FBrushCache BrushCache;
private:
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