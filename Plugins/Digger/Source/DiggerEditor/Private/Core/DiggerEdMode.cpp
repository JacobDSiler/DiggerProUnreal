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

static bool ShouldSpawnHole(
    ADiggerManager* Digger,
    const FVector& BrushPos,
    float BrushRadius)
{
    if (!Digger)
        return false;

    UWorld* World = Digger->GetWorld();
    if (!World)
        return false;

    // 1) Check if brush is over landscape
    TOptional<float> TerrainHeight = Digger->GetLandscapeHeightAt_Internal(BrushPos);
    if (!TerrainHeight.IsSet())
    {
        // Not over landscape → no hole BPs above ground
        return false;
    }

    const float Height = TerrainHeight.GetValue();

    // 2) Brush must intersect the landscape surface at all
    const float VerticalDist = FMath::Abs(BrushPos.Z - Height);
    if (VerticalDist > BrushRadius)
    {
        // Too far above or below to touch the surface
        return false;
    }

    // 3) 60% BELOW RULE
    //
    // Depth = how far the TOP of the brush sphere is below the landscape
    const float Depth = Height - (BrushPos.Z + BrushRadius);

    // Allow up to 60% burial
    const float MaxAllowedDepth = BrushRadius * 0.6f;

    if (Depth > MaxAllowedDepth)
    {
        // Brush is too far underground → suppress hole spawn
        return false;
    }

    // 4) Redundancy gate
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
    
    // 2. Start Listening for new spawns
    OnLevelActorAddedHandle = GEngine->OnLevelActorAdded().AddRaw(this, &FDiggerEdMode::OnLevelActorAdded);
}



void FDiggerEdMode::Exit()
{
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
    BrushHUD.ModeText =
        (CurrentMode == EDiggerMainMode::Rotate) ? TEXT("Rotation Mode") :
        (CurrentMode == EDiggerMainMode::Offset) ? TEXT("Offset Mode") :
        TEXT("Sculpt Mode");

    BrushHUD.Radius   = BrushCache.Radius;
    BrushHUD.Strength = BrushCache.Strength;
    BrushHUD.Falloff  = BrushCache.Falloff;
    BrushHUD.Force    = BrushCache.Force;
    BrushHUD.PushMode = BrushCache.PushMode;

    BrushHUD.TimeRemaining = 1.5f;
    BrushHUD.Alpha = 1.f;
    BrushHUD.bVisible = true;
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
// Helper to detect if a new hole is redundant (overlapping an existing one)
// This prevents "Skirt Peeling" when digging deeper inside an existing cave.
static bool IsHoleRedundant(UWorld* World, const FVector& NewHolePos, float NewHoleRadius)
{
    if (!World) return false;

    // Iterate over all existing Hole Actors
    // Note: If you have a list in DiggerManager, use that instead of TActorIterator for better performance.
    // We should update this with the manager's method, by first getting the manager with the static manager finder:
    // ADiggerManager* Mgr = ADiggerManager::FindDiggerManager(WorldContextObject);
    // ADiggerManager::void GetAllHoleActors(TArray<AActor*>& OutHoles) const;

    for (TActorIterator<ADynamicHole> It(World); It; ++It)
    {
        ADynamicHole* ExistingHole = *It;
        if (!IsValid(ExistingHole)) continue;

        // Get existing hole data
        // Assuming your ADynamicHole has a method to get its radius or scale. 
        // If it uses scale for size: Radius = ExistingHole->GetActorScale3D().X * BaseSize
        float ExistingRadius = ExistingHole->CachedStroke.BrushRadius; 
        FVector ExistingPos = ExistingHole->GetActorLocation();

        // Check 2D Distance (Top-Down overlap)
        float DistSquared2D = FVector::DistSquared2D(NewHolePos, ExistingPos);
        float Dist2D = FMath::Sqrt(DistSquared2D);

        // THE GATING LOGIC:
        // If the new hole center is essentially inside the existing hole...
        if (Dist2D < ExistingRadius)
        {
            // Calculate how much "New" ground we are breaking.
            // If the new hole is completely contained within the old one, it's 100% redundant.
            // We allow a small tolerance (e.g. 10%) for widening the edge slightly.
            
            float Coverage = Dist2D + (NewHoleRadius * 0.9f); // 90% of new radius
            
            if (Coverage <= ExistingRadius)
            {
                // The new hole doesn't extend significantly past the existing rim/skirt.
                // It is redundant.
                return true; 
            }
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
    if (!ContinuousSettings.bIsValid)
        return;

    ADiggerManager* Digger = FindDiggerManager();
    if (!IsValid(Digger) || !InViewportClient || !InViewportClient->Viewport)
        return;

    // 1. Modifier check — ends the stroke
    const bool bShift = InViewportClient->Viewport->KeyState(EKeys::LeftShift)  || InViewportClient->Viewport->KeyState(EKeys::RightShift);
    const bool bCtrl  = InViewportClient->Viewport->KeyState(EKeys::LeftControl) || InViewportClient->Viewport->KeyState(EKeys::RightControl);
    const bool bAlt   = InViewportClient->Viewport->KeyState(EKeys::LeftAlt)    || InViewportClient->Viewport->KeyState(EKeys::RightAlt);

    if (bShift || bAlt)
    {
        FVector PauseHitLoc;
        FHitResult PauseHit;
        if (GetMouseWorldHit(InViewportClient, PauseHitLoc, PauseHit))
        {
            LastStrokeHitLocation = PauseHitLoc;
        }

        bMouseButtonDown          = false;
        bIsPainting               = false;
        bIsContinuouslyApplying   = false;
        ContinuousSettings.bIsValid = false;

        if (ADiggerManager* Manager = FindDiggerManager())
        {
            Manager->bIsEditorPainting = false;
        }

        StopContinuousApplication();
        return;
    }

    // 2. Raycast
    FVector MouseHitLocation;
    FHitResult Hit;
    if (!GetMouseWorldHit(InViewportClient, MouseHitLocation, Hit))
        return;

    // Use preview center as canonical brush position
    const FVector HitLocation = BrushCache.CachedBrushPreviewCenter.IsNearlyZero()
        ? MouseHitLocation
        : BrushCache.CachedBrushPreviewCenter;

    // ---------------------------------------------------------
    // ⭐ FIRST SAMPLE — ALWAYS APPLY
    // ---------------------------------------------------------
    if (!bHasLastStrokeSample)
    {
        // Apply a single brush sample immediately
        ApplyBrushWithSettings(Digger, HitLocation, Hit, BrushCache);

        LastStrokeHitLocation = HitLocation;
        bHasLastStrokeSample = true;
        return;
    }

    // ---------------------------------------------------------
    // ⭐ GLOBAL JUMP FILTER — protects ALL brush types
    // ---------------------------------------------------------
    const float DistanceSquared = FVector::DistSquared(HitLocation, LastStrokeHitLocation);
    const float JumpDistSq = FMath::Square(BrushCache.Radius);

    if (DistanceSquared > JumpDistSq)
    {
        // Skip entire tick — no voxels, no holes, no interpolation
        LastStrokeHitLocation = HitLocation;
        return;
    }

    // ---------------------------------------------------------
    // ⭐ OVERSAMPLING FILTER
    // ---------------------------------------------------------
    if (bIsContinuouslyApplying && DistanceSquared < 1.0f)
        return;

    // Snapshot settings
    FBrushCache CurrentSettings = BrushCache;

    const bool bIsLandscapeHit = Hit.GetActor() && Hit.GetActor()->IsA(ALandscapeProxy::StaticClass());

    // ---------------------------------------------------------
    // 3. Sphere / Capsule swept strokes
    // ---------------------------------------------------------
    if (CurrentSettings.BrushType == EVoxelBrushType::Sphere ||
        CurrentSettings.BrushType == EVoxelBrushType::Capsule)
    {
        if (Digger->ActiveBrush)
        {
            FBrushStroke SweptStroke;
            SweptStroke.bDig          = CurrentSettings.bFinalBrushDig;
            SweptStroke.BrushStrength = CurrentSettings.Strength;
            SweptStroke.BrushFalloff  = CurrentSettings.Falloff;
            SweptStroke.BrushType     = EVoxelBrushType::Capsule;
            SweptStroke.LightType     = CurrentSettings.LightType;

            Digger->ActiveBrush->SetupSweptStroke(
                SweptStroke,
                LastStrokeHitLocation,
                HitLocation,
                CurrentSettings.Radius
            );

            Digger->ApplyBrushToAllChunks(SweptStroke);

            if (SweptStroke.bDig && ShouldSpawnHole(Digger, SweptStroke.BrushPosition, CurrentSettings.Radius))
            {
                Digger->HandleHoleSpawn(SweptStroke);
            }
        }
    }
    else
    {
        // ---------------------------------------------------------
        // 4. Cube / non-swept interpolation
        // ---------------------------------------------------------
        const float StepSize = CurrentSettings.Radius * 0.25f;
        const float Distance = FMath::Sqrt(DistanceSquared);
        const int32 Steps    = FMath::Clamp(FMath::FloorToInt(Distance / StepSize), 1, 20);

        const FVector Direction = (HitLocation - LastStrokeHitLocation).GetSafeNormal();

        for (int32 i = 1; i <= Steps; ++i)
        {
            const FVector Pos = LastStrokeHitLocation + Direction * StepSize * i;

            FHitResult StepHit = Hit;
            StepHit.ImpactPoint = Pos;

            FBrushCache StepSettings = CurrentSettings;
            StepSettings.CachedBrushPreviewCenter = Pos;

            ApplyBrushWithSettings(Digger, Pos, StepHit, StepSettings);
        }
    }

    // 5. Update last stroke location
    LastStrokeHitLocation = HitLocation;
    bIsContinuouslyApplying = true;
}


void FDiggerEdMode::ApplyBrushWithSettings(
    ADiggerManager* Digger,
    const FVector& HitLocation,
    const FHitResult& Hit,
    const FBrushCache& Settings)
{
    if (!Digger) return;

    Digger->Modify(); // Undo support

    // ---------------------------------------------------------------------
    // 1. Push scalar settings to Manager
    // ---------------------------------------------------------------------
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

    // ---------------------------------------------------------------------
    // 2. Offset handling (local, not world)
    // ---------------------------------------------------------------------
    FVector OffsetXY(Settings.Offset.X, Settings.Offset.Y, 0.f);
    const float ZDistance = Settings.Offset.Z;
    FVector FinalOffset   = OffsetXY;

    if (!Settings.Rotation.IsNearlyZero())
        FinalOffset += Hit.ImpactNormal * ZDistance;
    else
        FinalOffset.Z = ZDistance;

    // ---------------------------------------------------------------------
    // 3. WYSIWYG brush position
    // ---------------------------------------------------------------------
    const FVector BrushPos = Settings.CachedBrushPreviewCenter;

    Digger->EditorBrushPosition = BrushPos;
    Digger->EditorBrushOffset   = FinalOffset;
    Digger->EditorBrushRotation = Settings.Rotation;

    if (Preview.IsValid())
        Digger->EditorBrushRotation = Preview->GetActorRotation();

    // ---------------------------------------------------------------------
    // 4. Apply voxel sculpting
    // ---------------------------------------------------------------------
    Digger->ApplyBrushInEditor(Settings.bFinalBrushDig);

    // ---------------------------------------------------------------------
    // 5. HOLE SPAWN GATES (Editor only)
    // ---------------------------------------------------------------------
#if WITH_EDITOR
    if (Settings.bFinalBrushDig)
    {
        UWorld* World = Digger->GetWorld();
        if (!World)
            return;

        // -------------------------------------------------------------
        // 5.a Cooldown: limit hole spawn rate
        // -------------------------------------------------------------
        static float LastHoleSpawnTime = -1000.f;
        const float HoleSpawnInterval  = 0.10f; // 100ms

        const float Now = World->GetTimeSeconds();
        if (Now - LastHoleSpawnTime < HoleSpawnInterval)
            return;

        // -------------------------------------------------------------
        // 5.b Inside existing hole? (use real shape logic)
        // -------------------------------------------------------------
        for (TActorIterator<ADynamicHole> It(World); It; ++It)
        {
            ADynamicHole* Hole = *It;
            if (!IsValid(Hole)) continue;

            if (Hole->ContainsPoint(BrushPos))
            {
                // Already inside a hole → do NOT spawn another
                return;
            }
        }

        // -------------------------------------------------------------
        // 5.c Landscape height + 60% burial rule
        // -------------------------------------------------------------
        TOptional<float> TerrainHeight = Digger->GetLandscapeHeightAt_Internal(BrushPos);
        if (!TerrainHeight.IsSet())
            return; // Not over landscape → no hole BPs

        const float Height = TerrainHeight.GetValue();

        // How far the TOP of the brush sphere is below the landscape
        const float Depth = Height - (BrushPos.Z + Settings.Radius);
        const float MaxAllowedDepth = Settings.Radius * 0.6f;

        if (Depth > MaxAllowedDepth)
        {
            // Too deep → suppress hole spawn
            return;
        }

        // -------------------------------------------------------------
        // 5.d Final redundancy / surface gate
        // -------------------------------------------------------------
        if (!ShouldSpawnHole(Digger, BrushPos, Settings.Radius))
            return;

        // -------------------------------------------------------------
        // 6. Spawn hole
        // -------------------------------------------------------------
        FBrushStroke HoleStroke;
        HoleStroke.BrushPosition = BrushPos;
        HoleStroke.BrushRadius   = Settings.Radius;
        HoleStroke.BrushType     = Settings.BrushType;
        HoleStroke.bDig          = true;

        Digger->HandleHoleSpawn(HoleStroke);
        LastHoleSpawnTime = Now;
    }
#endif // WITH_EDITOR
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

    // Ignore preview light actor if present
    if (PreviewLightActor.IsValid())
        Params.AddIgnoredActor(PreviewLightActor.Get());

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


void FDiggerEdMode::SpawnOrUpdatePreviewLight()
{
    if (!Preview.IsValid() || !PreviewLightActor.IsValid())
        return;

    PreviewLightActor->SetActorLocation(Preview->GetActorLocation());
    PreviewLightActor->SetActorRotation(Preview->GetActorRotation());
}

void FDiggerEdMode::DestroyPreviewLight()
{
    if (PreviewLightActor.IsValid())
    {
        PreviewLightActor->Destroy();
        PreviewLightActor.Reset();
    }

    PreviewLightComponent = nullptr;
}

void FDiggerEdMode::UpdatePreviewModeIndicator()
{
    if (!Preview.IsValid())
        return;

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

    // 2. Ensure preview light actor exists (PIE or viewport world)
    if (!PreviewLightActor.IsValid())
    {
        if (!ViewportClient)
            return;

        UWorld* LightWorld = ViewportClient->GetWorld();   // ⭐ same as worklight
        if (!LightWorld)
            return;

        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Params.ObjectFlags = RF_Transient;                 // ⭐ prevents gizmo
        Params.bHideFromSceneOutliner = true;             // ⭐ prevents gizmo

        AActor* LightActor = LightWorld->SpawnActor<AActor>(
            AActor::StaticClass(),
            FVector::ZeroVector,
            FRotator::ZeroRotator,
            Params
        );

        if (!LightActor)
            return;

        PreviewLightActor = LightActor;
        LightActor->SetActorEnableCollision(false);

        // Create the light component
        PreviewLightComponent = NewObject<UPointLightComponent>(LightActor);
        PreviewLightComponent->RegisterComponent();
        PreviewLightComponent->SetMobility(EComponentMobility::Movable);
        LightActor->SetRootComponent(PreviewLightComponent);

        // Initial settings
        PreviewLightComponent->SetIntensity(5000.f);
        PreviewLightComponent->SetAttenuationRadius(800.f);
        PreviewLightComponent->SetCastShadows(false);
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

    SpawnOrUpdatePreviewLight();

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
    Preview->UpdatePreview(
        Center,
        Extents,
        Falloff,
        Stroke.bDig,
        P.CellSize,
        PreviewShapeType,
        Rotation);

    BrushCache.CachedBrushPreviewCenter = Center;
    BrushCache.Radius = P.RadiusXYZ.X;
    LastStrokePreviewCenter = Center;

    // ---------------------------------------------------------
    // 8. COLOR & LIGHTING
    // ---------------------------------------------------------
    if (Settings)
    {
        FLinearColor TargetColor = P.bAdd ? Settings->BrushColorDig : Settings->BrushColorAdd;

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
                MID->SetVectorParameterValue(FName("PreviewColor"), TargetColor);
            }
        }

        if (UPointLightComponent* LightComp = Preview->FindComponentByClass<UPointLightComponent>())
        {
            LightComp->SetIntensity(Settings->BrushLightIntensity);
            LightComp->SetAttenuationRadius(Settings->BrushLightAttenuationRadius);

            if (Settings->bMatchLightColorToBrush)
            {
                FLinearColor LightColor = TargetColor;
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
            Key == EKeys::X || Key == EKeys::Y || Key == EKeys::Z)
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
            // We let InputAxis(MouseWheelAxis) handle the actual parameter changes.
            // Here we just prevent the editor camera from zooming.
            InterruptContinuousSculpting();
            return true; // consume scroll key → no zoom
        }

        // Not consuming: let the editor zoom normally.
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
            UpdateBrushHUDPanel();

//            ShowBrushHUDMessage(TEXT("Rotation Mode"), FLinearColor(0.3f, 0.8f, 1.0f));
        }
        else
        {
            CurrentMode = EDiggerMainMode::Sculpt;
            CurrentAxis = EDiggerAxisMode::None;
            UpdateBrushHUDPanel();

            //          ShowBrushHUDMessage(TEXT("Sculpt Mode"), FLinearColor(0.6f, 1.0f, 0.6f));
        }

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
            UpdateBrushHUDPanel();

//            ShowBrushHUDMessage(TEXT("Offset Mode"), FLinearColor(1.0f, 0.6f, 0.2f));
        }
        else
        {
            CurrentMode = EDiggerMainMode::Sculpt;
            CurrentAxis = EDiggerAxisMode::None;
            UpdateBrushHUDPanel();

//            ShowBrushHUDMessage(TEXT("Sculpt Mode"), FLinearColor(0.6f, 1.0f, 0.6f));
        }

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

            const TCHAR* AxisName =
                (Key == EKeys::X) ? TEXT("X") :
                (Key == EKeys::Y) ? TEXT("Y") : TEXT("Z");

            const bool bRot = (CurrentMode == EDiggerMainMode::Rotate);

            UpdateBrushHUDPanel();

            // ShowBrushHUDMessage(
            //     FString::Printf(TEXT("%s Axis: %s"),
            //         bRot ? TEXT("Rotate") : TEXT("Offset"),
            //         AxisName),
            //     bRot ? FLinearColor(0.3f, 0.8f, 1.0f)
            //          : FLinearColor(1.0f, 0.6f, 0.2f));

            UpdatePreviewModeIndicator();
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

        // ShowBrushHUDMessage(
        //     bPaintingEnabled ? TEXT("Paint Mode: ON") : TEXT("Paint Mode: OFF"),
        //     bPaintingEnabled ? FLinearColor(0.6f, 1.0f, 0.6f) : FLinearColor(1.0f, 0.4f, 0.4f));

        return true;
    }

    // ---------------------------------------------------------
    // 7. PAINTING LOGIC (LMB / RMB) — unchanged from your version
    // ---------------------------------------------------------
    if (bPaintingEnabled &&
        (Key == EKeys::LeftMouseButton || Key == EKeys::RightMouseButton))
    {
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

            // CTRL + CLICK = SAMPLE NORMAL, DO NOT PAINT
            if (bCtrlDown)
            {
                if (TSharedPtr<FDiggerEdModeToolkit> DiggerToolkit = GetDiggerToolkit())
                {
                    const FVector Normal = Hit.ImpactNormal.GetSafeNormal();
                    const FQuat AlignRotation = FQuat::FindBetweenNormals(FVector::UpVector, Normal);
                    const FRotator NewRot = AlignRotation.Rotator();
                    DiggerToolkit->SetBrushRotation(NewRot);
                }

                UpdateBrushHUDPanel();

              //  ShowBrushHUDMessage(TEXT("Sampled Surface Normal"), FLinearColor(0.8f, 0.8f, 1.0f));
                return true;
            }

            DeselectAllSceneActors();

            const bool bRightClick = (Key == EKeys::RightMouseButton);
            UpdateBrushSettingsFromUI(Hit, bRightClick);

            LastStrokeHitLocation = HitLocation;
            LastPaintLocation     = FVector2D(Viewport->GetMouseX(), Viewport->GetMouseY());

            bMouseButtonDown            = true;
            bIsPainting                 = true;
            bIsContinuouslyApplying     = true;
            ContinuousSettings.bIsValid = true;
            ContinuousSettings.bRightClick = bRightClick;

            CurrentMode = EDiggerMainMode::Sculpt;
            CurrentAxis = EDiggerAxisMode::None;
            UpdatePreviewModeIndicator();

            if (ADiggerManager* Digger = FindDiggerManager())
            {
                Digger->bIsEditorPainting = true;
                ApplyBrushWithSettings(Digger, HitLocation, Hit, BrushCache);
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
        DrawLine(FString::Printf(TEXT("Radius: %.1f"), BrushHUD.Radius));
        DrawLine(FString::Printf(TEXT("Strength: %.2f"), BrushHUD.Strength));
        DrawLine(FString::Printf(TEXT("Falloff: %.2f"), BrushHUD.Falloff));
        DrawLine(FString::Printf(TEXT("Force: %.2f (%s)"),
            BrushHUD.Force,
            *StaticEnum<EDiggerPushMode>()->GetNameStringByValue((int64)BrushHUD.PushMode)
        ));
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

bool FDiggerEdMode::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
    // While painting mode is on, swallow clicks so the editor doesn't open menus or select actors.
    if (bPaintingEnabled)
    {
        return true;
    }

    // Single‑click brush application when NOT in painting mode
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

        // Debug brush: place once and bail
        if (BrushCache.BrushType == EVoxelBrushType::Debug)
        {
            Digger->DebugBrushPlacement(HitLocation);
            return true;
        }

        LastStrokeHitLocation = HitLocation;

        ApplyBrushWithSettings(Digger, HitLocation, Hit, BrushCache);
        return true;
    }

    return false;
}

bool FDiggerEdMode::HandleClickSimple(const FVector& RayOrigin, const FVector& RayDirection)
{
    return false; // Stub
}

#undef LOCTEXT_NAMESPACE
