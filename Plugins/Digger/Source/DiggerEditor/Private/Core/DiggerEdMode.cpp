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
            (Key == EKeys::R || Key == EKeys::O || Key == EKeys::P || Key == EKeys::C);

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

        // ⭐ Immediately trigger EdMode logic for mode keys
        if (bIsModeKey)
        {
            if (Key == EKeys::R)
                DiggerMode->SetCurrentMode(EDiggerMainMode::Rotate);

            else if (Key == EKeys::O)
                DiggerMode->SetCurrentMode(EDiggerMainMode::Offset);

            else if (Key == EKeys::P)
                DiggerMode->SetCurrentMode(EDiggerMainMode::Sculpt);

            else if (Key == EKeys::C)
            {
                const double Now = FPlatformTime::Seconds();
                const double Delta = Now - DiggerMode->LastCPressTime;

                DiggerMode->LastCPressTime = Now;

                if (Delta < 0.25) // double‑press threshold
                {
                    DiggerMode->ResetAllAxes();
                }
                else
                {
                    DiggerMode->ResetActiveAxis();
                    DiggerMode->CycleAxisMode();
                }
            }

        }

        // ⭐ Immediately trigger modifier logic
        if (bIsModifier)
        {
            DiggerMode->HandleModifierBlocked(true);
        }

        // ⭐ Force viewport focus
        if (GEditor)
        {
            if (FViewport* ActiveViewport = GEditor->GetActiveViewport())
            {
                ActiveViewport->SetUserFocus(true);
            }
        }

        // Let the event bubble to the viewport (now focused)
        return false;
    }

bool HandleMouseWheelOrGestureEvent(
    FSlateApplication& SlateApp,
    const FPointerEvent& WheelEvent)
{
    if (!DiggerMode)
        return false;

    // ---------------------------------------------------------
    // 1. Force viewport focus (always)
    // ---------------------------------------------------------
    FViewport* ActiveViewport = nullptr;
    FEditorViewportClient* Client = nullptr;

    if (GEditor)
    {
        ActiveViewport = GEditor->GetActiveViewport();
        if (ActiveViewport)
        {
            ActiveViewport->SetUserFocus(true);
            Client = static_cast<FEditorViewportClient*>(ActiveViewport->GetClient());
        }
    }

    if (!ActiveViewport || !Client)
        return false;

    // ---------------------------------------------------------
    // 2. Read modifier keys directly from the viewport
    // ---------------------------------------------------------
    const bool bShift =
        ActiveViewport->KeyState(EKeys::LeftShift) ||
        ActiveViewport->KeyState(EKeys::RightShift);

    const bool bCtrl =
        ActiveViewport->KeyState(EKeys::LeftControl) ||
        ActiveViewport->KeyState(EKeys::RightControl);

    const bool bAlt =
        ActiveViewport->KeyState(EKeys::LeftAlt) ||
        ActiveViewport->KeyState(EKeys::RightAlt);

    const bool bAnyModifier = bShift || bCtrl || bAlt;

    // ---------------------------------------------------------
    // 3. Decide routing based on mode + modifiers
    // ---------------------------------------------------------
    const bool bIsSculptMode =
        (DiggerMode->GetCurrentMode() == EDiggerMainMode::Sculpt);

    if (bIsSculptMode && !bAnyModifier)
    {
        // Camera zoom (default Unreal behavior)
        return false;
    }

    // ---------------------------------------------------------
    // 4. Brush adjustment (Shift/Ctrl/Alt OR non-sculpt mode)
    // ---------------------------------------------------------
    Client->InputAxis(
        ActiveViewport,
        WheelEvent.GetPointerIndex(),   // DeviceId-style value
        EKeys::MouseWheelAxis,
        WheelEvent.GetWheelDelta(),
        SlateApp.GetDeltaTime(),
        1,
        false
    );

    return true; // consume scroll so camera does NOT zoom
}




    // We don't need to intercept these
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

        // Try to restore viewport focus when mouse is inside it
        if (GEditor)
        {
            if (FViewport* ActiveViewport = GEditor->GetActiveViewport())
            {
                // Check if mouse is inside viewport bounds
                FIntPoint Pos;
                ActiveViewport->GetMousePos(Pos);
                const FIntPoint Size = ActiveViewport->GetSizeXY();

                if (Pos.X >= 0 && Pos.Y >= 0 &&
                    Pos.X < Size.X && Pos.Y < Size.Y)
                {
                    ActiveViewport->SetUserFocus(true);
                }
            }
        }

        return false; // allow normal viewport behavior
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
// ShouldSpawnHole — authoritative pre-flight check called from HandleHoleSpawn
// in ADiggerManager.  The EdMode's ApplyBrushWithSettings has its OWN inline
// check so the manager path is still guarded when invoked from runtime or
// non-editor code paths.
//
// IMPORTANT: keep this logic in sync with ApplyBrushWithSettings's inline gate.
// ---------------------------------------------------------------------------
static bool ShouldSpawnHole(
    ADiggerManager* Digger,
    const FVector& BrushPos,
    float BrushRadius)
{
    if (!Digger) return false;

    UWorld* World = Digger->GetWorld();
    if (!World)   return false;

    // --- 1. Landscape height with XY search fallback ---
    TOptional<float> TerrainHeight = Digger->GetLandscapeHeightAt_Internal(BrushPos);

    if (!TerrainHeight.IsSet())
    {
        const float SearchRadius = BrushRadius * 0.5f;
        const FVector Offsets[] = {
            FVector( SearchRadius,  0.f,          0.f),
            FVector(-SearchRadius,  0.f,          0.f),
            FVector( 0.f,           SearchRadius, 0.f),
            FVector( 0.f,          -SearchRadius, 0.f),
        };
        for (const FVector& Off : Offsets)
        {
            TerrainHeight = Digger->GetLandscapeHeightAt_Internal(BrushPos + Off);
            if (TerrainHeight.IsSet()) break;
        }
    }

    if (!TerrainHeight.IsSet())
    {
        // Last resort: vertical line trace for landscape
        const FVector Start(BrushPos.X, BrushPos.Y, BrushPos.Z + BrushRadius * 2.f);
        const FVector End  (BrushPos.X, BrushPos.Y, BrushPos.Z - BrushRadius * 4.f);
        FHitResult ProbeHit;
        FCollisionQueryParams ProbeParams(SCENE_QUERY_STAT(DiggerShouldSpawnProbe), true);

        if (World->LineTraceSingleByChannel(ProbeHit, Start, End, ECC_Visibility, ProbeParams))
        {
            if (ProbeHit.GetActor() && ProbeHit.GetActor()->IsA(ALandscapeProxy::StaticClass()))
            {
                TerrainHeight = ProbeHit.ImpactPoint.Z;
            }
        }
    }

    if (!TerrainHeight.IsSet())
        return false; // No landscape found near brush

    const float LandscapeZ = TerrainHeight.GetValue();

    // --- 2. Brush must intersect the landscape surface ---
    const float VerticalDist = FMath::Abs(BrushPos.Z - LandscapeZ);
    if (VerticalDist > BrushRadius)
        return false;

    // --- 3. 60% burial gate (top of sphere must not be too deep) ---
    const float TopOfBrush     = BrushPos.Z + BrushRadius;
    const float BurialDepth    = LandscapeZ - TopOfBrush;
    const float MaxBurialDepth = BrushRadius * 0.60f;

    if (BurialDepth > MaxBurialDepth)
        return false;

    // --- 4. Redundancy gate ---
    if (IsHoleRedundant(World, BrushPos, BrushRadius))
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

    ADiggerManager* Mgr = FindExistingManager(World);
    if (!Mgr)
    {
        FActorSpawnParameters S;
        S.Name = FName(TEXT("DiggerManager"));
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
    
    // 2. Start Listening for new Actor spawns
    OnLevelActorAddedHandle = GEngine->OnLevelActorAdded().AddRaw(this, &FDiggerEdMode::OnLevelActorAdded);

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
        // Check GEngine exists before accessing (prevent crash on engine shutdown)
        if (GEngine)
        {
            GEngine->OnLevelActorAdded().Remove(OnLevelActorAddedHandle);
        }
        OnLevelActorAddedHandle.Reset();
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

            // Also show rotation/offset for sculpt mode (your request)
            BrushHUD.Rotation = BrushCache.Rotation;
            BrushHUD.Offset   = BrushCache.Offset;
            break;
        }

    case EDiggerMainMode::Offset:
        {
            BrushHUD.ModeText = TEXT("Offset Mode");

            BrushHUD.Offset   = BrushCache.Offset;

            // Clear sculpt values so UI doesn’t show stale data
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
    // Hardened for launch: single authoritative decision path.
    // -----------------------------------------------------------------------
#if WITH_EDITOR
    if (Settings.bFinalBrushDig)
    {
        UWorld* World = Digger->GetWorld();
        if (!World) return;

        // [FIX 1] File-scope static cooldown (safer than function-local static which
        //         never resets if the function's translation unit is hot-reloaded, and
        //         avoids needing a header change for a member variable).  A single
        //         EdMode instance is active at a time so this is safe.
        static float s_LastHoleSpawnTime = -1000.f;
        const float Now = World->GetTimeSeconds();
        if (Now - s_LastHoleSpawnTime < 0.10f) return;

        // [FIX 2] Reliable landscape height sampling with search fallback.
        //         GetLandscapeHeightAt_Internal can miss if the brush center
        //         sits on a voxel mesh rather than the raw landscape surface.
        //         We try up to 5 XY offsets before giving up.
        TOptional<float> TerrainHeight = Digger->GetLandscapeHeightAt_Internal(HitLocation);

        if (!TerrainHeight.IsSet())
        {
            // Search a ring of nearby sample points (N/S/E/W + centre-above).
            const float SearchRadius = Settings.Radius * 0.5f;
            const FVector Offsets[] = {
                FVector( SearchRadius,  0.f,           0.f),
                FVector(-SearchRadius,  0.f,           0.f),
                FVector( 0.f,           SearchRadius,  0.f),
                FVector( 0.f,          -SearchRadius,  0.f),
                FVector( 0.f,           0.f,           Settings.Radius), // straight up
            };
            for (const FVector& Off : Offsets)
            {
                TerrainHeight = Digger->GetLandscapeHeightAt_Internal(HitLocation + Off);
                if (TerrainHeight.IsSet()) break;
            }
        }

        // [FIX 3] If still no landscape → do a vertical line trace to find one.
        //         This handles cases where the brush is positioned above voxel
        //         geometry that occludes the landscape sample.
        if (!TerrainHeight.IsSet())
        {
            if (UWorld* W = Digger->GetWorld())
            {
                const FVector TraceStart(HitLocation.X, HitLocation.Y, HitLocation.Z + Settings.Radius * 2.f);
                const FVector TraceEnd  (HitLocation.X, HitLocation.Y, HitLocation.Z - Settings.Radius * 4.f);
                FHitResult LandscapeHit;
                FCollisionQueryParams LandscapeParams(SCENE_QUERY_STAT(DiggerHoleLandscapeProbe), true);

                if (W->LineTraceSingleByChannel(LandscapeHit, TraceStart, TraceEnd, ECC_Visibility, LandscapeParams))
                {
                    if (LandscapeHit.GetActor() && LandscapeHit.GetActor()->IsA(ALandscapeProxy::StaticClass()))
                    {
                        TerrainHeight = LandscapeHit.ImpactPoint.Z;
                    }
                }
            }
        }

        if (!TerrainHeight.IsSet())
        {
            if (DiggerDebug::Holes())
            {
                UE_LOG(LogTemp, Verbose,
                    TEXT("ApplyBrushWithSettings: No landscape found near %s — hole skipped."),
                    *HitLocation.ToString());
            }
            return;
        }

        const float LandscapeZ = TerrainHeight.GetValue();

        // [FIX 4] Unified depth gate.  
        //         The brush must intersect the landscape surface (within one radius)
        //         AND the TOP of the brush must not be more than 60% below it.
        //         Previously this check ran twice (here and in ShouldSpawnHole),
        //         using slightly different math — now it's done once.
        const float VerticalDist = FMath::Abs(HitLocation.Z - LandscapeZ);
        if (VerticalDist > Settings.Radius)
        {
            // Brush doesn't reach the surface at all — suppress.
            if (DiggerDebug::Holes())
            {
                UE_LOG(LogTemp, Verbose,
                    TEXT("ApplyBrushWithSettings: Brush too far from surface (dist=%.1f, r=%.1f) — hole skipped."),
                    VerticalDist, Settings.Radius);
            }
            return;
        }

        const float TopOfBrush    = HitLocation.Z + Settings.Radius;
        const float BurialDepth   = LandscapeZ - TopOfBrush;           // positive = buried
        const float MaxBurialDepth = Settings.Radius * 0.60f;

        if (BurialDepth > MaxBurialDepth)
        {
            if (DiggerDebug::Holes())
            {
                UE_LOG(LogTemp, Verbose,
                    TEXT("ApplyBrushWithSettings: Brush too deep (burial=%.1f, max=%.1f) — hole skipped."),
                    BurialDepth, MaxBurialDepth);
            }
            return;
        }

        // [FIX 5] Redundancy check via manager's cached list (O(n) but cheaper
        //         than TActorIterator every tick). Falls back to iterator if manager
        //         doesn't expose a list yet.
        if (IsHoleRedundant(World, HitLocation, Settings.Radius))
        {
            return;
        }

        // All gates passed — spawn the hole.
        FBrushStroke HoleStroke;
        HoleStroke.BrushPosition = HitLocation;
        HoleStroke.BrushRadius   = Settings.Radius;
        HoleStroke.BrushType     = Settings.BrushType;
        HoleStroke.bDig          = true;
        HoleStroke.BrushRotation = Settings.Rotation;

        Digger->HandleHoleSpawn(HoleStroke);
        s_LastHoleSpawnTime = Now;

        // [FIX 6] Flush shadow / render state immediately to prevent black ghost artifacts.
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
                TEXT("ApplyBrushWithSettings: Hole spawned at %s  (LandscapeZ=%.1f, burial=%.1f)"),
                *HitLocation.ToString(), LandscapeZ, BurialDepth);
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
        
        // A. Calculate the color for the LIGHT COMPONENT (Simple logic)
        FLinearColor ActiveLightColor = P.bAdd ? Settings->BrushColorAdd : Settings->BrushColorDig;
        
        // If user overrides light color in settings:
        if (Settings && !Settings->bMatchLightColorToBrush)
        {
            ActiveLightColor = Settings->BrushLightColor;
        }
    
        // B. Call UpdatePreview with all material parameters
        Preview->UpdatePreview(
            Center,
            Extents,
            Falloff,
            Stroke.bDig,
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
    


    // ---------------------------------------------------------
    // 8. COLOR & LIGHTING
    // ---------------------------------------------------------
    if (Settings)
    {
        FLinearColor TargetLightColor = P.bAdd ? Settings->BrushColorDig : Settings->BrushColorAdd;

        TArray<UStaticMeshComponent*> MeshComps;
        Preview->GetComponents<UStaticMeshComponent>(MeshComps);

        for (UStaticMeshComponent* MeshComp : MeshComps)
        {
            if (!MeshComp) continue;

            UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(MeshComp->GetMaterial(0));
            if (!MID)
            {
                MID = MeshComp->CreateAndSetMaterialInstanceDynamic(0);
            }
            if (MID)
            {
                MID->SetVectorParameterValue(FName("PreviewColor"), TargetLightColor);
            }
        }

        if (UPointLightComponent* LightComp = Preview->FindComponentByClass<UPointLightComponent>())
        {
            LightComp->SetIntensity(Settings->BrushLightIntensity);
            LightComp->SetAttenuationRadius(Settings->BrushLightAttenuationRadius);

            if (Settings->bMatchLightColorToBrush)
            {
                FLinearColor LightColor = TargetLightColor;
                LightColor.A = 1.0f;
                LightComp->SetLightColor(LightColor);
            }
            else
            {
                LightComp->SetLightColor(Settings->BrushLightColor);
            }
        }
    }
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
    const bool bShift = Viewport->KeyState(EKeys::LeftShift)   || Viewport->KeyState(EKeys::RightShift);
    const bool bCtrl  = Viewport->KeyState(EKeys::LeftControl) || Viewport->KeyState(EKeys::RightControl);
    const bool bAlt   = Viewport->KeyState(EKeys::LeftAlt)     || Viewport->KeyState(EKeys::RightAlt);
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

        bRotationModeLatched = !bRotationModeLatched;
        bOffsetModeLatched   = false;

        if (bRotationModeLatched)
        {
            CurrentMode = EDiggerMainMode::Rotate;
            CurrentAxis = EDiggerAxisMode::None;
        }
        else
        {
            CurrentMode = EDiggerMainMode::Sculpt;
            CurrentAxis = EDiggerAxisMode::None;
        }

        UpdateBrushHUDPanel();
        UpdatePreviewModeIndicator();
        return true;
    }

    if (Key == EKeys::O && bPressed)
    {
        InterruptContinuousSculpting();

        bOffsetModeLatched   = !bOffsetModeLatched;
        bRotationModeLatched = false;

        if (bOffsetModeLatched)
        {
            CurrentMode = EDiggerMainMode::Offset;
            CurrentAxis = EDiggerAxisMode::None;
        }
        else
        {
            CurrentMode = EDiggerMainMode::Sculpt;
            CurrentAxis = EDiggerAxisMode::None;
        }

        UpdateBrushHUDPanel();
        UpdatePreviewModeIndicator();
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
    // 4.5. RESET TRANSFORM (C) — SINGLE OR DOUBLE TAP
    // ---------------------------------------------------------
    if (Key == EKeys::C && bPressed)
    {
        // Only valid in Rotation or Offset modes
        if (CurrentMode == EDiggerMainMode::Rotate || CurrentMode == EDiggerMainMode::Offset)
        {
            InterruptContinuousSculpting();

            // Detect Double Tap manually
            const double CurrentTime = FPlatformTime::Seconds();
            const bool bIsDoubleTap  = (CurrentTime - LastCPressTime) < 0.30f; // 300ms threshold
            LastCPressTime           = CurrentTime;

            TSharedPtr<FDiggerEdModeToolkit> DiggerToolkit = GetDiggerToolkit();
            if (DiggerToolkit.IsValid())
            {
                // Logic:
                // 1. Double Tap -> Reset ALL axes.
                // 2. Single Tap (No Axis Selected) -> Reset ALL axes.
                // 3. Single Tap (Axis Selected) -> Reset ONLY that axis.
                
                const bool bResetAll = bIsDoubleTap || (CurrentAxis == EDiggerAxisMode::None);

                if (CurrentMode == EDiggerMainMode::Rotate)
                {
                    FRotator Rot = DiggerToolkit->GetBrushRotation();
                    
                    if (bResetAll)
                    {
                        Rot = FRotator::ZeroRotator;
                    }
                    else
                    {
                        if (CurrentAxis == EDiggerAxisMode::X) Rot.Pitch = 0.0f;
                        if (CurrentAxis == EDiggerAxisMode::Y) Rot.Yaw   = 0.0f;
                        if (CurrentAxis == EDiggerAxisMode::Z) Rot.Roll  = 0.0f;
                    }
                    DiggerToolkit->SetBrushRotation(Rot);
                }
                else if (CurrentMode == EDiggerMainMode::Offset)
                {
                    FVector Off = DiggerToolkit->GetBrushOffset();

                    if (bResetAll)
                    {
                        Off = FVector::ZeroVector;
                    }
                    else
                    {
                        if (CurrentAxis == EDiggerAxisMode::X) Off.X = 0.0f;
                        if (CurrentAxis == EDiggerAxisMode::Y) Off.Y = 0.0f;
                        if (CurrentAxis == EDiggerAxisMode::Z) Off.Z = 0.0f;
                    }
                    DiggerToolkit->SetBrushOffset(Off);
                }

                UpdateBrushHUDPanel();
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

    // --- Interrupt continuous sculpting on any scroll used for parameters/modes ---
    auto InterruptContinuousSculpting = [&]()
    {
        if (bIsContinuouslyApplying)
        {
            StopContinuousApplication();
        }
    };

    const bool bCtrl  = Viewport->KeyState(EKeys::LeftControl) || Viewport->KeyState(EKeys::RightControl);
    const bool bShift = Viewport->KeyState(EKeys::LeftShift)   || Viewport->KeyState(EKeys::RightShift);
    const bool bAlt   = Viewport->KeyState(EKeys::LeftAlt)     || Viewport->KeyState(EKeys::RightAlt);
    const bool bAnyModifier = bCtrl || bShift || bAlt;

    // In Digger mode:
    // - If no modifiers and we are in Sculpt mode → let the editor zoom.
    // - If any modifier OR we are in Rotate/Offset mode → consume scroll.
    const bool bIsSculptMode = (CurrentMode == EDiggerMainMode::Sculpt);
    const bool bShouldConsumeScroll = bAnyModifier || !bIsSculptMode;

    if (!bShouldConsumeScroll)
    {
        // Pure sculpt, no modifiers: allow normal viewport zoom
        return FEdMode::InputAxis(ViewportClient, Viewport, ControllerId, Key, Delta, DeltaTime);
    }

    // From here on, we are *intentionally* blocking viewport zoom.
    InterruptContinuousSculpting();

    TSharedPtr<FDiggerEdModeToolkit> IAToolkit = GetDiggerToolkit();
    if (!IAToolkit.IsValid())
    {
        // Still consume to prevent unwanted zoom
        return true;
    }

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

        // ShowBrushHUDMessage(
        //     FString::Printf(TEXT("Rotate %s: %.1f"), AxisLabel, AxisValue),
        //     FLinearColor(0.3f, 0.8f, 1.0f));

        return true;
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

        // ShowBrushHUDMessage(
        //     FString::Printf(TEXT("Offset %s: %.1f"), AxisLabel, AxisValue),
        //     FLinearColor(1.0f, 0.6f, 0.2f));

        return true;
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

            //ShowBrushHUDMessage(FString::Printf(TEXT("Radius: %.0f"), Radius));
        }
        else if (bStrengthMode)
        {
            float Strength = IAToolkit->GetBrushStrength();
            Strength = FMath::Clamp(Strength + Step, STRENGTH_MIN, STRENGTH_MAX);
            Strength = SnapIf(Strength, STRENGTH_SNAP_STEP);
            IAToolkit->SetBrushStrength(Strength);

            UpdateBrushHUDPanel();

            //ShowBrushHUDMessage(FString::Printf(TEXT("Strength: %.2f"), Strength));
        }
        else if (bForceMode)
        {
            float Force = IAToolkit->GetBrushForce();
            Force = FMath::Clamp(Force + Step, FORCE_MIN, FORCE_MAX);
            Force = SnapIf(Force, FORCE_SNAP_STEP);
            IAToolkit->SetBrushForce(Force);

            UpdateBrushHUDPanel();

            //ShowBrushHUDMessage(FString::Printf(TEXT("Strength: %.2f"), Strength));
        }
        else if (bFalloffMode)
        {
            float Falloff = IAToolkit->GetBrushFalloff();
            Falloff = FMath::Clamp(Falloff + Step, FALLOFF_MIN, FALLOFF_MAX);
            Falloff = SnapIf(Falloff, FALLOFF_SNAP_STEP);
            IAToolkit->SetBrushFalloff(Falloff);

            UpdateBrushHUDPanel();
            //ShowBrushHUDMessage(FString::Printf(TEXT("Falloff: %.2f"), Falloff));
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
