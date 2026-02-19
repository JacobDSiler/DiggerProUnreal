// ============================================================================
// 0. DiggerEdMode and then Core Minimal — MUST be first
// ============================================================================
#include "DiggerEdMode.h"
#include "CoreMinimal.h"

// ============================================================================
// 1. Engine.h
// ============================================================================
#include "Engine/Engine.h"       // GEngine->GetSmallFont()

// ============================================================================
// 2. Standard Library (Safe after CoreMinimal)
// ============================================================================
#include <mutex>
#include <queue>

// ============================================================================
// 3. Engine Runtime Framework
// ============================================================================
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"              // GEngine, fonts
#include "EngineUtils.h"
#include "Landscape.h"
#include "SceneManagement.h"
#include "CollisionQueryParams.h"
#include "DrawDebugHelpers.h"
#include "Kismet/GameplayStatics.h"

// ============================================================================
// 4. Editor Framework (Editor‑only, but this file *is* editor‑only)
// ============================================================================
#include "Editor.h"
#include "EditorModeManager.h"
#include "EditorViewportClient.h"
#include "Selection.h"
#include "Toolkits/ToolkitManager.h"

// ============================================================================
// 5. Digger Editor Internal
// ============================================================================
#include "DiggerEdMode.h"
#include "DiggerEdModeToolkit.h"
#include "DiggerEditorAccess.h"
#include "DiggerEditorSettings.h"
#include "DiggerDebug.h"

// ============================================================================
// 6. Digger Runtime Internal (used by editor mode)
// ============================================================================
#include "DiggerManager.h"
#include "BrushPreviewActor.h"
#include "VoxelBrushShape.h"
#include "VoxelBrushTypes.h"
#include "FBrushStroke.h"

// ============================================================================
// 7. HUD / Canvas Rendering
// ============================================================================
#include "CanvasItem.h"          // FCanvasTextItem
#include "Engine/Canvas.h"       // FCanvas, SizeX, SizeY

// ============================================================================
// 8. Light / Material Assets
// ============================================================================
#include "DynamicHole.h"
#include "Components/BillboardComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"





#define LOCTEXT_NAMESPACE "DiggerEditorMode"

// -----------------------------------------------------------------------------------
// GLOBAL INPUT PROCESSOR
// This allows hotkeys (R, O, Shift, etc) to work even if the Details Panel has focus,
// provided the user is not currently typing text.
// -----------------------------------------------------------------------------------
class FDiggerInputProcessor : public IInputProcessor
{
public:
    FDiggerInputProcessor(FDiggerEdMode* InMode) : DiggerMode(InMode) {}

    virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

    virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
    {
        if (!DiggerMode) return false;

        const FKey Key = InKeyEvent.GetKey();

        const bool bIsModeKey =
            (Key == EKeys::R || Key == EKeys::O || Key == EKeys::P || Key == EKeys::C ||
             Key == EKeys::X || Key == EKeys::Y || Key == EKeys::Z);

        const bool bIsModifier =
            (Key == EKeys::LeftShift || Key == EKeys::RightShift ||
             Key == EKeys::LeftControl || Key == EKeys::RightControl ||
             Key == EKeys::LeftAlt || Key == EKeys::RightAlt);

        if (!bIsModeKey && !bIsModifier)
            return false;

        // Ignore text-entry widgets
        if (TSharedPtr<SWidget> Focused = SlateApp.GetKeyboardFocusedWidget())
        {
            FString Type = Focused->GetTypeAsString();
            if (Type == "SEditableText" ||
                Type == "SMultiLineEditableText" ||
                Type == "SSpinBox")
            {
                return false;
            }
        }

        // ⭐ Immediately trigger modifier logic
        if (bIsModifier)
        {
            DiggerMode->HandleModifierBlocked(true);
        }

        // ⭐ Always force viewport focus first
        if (GEditor)
        {
            if (FViewport* ActiveViewport = GEditor->GetActiveViewport())
            {
                ActiveViewport->SetUserFocus(true);
                ActiveViewport->CaptureMouse(true);
            }
        }

        // ⭐ For mode keys: directly invoke the mode logic here.
        // We cannot rely on bubble-up because Slate buttons/checkboxes hold focus
        // after a panel click and swallow the key before it reaches the viewport's
        // InputKey — even after SetUserFocus. Calling directly is the only safe path.
        if (bIsModeKey)
        {
            if (Key == EKeys::R)
            {
                // ToggleRotationMode owns all latch + mode + axis + HUD + indicator logic.
                DiggerMode->ToggleRotationMode();
            }
            else if (Key == EKeys::O)
            {
                // ToggleOffsetMode owns all latch + mode + axis + HUD + indicator logic.
                DiggerMode->ToggleOffsetMode();
            }
            else if (Key == EKeys::P)
            {
                DiggerMode->SetPaintingEnabled(!DiggerMode->IsPaintingEnabled());
                DiggerMode->UpdateBrushHUDPanel();
            }
            else if (Key == EKeys::C)
            {
                if (DiggerMode->GetCurrentMode() == EDiggerMainMode::Rotate ||
                    DiggerMode->GetCurrentMode() == EDiggerMainMode::Offset)
                {
                    // Check Alt via Slate (authoritative when viewport may not have focus)
                    const bool bAlt = SlateApp.GetModifierKeys().IsAltDown();
                    if (bAlt)
                    {
                        DiggerMode->ResetAllTransforms();
                        DiggerMode->LastCPressTime = 0.0;
                    }
                    else
                    {
                        const double Now    = FPlatformTime::Seconds();
                        const bool bDouble  = (Now - DiggerMode->LastCPressTime) < 0.35;
                        DiggerMode->LastCPressTime = Now;

                        if (bDouble || DiggerMode->GetCurrentAxis() == EDiggerAxisMode::None)
                            DiggerMode->ResetCurrentModeAxes();
                        else
                            DiggerMode->ResetCurrentModeAxis();
                    }
                    DiggerMode->UpdateBrushHUDPanel();
                }
            }
            else if (Key == EKeys::X)
            {
                DiggerMode->SelectAxis(EDiggerAxisMode::X);
            }
            else if (Key == EKeys::Y)
            {
                DiggerMode->SelectAxis(EDiggerAxisMode::Y);
            }
            else if (Key == EKeys::Z)
            {
                DiggerMode->SelectAxis(EDiggerAxisMode::Z);
            }
            // Consume: prevents double-processing by InputKey.
            return true;
        }

        // For modifier keys: return false so the viewport still gets
        // shift/ctrl/alt for camera navigation.
        return false;
    }

    // -----------------------------------------------------------------------
    // Scroll wheel interception.
    // CRITICAL: Must be declared virtual+override and use the correct 3-arg
    // UE5 signature — the missing third param (GestureEvent) and missing
    // virtual/override keywords meant the engine never called this before.
    // -----------------------------------------------------------------------
    virtual bool HandleMouseWheelOrGestureEvent(
        FSlateApplication& SlateApp,
        const FPointerEvent& WheelEvent,
        const FPointerEvent* GestureEvent) override
    {
        if (!DiggerMode)
            return false;

        // 1. We no longer route through Client->InputAxis, so we only need
        //    to verify the viewport exists. Focus is not required for
        //    ProcessScrollDelta — it reads modifier state from Slate directly.
        if (!GEditor || !GEditor->GetActiveViewport())
            return false;

        // 2. Read modifier state from Slate (OS-level, global — unlike Viewport->KeyState
        //    which only knows keys pressed while the viewport held focus). This is the
        //    only reliable source when the user holds Shift/Ctrl/Alt and then moves the
        //    cursor into the viewport without clicking first.
        const FModifierKeysState Mods = SlateApp.GetModifierKeys();
        const bool bShift = Mods.IsShiftDown();
        const bool bCtrl  = Mods.IsControlDown();
        const bool bAlt   = Mods.IsAltDown();
        const bool bAnyModifier = bShift || bCtrl || bAlt;

        // 3. Only intercept when the cursor is actually over the viewport.
        //    Uses viewport-relative cursor position — no FindWidgetUnderCursor needed.
        {
            bool bOverViewport = false;
            if (FViewport* VP = GEditor->GetActiveViewport())
            {
                FIntPoint CursorVP;
                VP->GetMousePos(CursorVP);
                const FIntPoint VPSize = VP->GetSizeXY();
                bOverViewport = (CursorVP.X >= 0 && CursorVP.Y >= 0 &&
                                 CursorVP.X < VPSize.X && CursorVP.Y < VPSize.Y);
            }
            if (!bOverViewport)
                return false; // cursor over panel/toolbar — let Slate handle normally
        }

        // 4. Sculpt + no modifier = camera zoom, pass through.
        //    At this point we know the cursor is over the viewport.
        const bool bIsSculptMode = (DiggerMode->GetCurrentMode() == EDiggerMainMode::Sculpt);
        if (bIsSculptMode && !bAnyModifier)
            return false;

        // 5. Call ProcessScrollDelta directly — bypasses all viewport input routing.
        //    Works regardless of OS focus, no warmup click needed.
        DiggerMode->ProcessScrollDelta(WheelEvent.GetWheelDelta(), SlateApp.GetDeltaTime());

        return true; // consumed — prevent viewport camera zoom
    }

    virtual bool HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override { return false; }
    virtual bool HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent) override { return false; }
    virtual bool HandleMouseMoveEvent(
        FSlateApplication& SlateApp,
        const FPointerEvent& MouseEvent) override
    {
        if (!DiggerMode)
            return false;

        // If a text box has focus, do NOT steal it
        if (TSharedPtr<SWidget> Focused = SlateApp.GetKeyboardFocusedWidget())
        {
            FString Type = Focused->GetTypeAsString();
            if (Type == "SEditableText" ||
                Type == "SMultiLineEditableText" ||
                Type == "SSpinBox")
            {
                return false;
            }
        }

        // When the cursor enters the viewport, restore Slate focus to it.
        // This handles "came back from another OS application" without requiring
        // a click. We check the viewport-relative cursor position (from FViewport)
        // which is reliable without needing FindWidgetUnderCursor.
        {
            bool bOverViewport = false;
            if (FViewport* VP = GEditor ? GEditor->GetActiveViewport() : nullptr)
            {
                FIntPoint CursorVP;
                VP->GetMousePos(CursorVP);
                const FIntPoint VPSize = VP->GetSizeXY();
                bOverViewport = (CursorVP.X >= 0 && CursorVP.Y >= 0 &&
                                 CursorVP.X < VPSize.X && CursorVP.Y < VPSize.Y);
            }

            if (bOverViewport)
            {
                // SetUserFocus on the raw viewport handles internal Unreal routing.
                if (FViewport* VP = GEditor->GetActiveViewport())
                    VP->SetUserFocus(true);

                // ClearAllUserFocus + re-focus via keyboard routes Slate's full
                // focus pipeline, which flushes any stale focus from the prior
                // active window and ensures scroll events route to the viewport.
                if (TSharedPtr<SWidget> FocusedWidget = SlateApp.GetKeyboardFocusedWidget())
                {
                    // Only re-route if something OTHER than the viewport held focus
                    const FString FType = FocusedWidget->GetTypeAsString();
                    const bool bFocusOnPanel =
                        FType != TEXT("SViewport") &&
                        FType != TEXT("SEditorViewport") &&
                        FType != TEXT("SLevelViewport");

                    if (bFocusOnPanel)
                    {
                        // Release focus from the panel — the viewport will reclaim
                        // it naturally on the next Slate tick via mouse hover.
                        SlateApp.ClearKeyboardFocus(EFocusCause::Mouse);
                    }
                }
                else
                {
                    // Nothing held Slate focus (came from another OS window).
                    // Clearing focus here nudges Slate to re-evaluate hover focus
                    // on the next event, which will land on the viewport.
                    SlateApp.ClearKeyboardFocus(EFocusCause::Mouse);
                }
            }
        }

        return false; // always pass through — never consume mouse move events
    }


private:
    FDiggerEdMode* DiggerMode;
};


const FEditorModeID FDiggerEdMode::EM_DiggerEdModeId = TEXT("EM_DiggerEdMode");
FOnDiggerModeChanged FDiggerEdMode::OnDiggerModeChanged;
bool FDiggerEdMode::bIsDiggerModeCurrentlyActive = false;

// --- Tunables ---
static constexpr float RADIUS_MIN = 10.f;
static constexpr float RADIUS_MAX = 4096.f;
static constexpr float STRENGTH_MIN = 0.f;
static constexpr float STRENGTH_MAX = 1.f;
static constexpr float FALLOFF_MIN = 0.f;
static constexpr float FALLOFF_MAX = 1.f;
static constexpr float FORCE_MIN = 0.f;
static constexpr float FORCE_MAX = 1.f;

static constexpr float BASE_RADIUS_STEP   = 8.f;
static constexpr float BASE_STRENGTH_STEP = 0.02f;
static constexpr float BASE_FALLOFF_STEP  = 0.02f;
static constexpr float BASE_FORCE_STEP  = 0.02f;


static constexpr float ACCEL_GAIN   = 0.35f;
static constexpr float SPEED_CLAMP  = 30.f;

// Optional snapping
static constexpr float RADIUS_SNAP_STEP   = 0.f;   // set to voxel size if desired
static constexpr float FALLOFF_SNAP_STEP  = 0.f;   // e.g. 0.05f
static constexpr float STRENGTH_SNAP_STEP = 0.f;   // e.g. 0.05f
static constexpr float FORCE_SNAP_STEP = 0.f;   // e.g. 0.05f

// Local epsilon to avoid macro collisions
static constexpr float LOCAL_KINDA_SMALL_NUMBER = 1.e-4f;

// Helpers
static FORCEINLINE float SnapIf(float Value, float Step)
{
    if (Step <= LOCAL_KINDA_SMALL_NUMBER) return Value;
    const float q = FMath::RoundToFloat(Value / Step);
    return q * Step;
}

static FORCEINLINE float WheelStep(float BaseStep, float Delta, float DeltaTime, bool bFine, bool bCoarse)
{
    const float speed = FMath::Clamp(
        FMath::Abs(Delta) / FMath::Max(DeltaTime, LOCAL_KINDA_SMALL_NUMBER),
        0.f,
        SPEED_CLAMP);

    float accel = 1.f + speed * ACCEL_GAIN;

    if (bFine)   accel *= 0.25f;
    if (bCoarse) accel *= 2.5f;

    const float sgn = (Delta >= 0.f) ? 1.f : -1.f;
    return sgn * BaseStep * accel;
}

static bool IsInsideExistingHole(ADiggerManager* Digger, const FVector& Pos, float Radius)
{
    UWorld* World = Digger->GetWorld();
    if (!World) return false;

    for (TActorIterator<ADynamicHole> It(World); It; ++It)
    {
        ADynamicHole* Hole = *It;
        if (!Hole) continue;

        const float HR = Hole->GetEffectiveRadius();
        const float Dist = FVector::Dist2D(Pos, Hole->GetActorLocation());

        if (Dist < HR * 0.9f) // 90% inside
            return true;
    }

    return false;
}



static bool IsHoleRedundant(UWorld* World, const FVector& NewHolePos, float NewHoleRadius);

// ---------------------------------------------------------------------------
// ShouldSpawnHole
//
// Called from HandleHoleSpawn in ADiggerManager as a runtime-path guard.
// Uses the same canonical vertical probe logic as ApplyBrushWithSettings
// so both code paths are consistent.
//
// NOTE: BrushPos here should already be surface-anchored (XY of brush,
// Z = canonical surface Z) when called from the editor path.  For runtime
// paths that haven't pre-anchored, the vertical probe here will correct it.
// ---------------------------------------------------------------------------
static bool ShouldSpawnHole(
    ADiggerManager* Digger,
    const FVector& BrushPos,
    float BrushRadius)
{
    if (!Digger) return false;

    UWorld* World = Digger->GetWorld();
    if (!World)   return false;

    // --- 1. Canonical vertical surface probe at BrushPos.XY ---
    float CanonicalSurfaceZ = UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT;

    // Try height cache first.
    TOptional<float> CacheZ = Digger->GetLandscapeHeightAt_Internal(BrushPos);
    if (CacheZ.IsSet())
    {
        CanonicalSurfaceZ = CacheZ.GetValue();
    }
    else
    {
        // Vertical line trace fallback.
        const FVector Start(BrushPos.X, BrushPos.Y, BrushPos.Z + BrushRadius * 3.f);
        const FVector End  (BrushPos.X, BrushPos.Y, BrushPos.Z - BrushRadius * 3.f);
        FHitResult ProbeHit;
        FCollisionQueryParams ProbeParams(SCENE_QUERY_STAT(DiggerShouldSpawnProbe), false);

        if (World->LineTraceSingleByChannel(ProbeHit, Start, End, ECC_Visibility, ProbeParams)
            && ProbeHit.GetActor()
            && ProbeHit.GetActor()->IsA(ALandscapeProxy::StaticClass()))
        {
            CanonicalSurfaceZ = ProbeHit.ImpactPoint.Z;
        }

        // Ring search if center XY is over an already-open hole.
        if (CanonicalSurfaceZ <= UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT)
        {
            const float RingR = BrushRadius * 0.4f;
            const float Angles[] = { 0.f, 90.f, 180.f, 270.f };
            for (float Ang : Angles)
            {
                const float Rad = FMath::DegreesToRadians(Ang);
                FVector RingPos(BrushPos.X + RingR * FMath::Cos(Rad),
                                BrushPos.Y + RingR * FMath::Sin(Rad),
                                BrushPos.Z);

                TOptional<float> RingZ = Digger->GetLandscapeHeightAt_Internal(RingPos);
                if (RingZ.IsSet())
                {
                    CanonicalSurfaceZ = RingZ.GetValue();
                    break;
                }
            }
        }
    }

    if (CanonicalSurfaceZ <= UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT)
        return false; // No landscape found

    // --- 2. Brush center must be within one radius of the surface ---
    const float BrushCenterToSurface = BrushPos.Z - CanonicalSurfaceZ;
    if (BrushCenterToSurface > BrushRadius)
        return false; // Brush floating above — no hole BP needed

    // --- 3. Top of brush sphere must not be more than 60% below surface ---
    const float TopOfBrush     = BrushPos.Z + BrushRadius;
    const float BurialDepth    = CanonicalSurfaceZ - TopOfBrush;
    const float MaxBurialDepth = BrushRadius * 0.60f;
    if (BurialDepth > MaxBurialDepth)
        return false;

    // --- 4. Redundancy gate (test at surface-anchored position) ---
    const FVector SurfaceAnchoredPos(BrushPos.X, BrushPos.Y, CanonicalSurfaceZ);
    if (IsHoleRedundant(World, SurfaceAnchoredPos, BrushRadius))
        return false;

    return true;
}





// -----------------------------------------------------------------------------------
// LIFECYCLE
// -----------------------------------------------------------------------------------

FDiggerEdMode::FDiggerEdMode() {}
FDiggerEdMode::~FDiggerEdMode() {}

void FDiggerEdMode::DeselectAllSceneActors()
{
    if (GEditor && GEditor->GetSelectedActors())
    {
        GEditor->GetSelectedActors()->DeselectAll();
    }
}

static ADiggerManager* FindExistingManager(UWorld* World)
{
    if (!World) return nullptr;
    return ADiggerManager::FindDiggerManager(World);
}

static void EnsureDiggerPrereqs()
{
#if WITH_EDITOR
    if (!GEditor) return;

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World) return;

    // --- Crash fix: pending-kill manager holds the name "DiggerManager" in the
    // level registry even after deletion.  GetActorOfClass skips pending-kill
    // actors, so FindExistingManager returns nullptr while the name is still
    // reserved — causing SpawnActor to fatal-assert.
    // Solution: scan with AllActors flag; if a pending-kill instance exists,
    // flush GC synchronously to release the name before we try to spawn.
    ADiggerManager* Mgr = FindExistingManager(World);
    if (!Mgr)
    {
        for (TActorIterator<ADiggerManager> It(World, ADiggerManager::StaticClass(),
             EActorIteratorFlags::AllActors); It; ++It)
        {
            // A pending-kill instance is still holding the name — purge it.
            GEngine->ForceGarbageCollection(/*bForcePurge=*/true);
            Mgr = FindExistingManager(World);
            break;
        }
    }

    if (!Mgr)
    {
        FActorSpawnParameters S;
        S.Name     = FName(TEXT("DiggerManager"));
        // NameMode::Requested: if "DiggerManager" is somehow still reserved after
        // the GC flush, UE5 will auto-rename (e.g. DiggerManager_1) instead of
        // calling a fatal check().  This is the belt-and-suspenders safety net.
        S.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
        S.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        S.ObjectFlags = RF_Transactional;

        Mgr = World->SpawnActor<ADiggerManager>(ADiggerManager::StaticClass(), FTransform::Identity, S);
    }

    if (Mgr)
    {
        if (!IsValid(Mgr->HoleShapeLibrary))
        {
            const UDiggerEditorSettings* Settings = UDiggerEditorSettings::Get();
            UHoleShapeLibrary* LoadedLib = nullptr;

            if (Settings && !Settings->DefaultHoleLibrary.IsNull())
            {
                LoadedLib = Settings->DefaultHoleLibrary.LoadSynchronous();
                UE_LOG(LogTemp, Log, TEXT("DiggerPrereqs: Loaded HoleShapeLibrary from Project Settings."));
            }

            if (LoadedLib)
            {
                Mgr->HoleShapeLibrary = LoadedLib;
                UE_LOG(LogTemp, Log, TEXT("DiggerPrereqs: Successfully assigned existing HoleShapeLibrary."));
            }
            else
            {
                Mgr->HoleShapeLibrary = NewObject<UHoleShapeLibrary>(Mgr, UHoleShapeLibrary::StaticClass());
                UE_LOG(LogTemp, Warning, TEXT("DiggerPrereqs: Could not load Default Library. Created a new transient one. Check Project Settings -> Digger."));
            }

            Mgr->Modify();
        }
    }
#endif
}


void FDiggerEdMode::OnLevelActorDeleted(AActor* InActor)
{
    // Only care about DiggerManager being deleted while our mode is live.
    if (!InActor || !InActor->IsA(ADiggerManager::StaticClass())) return;

    UE_LOG(LogTemp, Warning,
        TEXT("Digger: DiggerManager was deleted while the editor mode was active. "
             "Scheduling automatic respawn..."));

    // Defer by one tick so the deletion transaction has fully committed and the
    // actor name has been released from the level registry before we respawn.
    if (GEditor)
    {
        GEditor->GetTimerManager()->SetTimerForNextTick([this]()
        {
            // Crash-safe respawn — same path used at Enter().
            EnsureDiggerPrereqs();

            // Rebind the modifier-blocked delegate to the freshly spawned manager.
            if (ADiggerManager* NewMgr = FindDiggerManager())
            {
                NewMgr->OnModifierBlocked.RemoveAll(this);
                NewMgr->OnModifierBlocked.AddRaw(this, &FDiggerEdMode::HandleModifierBlocked);
            }

            // Tell the toolkit to pick up the new manager instance.
            if (TSharedPtr<FDiggerEdModeToolkit> DiggerToolkit = GetDiggerToolkit())
            {
                DiggerToolkit->OnManagerRespawned();
            }

            UE_LOG(LogTemp, Log, TEXT("Digger: DiggerManager respawned successfully."));
        });
    }
}

void FDiggerEdMode::OnLevelActorAdded(AActor* InActor)
{
    // Safety checks
    if (!InActor || !InActor->IsA(ADynamicHole::StaticClass())) return;

    // Check Settings
    const UDiggerEditorSettings* Settings = UDiggerEditorSettings::Get();
    bool bShowHoles = Settings ? Settings->bShowDynamicHolesFolder : false;

    // Apply Visibility Logic
    if (!bShowHoles)
    {
        // 1. Ensure it's hidden in outliner
        // (We call the static helper we made earlier)
        FDiggerEdModeToolkit::SetActorListedInOutliner(InActor, false);

        // 2. Ensure it's at the root (prevents folder creation)
        if (!InActor->GetFolderPath().IsNone())
        {
            InActor->SetFolderPath(NAME_None);
        }

        // 3. FORCE UI REFRESH
        // This is the missing link! It tells the Outliner to redraw immediately.
        if (GEditor)
        {
            // We use a timer to defer the refresh slightly, ensuring the spawn transaction is fully complete
            // before we ask the UI to rebuild. This prevents glitches.
            GEditor->GetTimerManager()->SetTimerForNextTick([this]()
            {
                if (GEditor) GEditor->BroadcastLevelActorListChanged();
            });
        }
    }
}


void FDiggerEdMode::Enter()
{
    FEdMode::Enter();

    // Mark mode active for any external systems
    bIsDiggerModeCurrentlyActive = true;

    // Make sure nothing is selected when we enter Digger mode
    DeselectAllSceneActors();

    // Ensure manager, libraries, etc. exist
    EnsureDiggerPrereqs();

    // Give keyboard/mouse focus to the active viewport so R/O/X/Y/Z + wheel are seen
    if (GEditor)
    {
        if (FViewport* ActiveViewport = GEditor->GetActiveViewport())
        {
            ActiveViewport->SetUserFocus(true);
            ActiveViewport->CaptureMouse(true);
        }
    }

    // Restore visibility based on saved user preference
    const UDiggerEditorSettings* Settings = UDiggerEditorSettings::Get();
    if (Settings)
    {
        TSharedPtr<FDiggerEdModeToolkit> DiggerToolkit = StaticCastSharedPtr<FDiggerEdModeToolkit>(Toolkit);
        if (DiggerToolkit.IsValid())
        {
            DiggerToolkit->SetDynamicHolesFolderVisible(Settings->bShowDynamicHolesFolder);
        }
                FDiggerEdModeToolkit::SetDynamicHolesFolderVisible(Settings->bShowDynamicHolesFolder);
    }

    // Subscribe to modifier‑blocked notifications
    if (ADiggerManager* Manager = FindDiggerManager())
    {
        Manager->OnModifierBlocked.AddRaw(this, &FDiggerEdMode::HandleModifierBlocked);
    }

    // Ensure toolkit exists
    if (!Toolkit.IsValid())
    {
        FDiggerEditorAccess::SetEditorModeActive(true);
        Toolkit = MakeShareable(new FDiggerEdModeToolkit);
        Toolkit->Init(Owner->GetToolkitHost());
    }

    // Make sure the preview actor exists (no viewport client needed here)
    EnsurePreviewExists(nullptr);
    
    // Start listening for new Actor spawns
    OnLevelActorAddedHandle = GEngine->OnLevelActorAdded().AddRaw(this, &FDiggerEdMode::OnLevelActorAdded);

    // Watch for DiggerManager being deleted so we can auto-respawn it.
    OnLevelActorDeletedHandle = GEngine->OnLevelActorDeleted().AddRaw(this, &FDiggerEdMode::OnLevelActorDeleted);

    // ---------------------------------------------------------
    // REGISTER GLOBAL INPUT PROCESSOR
    // ---------------------------------------------------------
    DiggerInputProcessor = MakeShareable(new FDiggerInputProcessor(this));
    if (FSlateApplication::IsInitialized())
    {
        FSlateApplication::Get().RegisterInputPreProcessor(DiggerInputProcessor);
    }
}



void FDiggerEdMode::Exit()
{

    // ---------------------------------------------------------
    // UNREGISTER GLOBAL INPUT PROCESSOR
    // ---------------------------------------------------------
    if (DiggerInputProcessor.IsValid() && FSlateApplication::IsInitialized())
    {
        FSlateApplication::Get().UnregisterInputPreProcessor(DiggerInputProcessor);
        DiggerInputProcessor.Reset();
    }

    
    if (ADiggerManager* Manager = FindDiggerManager())
    {
        Manager->OnModifierBlocked.RemoveAll(this);
    }

    DestroyPreview();
    
    // 1. Stop Listening
    if (OnLevelActorAddedHandle.IsValid())
    {
        if (GEngine)
        {
            GEngine->OnLevelActorAdded().Remove(OnLevelActorAddedHandle);
        }
        OnLevelActorAddedHandle.Reset();
    }

    if (OnLevelActorDeletedHandle.IsValid())
    {
        if (GEngine)
        {
            GEngine->OnLevelActorDeleted().Remove(OnLevelActorDeletedHandle);
        }
        OnLevelActorDeletedHandle.Reset();
    }



    if (Toolkit.IsValid())
    {
        TSharedPtr<FDiggerEdModeToolkit> DiggerToolkit = StaticCastSharedPtr<FDiggerEdModeToolkit>(Toolkit);
        if (DiggerToolkit.IsValid())
        {
            DiggerToolkit->DestroyWorklight();
        }

        // 1. Get the user's preference
        const UDiggerEditorSettings* Settings = UDiggerEditorSettings::Get();
    
        // 2. If Settings exist, stick to what the user chose. 
        // If Checked: Leave it visible. 
        // If Unchecked: Hide it.
        bool bFinalVisibilityState = Settings ? Settings->bShowDynamicHolesFolder : false;

        // 3. Apply the state
        DiggerToolkit->SetDynamicHolesFolderVisible(bFinalVisibilityState);
        
        FDiggerEditorAccess::SetEditorModeActive(false);
        FToolkitManager::Get().CloseToolkit(Toolkit.ToSharedRef());
        Toolkit.Reset();
    }

    FEdMode::Exit();
}



bool FDiggerEdMode::IsSelectionAllowed(AActor* InActor, bool bInSelection) const
{
    if (bPaintingEnabled)
    {
        return false;
    }

    return FEdMode::IsSelectionAllowed(InActor, bInSelection);
}

bool FDiggerEdMode::Select(AActor* InActor, bool bInSelection)
{
    if (bPaintingEnabled)
    {
        return true; // consume selection while painting
    }
    return FEdMode::Select(InActor, bInSelection);
}

// -----------------------------------------------------------------------------------
// INPUT & TRANSACTIONS
// -----------------------------------------------------------------------------------


bool FDiggerEdMode::CapturedMouseMove(
    FEditorViewportClient* InViewportClient,
    FViewport* InViewport,
    int32 InMouseX,
    int32 InMouseY)
{
    // Always update preview when the mouse moves
    if (InViewportClient)
    {
        UpdatePreviewAtCursor(InViewportClient);
    }

    // ❌ Do NOT paint here anymore.
    // Continuous painting is now handled entirely in Tick().
    // This preserves deep digging / building even when the mouse is stationary.

    return false; // allow viewport navigation, selection, etc.
}


// -----------------------------------------------------------------------------------
// BRUSH HUD MESSAGES
// -----------------------------------------------------------------------------------

// void FDiggerEdMode::ShowBrushHUDMessage(const FString& Msg, const FLinearColor& Color)
// {
//     FBrushHUDMessage NewMsg;
//     NewMsg.Text = Msg;
//     NewMsg.Color = Color;
//     NewMsg.TimeRemaining = 1.2f;
//     NewMsg.Alpha = 1.f;
//     NewMsg.YOffset = 20.f; // slide-in start
//
//     BrushHUD.Messages.Insert(NewMsg, 0); // newest at top
// }


void FDiggerEdMode::UpdateBrushHUDPanel()
{
    BrushHUD.bVisible = true;
    BrushHUD.TimeRemaining = 1.5f;
    BrushHUD.Alpha = 1.f;

    // Force redraw immediately
    if (Owner && Owner->GetToolkitHost().IsValid())
    {
        Owner->GetToolkitHost()->GetParentWidget()->Invalidate(EInvalidateWidget::LayoutAndVolatility);
    }

    // Always pull live values from the toolkit so the HUD reflects current state
    // regardless of how values were changed (scroll wheel, spinbox, key press).
    if (TSharedPtr<FDiggerEdModeToolkit> DiggerToolkit = GetDiggerToolkit())
    {
        BrushCache.Rotation = DiggerToolkit->GetBrushRotation();
        BrushCache.Offset   = DiggerToolkit->GetBrushOffset();
        BrushCache.Radius   = DiggerToolkit->GetBrushRadius();
        BrushCache.Strength = DiggerToolkit->GetBrushStrength();
        BrushCache.Falloff  = DiggerToolkit->GetBrushFalloff();
        BrushCache.Force    = DiggerToolkit->GetBrushForce();
        BrushCache.PushMode = DiggerToolkit->GetBrushPushMode();
    }

    switch (CurrentMode)
    {
    case EDiggerMainMode::Sculpt:
        {
            BrushHUD.ModeText = TEXT("Sculpt Mode");
            BrushHUD.Radius   = BrushCache.Radius;
            BrushHUD.Strength = BrushCache.Strength;
            BrushHUD.Falloff  = BrushCache.Falloff;
            BrushHUD.Force    = BrushCache.Force;
            BrushHUD.PushMode = BrushCache.PushMode;
            BrushHUD.Rotation = BrushCache.Rotation;
            BrushHUD.Offset   = BrushCache.Offset;
            break;
        }

    case EDiggerMainMode::Offset:
        {
            BrushHUD.ModeText = TEXT("Offset Mode");
            BrushHUD.Offset   = BrushCache.Offset;
            BrushHUD.Radius   = 0.f;
            BrushHUD.Strength = 0.f;
            BrushHUD.Falloff  = 0.f;
            BrushHUD.Force    = 0.f;
            break;
        }

    case EDiggerMainMode::Rotate:
        {
            BrushHUD.ModeText = TEXT("Rotation Mode");
            BrushHUD.Rotation = BrushCache.Rotation;
            BrushHUD.Radius   = 0.f;
            BrushHUD.Strength = 0.f;
            BrushHUD.Falloff  = 0.f;
            BrushHUD.Force    = 0.f;
            break;
        }
    }
}


// -----------------------------------------------------------------------------------
// TRANSFORM RESET HELPERS
// All live in .cpp — they call FDiggerEdModeToolkit methods which require the full
// type (not available from the forward declaration in DiggerEdMode.h).
//
//  ResetCurrentModeAxis()  C        — selected axis, current mode only
//  ResetCurrentModeAxes()  CC       — all axes, current mode only
//  ResetAllTransforms()    Alt+C    — wipe rotation + offset entirely
//  ResetActiveAxis / ResetAllAxes   — legacy aliases
// -----------------------------------------------------------------------------------

static void ApplyRotationReset(TSharedPtr<FDiggerEdModeToolkit>& Tk, FDiggerEdMode::FBrushCache& Cache,
                                EDiggerAxisMode Axis, bool bAll)
{
    FRotator Rot = Tk->GetBrushRotation();
    if (bAll)
    {
        Rot = FRotator::ZeroRotator;
        Cache.Rotation = FRotator::ZeroRotator;
    }
    else
    {
        switch (Axis)
        {
        case EDiggerAxisMode::X: Rot.Pitch = 0.f; Cache.Rotation.Pitch = 0.f; break;
        case EDiggerAxisMode::Y: Rot.Yaw   = 0.f; Cache.Rotation.Yaw   = 0.f; break;
        case EDiggerAxisMode::Z: Rot.Roll  = 0.f; Cache.Rotation.Roll  = 0.f; break;
        default: break;
        }
    }
    Tk->SetBrushRotation(Rot);
}

static void ApplyOffsetReset(TSharedPtr<FDiggerEdModeToolkit>& Tk, FDiggerEdMode::FBrushCache& Cache,
                              EDiggerAxisMode Axis, bool bAll)
{
    FVector Off = Tk->GetBrushOffset();
    if (bAll)
    {
        Off = FVector::ZeroVector;
        Cache.Offset = FVector::ZeroVector;
    }
    else
    {
        switch (Axis)
        {
        case EDiggerAxisMode::X: Off.X = 0.f; Cache.Offset.X = 0.f; break;
        case EDiggerAxisMode::Y: Off.Y = 0.f; Cache.Offset.Y = 0.f; break;
        case EDiggerAxisMode::Z: Off.Z = 0.f; Cache.Offset.Z = 0.f; break;
        default: break;
        }
    }
    Tk->SetBrushOffset(Off);
}

// C — clear selected axis on CURRENT mode only (rotation OR offset, not both)
void FDiggerEdMode::ResetCurrentModeAxis()
{
    if (TSharedPtr<FDiggerEdModeToolkit> Tk = GetDiggerToolkit())
    {
        if (CurrentMode == EDiggerMainMode::Rotate)
            ApplyRotationReset(Tk, BrushCache, CurrentAxis, false);
        else if (CurrentMode == EDiggerMainMode::Offset)
            ApplyOffsetReset(Tk, BrushCache, CurrentAxis, false);
    }
    UpdateBrushHUDPanel();
}

// CC — clear all axes on CURRENT mode only (rotation OR offset, not both)
void FDiggerEdMode::ResetCurrentModeAxes()
{
    if (TSharedPtr<FDiggerEdModeToolkit> Tk = GetDiggerToolkit())
    {
        if (CurrentMode == EDiggerMainMode::Rotate)
            ApplyRotationReset(Tk, BrushCache, CurrentAxis, true);
        else if (CurrentMode == EDiggerMainMode::Offset)
            ApplyOffsetReset(Tk, BrushCache, CurrentAxis, true);
    }
    UpdateBrushHUDPanel();
}

// Alt+C — nuclear reset: wipe both rotation AND offset entirely
void FDiggerEdMode::ResetAllTransforms()
{
    if (TSharedPtr<FDiggerEdModeToolkit> Tk = GetDiggerToolkit())
    {
        Tk->SetBrushRotation(FRotator::ZeroRotator);
        Tk->SetBrushOffset(FVector::ZeroVector);
    }
    BrushCache.Rotation = FRotator::ZeroRotator;
    BrushCache.Offset   = FVector::ZeroVector;
    UpdateBrushHUDPanel();
}

// Legacy aliases
void FDiggerEdMode::ResetActiveAxis()  { ResetCurrentModeAxis();  }
void FDiggerEdMode::ResetAllAxes()     { ResetCurrentModeAxes();  }

// ---------------------------------------------------------------------------
// SelectAxis — public entry point for X/Y/Z key presses.
// Called directly from FDiggerInputProcessor (focus-independent).
// ---------------------------------------------------------------------------
void FDiggerEdMode::SelectAxis(EDiggerAxisMode Axis)
{
    if (CurrentMode != EDiggerMainMode::Rotate && CurrentMode != EDiggerMainMode::Offset)
        return;

    if (bIsContinuouslyApplying) StopContinuousApplication();

    CurrentAxis = Axis;
    UpdateBrushHUDPanel();
    UpdatePreviewModeIndicator();
}

void FDiggerEdMode::HandleModifierBlocked(bool bBlocked)
{
    if (bBlocked)
        UpdateBrushHUDPanel();
    else
        UpdateBrushHUDPanel();
}



// -----------------------------------------------------------------------------------
// BRUSH APPLICATION
// -----------------------------------------------------------------------------------

// Brush Helpers


// Helper to push scalar settings to the manager
void FDiggerEdMode::SyncBrushSettingsToManager(ADiggerManager* Digger, const FBrushCache& Settings)
{
    if (!Digger) return;

    Digger->EditorBrushRadius           = Settings.Radius;
    Digger->EditorBrushDig              = Settings.bFinalBrushDig;
    Digger->EditorBrushIsFilled         = Settings.bIsFilled;
    Digger->EditorBrushAngle            = Settings.Angle;
    Digger->EditorBrushType             = Settings.BrushType;
    Digger->EditorBrushLightType        = Settings.LightType;
    Digger->EditorBrushHiddenSeam       = Settings.bHiddenSeam;
    Digger->EditorbUseAdvancedCubeBrush = Settings.bUseAdvancedCube;
    Digger->EditorCubeHalfExtentX       = Settings.CubeHalfExtentX;
    Digger->EditorCubeHalfExtentY       = Settings.CubeHalfExtentY;
    Digger->EditorCubeHalfExtentZ       = Settings.CubeHalfExtentZ;
    Digger->EditorBrushRotation         = Settings.Rotation;
    
    // WYSIWYG: We assume the position passed to drawing functions is FINAL.
    // We set the offset to zero so the manager doesn't apply it a second time.
    Digger->EditorBrushOffset           = FVector::ZeroVector; 
}

// Helper to detect if a new hole is redundant (overlapping an existing one)
// This prevents "Skirt Peeling" when digging deeper inside an existing cave.
//
// HARDENED: The previous coverage formula used `Dist2D + NewRadius * 0.9` which
// suppressed any new hole whose centre was inside an existing hole, even when
// the new hole meaningfully extends the opening.  The corrected formula only
// suppresses holes that are *completely* contained within an existing hole
// (center + newRadius still inside existingRadius, with a 5% tolerance).
static bool IsHoleRedundant(UWorld* World, const FVector& NewHolePos, float NewHoleRadius)
{
    if (!World || NewHoleRadius <= 0.f) return false;

    for (TActorIterator<ADynamicHole> It(World); It; ++It)
    {
        ADynamicHole* ExistingHole = *It;
        if (!IsValid(ExistingHole)) continue;

        const float ExistingRadius = ExistingHole->CachedStroke.BrushRadius;
        if (ExistingRadius <= 0.f) continue;

        const FVector ExistingPos = ExistingHole->GetActorLocation();
        const float Dist2D = FVector::Dist2D(NewHolePos, ExistingPos);

        // A new hole is redundant only if it is *fully enclosed* by the existing
        // hole — i.e. every point of the new hole circle falls inside the
        // existing hole circle.  We add 5% tolerance so small surface offsets
        // don't re-stamp identical holes, but edge-widening strokes still go through.
        const float FurthestNewPoint = Dist2D + NewHoleRadius;
        if (FurthestNewPoint < ExistingRadius * 1.05f)
        {
            return true; // fully contained — redundant
        }
    }

    return false;
}
// Brush Worker Methods

void FDiggerEdMode::UpdateBrushSettingsFromUI(const FHitResult& TraceHit, bool bRightClick)
{
    TSharedPtr<FDiggerEdModeToolkit> DiggerToolkit = GetDiggerToolkit();
    if (!DiggerToolkit.IsValid()) return;

    // Core scalar settings
    BrushCache.Radius    = DiggerToolkit->GetBrushRadius();
    BrushCache.Falloff   = DiggerToolkit->GetBrushFalloff();
    BrushCache.Strength  = DiggerToolkit->GetBrushStrength();
    BrushCache.Force    = DiggerToolkit->GetBrushForce();
    BrushCache.PushMode = DiggerToolkit->GetBrushPushMode();
    BrushCache.LightType = DiggerToolkit->GetCurrentLightType();

    // Dig / Add mode (with right‑click inversion)
    const bool bUiDig = DiggerToolkit->IsDigMode();
    BrushCache.bFinalBrushDig = bRightClick ? !bUiDig : bUiDig;

    // Keep DiggerManager in sync so runtime knows current dig/add state
    if (ADiggerManager* Digger = FindDiggerManager())
    {
        Digger->EditorBrushDig = BrushCache.bFinalBrushDig;
    }

    // Rotation (optionally aligned to surface normal)
    BrushCache.Rotation = DiggerToolkit->GetBrushRotation();
    if (DiggerToolkit->UseSurfaceNormalRotation())
    {
        const FVector Normal = TraceHit.ImpactNormal.GetSafeNormal();
        const FQuat AlignRotation = FQuat::FindBetweenNormals(FVector::UpVector, Normal);
        BrushCache.Rotation = (AlignRotation * BrushCache.Rotation.Quaternion()).Rotator();
    }

    // Shape / mode flags
    BrushCache.bIsFilled   = DiggerToolkit->GetBrushIsFilled();
    BrushCache.Angle       = DiggerToolkit->GetBrushAngle();
    BrushCache.BrushType   = DiggerToolkit->GetCurrentBrushType();
    BrushCache.bHiddenSeam = DiggerToolkit->GetHiddenSeam();

    // Advanced cube
    BrushCache.bUseAdvancedCube      = DiggerToolkit->IsUsingAdvancedCubeBrush();
    BrushCache.CubeHalfExtentX       = DiggerToolkit->GetAdvancedCubeHalfExtentX();
    BrushCache.CubeHalfExtentY       = DiggerToolkit->GetAdvancedCubeHalfExtentY();
    BrushCache.CubeHalfExtentZ       = DiggerToolkit->GetAdvancedCubeHalfExtentZ();

    // Offset
    BrushCache.Offset = DiggerToolkit->GetBrushOffset();

    // --- FIX FOR ISSUE TWO (STRAIGHT LINE PAINTING) ---
    // Previously, this was set to Radius * 0.5f. Since this variable is used against DeltaTime
    // in Tick(), a radius of 100 would result in a 50-second delay between strokes.
    // We set this to a low value (e.g. 0.01s) to allow Tick to fire rapidly.
    // The distance check inside ApplyContinuousBrush() will prevent over-painting.
    ContinuousApplicationInterval = 0.01f; 
    
    // Use the WYSIWYG preview center, not the raw surface hit
    // (UpdatePreviewAtCursor is responsible for keeping CachedBrushPreviewCenter in sync)
    LastStrokePreviewCenter = BrushCache.CachedBrushPreviewCenter;
    bHasLastStrokeSample = false;
}

void FDiggerEdMode::ApplyContinuousBrush(FEditorViewportClient* InViewportClient)
{
    if (!ContinuousSettings.bIsValid) return;

    ADiggerManager* Digger = FindDiggerManager();
    if (!IsValid(Digger) || !InViewportClient || !InViewportClient->Viewport) return;

    // 1. Modifier check — ends the stroke
    const bool bShift = InViewportClient->Viewport->KeyState(EKeys::LeftShift)  || InViewportClient->Viewport->KeyState(EKeys::RightShift);
    const bool bCtrl  = InViewportClient->Viewport->KeyState(EKeys::LeftControl) || InViewportClient->Viewport->KeyState(EKeys::RightControl);
    const bool bAlt   = InViewportClient->Viewport->KeyState(EKeys::LeftAlt)    || InViewportClient->Viewport->KeyState(EKeys::RightAlt);

    if (bShift || bAlt)
    {
        // Update LastPos to current visual pos to prevent snapping on release
        FVector PauseHitLoc;
        FHitResult PauseHit;
        if (GetMouseWorldHit(InViewportClient, PauseHitLoc, PauseHit))
        {
            LastStrokeHitLocation = GetVisualLocation(PauseHitLoc, BrushCache);
        }

        bMouseButtonDown          = false;
        bIsPainting               = false;
        bIsContinuouslyApplying   = false;
        ContinuousSettings.bIsValid = false;

        if (Digger) Digger->bIsEditorPainting = false;
        StopContinuousApplication();
        return;
    }

    // 2. Raycast to get Mouse Position on Surface
    FVector MouseHitLocation;
    FHitResult Hit;
    if (!GetMouseWorldHit(InViewportClient, MouseHitLocation, Hit))
        return;

    // ---------------------------------------------------------
    // DETERMINE VISUAL TARGET (WYSIWYG)
    // ---------------------------------------------------------
    const FVector VisualHitLocation = GetVisualLocation(MouseHitLocation, BrushCache);

    // ---------------------------------------------------------
    // 3. FIRST SAMPLE CHECK
    // ---------------------------------------------------------
    if (!bHasLastStrokeSample)
    {
        ApplyBrushWithSettings(Digger, VisualHitLocation, Hit, BrushCache);
        LastStrokeHitLocation = VisualHitLocation;
        bHasLastStrokeSample = true;
        return;
    }

    // ---------------------------------------------------------
    // 4. DISTANCE FILTERS
    // ---------------------------------------------------------
    const float DistanceSquared = FVector::DistSquared(VisualHitLocation, LastStrokeHitLocation);
    const float JumpDistSq = FMath::Square(BrushCache.Radius * 4.0f);

    // Prevent cross-map teleports
    if (DistanceSquared > JumpDistSq)
    {
        LastStrokeHitLocation = VisualHitLocation;
        return;
    }

    // Oversampling filter (don't paint if mouse hasn't moved enough)
    if (DistanceSquared < 1.0f)
    {
        return;
    }

    // ---------------------------------------------------------
    // 5. UNIFIED DISCRETE STEPPING (HARMONY FIX)
    // ---------------------------------------------------------
    // Previously, Sphere brushes used a "Swept" logic that failed to update
    // the Manager's bounds correctly when offsets were applied.
    // Now, ALL brushes use the discrete stepping logic (String of Pearls).
    // This ensures ApplyBrushWithSettings is called for every step,
    // guaranteeing the Offset is Zeroed and the correct Chunks are woken up.
    
    // Step size: 20% of radius ensures smooth overlap for spheres.
    // Ensure at least 1 unit step to prevent infinite loops on tiny brushes.
    const float StepSize = FMath::Max(1.0f, BrushCache.Radius * 0.20f); 
    
    const float Distance = FMath::Sqrt(DistanceSquared);
    const int32 Steps    = FMath::Clamp(FMath::FloorToInt(Distance / StepSize), 1, 100);

    const FVector Direction = (VisualHitLocation - LastStrokeHitLocation).GetSafeNormal();
    
    // We snapshot settings once, but we will update the PreviewCenter inside the loop
    FBrushCache CurrentSettings = BrushCache;
    
    // If using a specialized capsule brush in UI, force it to behave as a Sphere
    // during stepping to create a consistent tube, unless it's a specific shape user wants.
    // For now, we trust the UI settings, as stepping a "Sphere" creates a perfect capsule result.

    for (int32 i = 1; i <= Steps; ++i)
    {
        // Calculate the exact Visual Position for this step
        const FVector Pos = LastStrokeHitLocation + Direction * StepSize * i;

        FHitResult StepHit = Hit;
        StepHit.ImpactPoint = Pos; // Floating point (approximate surface)

        FBrushCache StepSettings = CurrentSettings;
        StepSettings.CachedBrushPreviewCenter = Pos; 

        // This calls the robust logic that correctly handles:
        // 1. Setting EditorBrushPosition = Pos
        // 2. Setting EditorBrushOffset = Zero
        // 3. Waking up the specific chunk at 'Pos'
        ApplyBrushWithSettings(Digger, Pos, StepHit, StepSettings);
    }
    
    // Ensure the final point is hit exactly (closes small gaps at the end)
    ApplyBrushWithSettings(Digger, VisualHitLocation, Hit, CurrentSettings);

    // 6. Update last stroke location
    LastStrokeHitLocation = VisualHitLocation;
    bIsContinuouslyApplying = true;
}


void FDiggerEdMode::ApplyBrushWithSettings(
    ADiggerManager* Digger,
    const FVector& HitLocation,
    const FHitResult& Hit,
    const FBrushCache& Settings)
{
    if (!Digger) return;

    Digger->Modify();

    FScopedBrushBusy Busy(this);

    // Sync scalar settings (Radius, Strength, etc.)
    SyncBrushSettingsToManager(Digger, Settings);

    // [CRITICAL] 
    // HitLocation is the FINAL Visual World Position.
    // Offset is set to ZERO because HitLocation already includes it.
    Digger->EditorBrushPosition = HitLocation;
    Digger->EditorBrushOffset   = FVector::ZeroVector; 
    
    // Apply
    Digger->ApplyBrushInEditor(Settings.bFinalBrushDig);

    // -----------------------------------------------------------------------
    // HOLE SPAWN LOGIC
    // -----------------------------------------------------------------------
    // ROOT CAUSE OF SURFACE PEELING:
    //
    //   HitLocation is the visual brush center — computed from an angled camera
    //   ray hitting a sloped landscape surface.  Its Z is the Z of the angled
    //   hit point, NOT the vertical surface height at that XY column.  When the
    //   hole BP is placed at HitLocation directly, it is anchored to the slope
    //   tangent rather than the landscape surface, producing wide shallow peels
    //   instead of precise circular punches.
    //
    // THE FIX — CANONICAL SURFACE ANCHOR:
    //
    //   1. Determine the landscape surface Z at HitLocation.XY via a strict
    //      vertical downward probe (not the angled ray hit).  This is the
    //      "canonical surface Z" regardless of camera angle.
    //
    //   2. All depth/burial gates test the brush center Z against this
    //      canonical surface Z (not the angled hit Z).
    //
    //   3. The hole BP spawn position is pinned to:
    //        (HitLocation.X, HitLocation.Y, CanonicalSurfaceZ)
    //      so the hole always sits flat on the landscape, not tilted into it.
    //
    //   4. The brush center Z is still used for voxel digging (that's correct —
    //      you want to dig into the slope) — only the HOLE BP placement changes.
    // -----------------------------------------------------------------------
#if WITH_EDITOR
    if (Settings.bFinalBrushDig)
    {
        UWorld* World = Digger->GetWorld();
        if (!World) return;

        // Per-session cooldown — file-scope static is safe because only one
        // EdMode instance is active at a time.
        static float s_LastHoleSpawnTime = -1000.f;
        const float Now = World->GetTimeSeconds();
        if (Now - s_LastHoleSpawnTime < 0.10f) return;

        // ---------------------------------------------------------------
        // STEP 1: CANONICAL VERTICAL SURFACE PROBE
        //
        // Drop a vertical ray at HitLocation.XY from well above to well
        // below. Accept only landscape hits. This gives us the true surface
        // Z at this XY column, independent of camera angle.
        //
        // We search a small ring too, because the XY may sit exactly on a
        // voxel hole boundary where the landscape has already been cut away.
        // ---------------------------------------------------------------
        float CanonicalSurfaceZ = UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT;

        auto VerticalLandscapeProbe = [&](float ProbeX, float ProbeY) -> bool
        {
            // Try the landscape height cache first (cheap, no physics).
            TOptional<float> CacheZ = Digger->GetLandscapeHeightAt_Internal(
                FVector(ProbeX, ProbeY, HitLocation.Z));
            if (CacheZ.IsSet())
            {
                CanonicalSurfaceZ = CacheZ.GetValue();
                return true;
            }

            // Fall back to a vertical line trace (accurate, hits actual geometry).
            const float SearchHigh = HitLocation.Z + Settings.Radius * 3.f;
            const float SearchLow  = HitLocation.Z - Settings.Radius * 3.f;
            FHitResult ProbeHit;
            FCollisionQueryParams ProbeParams(SCENE_QUERY_STAT(DiggerCanonicalSurfaceProbe), /*bTraceComplex=*/false);
            // Ignore the preview actor so it can't occlude our probe.
            if (Preview.IsValid()) ProbeParams.AddIgnoredActor(Preview.Get());

            if (World->LineTraceSingleByChannel(
                    ProbeHit,
                    FVector(ProbeX, ProbeY, SearchHigh),
                    FVector(ProbeX, ProbeY, SearchLow),
                    ECC_Visibility, ProbeParams)
                && ProbeHit.GetActor()
                && ProbeHit.GetActor()->IsA(ALandscapeProxy::StaticClass()))
            {
                CanonicalSurfaceZ = ProbeHit.ImpactPoint.Z;
                return true;
            }
            return false;
        };

        // Try brush center XY first, then a ring at 40% radius to handle
        // edge cases where the center XY is over an already-open hole.
        bool bFoundSurface = VerticalLandscapeProbe(HitLocation.X, HitLocation.Y);

        if (!bFoundSurface)
        {
            const float RingR = Settings.Radius * 0.4f;
            const float Angles[] = { 0.f, 90.f, 180.f, 270.f, 45.f, 135.f, 225.f, 315.f };
            for (float Ang : Angles)
            {
                const float Rad = FMath::DegreesToRadians(Ang);
                if (VerticalLandscapeProbe(
                        HitLocation.X + RingR * FMath::Cos(Rad),
                        HitLocation.Y + RingR * FMath::Sin(Rad)))
                {
                    bFoundSurface = true;
                    break;
                }
            }
        }

        if (!bFoundSurface)
        {
            if (DiggerDebug::Holes())
            {
                UE_LOG(LogTemp, Verbose,
                    TEXT("HoleSpawn: No landscape surface found at XY=(%.1f,%.1f) — skipped."),
                    HitLocation.X, HitLocation.Y);
            }
            return;
        }

        // ---------------------------------------------------------------
        // STEP 2: DEPTH GATES (against canonical surface, not angled hit Z)
        //
        // We test the brush CENTER Z against CanonicalSurfaceZ. The brush
        // must be close enough to the surface to warrant a hole BP, but not
        // so deep that it's fully underground (where a hole BP would just
        // clip or float, causing peeling artifacts).
        // ---------------------------------------------------------------

        // Gate A: brush center must be within one radius of the surface.
        // If it's floating more than one radius above, the user is digging
        // in mid-air and no landscape peel-prevention hole is needed.
        const float BrushCenterToSurface = HitLocation.Z - CanonicalSurfaceZ;

        if (BrushCenterToSurface > Settings.Radius)
        {
            // Brush is floating above — voxel dig is fine, no hole BP needed.
            if (DiggerDebug::Holes())
            {
                UE_LOG(LogTemp, Verbose,
                    TEXT("HoleSpawn: Brush above surface by %.1f (r=%.1f) — no BP needed."),
                    BrushCenterToSurface, Settings.Radius);
            }
            return;
        }

        // Gate B: the top of the brush sphere must not be more than 60%
        // below the surface. Beyond that the hole BP would be entirely
        // underground and would produce a floating peel, not a clean punch.
        const float TopOfBrush     = HitLocation.Z + Settings.Radius;
        const float BurialDepth    = CanonicalSurfaceZ - TopOfBrush; // positive = top buried
        const float MaxBurialDepth = Settings.Radius * 0.60f;

        if (BurialDepth > MaxBurialDepth)
        {
            if (DiggerDebug::Holes())
            {
                UE_LOG(LogTemp, Verbose,
                    TEXT("HoleSpawn: Top of brush buried %.1f > max %.1f — skipped."),
                    BurialDepth, MaxBurialDepth);
            }
            return;
        }

        // ---------------------------------------------------------------
        // STEP 3: REDUNDANCY CHECK
        //
        // Use the surface-anchored XY position for overlap detection so the
        // test is consistent with where we'll actually place the hole BP.
        // ---------------------------------------------------------------
        const FVector SurfaceAnchoredPos(HitLocation.X, HitLocation.Y, CanonicalSurfaceZ);

        if (IsHoleRedundant(World, SurfaceAnchoredPos, Settings.Radius))
        {
            return;
        }

        // ---------------------------------------------------------------
        // STEP 4: SPAWN — always at the canonical surface position.
        //
        // The hole BP's job is to punch a hole in the landscape surface.
        // It must be anchored to the surface (CanonicalSurfaceZ), not to
        // the angled brush center (HitLocation.Z). Using HitLocation.Z was
        // the direct cause of the tilted / peeling landscape surface.
        // ---------------------------------------------------------------
        FBrushStroke HoleStroke;
        HoleStroke.BrushPosition = SurfaceAnchoredPos;  // ← surface-anchored, not angled hit
        HoleStroke.BrushRadius   = Settings.Radius;
        HoleStroke.BrushType     = Settings.BrushType;
        HoleStroke.bDig          = true;
        // Hole BPs are always axis-aligned (no rotation) — they punch
        // straight down through the landscape, regardless of brush tilt.
        HoleStroke.BrushRotation = FRotator::ZeroRotator;

        Digger->HandleHoleSpawn(HoleStroke);
        s_LastHoleSpawnTime = Now;

        // Flush shadow/render state to prevent black ghost artifacts.
        if (AActor* HitActor = Hit.GetActor())
        {
            HitActor->MarkComponentsRenderStateDirty();
        }
        if (GEditor && GEditor->GetActiveViewport())
        {
            GEditor->GetActiveViewport()->Invalidate();
        }

        if (DiggerDebug::Holes())
        {
            UE_LOG(LogTemp, Log,
                TEXT("HoleSpawn: Spawned at surface (%.1f,%.1f,%.1f). BrushZ=%.1f, SurfZ=%.1f, Burial=%.1f"),
                SurfaceAnchoredPos.X, SurfaceAnchoredPos.Y, SurfaceAnchoredPos.Z,
                HitLocation.Z, CanonicalSurfaceZ, BurialDepth);
        }
    }
#endif
}




// -----------------------------------------------------------------------------------
// RAYCASTING / PREVIEW
// -----------------------------------------------------------------------------------

bool FDiggerEdMode::GetMouseWorldHit(FEditorViewportClient* ViewportClient,
                                     FVector& OutHitLocation,
                                     FHitResult& OutHit)
{
    if (!ViewportClient || !ViewportClient->Viewport)
        return false;

    // ---------------------------------------------------------
    // 1. Get mouse position in viewport
    // ---------------------------------------------------------
    FIntPoint MousePos;
    ViewportClient->Viewport->GetMousePos(MousePos);

    FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
        ViewportClient->Viewport,
        ViewportClient->GetScene(),
        ViewportClient->EngineShowFlags));

    FSceneView* SceneView = ViewportClient->CalcSceneView(&ViewFamily);
    if (!SceneView)
        return false;

    FVector WorldOrigin, WorldDirection;
    SceneView->DeprojectFVector2D(MousePos, WorldOrigin, WorldDirection);

    const FVector TraceStart = WorldOrigin;
    const FVector TraceEnd   = WorldOrigin + WorldDirection * 100000.f;

    // ---------------------------------------------------------
    // 2. Ensure DiggerManager exists
    // ---------------------------------------------------------
    ADiggerManager* Digger = FindDiggerManager();
    if (!IsValid(Digger))
    {
        if (DiggerDebug::Casts() || DiggerDebug::Manager())
        {
            UE_LOG(LogTemp, Error,
                TEXT("No DiggerManager found in DiggerEdMode::GetMouseWorldHit!"));
        }
        return false;
    }

    // ---------------------------------------------------------
    // 3. Ensure ActiveBrush exists and is initialized
    // ---------------------------------------------------------
    if (!IsValid(Digger->ActiveBrush))
    {
        if (UWorld* World = ViewportClient->GetWorld())
        {
            Digger->ActiveBrush = NewObject<UVoxelBrushShape>(
                Digger, UVoxelBrushShape::StaticClass());

            if (IsValid(Digger->ActiveBrush))
            {
                Digger->ActiveBrush->InitializeBrush(
                    Digger->ActiveBrush->GetBrushType(),
                    Digger->ActiveBrush->GetBrushSize(),
                    Digger->ActiveBrush->GetBrushLocation(),
                    Digger);

                Digger->InitializeBrushShapes();
            }
        }
    }

    // ---------------------------------------------------------
    // 4. SmartTrace (preferred)
    // ---------------------------------------------------------
    if (IsValid(Digger->ActiveBrush))
    {
        const FHitResult SmartHit =
            Digger->ActiveBrush->SmartTrace(TraceStart, TraceEnd);

        if (SmartHit.bBlockingHit)
        {
            OutHit        = SmartHit;
            OutHitLocation = SmartHit.ImpactPoint;

            if (DiggerDebug::Casts())
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("SmartTrace returned: %s"),
                    *SmartHit.ImpactPoint.ToString());
            }
            return true;
        }
    }

    // ---------------------------------------------------------
    // 5. Fallback line trace
    // ---------------------------------------------------------
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
        return false;

    FCollisionQueryParams Params(SCENE_QUERY_STAT(DiggerEdMode_MouseTrace), true);
    Params.bReturnPhysicalMaterial = false;

    // Ignore preview mesh
    if (Preview.IsValid())
        Params.AddIgnoredActor(Preview.Get());
    

    FHitResult FallbackHit;
    if (World->LineTraceSingleByChannel(
            FallbackHit, TraceStart, TraceEnd, ECC_Visibility, Params) &&
        FallbackHit.bBlockingHit)
    {
        OutHit        = FallbackHit;
        OutHitLocation = FallbackHit.ImpactPoint;

        if (DiggerDebug::Casts())
        {
            UE_LOG(LogTemp, Error,
                TEXT("Line Trace Fallback returned hit: %s"),
                *FallbackHit.ImpactPoint.ToString());
        }
        return true;
    }

    return false;
}

bool FDiggerEdMode::TraceUnderCursor(FEditorViewportClient* InViewportClient,
                                     FHitResult& OutHit)
{
    FVector Dummy;
    return GetMouseWorldHit(InViewportClient, Dummy, OutHit);
}


void FDiggerEdMode::UpdatePreviewModeIndicator()
{
    if (!Preview.IsValid())
        return;
    FScopedBrushBusy Busy(this);

    Preview->UpdateModeIcon(CurrentMode);
}


void FDiggerEdMode::EnsurePreviewExists(FEditorViewportClient* ViewportClient)
{
    // 1. Ensure preview mesh actor exists (editor world)
    if (!Preview.IsValid())
    {
        UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
        if (!EditorWorld)
            return;

        ABrushPreviewActor* PreviewActor = EditorWorld->SpawnActor<ABrushPreviewActor>();
        if (!PreviewActor)
            return;

        PreviewActor->Initialize(nullptr, nullptr);
        PreviewActor->SetVisible(true);

        Preview = PreviewActor;
    }
}





void FDiggerEdMode::DestroyPreview()
{
    if (Preview.IsValid())
    {
        Preview->Destroy();
        Preview.Reset();
    }
}

FDiggerEdMode::FBrushUIParams FDiggerEdMode::GetCurrentBrushUI() const
{
    FBrushUIParams P;

    if (TSharedPtr<FDiggerEdModeToolkit> DiggerToolkit = GetDiggerToolkit())
    {
        P.RadiusXYZ = FVector(DiggerToolkit->GetBrushRadius());
        P.Falloff   = DiggerToolkit->GetBrushFalloff();
        P.bAdd      = DiggerToolkit->IsDigMode();
        P.Rotation  = DiggerToolkit->GetBrushRotation();
        P.Offset    = DiggerToolkit->GetBrushOffset();
        P.CellSize  = 50.0f;
    }

    return P;
}


void FDiggerEdMode::UpdatePreviewAtCursor(FEditorViewportClient* InViewportClient)
{
    FScopedBrushBusy Busy(this);
    EnsurePreviewExists(nullptr);
    if (!Preview.IsValid())
        return;

    const UDiggerEditorSettings* Settings = UDiggerEditorSettings::Get();
    const bool bSnapPreviewToGrid = Settings ? Settings->bSnapPreviewToGrid : true;

    // ---------------------------------------------------------
    // 1. TRACE UNDER CURSOR
    // ---------------------------------------------------------
    FHitResult Hit;
    if (!TraceUnderCursor(InViewportClient, Hit))
    {
        Preview->SetVisible(false);
        BrushCache.CachedBrushPreviewCenter = FVector::ZeroVector;
        return;
    }

    Preview->SetVisible(true);

    // ---------------------------------------------------------
    // 2. UI BRUSH PARAMS (includes Force + PushMode)
    // ---------------------------------------------------------
    FBrushUIParams P = GetCurrentBrushUI();

    // ---------------------------------------------------------
    // 3. BASE CENTER = SMART TRACE RESULT
    // ---------------------------------------------------------
    FVector Center = Hit.ImpactPoint;
    

    // ---------------------------------------------------------
    // 3A. SURFACE NORMAL ALIGNMENT
    // ---------------------------------------------------------
    bool bCtrlHeld = false;
    if (InViewportClient && InViewportClient->Viewport)
    {
        bCtrlHeld = InViewportClient->Viewport->KeyState(EKeys::LeftControl) ||
                    InViewportClient->Viewport->KeyState(EKeys::RightControl);
    }

    bool bShouldAlign = false;
    if (TSharedPtr<FDiggerEdModeToolkit> LocalToolkit = GetDiggerToolkit())
    {
        bShouldAlign = LocalToolkit->UseSurfaceNormalRotation();
    }

    if (bCtrlHeld)
        bShouldAlign = true;

    if (bShouldAlign)
    {
        const FVector Normal = Hit.ImpactNormal.GetSafeNormal();
        const FQuat AlignRotation = FQuat::FindBetweenNormals(FVector::UpVector, Normal);

        if (bCtrlHeld)
        {
            P.Rotation = AlignRotation.Rotator();
        }
        else
        {
            P.Rotation = (AlignRotation * P.Rotation.Quaternion()).Rotator();
        }
    }

    // ---------------------------------------------------------
    // 3B. APPLY FORCE PUSH BEFORE OFFSET
    // ---------------------------------------------------------
    if (P.Force > KINDA_SMALL_NUMBER)
    {
        FVector PushDir = FVector::ZeroVector;

        switch (P.PushMode)
        {
            case EDiggerPushMode::Ray:
                PushDir = (Hit.TraceStart - Hit.ImpactPoint).GetSafeNormal();
                break;

            case EDiggerPushMode::Normal:
                PushDir = Hit.ImpactNormal.GetSafeNormal();
                break;

            case EDiggerPushMode::Blended:
            {
                FVector RayDir = (Hit.TraceStart - Hit.ImpactPoint).GetSafeNormal();
                FVector Normal = Hit.ImpactNormal.GetSafeNormal();
                PushDir = (RayDir + Normal).GetSafeNormal();
                break;
            }
        }

        const float MaxPushDistance = P.RadiusXYZ.X * 2.0f;
        const float PushDistance = P.Force * MaxPushDistance;

        Center += PushDir * PushDistance;
    }

    // ---------------------------------------------------------
    // 3C. APPLY OFFSET (local)
    // ---------------------------------------------------------
    FVector FinalOffset = P.Offset;

    if (!P.Rotation.IsNearlyZero())
    {
        FinalOffset = P.Rotation.RotateVector(P.Offset);
    }

    Center += FinalOffset;

    // ---------------------------------------------------------
    // 4. OPTIONAL GRID SNAP
    // ---------------------------------------------------------
    if (bSnapPreviewToGrid)
    {
        ADiggerManager* Manager = FindDiggerManager();
        if (Manager)
        {
            TOptional<float> TerrainHeight = Manager->GetLandscapeHeightAt_Internal(Center);
            const bool bIsLandscapeHit = TerrainHeight.IsSet();

            if (bIsLandscapeHit &&
                Hit.GetActor() &&
                Hit.GetActor()->IsA(ALandscapeProxy::StaticClass()))
            {
                const float Height = TerrainHeight.GetValue();

                if (Center.Z >= Height)
                {
                    UWorld* World = InViewportClient ? InViewportClient->GetWorld() : nullptr;
                    if (World)
                    {
                        FHitResult VerticalHit;
                        const FVector XY(Center.X, Center.Y, 0.f);
                        const FVector Start(XY.X, XY.Y, 100000.f);
                        const FVector End  (XY.X, XY.Y, -100000.f);

                        if (World->LineTraceSingleByChannel(VerticalHit, Start, End, ECC_Visibility))
                        {
                            if (VerticalHit.GetActor() && VerticalHit.GetActor()->IsA(ALandscapeProxy::StaticClass()))
                            {
                                Center.Z = VerticalHit.ImpactPoint.Z;
                            }
                        }
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------
    // 5. BUILD STROKE DATA (Force + PushMode included)
    // ---------------------------------------------------------
    FBrushStroke Stroke;
    Stroke.BrushPosition = Center;
    Stroke.BrushOffset   = FinalOffset;
    Stroke.BrushRadius   = P.RadiusXYZ.X;
    Stroke.BrushFalloff  = P.Falloff;
    Stroke.bDig          = P.bAdd;
    Stroke.BrushRotation = P.Rotation;
    Stroke.BrushLength   = P.RadiusXYZ.Z;
    
    float SettingsForceMultiplier = Settings? Settings->ForceMultiplier : 250.f;
    Stroke.BrushForce    = P.Force * SettingsForceMultiplier; // ToDo: replace magic number with settings ForceMultiplier value!
    Stroke.PushMode      = P.PushMode;

    if (TSharedPtr<FDiggerEdModeToolkit> FoundToolkit = GetDiggerToolkit())
    {
        Stroke.BrushType = FoundToolkit->GetCurrentBrushType();
        
    }
    else
    {
        Stroke.BrushType = EVoxelBrushType::Sphere;
    }

    // ---------------------------------------------------------
    // 6. PREVIEW SHAPE
    // ---------------------------------------------------------
    FVector Extents(P.RadiusXYZ.X);
    FQuat Rotation = P.Rotation.Quaternion();
    float Falloff = P.Falloff;
    EBrushPreviewShape PreviewShapeType = EBrushPreviewShape::Sphere;

    if (ADiggerManager* Digger = FindDiggerManager())
    {
        if (IsValid(Digger->ActiveBrush))
        {
            Digger->ActiveBrush->GetPreviewData(
                Center,
                Extents,
                Rotation,
                Falloff,
                Stroke.BrushType,
                Stroke
            );

            switch (Stroke.BrushType)
            {
                case EVoxelBrushType::Cube:
                case EVoxelBrushType::AdvancedCube:
                    PreviewShapeType = EBrushPreviewShape::Box;
                    break;
                case EVoxelBrushType::Cylinder:
                    PreviewShapeType = EBrushPreviewShape::Cylinder;
                    break;
                case EVoxelBrushType::Capsule:
                    PreviewShapeType = EBrushPreviewShape::Capsule;
                    break;
                case EVoxelBrushType::Cone:
                case EVoxelBrushType::Pyramid:
                    PreviewShapeType = EBrushPreviewShape::Cone;
                    break;
                case EVoxelBrushType::Torus:
                    PreviewShapeType = EBrushPreviewShape::Torus;
                    break;
                default:
                    PreviewShapeType = EBrushPreviewShape::Sphere;
                    break;
            }
        }
    }
    
        // ---------------------------------------------------------
        // 7. UPDATE PREVIEW ACTOR
        // ---------------------------------------------------------
        
        // A. Determine the effective dig/add state for color:
        //    - During a right-click paint stroke, BrushCache.bFinalBrushDig is the inverted truth.
        //    - During hover, use IsDigMode() which respects the UI toggle.
        //    We also check if right mouse button is currently held to preview the inverted color.
        TSharedPtr<FDiggerEdModeToolkit> DiggerToolkit = GetDiggerToolkit();
    
        bool bEffectiveDigForColor = DiggerToolkit.IsValid() ? DiggerToolkit->IsDigMode() : P.bAdd;

        // If we're actively painting with a right-click stroke, use the inverted (actual) state
        if (bIsPainting && ContinuousSettings.bIsValid && ContinuousSettings.bRightClick)
        {
            bEffectiveDigForColor = !bEffectiveDigForColor;
        }
        // Also check if the right mouse button is physically held right now (pre-click preview)
        else if (InViewportClient && InViewportClient->Viewport &&
                 InViewportClient->Viewport->KeyState(EKeys::RightMouseButton))
        {
            bEffectiveDigForColor = !bEffectiveDigForColor;
        }

        // B. Calculate light color using the effective state
        FLinearColor ActiveLightColor = bEffectiveDigForColor ? Settings->BrushColorDig : Settings->BrushColorAdd;
        
        // If user has chosen a custom light color instead of matching brush mode:
        if (Settings && !Settings->bMatchLightColorToBrush)
        {
            ActiveLightColor = Settings->BrushLightColor;
        }
    
        // C. Call UpdatePreview with all material parameters
        Preview->UpdatePreview(
            Center,
            Extents,
            Falloff,
            bEffectiveDigForColor,  // Drive material IsAdd flag with the effective state
            P.CellSize,
            PreviewShapeType,
            Rotation,
            
            // Material Colors (Read from Settings)
            Settings->BrushColorDig,
            Settings->BrushColorAdd,
            Settings->BrushColorFalloff,
            
            // Material Depth/Opacity (Read from Settings)
            Settings->BrushDepthFadeDistance,
            Settings->BrushOpacity,
            
            // Light Component Settings
            ActiveLightColor,
            Settings->BrushLightIntensity,
            Settings->BrushLightAttenuationRadius
        );
    
        // Cache center for painting logic
        BrushCache.CachedBrushPreviewCenter = Center;
        BrushCache.Radius = P.RadiusXYZ.X;
        LastStrokePreviewCenter = Center;
    
}


// -----------------------------------------------------------------------------------
// MISC INPUT / TICK
// -----------------------------------------------------------------------------------

bool FDiggerEdMode::MouseEnter(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y)
{
    if (ViewportClient && Viewport)
    {
        Viewport->SetUserFocus(true);
    }
    return FEdMode::MouseEnter(ViewportClient, Viewport, x, y);
}

bool FDiggerEdMode::InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale)
{
    // Painting is handled via InputKey
    return FEdMode::InputDelta(InViewportClient, InViewport, InDrag, InRot, InScale);
}

bool FDiggerEdMode::StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
    // Check modifiers directly from the viewport
    const bool bShift = InViewport->KeyState(EKeys::LeftShift) || InViewport->KeyState(EKeys::RightShift);
    const bool bCtrl  = InViewport->KeyState(EKeys::LeftControl) || InViewport->KeyState(EKeys::RightControl);
    const bool bAlt   = InViewport->KeyState(EKeys::LeftAlt) || InViewport->KeyState(EKeys::RightAlt);

    // If a modifier is held, return FALSE.
    // This tells the Editor: "I don't want to track this mouse drag; YOU handle it."
    // This enables Alt-Orbit / Pan / Zoom.
    if (bShift || bCtrl || bAlt)
    {
        return false; 
    }

    // If painting is enabled (and no modifiers), WE track the mouse.
    if (bPaintingEnabled)
    {
        bIsDragging = true;
        return true;
    }

    return FEdMode::StartTracking(InViewportClient, InViewport);
}

bool FDiggerEdMode::InputKey(
    FEditorViewportClient* ViewportClient,
    FViewport* Viewport,
    FKey Key,
    EInputEvent Event)
{
    if (!ViewportClient || !Viewport)
    {
        return false;
    }

    const bool bPressed  = (Event == IE_Pressed);
    const bool bReleased = (Event == IE_Released);

    // ---------------------------------------------------------
    // 0. IGNORE AUTO-REPEATS FOR OUR BRUSH CONTROL KEYS
    // ---------------------------------------------------------
    if (Event == IE_Repeat)
    {
        if (Key == EKeys::R || Key == EKeys::O ||
            Key == EKeys::X || Key == EKeys::Y || Key == EKeys::Z || 
            Key == EKeys::C) // Added C here so holding it doesn't flicker reset
        {
            return true;
        }
        return FEdMode::InputKey(ViewportClient, Viewport, Key, Event);
    }

    // ---------------------------------------------------------
    // 1. MODIFIER STATE (SHARED WITH SCROLL CONSUMPTION)
    // ---------------------------------------------------------
    // Use Slate (OS-level) modifier state — consistent with the scroll handler
    // and works correctly even when modifiers were held before viewport focus.
    const FModifierKeysState KeyMods = FSlateApplication::Get().GetModifierKeys();
    const bool bShift = KeyMods.IsShiftDown();
    const bool bCtrl  = KeyMods.IsControlDown();
    const bool bAlt   = KeyMods.IsAltDown();
    const bool bAnyModifier = bShift || bCtrl || bAlt;

    const bool bIsSculptMode        = (CurrentMode == EDiggerMainMode::Sculpt);
    const bool bShouldConsumeScroll = bAnyModifier || !bIsSculptMode;

    auto InterruptContinuousSculpting = [this, ViewportClient]()
    {
        FScopedBrushBusy Busy(this);
        if (bIsContinuouslyApplying)
        {
            ApplyContinuousBrush(ViewportClient); // optional final sweep
            StopContinuousApplication();
        }
    };

    // ---------------------------------------------------------
    // 2. MOUSE SCROLL KEYS: BLOCK VIEWPORT ZOOM WHEN WE OWN SCROLL
    // ---------------------------------------------------------
    if ((Key == EKeys::MouseScrollUp || Key == EKeys::MouseScrollDown) &&
        (Event == IE_Pressed || Event == IE_Repeat))
    {
        if (bShouldConsumeScroll)
        {
            InterruptContinuousSculpting();
            return true; 
        }
        return FEdMode::InputKey(ViewportClient, Viewport, Key, Event);
    }

    // ---------------------------------------------------------
    // 3. MODE KEYS (R / O) — TOGGLE MODES, INTERRUPT SCULPTING
    // ---------------------------------------------------------
    if (Key == EKeys::R && bPressed)
    {
        InterruptContinuousSculpting();
        // NOTE: The input processor may have already handled this key.
        // ToggleRotationMode is idempotent via its latch — calling it here
        // is safe only when InputKey fires (viewport had focus to begin with).
        // The processor returns true (consuming) so this path only runs when
        // the viewport already held focus and the processor did NOT fire.
        ToggleRotationMode();
        return true;
    }

    if (Key == EKeys::O && bPressed)
    {
        InterruptContinuousSculpting();
        ToggleOffsetMode();
        return true;
    }

    // ---------------------------------------------------------
    // 4. AXIS SELECTION (X/Y/Z) — ONLY VALID IN ROTATE/OFFSET
    // ---------------------------------------------------------
    if (bPressed && (Key == EKeys::X || Key == EKeys::Y || Key == EKeys::Z))
    {
        if (CurrentMode == EDiggerMainMode::Rotate ||
            CurrentMode == EDiggerMainMode::Offset)
        {
            InterruptContinuousSculpting();

            if (Key == EKeys::X) CurrentAxis = EDiggerAxisMode::X;
            if (Key == EKeys::Y) CurrentAxis = EDiggerAxisMode::Y;
            if (Key == EKeys::Z) CurrentAxis = EDiggerAxisMode::Z;

            UpdateBrushHUDPanel();
            UpdatePreviewModeIndicator();
            return true;
        }
    }

    // ---------------------------------------------------------
    // 4.5. RESET TRANSFORM (C / CC / Alt+C)
    //
    //  C         — clear selected axis on the CURRENT mode only
    //              (no axis selected → clears all axes on current mode)
    //  C C       — clear ALL axes on the CURRENT mode only
    //  Alt + C   — reset BOTH rotation AND offset entirely
    //
    //  Design rationale: Alt+C is the "nuclear" action (common in Blender/Maya)
    //  which is immediately discoverable and has no timing pressure.
    // ---------------------------------------------------------
    if (Key == EKeys::C && bPressed)
    {
        if (CurrentMode == EDiggerMainMode::Rotate || CurrentMode == EDiggerMainMode::Offset)
        {
            InterruptContinuousSculpting();

            // bAlt already declared in outer InputKey scope — using it directly.

            // Alt+C: nuclear reset — wipe both rotation and offset regardless of mode
            if (bAlt)
            {
                ResetAllTransforms();
                LastCPressTime = 0.0; // invalidate double-tap timer
                return true;
            }

            const double Now       = FPlatformTime::Seconds();
            const bool bIsDoubleTap = (Now - LastCPressTime) < 0.35;
            LastCPressTime          = Now;

            if (bIsDoubleTap || CurrentAxis == EDiggerAxisMode::None)
            {
                // CC or no-axis: clear all axes on current mode only
                ResetCurrentModeAxes();
            }
            else
            {
                // Single C with axis selected: clear that axis on current mode only
                ResetCurrentModeAxis();
            }

            return true;
        }
    }

    // ---------------------------------------------------------
    // 5. MODIFIER KEYS — END STROKE BUT PASS THROUGH
    // ---------------------------------------------------------
    if (bPressed &&
        (Key == EKeys::LeftShift || Key == EKeys::RightShift ||
         Key == EKeys::LeftControl || Key == EKeys::RightControl ||
         Key == EKeys::LeftAlt || Key == EKeys::RightAlt))
    {
        InterruptContinuousSculpting();
        return FEdMode::InputKey(ViewportClient, Viewport, Key, Event);
    }

    // ---------------------------------------------------------
    // 6. TOGGLE PAINTING MODE (P)
    // ---------------------------------------------------------
    if (Key == EKeys::P && bPressed)
    {
        bPaintingEnabled = !bPaintingEnabled;
        UpdateBrushHUDPanel();
        return true;
    }

    // ---------------------------------------------------------
    // 7. PAINTING LOGIC (LMB / RMB)
    // ---------------------------------------------------------
    if (bPaintingEnabled &&
        (Key == EKeys::LeftMouseButton || Key == EKeys::RightMouseButton))
    {
        // Navigation Pass-Through
        const bool bShiftDown = Viewport->KeyState(EKeys::LeftShift) || Viewport->KeyState(EKeys::RightShift);
        const bool bAltDown   = Viewport->KeyState(EKeys::LeftAlt)   || Viewport->KeyState(EKeys::RightAlt);

        if (bShiftDown || bAltDown)
        {
            return false;
        }

        if (bPressed)
        {
            FVector HitLocation;
            FHitResult Hit;

            if (!GetMouseWorldHit(ViewportClient, HitLocation, Hit))
            {
                return false;
            }

            const bool bCtrlDown =
                Viewport->KeyState(EKeys::LeftControl) ||
                Viewport->KeyState(EKeys::RightControl);

            // CTRL + CLICK: Sample Normal
            if (bCtrlDown)
            {
                if (TSharedPtr<FDiggerEdModeToolkit> DiggerToolkit = GetDiggerToolkit())
                {
                    const FVector Normal = Hit.ImpactNormal.GetSafeNormal();
                    const FQuat AlignRotation = FQuat::FindBetweenNormals(FVector::UpVector, Normal);
                    DiggerToolkit->SetBrushRotation(AlignRotation.Rotator());
                }
                UpdateBrushHUDPanel();
                return true;
            }

            DeselectAllSceneActors();

            const bool bRightClick = (Key == EKeys::RightMouseButton);
            UpdateBrushSettingsFromUI(Hit, bRightClick);

            // -----------------------------------------------------------------
            // [FIX] CALCULATE VISUAL LOCATION IMMEDIATELY
            // -----------------------------------------------------------------
            // We do NOT use 'HitLocation' (Surface) for painting.
            // We use the calculated Visual Location (Surface + Offset).
            FVector VisualLocation = GetVisualLocation(HitLocation, BrushCache);

            // Set the "Last Stroke" to this visual location so the next Tick
            // interpolates from here, not from the surface.
            LastStrokeHitLocation   = VisualLocation;
            LastPaintLocation       = FVector2D(Viewport->GetMouseX(), Viewport->GetMouseY());

            bMouseButtonDown            = true;
            bIsPainting                 = true;
            bIsContinuouslyApplying     = true;
            ContinuousSettings.bIsValid = true;
            ContinuousSettings.bRightClick = bRightClick;

            // Mark that we have fired the first shot
            bHasLastStrokeSample = true;

            CurrentMode = EDiggerMainMode::Sculpt;
            CurrentAxis = EDiggerAxisMode::None;
            UpdatePreviewModeIndicator();

            if (ADiggerManager* Digger = FindDiggerManager())
            {
                Digger->bIsEditorPainting = true;
                // Apply the first brush at the VISUAL location
                ApplyBrushWithSettings(Digger, VisualLocation, Hit, BrushCache);
            }

            return true;
        }
        else if (bReleased)
        {
            InterruptContinuousSculpting();
            return true;
        }
    }

    // ---------------------------------------------------------
    // 8. FALLBACK
    // ---------------------------------------------------------
    return FEdMode::InputKey(ViewportClient, Viewport, Key, Event);
}






bool FDiggerEdMode::EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
    if (bIsPainting)
    {
        bIsDragging = false;
        bIsPainting = false;
        StopContinuousApplication();

        if (ActiveTransaction.IsValid())
        {
            ActiveTransaction.Reset();
        }
        return true;
    }

    if (TSharedPtr<FDiggerEdModeToolkit> DiggerToolkit = GetDiggerToolkit())
    {
        DiggerToolkit->SetTemporaryDigOverride(TOptional<bool>());
    }

    return FEdMode::EndTracking(InViewportClient, InViewport);
}


bool FDiggerEdMode::InputAxis(
    FEditorViewportClient* ViewportClient,
    FViewport* Viewport,
    int32 ControllerId,
    FKey Key,
    float Delta,
    float DeltaTime)
{
    if (!ViewportClient || !Viewport)
    {
        return false;
    }

    // Let base handle everything except the mouse wheel (camera look, etc.)
    if (Key != EKeys::MouseWheelAxis)
    {
        return FEdMode::InputAxis(ViewportClient, Viewport, ControllerId, Key, Delta, DeltaTime);
    }

    // Ignore tiny deltas
    if (FMath::IsNearlyZero(Delta))
    {
        return false;
    }

    // Delegate entirely to ProcessScrollDelta — which is also called directly
    // from FDiggerInputProcessor and works without viewport focus.
    const FModifierKeysState ChkMods = FSlateApplication::Get().GetModifierKeys();
    const bool bIsSculptMode = (CurrentMode == EDiggerMainMode::Sculpt);
    const bool bAnyModifier  = ChkMods.IsShiftDown() || ChkMods.IsControlDown() || ChkMods.IsAltDown();

    if (bIsSculptMode && !bAnyModifier)
        return FEdMode::InputAxis(ViewportClient, Viewport, ControllerId, Key, Delta, DeltaTime);

    ProcessScrollDelta(Delta, DeltaTime);
    return true;
}


// ---------------------------------------------------------------------------
// ProcessScrollDelta — the single implementation of all scroll-driven brush
// parameter changes. Called from InputAxis (viewport path) AND directly from
// FDiggerInputProcessor (focus-independent path). Modifier state is read from
// Slate so it works whether or not the viewport currently has OS focus.
// ---------------------------------------------------------------------------
bool FDiggerEdMode::ProcessScrollDelta(float Delta, float DeltaTime)
{
    if (FMath::IsNearlyZero(Delta)) return false;

    if (bIsContinuouslyApplying) StopContinuousApplication();

    const FModifierKeysState InputAxisMods = FSlateApplication::Get().GetModifierKeys();
    const bool bCtrl  = InputAxisMods.IsControlDown();
    const bool bShift = InputAxisMods.IsShiftDown();
    const bool bAlt   = InputAxisMods.IsAltDown();
    const bool bAnyModifier = bCtrl || bShift || bAlt;

    TSharedPtr<FDiggerEdModeToolkit> IAToolkit = GetDiggerToolkit();
    if (!IAToolkit.IsValid()) return false;


    // -------------------------------------------------------------------------
    // A. ROTATION MODE (R) + Axis (X/Y/Z)
    // -------------------------------------------------------------------------
    if (CurrentMode == EDiggerMainMode::Rotate && CurrentAxis != EDiggerAxisMode::None)
    {
        FRotator Rot = IAToolkit->GetBrushRotation();
        const float Step = Delta * 15.0f;

        switch (CurrentAxis)
        {
        case EDiggerAxisMode::X: Rot.Pitch += Step; break;
        case EDiggerAxisMode::Y: Rot.Yaw   += Step; break;
        case EDiggerAxisMode::Z: Rot.Roll  += Step; break;
        default: break;
        }

        Rot.Normalize();
        IAToolkit->SetBrushRotation(Rot);

        const TCHAR* AxisLabel =
            (CurrentAxis == EDiggerAxisMode::X) ? TEXT("Pitch") :
            (CurrentAxis == EDiggerAxisMode::Y) ? TEXT("Yaw")   :
                                                  TEXT("Roll");

        const float AxisValue =
            (CurrentAxis == EDiggerAxisMode::X) ? Rot.Pitch :
            (CurrentAxis == EDiggerAxisMode::Y) ? Rot.Yaw   :
                                                  Rot.Roll;
        UpdateBrushHUDPanel();

        return false;
    }

    // -------------------------------------------------------------------------
    // B. OFFSET MODE (O) + Axis (X/Y/Z)
    // -------------------------------------------------------------------------
    if (CurrentMode == EDiggerMainMode::Offset && CurrentAxis != EDiggerAxisMode::None)
    {
        FVector Off = IAToolkit->GetBrushOffset();
        const float Step = Delta * (bShift ? 1.0f : 5.0f);

        switch (CurrentAxis)
        {
        case EDiggerAxisMode::X: Off.X += Step; break;
        case EDiggerAxisMode::Y: Off.Y += Step; break;
        case EDiggerAxisMode::Z: Off.Z += Step; break;
        default: break;
        }

        IAToolkit->SetBrushOffset(Off);

        const TCHAR* AxisLabel =
            (CurrentAxis == EDiggerAxisMode::X) ? TEXT("X") :
            (CurrentAxis == EDiggerAxisMode::Y) ? TEXT("Y") :
                                                  TEXT("Z");

        const float AxisValue =
            (CurrentAxis == EDiggerAxisMode::X) ? Off.X :
            (CurrentAxis == EDiggerAxisMode::Y) ? Off.Y :
                                                  Off.Z;
        UpdateBrushHUDPanel();

        return false;
    }

    // -------------------------------------------------------------------------
    // C. BRUSH PARAMETER SCROLL (Radius / Strength / Falloff)
    // -------------------------------------------------------------------------
    if (bAnyModifier)
    {
        const bool bRadiusMode   =  bShift && !bCtrl && !bAlt;
        const bool bStrengthMode =  bCtrl  && !bShift && !bAlt;
        const bool bFalloffMode  =  bAlt   && !bShift && !bCtrl;
        const bool bForceMode  =  bCtrl &&  bAlt   && !bShift;

        const float BaseStep =
            bRadiusMode   ? BASE_RADIUS_STEP   :
            bStrengthMode ? BASE_STRENGTH_STEP :
            bFalloffMode  ? BASE_FALLOFF_STEP  :
            bForceMode  ? BASE_FORCE_STEP  :
                            BASE_RADIUS_STEP;

        const bool bFine   = false;
        const bool bCoarse = false;

        const float Step = WheelStep(BaseStep, Delta, DeltaTime, bFine, bCoarse);
        ScrollVelocity += Step;

        if (bRadiusMode)
        {
            float Radius = IAToolkit->GetBrushRadius();
            Radius = FMath::Clamp(Radius + Step, RADIUS_MIN, RADIUS_MAX);
            Radius = SnapIf(Radius, RADIUS_SNAP_STEP);
            IAToolkit->SetBrushRadius(Radius);
            UpdateBrushHUDPanel();
        }
        else if (bStrengthMode)
        {
            float Strength = IAToolkit->GetBrushStrength();
            Strength = FMath::Clamp(Strength + Step, STRENGTH_MIN, STRENGTH_MAX);
            Strength = SnapIf(Strength, STRENGTH_SNAP_STEP);
            IAToolkit->SetBrushStrength(Strength);

            UpdateBrushHUDPanel();
        }
        else if (bForceMode)
        {
            float Force = IAToolkit->GetBrushForce();
            Force = FMath::Clamp(Force + Step, FORCE_MIN, FORCE_MAX);
            Force = SnapIf(Force, FORCE_SNAP_STEP);
            IAToolkit->SetBrushForce(Force);

            UpdateBrushHUDPanel();
        }
        else if (bFalloffMode)
        {
            float Falloff = IAToolkit->GetBrushFalloff();
            Falloff = FMath::Clamp(Falloff + Step, FALLOFF_MIN, FALLOFF_MAX);
            Falloff = SnapIf(Falloff, FALLOFF_SNAP_STEP);
            IAToolkit->SetBrushFalloff(Falloff);

            UpdateBrushHUDPanel();
        }

        return true;
    }

    // If we’re here, we decided to consume scroll (e.g. Rotate/Offset mode but no axis),
    // so we still return true to block viewport zoom.
    return true;
}


bool FDiggerEdMode::ShouldApplyContinuously() const
{
    return bIsContinuouslyApplying &&
           bMouseButtonDown &&
           ContinuousSettings.bIsValid &&
           bPaintingEnabled;
}


void FDiggerEdMode::StartContinuousApplication(const FViewportClick& Click)
{
    // If painting is disabled, do nothing
    if (!bPaintingEnabled)
        return;

    FEditorViewportClient* VC = (FEditorViewportClient*)Click.GetViewportClient();
    if (!VC)
        return;

    // Do not start painting while modifiers are held
    if (VC->IsAltPressed() || VC->IsCtrlPressed() || VC->IsShiftPressed())
        return;

    // Must have valid settings
    if (!ContinuousSettings.bIsValid)
        return;

    bIsPainting               = true;
    bIsContinuouslyApplying   = true;
    bMouseButtonDown          = true;
    ContinuousApplicationTimer = 0.0f;

    // DiggerManager painting flag
    if (ADiggerManager* Digger = FindDiggerManager())
    {
        Digger->bIsEditorPainting = true;
    }
}


void FDiggerEdMode::StopContinuousApplication()
{
    bIsPainting               = false;
    bIsContinuouslyApplying   = false;
    bMouseButtonDown          = false;
    ContinuousSettings.bIsValid = false;
    ContinuousApplicationTimer = 0.0f;

    // Reset DiggerManager painting flag
    if (ADiggerManager* Digger = FindDiggerManager())
    {
        Digger->bIsEditorPainting = false;
    }

    // Clear temporary dig override
    if (TSharedPtr<FDiggerEdModeToolkit> DiggerToolkit = GetDiggerToolkit())
    {
        DiggerToolkit->SetTemporaryDigOverride(TOptional<bool>());
    }
}


void FDiggerEdMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
    FEdMode::Tick(ViewportClient, DeltaTime);

    // Get Modifier Input States
    const bool bShift = ViewportClient->Viewport->KeyState(EKeys::LeftShift)   ||
                        ViewportClient->Viewport->KeyState(EKeys::RightShift);
    const bool bCtrl  = ViewportClient->Viewport->KeyState(EKeys::LeftControl) ||
                        ViewportClient->Viewport->KeyState(EKeys::RightControl);
    const bool bAlt   = ViewportClient->Viewport->KeyState(EKeys::LeftAlt)     ||
                        ViewportClient->Viewport->KeyState(EKeys::RightAlt);
    
    // ---------------------------------------------------------------------
    // Ensure viewport focus when in Rotate/Offset mode so R/O/X/Y/Z always work
    // ---------------------------------------------------------------------
    if (ViewportClient && ViewportClient->Viewport)
    {
        const bool bModeNeedsFocus =
            (CurrentMode == EDiggerMainMode::Rotate) ||
            (CurrentMode == EDiggerMainMode::Offset);

        //if (bModeNeedsFocus)
       // {
            ViewportClient->Viewport->SetUserFocus(true);
            ViewportClient->Viewport->CaptureMouse(true);
        //}
    }

    
    // ---------------------------------------------------------------------
    // 1. BRUSH HUD TICK (UNCHANGED)
    // ---------------------------------------------------------------------
    if (BrushHUD.bVisible)
    {
        BrushHUD.TimeRemaining -= DeltaTime;

        if (BrushHUD.TimeRemaining <= 0.f)
        {
            BrushHUD.Alpha -= DeltaTime * 2.f;

            if (BrushHUD.Alpha <= 0.f)
            {
                BrushHUD.bVisible = false;
            }
        }
    }
    

    // ---------------------------------------------------------------------
    // 2. PREVIEW UPDATE (UNCHANGED)
    // ---------------------------------------------------------------------
    UpdatePreviewAtCursor(ViewportClient);

    // ---------------------------------------------------------------------
    // 3. SHIFT-FOCUS LOGIC (UNCHANGED)
    // ---------------------------------------------------------------------
    if (ViewportClient && ViewportClient->Viewport)
    {
        const bool bShiftDown =
            ViewportClient->Viewport->KeyState(EKeys::LeftShift) ||
            ViewportClient->Viewport->KeyState(EKeys::RightShift);

        if (bShiftDown && ViewportClient->Viewport->HasMouseCapture())
        {
            ViewportClient->Viewport->SetUserFocus(true);
        }
    }

    // ---------------------------------------------------------------------
    // 4. WORKLIGHT + TOOLKIT SYNC (UNCHANGED)
    // ---------------------------------------------------------------------
    if (TSharedPtr<FDiggerEdModeToolkit> DiggerToolkit = GetDiggerToolkit())
    {
        DiggerToolkit->SpawnOrUpdateWorklight(ViewportClient);
        DiggerToolkit->SetViewportClient(ViewportClient);

        if (DiggerToolkit->GetAutoUnderLandscape())
        {
            float AbsZ, RelZ;
            DiggerToolkit->GetElevationInfo(AbsZ, RelZ);
            const bool bShouldEnable = RelZ < 0.f;

            if (DiggerToolkit->GetWorklightEnabled() != bShouldEnable)
            {
                DiggerToolkit->ToggleWorklight(bShouldEnable);
            }
        }
    }

    if (GEditor)
    {
        GEditor->RedrawAllViewports();
    }

    // ---------------------------------------------------------------------
    // 5. NEW: CONTINUOUS PAINTING DRIVEN BY TICK
    // ---------------------------------------------------------------------
    if (bIsContinuouslyApplying && bPaintingEnabled && ViewportClient)
    {
        ContinuousApplicationTimer += DeltaTime;
        if (ContinuousApplicationTimer >= ContinuousApplicationInterval)
        {
            ContinuousApplicationTimer = 0.0f;
            FScopedBrushBusy Busy(this);
            if (!bShift && !bCtrl && !bAlt)
                ApplyContinuousBrush(ViewportClient);
        }
    }

    // ---------------------------------------------------------------------
    // 6. SCROLL MOMENTUM SYSTEM (UNCHANGED)
    // ---------------------------------------------------------------------
    if (!ViewportClient || !ViewportClient->Viewport)
        return;

    if (FMath::IsNearlyZero(ScrollVelocity, 0.01f))
        return;

    ScrollVelocity = FMath::FInterpTo(ScrollVelocity, 0.0f, DeltaTime, ScrollDecayRate);

    TSharedPtr<FDiggerEdModeToolkit> TickToolkit = GetDiggerToolkit();
    if (!TickToolkit.IsValid())
        return;

    if (bShift && !bCtrl && !bAlt)
    {
        float R = TickToolkit->GetBrushRadius();
        R += ScrollVelocity * DeltaTime;
        R = FMath::Clamp(R, RADIUS_MIN, RADIUS_MAX);
        TickToolkit->SetBrushRadius(R);
        UpdateBrushHUDPanel();

    }
    else if (bCtrl && !bShift && !bAlt)
    {
        float S = TickToolkit->GetBrushStrength();
        S += ScrollVelocity * DeltaTime * 0.01f;
        S = FMath::Clamp(S, STRENGTH_MIN, STRENGTH_MAX);
        TickToolkit->SetBrushStrength(S);
        UpdateBrushHUDPanel();
    }
    else if (bAlt && !bShift && !bCtrl)
    {
        float F = TickToolkit->GetBrushFalloff();
        F += ScrollVelocity * DeltaTime * 0.01f;
        F = FMath::Clamp(F, FALLOFF_MIN, FALLOFF_MAX);
        TickToolkit->SetBrushFalloff(F);
        UpdateBrushHUDPanel();
    }
    // Busy Indicator Rotation Driver
    if (bIsBrushBusy)
    {
        // Rotate at 180 degrees per second
        LoadingSpriteRotation += DeltaTime * 180.f;

        // Keep it in [0, 360)
        if (LoadingSpriteRotation >= 360.f)
            LoadingSpriteRotation -= 360.f;
    }
    
    UpdatePreviewModeIndicator();
}



void FDiggerEdMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
    FCanvas* Canvas = Viewport->GetDebugCanvas();
    if (!Canvas)
        return;

    const UDiggerEditorSettings* Settings = UDiggerEditorSettings::Get();
    if (!Settings)
        return;

    // Viewport size
    const FIntPoint ViewSize = Viewport->GetSizeXY();
    const float SizeX = ViewSize.X;
    const float SizeY = ViewSize.Y;

    // Layout constants
    constexpr float TopSafeZone   = 60.f;
    constexpr float SidePadding   = 20.f;
    constexpr float BottomPadding = 50.f;
    constexpr float RightWidth    = 300.f;

    // Determine anchor position
    FVector2D Anchor;

    switch (Settings->BrushHUDPosition)
    {
    case EBrushHUDPosition::UpperLeft:
        Anchor = FVector2D(SidePadding, TopSafeZone);
        break;

    case EBrushHUDPosition::UpperRight:
        Anchor = FVector2D(SizeX - RightWidth, TopSafeZone);
        break;

    case EBrushHUDPosition::LowerLeft:
        Anchor = FVector2D(SidePadding, SizeY - BottomPadding);
        break;

    case EBrushHUDPosition::LowerRight:
        Anchor = FVector2D(SizeX - RightWidth, SizeY - BottomPadding);
        break;

    case EBrushHUDPosition::Custom:
        Anchor = Settings->CustomHUDOffset;
        break;

    default:
        Anchor = FVector2D(SidePadding, TopSafeZone);
        break;
    }

    // --- Brush HUD Render ---
    if (BrushHUD.bVisible && BrushHUD.Alpha > 0.f)
    {
        float Y = Anchor.Y;

        auto DrawLine = [&](const FString& Text)
        {
            FCanvasTextItem Item(
                FVector2D(Anchor.X, Y),
                FText::FromString(Text),
                GEngine->GetSmallFont(),
                FLinearColor(1,1,1,BrushHUD.Alpha)
            );
            Item.EnableShadow(FLinearColor::Black);
            Canvas->DrawItem(Item);
            Y += 18.f;
        };

        DrawLine(BrushHUD.ModeText);

        switch (CurrentMode)
        {
        case EDiggerMainMode::Sculpt:
            DrawLine(FString::Printf(TEXT("Radius: %.1f"), BrushHUD.Radius));
            DrawLine(FString::Printf(TEXT("Strength: %.2f"), BrushHUD.Strength));
            DrawLine(FString::Printf(TEXT("Falloff: %.2f"), BrushHUD.Falloff));
            DrawLine(FString::Printf(TEXT("Force: %.2f (%s)"),
                BrushHUD.Force,
                *StaticEnum<EDiggerPushMode>()->GetNameStringByValue((int64)BrushHUD.PushMode)
            ));

            // Also show rotation + offset in sculpt mode
            DrawLine(FString::Printf(TEXT("Rotation: %.1f, %.1f, %.1f"),
                BrushHUD.Rotation.Pitch,
                BrushHUD.Rotation.Yaw,
                BrushHUD.Rotation.Roll));

            DrawLine(FString::Printf(TEXT("Offset: %.1f, %.1f, %.1f"),
                BrushHUD.Offset.X,
                BrushHUD.Offset.Y,
                BrushHUD.Offset.Z));
            break;

        case EDiggerMainMode::Offset:
            DrawLine(FString::Printf(TEXT("Offset: %.1f, %.1f, %.1f"),
                BrushHUD.Offset.X,
                BrushHUD.Offset.Y,
                BrushHUD.Offset.Z));
            break;

        case EDiggerMainMode::Rotate:
            DrawLine(FString::Printf(TEXT("Rotation: %.1f, %.1f, %.1f"),
                BrushHUD.Rotation.Pitch,
                BrushHUD.Rotation.Yaw,
                BrushHUD.Rotation.Roll));
            break;
        }
    }

    // Rendering the loading aware mode indicator.
    if (bIsBrushBusy)
    {
        if (Preview.IsValid())
        {
            Preview->ShowLoadingSprite(LoadingSpriteRotation);
        }
    }
    else
    {
        UpdatePreviewModeIndicator(); // your existing function
    }
}



// -----------------------------------------------------------------------------------
// BOILERPLATE & HELPERS
// -----------------------------------------------------------------------------------

ADiggerManager* FDiggerEdMode::FindDiggerManager()
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    return ADiggerManager::FindDiggerManager(World);
}

TSharedPtr<FDiggerEdModeToolkit> FDiggerEdMode::GetDiggerToolkit()
{
    return StaticCastSharedPtr<FDiggerEdModeToolkit>(Toolkit);
}

TSharedPtr<FDiggerEdModeToolkit> FDiggerEdMode::GetDiggerToolkit() const
{
    return StaticCastSharedPtr<FDiggerEdModeToolkit>(Toolkit);
}

void FDiggerEdMode::AddReferencedObjects(FReferenceCollector& Collector)
{
    FEdMode::AddReferencedObjects(Collector);
}

bool FDiggerEdMode::UsesToolkits() const
{
    return true;
}

// Helper to calculate the final "Visual" location (where the ghost is)
FVector FDiggerEdMode::GetVisualLocation(const FVector& RawHitLocation, const FBrushCache& Settings) const
{
    // 1. If we have a valid cached preview that matches this general area, use it.
    // (This handles complex snapping if the preview actor logic did it)
    if (!Settings.CachedBrushPreviewCenter.IsNearlyZero())
    {
        // Sanity check: If the mouse moved drastically but cache didn't update, 
        // the cache might be stale. If distance is huge, ignore cache.
        if (FVector::DistSquared(RawHitLocation, Settings.CachedBrushPreviewCenter) < FMath::Square(5000.0f))
        {
            return Settings.CachedBrushPreviewCenter;
        }
    }

    // 2. Fallback: Manual Calculation (Hit + Rotated Offset)
    // This is crucial for the very first frame of a click where cache might be empty.
    FVector FinalOffset = Settings.Offset;
    if (!Settings.Rotation.IsNearlyZero())
    {
        FinalOffset = Settings.Rotation.RotateVector(Settings.Offset);
    }
    
    return RawHitLocation + FinalOffset;
}


bool FDiggerEdMode::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
    if (bPaintingEnabled)
    {
        return true;
    }

    DeselectAllSceneActors();

    FVector HitLocation;
    FHitResult Hit;
    if (!GetMouseWorldHit(InViewportClient, HitLocation, Hit))
    {
        return false;
    }

    if (ADiggerManager* Digger = FindDiggerManager())
    {
        const bool bRightClick = (Click.GetKey() == EKeys::RightMouseButton);
        UpdateBrushSettingsFromUI(Hit, bRightClick);

        if (BrushCache.BrushType == EVoxelBrushType::Debug)
        {
            Digger->DebugBrushPlacement(HitLocation);
            return true;
        }

        // [FIX] Use Visual Location helper
        FVector VisualLocation = GetVisualLocation(HitLocation, BrushCache);
        LastStrokeHitLocation  = VisualLocation;

        ApplyBrushWithSettings(Digger, VisualLocation, Hit, BrushCache);
        return true;
    }

    return false;
}

bool FDiggerEdMode::HandleClickSimple(const FVector& RayOrigin, const FVector& RayDirection)
{
    return false; // Stub
}

#undef LOCTEXT_NAMESPACE
