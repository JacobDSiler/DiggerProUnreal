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

static constexpr float BASE_RADIUS_STEP   = 8.f;
static constexpr float BASE_STRENGTH_STEP = 0.02f;
static constexpr float BASE_FALLOFF_STEP  = 0.02f;

static constexpr float ACCEL_GAIN   = 0.35f;
static constexpr float SPEED_CLAMP  = 30.f;

// Optional snapping
static constexpr float RADIUS_SNAP_STEP   = 0.f;   // set to voxel size if desired
static constexpr float FALLOFF_SNAP_STEP  = 0.f;   // e.g. 0.05f
static constexpr float STRENGTH_SNAP_STEP = 0.f;   // e.g. 0.05f

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

void FDiggerEdMode::Enter()
{
    FEdMode::Enter();
    DeselectAllSceneActors();
    EnsureDiggerPrereqs();

    if (ADiggerManager* Manager = FindDiggerManager())
    {
        Manager->OnModifierBlocked.AddRaw(this, &FDiggerEdMode::HandleModifierBlocked);
    }

    if (!Toolkit.IsValid())
    {
        FDiggerEditorAccess::SetEditorModeActive(true);
        Toolkit = MakeShareable(new FDiggerEdModeToolkit);
        Toolkit->Init(Owner->GetToolkitHost());
    }

    EnsurePreviewExists();
}


void FDiggerEdMode::Exit()
{
    if (ADiggerManager* Manager = FindDiggerManager())
    {
        Manager->OnModifierBlocked.RemoveAll(this);
    }

    DestroyPreview();

    if (Toolkit.IsValid())
    {
        TSharedPtr<FDiggerEdModeToolkit> DiggerToolkit = StaticCastSharedPtr<FDiggerEdModeToolkit>(Toolkit);
        if (DiggerToolkit.IsValid())
        {
            DiggerToolkit->DestroyWorklight();
        }

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


bool FDiggerEdMode::CapturedMouseMove(FEditorViewportClient* InViewportClient, FViewport* InViewport,
                                      int32 InMouseX, int32 InMouseY)
{
    UpdatePreviewAtCursor(InViewportClient);

    if (!bIsPainting || !bPaintingEnabled)
        return false;

    const bool bAlt =
        InViewport->KeyState(EKeys::LeftAlt) || InViewport->KeyState(EKeys::RightAlt);

    // ⭐ Do NOT paint while orbiting
    if (bAlt)
        return false;

    FVector2D CurrentMousePos(InMouseX, InMouseY);
    const float DistanceMoved = FVector2D::Distance(CurrentMousePos, LastPaintLocation);

    if (DistanceMoved > 2.0f)
    {
        ApplyContinuousBrush(InViewportClient);
        LastPaintLocation = CurrentMousePos;
    }

    return true;
}


// -----------------------------------------------------------------------------------
// BRUSH HUD MESSAGES
// -----------------------------------------------------------------------------------

void FDiggerEdMode::ShowBrushHUDMessage(const FString& Msg)
{
    BrushHUD.Text = Msg;
    BrushHUD.TimeRemaining = 0.6f; // seconds
}

void FDiggerEdMode::HandleModifierBlocked(bool bBlocked)
{
    if (bBlocked)
        ShowBrushHUDMessage(TEXT("Painting paused (modifier held)"));
    else
        ShowBrushHUDMessage(TEXT("Painting active"));
}



// -----------------------------------------------------------------------------------
// BRUSH APPLICATION
// -----------------------------------------------------------------------------------

void FDiggerEdMode::UpdateBrushSettingsFromUI(const FHitResult& TraceHit, bool bRightClick)
{
    TSharedPtr<FDiggerEdModeToolkit> DiggerToolkit = GetDiggerToolkit();
    if (!DiggerToolkit.IsValid()) return;

    BrushCache.Radius   = DiggerToolkit->GetBrushRadius();
    BrushCache.Falloff  = DiggerToolkit->GetBrushFalloff();
    BrushCache.Strength = DiggerToolkit->GetBrushStrength();
    BrushCache.LightType = DiggerToolkit->GetCurrentLightType();

    const bool bUiDig = DiggerToolkit->IsDigMode();
    BrushCache.bFinalBrushDig = bRightClick ? !bUiDig : bUiDig;

    // Update EditorBrushDig in the DiggerManager so it knows it isn't digging.
    if (ADiggerManager* Digger = FindDiggerManager())
    {
        Digger->EditorBrushDig = BrushCache.bFinalBrushDig;
    }


    BrushCache.Rotation = DiggerToolkit->GetBrushRotation();
    if (DiggerToolkit->UseSurfaceNormalRotation())
    {
        const FVector Normal = TraceHit.ImpactNormal.GetSafeNormal();
        const FQuat AlignRotation = FQuat::FindBetweenNormals(FVector::UpVector, Normal);
        BrushCache.Rotation = (AlignRotation * BrushCache.Rotation.Quaternion()).Rotator();
    }

    BrushCache.bIsFilled   = DiggerToolkit->GetBrushIsFilled();
    BrushCache.Angle       = DiggerToolkit->GetBrushAngle();
    BrushCache.BrushType   = DiggerToolkit->GetCurrentBrushType();
    BrushCache.bHiddenSeam = DiggerToolkit->GetHiddenSeam();

    BrushCache.bUseAdvancedCube = DiggerToolkit->IsUsingAdvancedCubeBrush();
    BrushCache.CubeHalfExtentX  = DiggerToolkit->GetAdvancedCubeHalfExtentX();
    BrushCache.CubeHalfExtentY  = DiggerToolkit->GetAdvancedCubeHalfExtentY();
    BrushCache.CubeHalfExtentZ  = DiggerToolkit->GetAdvancedCubeHalfExtentZ();

    BrushCache.Offset = DiggerToolkit->GetBrushOffset();

    const float SafeRadius = FMath::Max(BrushCache.Radius, 10.0f);
    StrokeSpacing = SafeRadius * 0.5f;

    // Use the WYSIWYG center, not the surface hit
    LastStrokeHitLocation    = BrushCache.CachedBrushPreviewCenter;
    LastStrokePreviewCenter  = BrushCache.CachedBrushPreviewCenter;
}

void FDiggerEdMode::ApplyContinuousBrush(FEditorViewportClient* InViewportClient)
{
    if (!ContinuousSettings.bIsValid) return;

    FVector HitLocation;
    FHitResult Hit;
    if (!GetMouseWorldHit(InViewportClient, HitLocation, Hit)) return;

    // 1. Check Distance (don't paint if we haven't moved)
    float DistanceSquared = FVector::DistSquared(HitLocation, LastStrokeHitLocation);
    if (DistanceSquared < 1.0f) return; 

    ADiggerManager* Digger = FindDiggerManager();
    if (!IsValid(Digger)) return;

    // 2. Refresh Cache settings (in case modifiers changed during drag)
    // We rely on the BrushCache updated in InputKey/MouseMove, but we ensure the center is current.
    BrushCache.CachedBrushPreviewCenter = HitLocation;

    FBrushCache CurrentSettings = BrushCache; 
    
    // --- BRANCH A: SPHERE / CAPSULE (Swept Optimization) ---
    if (CurrentSettings.BrushType == EVoxelBrushType::Sphere || 
        CurrentSettings.BrushType == EVoxelBrushType::Capsule) 
    {
        if (Digger->ActiveBrush)
        {
            FBrushStroke SweptStroke;
        
            // COPY SETTINGS CRITICAL FOR ADD/REMOVE
            SweptStroke.bDig          = CurrentSettings.bFinalBrushDig; 
            SweptStroke.BrushStrength = CurrentSettings.Strength;
            SweptStroke.BrushFalloff  = CurrentSettings.Falloff;
            SweptStroke.BrushType     = EVoxelBrushType::Capsule; // Force capsule for sweep
            SweptStroke.LightType     = CurrentSettings.LightType; // Don't forget lights!

            // Setup Geometry (Start -> End)
            Digger->ActiveBrush->SetupSweptStroke(
                SweptStroke, 
                LastStrokeHitLocation, 
                HitLocation, 
                CurrentSettings.Radius
            );
        
            // A. APPLY VOXELS
            Digger->ApplyBrushToAllChunks(SweptStroke);

            // B. APPLY LANDSCAPE HOLE (This was missing!)
            if (SweptStroke.bDig)
            {
                // This ensures the hole follows the drag path
                Digger->HandleHoleSpawn(SweptStroke);
            }
        }
    }
    // --- BRANCH B: CUBE / OTHERS (Stepped Interpolation) ---
    else
    {
        float StepSize = CurrentSettings.Radius * 0.25f; // Tighter steps for cubes
        float Distance = FMath::Sqrt(DistanceSquared);
        int32 Steps = FMath::Clamp(FMath::FloorToInt(Distance / StepSize), 1, 20);
        
        FVector Direction = (HitLocation - LastStrokeHitLocation).GetSafeNormal();
        
        for (int i = 1; i <= Steps; ++i)
        {
            FVector Pos = LastStrokeHitLocation + Direction * StepSize * i;
            
            // Create a fake hit for the intermediate step
            FHitResult StepHit = Hit;
            StepHit.ImpactPoint = Pos;
            
            // Temporarily update cache center for this step
            FBrushCache StepSettings = CurrentSettings;
            StepSettings.CachedBrushPreviewCenter = Pos;

            ApplyBrushWithSettings(Digger, Pos, StepHit, StepSettings);
        }
    }

    // 3. Update Tracker for next frame
    LastStrokeHitLocation = HitLocation;
}


void FDiggerEdMode::ApplyBrushWithSettings(ADiggerManager* Digger, const FVector& HitLocation, const FHitResult& Hit, const FBrushCache& Settings)
{
    if (!Digger) return;

    Digger->Modify(); // Undo support

    // 1. Push settings to Manager (Logic from your file, kept intact)
    Digger->EditorBrushRadius = Settings.Radius;
    Digger->EditorBrushDig = Settings.bFinalBrushDig;
    Digger->EditorBrushRotation = Settings.Rotation;
    Digger->EditorBrushIsFilled = Settings.bIsFilled;
    Digger->EditorBrushAngle = Settings.Angle;
    Digger->EditorBrushType = Settings.BrushType;
    Digger->EditorBrushLightType = Settings.LightType;
    Digger->EditorBrushHiddenSeam = Settings.bHiddenSeam;
    Digger->EditorbUseAdvancedCubeBrush = Settings.bUseAdvancedCube;
    Digger->EditorCubeHalfExtentX = Settings.CubeHalfExtentX;
    Digger->EditorCubeHalfExtentY = Settings.CubeHalfExtentY;
    Digger->EditorCubeHalfExtentZ = Settings.CubeHalfExtentZ;

    // Handle Offsets
    FVector OffsetXY(Settings.Offset.X, Settings.Offset.Y, 0.f);
    float ZDistance = Settings.Offset.Z;
    FVector FinalOffset = OffsetXY;

    if (!Settings.Rotation.IsNearlyZero())
        FinalOffset += Hit.ImpactNormal * ZDistance;
    else
        FinalOffset.Z = ZDistance;

    Digger->EditorBrushOffset = FinalOffset;
    
    // Use the WYSIWYG center we calculated in UpdateBrushSettingsFromUI/Preview
    // (If CachedBrushPreviewCenter isn't set, fallback to HitLocation)
    Digger->EditorBrushPosition = Settings.CachedBrushPreviewCenter.IsNearlyZero() ? HitLocation : Settings.CachedBrushPreviewCenter;

    // 2. APPLY VOXELS
    Digger->ApplyBrushInEditor(Settings.bFinalBrushDig);

    // 3. APPLY HOLE (Add this check)
    // Only if digging and we are reasonably sure we are on landscape
    // Ensure this logic is inside FDiggerEdMode::ApplyBrushWithSettings
    if (Settings.bFinalBrushDig)
    {
        // If we are digging, we MUST try to spawn a hole.
        // The manager will ignore it if no landscape is hit, so it's safe to call.
        FBrushStroke HoleStroke;
        HoleStroke.BrushPosition = Digger->EditorBrushPosition;
        HoleStroke.BrushRadius   = Settings.Radius;
        HoleStroke.BrushType     = Settings.BrushType;
        HoleStroke.bDig          = true;
    
        Digger->HandleHoleSpawn(HoleStroke);
    }
}


// -----------------------------------------------------------------------------------
// RAYCASTING / PREVIEW
// -----------------------------------------------------------------------------------

bool FDiggerEdMode::GetMouseWorldHit(FEditorViewportClient* ViewportClient, FVector& OutHitLocation, FHitResult& OutHit)
{
    if (!ViewportClient || !ViewportClient->Viewport) return false;

    FIntPoint MousePos;
    ViewportClient->Viewport->GetMousePos(MousePos);

    FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
        ViewportClient->Viewport,
        ViewportClient->GetScene(),
        ViewportClient->EngineShowFlags));

    FSceneView* SceneView = ViewportClient->CalcSceneView(&ViewFamily);
    if (!SceneView) return false;

    FVector WorldOrigin, WorldDirection;
    SceneView->DeprojectFVector2D(MousePos, WorldOrigin, WorldDirection);

    const FVector TraceStart = WorldOrigin;
    const FVector TraceEnd   = WorldOrigin + WorldDirection * 100000.f;

    ADiggerManager* Digger = FindDiggerManager();
    if (!IsValid(Digger))
    {
        if (DiggerDebug::Casts() || DiggerDebug::Manager())
        {
            UE_LOG(LogTemp, Error, TEXT("No DiggerManager found in DiggerEdMode::GetMouseWorldHit!"));
        }
        return false;
    }

    // Ensure ActiveBrush exists and is initialized
    if (!IsValid(Digger->ActiveBrush))
    {
        if (UWorld* World = ViewportClient->GetWorld())
        {
            Digger->ActiveBrush = NewObject<UVoxelBrushShape>(Digger, UVoxelBrushShape::StaticClass());
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

    // Prefer SmartTrace
    if (IsValid(Digger->ActiveBrush))
    {
        const FHitResult SmartHit = Digger->ActiveBrush->SmartTrace(TraceStart, TraceEnd);
        if (SmartHit.bBlockingHit)
        {
            OutHit = SmartHit;
            OutHitLocation = SmartHit.ImpactPoint;

            if (DiggerDebug::Casts())
            {
                UE_LOG(LogTemp, Warning, TEXT("SmartTrace returned: %s"), *SmartHit.ImpactPoint.ToString());
            }
            return true;
        }
    }

    // Fallback line trace
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (World)
    {
        FCollisionQueryParams Params(SCENE_QUERY_STAT(DiggerEdMode_MouseTrace), true);
        Params.bReturnPhysicalMaterial = false;
        Params.AddIgnoredActor(Preview.IsValid() ? Preview.Get() : nullptr);

        FHitResult FallbackHit;
        if (World->LineTraceSingleByChannel(FallbackHit, TraceStart, TraceEnd, ECC_Visibility, Params) &&
            FallbackHit.bBlockingHit)
        {
            OutHit = FallbackHit;
            OutHitLocation = FallbackHit.ImpactPoint;

            if (DiggerDebug::Casts())
            {
                UE_LOG(LogTemp, Error, TEXT("Line Trace Fallback returned hit: %s"), *FallbackHit.ImpactPoint.ToString());
            }
            return true;
        }
    }

    return false;
}

bool FDiggerEdMode::TraceUnderCursor(FEditorViewportClient* InViewportClient, FHitResult& OutHit)
{
    FVector Loc;
    return GetMouseWorldHit(InViewportClient, Loc, OutHit);
}

void FDiggerEdMode::EnsurePreviewExists()
{
    if (Preview.IsValid()) return;

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World) return;

    ABrushPreviewActor* Actor = World->SpawnActor<ABrushPreviewActor>();
    if (!Actor) return;

    // Use defaults from UDiggerEditorSettings
    Actor->Initialize(nullptr, nullptr);
    Actor->SetVisible(true);

    Preview = Actor;
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
    EnsurePreviewExists();
    if (!Preview.IsValid())
        return;

    // ---------------------------------------------------------
    // 1. Trace under cursor
    // ---------------------------------------------------------
    FHitResult Hit;
    if (!TraceUnderCursor(InViewportClient, Hit))
    {
        Preview->SetVisible(false);
        return;
    }

    Preview->SetVisible(true);

    // ---------------------------------------------------------
    // 2. Gather UI brush params
    // ---------------------------------------------------------
    FBrushUIParams P = GetCurrentBrushUI();

    // Surface-normal alignment (optional)
    if (TSharedPtr<FDiggerEdModeToolkit> LocalToolkit = GetDiggerToolkit())
    {
        if (LocalToolkit->UseSurfaceNormalRotation())
        {
            const FVector Normal = Hit.ImpactNormal.GetSafeNormal();
            const FQuat AlignRotation = FQuat::FindBetweenNormals(FVector::UpVector, Normal);
            P.Rotation = (AlignRotation * P.Rotation.Quaternion()).Rotator();
        }
    }

    // ---------------------------------------------------------
    // 3. Apply offset in rotated space
    // ---------------------------------------------------------
    FVector FinalOffset = P.Offset;
    if (!P.Rotation.IsNearlyZero())
    {
        FinalOffset = P.Rotation.RotateVector(P.Offset);
    }

    // ---------------------------------------------------------
    // 4. WYSIWYG CENTER:
    //    The brush center is exactly the surface hit + offset.
    //    NO subtracting radius. NO pushing into terrain.
    // ---------------------------------------------------------
    const float BrushRadius = P.RadiusXYZ.X;

    FVector Center =
        Hit.Location   // surface hit
        + FinalOffset; // user offset

    // ---------------------------------------------------------
    // 5. Build stroke (for preview + brush refinement)
    // ---------------------------------------------------------
    FBrushStroke Stroke;
    Stroke.BrushPosition = Center;
    Stroke.BrushOffset   = FinalOffset;
    Stroke.BrushRadius   = BrushRadius;
    Stroke.BrushFalloff  = P.Falloff;
    Stroke.bDig          = P.bAdd;
    Stroke.BrushRotation = P.Rotation;
    Stroke.BrushLength   = P.RadiusXYZ.Z;

    // Brush type
    EVoxelBrushType BrushType = EVoxelBrushType::Sphere;
    if (TSharedPtr<FDiggerEdModeToolkit> FoundToolkit = GetDiggerToolkit())
    {
        BrushType = FoundToolkit->GetCurrentBrushType();
    }
    Stroke.BrushType = BrushType;

    // ---------------------------------------------------------
    // 6. Let the active brush refine extents/rotation (NOT center)
    // ---------------------------------------------------------
    FVector Extents(BrushRadius);
    FQuat Rotation = P.Rotation.Quaternion();
    float Falloff = P.Falloff;
    EBrushPreviewShape PreviewShapeType = EBrushPreviewShape::Sphere;

    if (ADiggerManager* Digger = FindDiggerManager())
    {
        if (!IsValid(Digger->ActiveBrush))
        {
            if (UWorld* World = InViewportClient ? InViewportClient->GetWorld() : nullptr)
            {
                Digger->ActiveBrush = NewObject<UVoxelBrushShape>(Digger, UVoxelBrushShape::StaticClass());
                if (IsValid(Digger->ActiveBrush))
                {
                    Digger->ActiveBrush->InitializeBrush(
                        BrushType,
                        Digger->ActiveBrush->GetBrushSize(),
                        Digger->ActiveBrush->GetBrushLocation(),
                        Digger);

                    Digger->InitializeBrushShapes();
                }
            }
        }

        if (IsValid(Digger->ActiveBrush))
        {
            // Brush may refine extents/rotation/falloff/type — but NOT center
            Digger->ActiveBrush->GetPreviewData(
                Center,     // stays the same
                Extents,
                Rotation,
                Falloff,
                BrushType,
                Stroke);

            switch (BrushType)
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
    // 7. Vertical correction:
    //    Keep preview glued to deformed terrain.
    //    DO NOT subtract radius here either.
    // ---------------------------------------------------------
    if (UWorld* World = InViewportClient ? InViewportClient->GetWorld() : nullptr)
    {
        FHitResult VerticalHit;
        const FVector XY(Center.X, Center.Y, 0.f);

        const FVector Start(XY.X, XY.Y, 100000.f);
        const FVector End  (XY.X, XY.Y, -100000.f);

        if (World->LineTraceSingleByChannel(VerticalHit, Start, End, ECC_Visibility))
        {
            Center =
                VerticalHit.ImpactPoint
                + FinalOffset;

            Stroke.BrushPosition = Center;
        }
    }

    // ---------------------------------------------------------
    // 8. Apply preview
    // ---------------------------------------------------------
    Preview->UpdatePreview(
        Center,
        Extents,
        Falloff,
        Stroke.bDig,
        P.CellSize,
        PreviewShapeType,
        Rotation);

    // ---------------------------------------------------------
    // 9. Cache WYSIWYG center for digging
    // ---------------------------------------------------------
    BrushCache.CachedBrushPreviewCenter = Center;
    LastStrokePreviewCenter             = Center;

    if (DiggerDebug::Brush())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("Preview Center: %s  Extents: %s  Type=%d"),
            *Center.ToString(), *Extents.ToString(), (int32)BrushType);
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
    // Painting is handled via InputKey + CapturedMouseMove
    return FEdMode::InputDelta(InViewportClient, InViewport, InDrag, InRot, InScale);
}

bool FDiggerEdMode::StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
    const bool bAlt =
        InViewport->KeyState(EKeys::LeftAlt) || InViewport->KeyState(EKeys::RightAlt);

    if (bAlt)
        return false; // let viewport handle orbit drag

    if (bPaintingEnabled)
    {
        bIsDragging = true;
        return true;
    }

    return FEdMode::StartTracking(InViewportClient, InViewport);
}


bool FDiggerEdMode::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
    // 1. Safety Checks
    if (!ViewportClient || !Viewport) return false;

    // 2. Check Modifiers
    // We strictly allow Alt to pass through so the user can Orbit the camera
    const bool bAlt = Viewport->KeyState(EKeys::LeftAlt) || Viewport->KeyState(EKeys::RightAlt);
    if (bAlt)
    {
        return false; // Let the Editor handle camera movement
    }

    // 3. Toggle Painting Mode (Optional Hotkey)
    if (Key == EKeys::P && Event == IE_Pressed)
    {
        bPaintingEnabled = !bPaintingEnabled;
        UE_LOG(LogTemp, Log, TEXT("Painting Mode: %s"), bPaintingEnabled ? TEXT("Enabled") : TEXT("Disabled"));
        return true;
    }

    // 4. Handle Mouse Buttons (The Meat)
    if (bPaintingEnabled && (Key == EKeys::LeftMouseButton || Key == EKeys::RightMouseButton))
    {
        if (Event == IE_Pressed)
        {
            // A. Raycast to see what we are hitting
            FVector HitLocation;
            FHitResult Hit;
            
            // If we hit nothing (the sky), let the editor handle it (deselect, etc.)
            if (!GetMouseWorldHit(ViewportClient, HitLocation, Hit))
            {
                return false; 
            }

            // B. Deselect other actors so we don't accidentally drag a Tree while digging
            DeselectAllSceneActors();

            // C. Setup Brush Settings
            // Right Click = Invert current tool (Dig vs Fill)
            const bool bRightClick = (Key == EKeys::RightMouseButton);
            UpdateBrushSettingsFromUI(Hit, bRightClick);

            // --- BULLETPROOF RESET START ---
            // Critical: Reset the "Last" location to "Current" location.
            // This stops the brush from calculating a massive sweep from the previous stroke.
            LastStrokeHitLocation = HitLocation;
            LastPaintLocation = FVector2D(Viewport->GetMouseX(), Viewport->GetMouseY());
            // -------------------------------

            // D. Set State Flags
            bMouseButtonDown = true;
            bIsPainting = true;
            ContinuousSettings.bIsValid = true;
            ContinuousSettings.bRightClick = bRightClick;

            // E. Notify Manager (Important for some internal state)
            if (ADiggerManager* Digger = FindDiggerManager())
            {
                Digger->bIsEditorPainting = true;

                // F. DEBUG TOOL CHECK
                if (BrushCache.BrushType == EVoxelBrushType::Custom)
                {
                    Digger->DebugBrushPlacement(HitLocation);
                    return true; // Consume input, done.
                }

                // G. INSTANT APPLICATION
                // Apply the brush NOW. Don't wait for the mouse to move.
                // This ensures a single click creates a single hole.
                ApplyBrushWithSettings(Digger, HitLocation, Hit, BrushCache);
            }

            // Return true to "Consume" the input. 
            // This tells Unreal: "I handled this click, don't select objects or move the camera."
            return true; 
        }
        else if (Event == IE_Released)
        {
            // A. Clear Flags
            bMouseButtonDown = false;
            bIsPainting = false;
            ContinuousSettings.bIsValid = false;

            // B. Notify Manager
            if (ADiggerManager* Digger = FindDiggerManager())
            {
                Digger->bIsEditorPainting = false;
            }

            // C. Cleanup Continuous Logic
            StopContinuousApplication();

            return true; // Consume the release
        }
    }

    // 5. Handle Tool Size Adjustment (Shift + Scroll)
    // We let InputAxis handle the scroll, but we might want to block other Shift+Click actions here.
    const bool bShift = Viewport->KeyState(EKeys::LeftShift) || Viewport->KeyState(EKeys::RightShift);
    if (bShift && Key.IsMouseButton())
    {
        // Return true to block Shift+Click selecting objects while we are resizing the brush
        return true; 
    }
    
    // 6. Fallback to base class (Handles standard editor hotkeys like Delete, etc.)
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

bool FDiggerEdMode::InputAxis(FEditorViewportClient* ViewportClient, FViewport* Viewport,
                              int32 ControllerId, FKey Key, float Delta, float DeltaTime)
{
    TSharedPtr<FDiggerEdModeToolkit> DiggerToolkit = GetDiggerToolkit();
    if (!DiggerToolkit.IsValid() || !Viewport)
        return false;

    const bool bShift = Viewport->KeyState(EKeys::LeftShift)   || Viewport->KeyState(EKeys::RightShift);
    const bool bCtrl  = Viewport->KeyState(EKeys::LeftControl) || Viewport->KeyState(EKeys::RightControl);
    const bool bAlt   = Viewport->KeyState(EKeys::LeftAlt)     || Viewport->KeyState(EKeys::RightAlt);

    // Only intercept scroll wheel
    if (Key == EKeys::MouseWheelAxis)
    {

        // 🔹 Check Settings For manually set Scroll Sensitivity
        const UDiggerEditorSettings* Settings = UDiggerEditorSettings::Get();
        const float Speed = Settings ? Settings->ScrollSpeed : ScrollImpulseScale;


        // 🔹 Add impulse to velocity (smooth momentum system)
        ScrollVelocity += Delta * Speed;

        // 🔹 If modifiers are active → consume the scroll
        //    (Tick will apply the smooth parameter changes)
        if (bShift || bCtrl || bAlt)
            return true;

        // 🔹 No modifiers → normal viewport zoom
        return FEdMode::InputAxis(ViewportClient, Viewport, ControllerId, Key, Delta, DeltaTime);
    }

    // Default behavior for all other axes
    return FEdMode::InputAxis(ViewportClient, Viewport, ControllerId, Key, Delta, DeltaTime);
}




bool FDiggerEdMode::ShouldApplyContinuously() const
{
    return bIsContinuouslyApplying && bMouseButtonDown && ContinuousSettings.bIsValid;
}

void FDiggerEdMode::StartContinuousApplication(const FViewportClick& Click)
{
    // Keep this simple and robust: only allow if settings are valid and not explicitly blocked
    if (!ContinuousSettings.bIsValid)
    {
        return;
    }

    // If your ContinuousSettings has a bCtrlPressed flag, you can gate here:
    // if (ContinuousSettings.bCtrlPressed) return;

    bIsContinuouslyApplying = true;
    ContinuousApplicationTimer = 0.0f;
}

void FDiggerEdMode::StopContinuousApplication()
{
    bIsContinuouslyApplying = false;
    ContinuousApplicationTimer = 0.0f;
    ContinuousSettings.bIsValid = false;

    if (TSharedPtr<FDiggerEdModeToolkit> DiggerToolkit = GetDiggerToolkit())
    {
        DiggerToolkit->SetTemporaryDigOverride(TOptional<bool>());
    }
}

void FDiggerEdMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
    FEdMode::Tick(ViewportClient, DeltaTime);

    if (BrushHUD.TimeRemaining > 0.f) 
    {
        BrushHUD.TimeRemaining -= DeltaTime;
        if (BrushHUD.TimeRemaining < 0.f)
            BrushHUD.TimeRemaining = 0.f; 
    }

    UpdatePreviewAtCursor(ViewportClient);

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

    // ---------------------------------------------------------
    // 🔥 NEW: Smooth momentum-based scroll adjustments
    // ---------------------------------------------------------

    if (!ViewportClient || !ViewportClient->Viewport)
        return;

    // If velocity is basically zero → nothing to do
    if (FMath::IsNearlyZero(ScrollVelocity, 0.01f))
        return;

    // Smoothly decay velocity toward zero
    ScrollVelocity = FMath::FInterpTo(ScrollVelocity, 0.0f, DeltaTime, ScrollDecayRate);

    // Determine active modifier
    const bool bShift = ViewportClient->Viewport->KeyState(EKeys::LeftShift)   ||
                        ViewportClient->Viewport->KeyState(EKeys::RightShift);
    const bool bCtrl  = ViewportClient->Viewport->KeyState(EKeys::LeftControl) ||
                        ViewportClient->Viewport->KeyState(EKeys::RightControl);
    const bool bAlt   = ViewportClient->Viewport->KeyState(EKeys::LeftAlt)     ||
                        ViewportClient->Viewport->KeyState(EKeys::RightAlt);

    TSharedPtr<FDiggerEdModeToolkit> DiggerToolkit = GetDiggerToolkit();
    if (!DiggerToolkit.IsValid())
        return;

    // Apply velocity → parameter change
    if (bShift && !bCtrl && !bAlt)
    {
        float R = DiggerToolkit->GetBrushRadius();
        R += ScrollVelocity * DeltaTime;
        R = FMath::Clamp(R, RADIUS_MIN, RADIUS_MAX);
        DiggerToolkit->SetBrushRadius(R);
        ShowBrushHUDMessage(FString::Printf(TEXT("Radius: %.0f"), R));
    }
    else if (bCtrl && !bShift && !bAlt)
    {
        float S = DiggerToolkit->GetBrushStrength();
        S += ScrollVelocity * DeltaTime * 0.01f;
        S = FMath::Clamp(S, STRENGTH_MIN, STRENGTH_MAX);
        DiggerToolkit->SetBrushStrength(S);
        ShowBrushHUDMessage(FString::Printf(TEXT("Strength: %.2f"), S));
    }
    else if (bAlt && !bShift && !bCtrl)
    {
        float F = DiggerToolkit->GetBrushFalloff();
        F += ScrollVelocity * DeltaTime * 0.01f;
        F = FMath::Clamp(F, FALLOFF_MIN, FALLOFF_MAX);
        DiggerToolkit->SetBrushFalloff(F);
        ShowBrushHUDMessage(FString::Printf(TEXT("Falloff: %.2f"), F));
    }
    // Add your SHIFT+CTRL, SHIFT+ALT, CTRL+ALT cases here if needed
}


void FDiggerEdMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
    // No message to draw
    if (BrushHUD.TimeRemaining <= 0.f)
        return;

    // Fade-out alpha
    constexpr float FadeDuration = 0.6f;
    const float Alpha = FMath::Clamp(BrushHUD.TimeRemaining / FadeDuration, 0.f, 1.f);

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
    constexpr float TopSafeZone   = 60.f;   // below UE5.7 chrome
    constexpr float SidePadding   = 20.f;
    constexpr float BottomPadding = 50.f;
    constexpr float RightWidth    = 300.f;

    FVector2D Pos;

    switch (Settings->BrushHUDPosition)
    {
    case EBrushHUDPosition::UpperLeft:
        Pos = FVector2D(SidePadding, TopSafeZone);
        break;

    case EBrushHUDPosition::UpperRight:
        Pos = FVector2D(SizeX - RightWidth, TopSafeZone);
        break;

    case EBrushHUDPosition::LowerLeft:
        Pos = FVector2D(SidePadding, SizeY - BottomPadding);
        break;

    case EBrushHUDPosition::LowerRight:
        Pos = FVector2D(SizeX - RightWidth, SizeY - BottomPadding);
        break;

    case EBrushHUDPosition::Custom:
        Pos = Settings->CustomHUDOffset;
        break;

    default:
        Pos = FVector2D(SidePadding, TopSafeZone);
        break;
    }

    // Text color with fade
    const FLinearColor Color(1.f, 1.f, 1.f, Alpha);

    FCanvasTextItem TextItem(Pos, FText::FromString(BrushHUD.Text), GEngine->GetSmallFont(), Color);
    TextItem.EnableShadow(FLinearColor::Black);

    Canvas->DrawItem(TextItem);
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
