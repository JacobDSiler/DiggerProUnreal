// Copyright Epic Games, Inc. All Rights Reserved.

#include "DiggerEdMode.h"
#include "DiggerEditorAccess.h"
#include "DiggerEdModeToolkit.h"
#include "DiggerManager.h"
#include "EditorModeManager.h"
#include "EditorViewportClient.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "Toolkits/ToolkitManager.h"
#include "DrawDebugHelpers.h"
#include "Selection.h"

#define LOCTEXT_NAMESPACE "DiggerEditorMode"

const FEditorModeID FDiggerEdMode::EM_DiggerEdModeId = TEXT("EM_DiggerEdMode");
FOnDiggerModeChanged FDiggerEdMode::OnDiggerModeChanged;
bool FDiggerEdMode::bIsDiggerModeCurrentlyActive = false;

FDiggerEdMode::FDiggerEdMode() {}
FDiggerEdMode::~FDiggerEdMode() {}

void FDiggerEdMode::DeselectAllSceneActors()
{
    GEditor->GetSelectedActors()->DeselectAll();
}

// forward
// forward
static ADiggerManager* FindExistingManager(UWorld* World)
{
    if (!World) return nullptr;
    for (TActorIterator<ADiggerManager> It(World); It; ++It) return *It;
    return nullptr;
}

// Create a minimal transient library so painting never crashes if you don’t have one yet
static UObject* CreateTransientHoleShapeLibrary()
{
#if WITH_EDITOR
    // Replace UHoleShapeLibrary with your actual class type
    UObject* Lib = NewObject<UObject>(GetTransientPackage(), FName(TEXT("DefaultHoleShapeLibrary")));
    Lib->AddToRoot(); // keep alive for editor session
    return Lib;
#else
    return nullptr;
#endif
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
        Mgr = World->SpawnActor<ADiggerManager>(ADiggerManager::StaticClass(), FTransform::Identity, S);
    }
    if (Mgr)
    {
        // If your property is a UHoleShapeLibrary* replace UObject* and property name as needed
        if (!IsValid(Mgr->HoleShapeLibrary))
        {
            // Try load a project default if you have one
            UObject* Lib = StaticLoadObject(UObject::StaticClass(), nullptr, TEXT("/Game/Digger/Defaults/DA_DefaultHoleShapes.DA_DefaultHoleShapes"));
            if (!Lib)
            {
                Lib = CreateTransientHoleShapeLibrary();
            }
            Mgr->EnsureHoleShapeLibrary();
            Mgr->Modify();
        }
    }
#endif
}

void FDiggerEdMode::Enter()
{
    FEdMode::Enter();
    DeselectAllSceneActors();

    if (!Toolkit.IsValid())
    {
        FDiggerEditorAccess::SetEditorModeActive(true);
        Toolkit = MakeShareable(new FDiggerEdModeToolkit);
        Toolkit->Init(Owner->GetToolkitHost());
    }

    // New: make sure a manager + library exist so the mode can paint safely
    EnsureDiggerPrereqs();
}



void FDiggerEdMode::Exit()
{
    if (Toolkit.IsValid())
    {
        FDiggerEditorAccess::SetEditorModeActive(false);
        FToolkitManager::Get().CloseToolkit(Toolkit.ToSharedRef());
        Toolkit.Reset();
    }
    FEdMode::Exit();
}

bool FDiggerEdMode::GetMouseWorldHit(FEditorViewportClient* ViewportClient, FVector& OutHitLocation, FHitResult& OutHit)
{
    if (!ViewportClient) return false;

    FViewport* Viewport = ViewportClient->Viewport;
    if (!Viewport) return false;

    FIntPoint MousePos;
    Viewport->GetMousePos(MousePos);

    FSceneViewFamilyContext ViewFamily(
        FSceneViewFamily::ConstructionValues(
            Viewport,
            ViewportClient->GetScene(),
            ViewportClient->EngineShowFlags));

    FSceneView* SceneView = ViewportClient->CalcSceneView(&ViewFamily);
    if (!SceneView) return false;

    FVector WorldOrigin, WorldDirection;
    SceneView->DeprojectFVector2D(MousePos, WorldOrigin, WorldDirection);

    const FVector TraceStart = WorldOrigin;
    const FVector TraceEnd = WorldOrigin + WorldDirection * 100000.f;

    ADiggerManager* Digger = FindDiggerManager();
    if (!IsValid(Digger))
    {
        return false;
    }

    // Create brush if it doesn't exist
    if (!IsValid(Digger->ActiveBrush))
    {
        UWorld* World = ViewportClient->GetWorld();
        if (!World) return false;

        // Create new brush object
        Digger->ActiveBrush = NewObject<UVoxelBrushShape>(Digger, UVoxelBrushShape::StaticClass());
        
        // Initialize with default values
        if (IsValid(Digger->ActiveBrush))
        {
            Digger->ActiveBrush->InitializeBrush(Digger->ActiveBrush->GetBrushType(),
                Digger->ActiveBrush->GetBrushSize(),
                Digger->ActiveBrush->GetBrushLocation(),
                Digger);
            Digger->InitializeBrushShapes();
        }
    }

    // If we still don't have a valid brush, fall back to simple trace
    if (!IsValid(Digger->ActiveBrush))
    {
        FCollisionQueryParams Params(SCENE_QUERY_STAT(DiggerEdMode_MouseTrace), true);
        FHitResult Hit;
        UWorld* World = GEditor->GetEditorWorldContext().World();
        if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, Params) && Hit.bBlockingHit)
        {
            OutHit = Hit;
            OutHitLocation = Hit.ImpactPoint;
            return true;
        }
        return false;
    }

    // Use the brush's smart trace
    const FHitResult Hit = Digger->ActiveBrush->SmartTrace(TraceStart, TraceEnd);
    if (Hit.bBlockingHit)
    {
        OutHit = Hit;
        OutHitLocation = Hit.ImpactPoint;
        return true;
    }
    return false;
}

void FDiggerEdMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
    if (!Viewport) return;
    FEditorViewportClient* ViewportClient = (FEditorViewportClient*)Viewport->GetClient();
    if (!ViewportClient) return;

    FVector HitLocation;
    FHitResult Hit;
    if (!GetMouseWorldHit(ViewportClient, HitLocation, Hit))
        return;

    ADiggerManager* Digger = FindDiggerManager();
    if (!IsValid(Digger))
        return;

    const float PreviewRadius = Digger->EditorBrushRadius;
    const EVoxelBrushType BrushType = Digger->EditorBrushType;

    switch (BrushType)
    {
    case EVoxelBrushType::Sphere:
        DrawDebugSphere(Digger->GetWorld(), HitLocation, PreviewRadius, 32, FColor(0,255,255,64), false, 0.f, 0, 2.f);
        break;
    case EVoxelBrushType::Cube:
        DrawDebugBox(Digger->GetWorld(), HitLocation, FVector(PreviewRadius), FColor(0,255,0,64), false, 0.f, 0, 2.f);
        break;
    case EVoxelBrushType::Cylinder:
        DrawDebugCylinder(Digger->GetWorld(), HitLocation - FVector(0,0,PreviewRadius), HitLocation + FVector(0,0,PreviewRadius), PreviewRadius, 32, FColor(255,255,0,64), false, 0.f, 0, 2.f);
        break;
    case EVoxelBrushType::Custom:
        DrawDebugSphere(Digger->GetWorld(), HitLocation, PreviewRadius, 32, FColor(255,0,255,64), false, 0.f, 0, 2.f);
        break;
    default:
        break;
    }
}

bool FDiggerEdMode::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
    DeselectAllSceneActors();

    FVector HitLocation;
    FHitResult Hit;
    if (!GetMouseWorldHit(InViewportClient, HitLocation, Hit))
        return false;

    if (ADiggerManager* Digger = FindDiggerManager())
    {
        if (TSharedPtr<FDiggerEdModeToolkit> DiggerToolkit = GetDiggerToolkit())
        {
            // Cache brush settings once
            BrushCache.Radius = DiggerToolkit->GetBrushRadius();
            BrushCache.bFinalBrushDig = DiggerToolkit->IsDigMode();
            if (Click.GetKey() == EKeys::RightMouseButton)
                BrushCache.bFinalBrushDig = !BrushCache.bFinalBrushDig;

            BrushCache.Rotation = DiggerToolkit->GetBrushRotation();
            if (DiggerToolkit->UseSurfaceNormalRotation())
            {
                const FVector Normal = Hit.ImpactNormal.GetSafeNormal();
                const FQuat AlignRotation = FQuat::FindBetweenNormals(FVector::UpVector, Normal);
                BrushCache.Rotation = (AlignRotation * BrushCache.Rotation.Quaternion()).Rotator();
            }

            BrushCache.bIsFilled = DiggerToolkit->GetBrushIsFilled();
            BrushCache.Angle = DiggerToolkit->GetBrushAngle();
            BrushCache.BrushType = DiggerToolkit->GetCurrentBrushType();
            BrushCache.bHiddenSeam = DiggerToolkit->GetHiddenSeam();
            BrushCache.bUseAdvancedCube = DiggerToolkit->IsUsingAdvancedCubeBrush();
            BrushCache.CubeHalfExtentX = DiggerToolkit->GetAdvancedCubeHalfExtentX();
            BrushCache.CubeHalfExtentY = DiggerToolkit->GetAdvancedCubeHalfExtentY();
            BrushCache.CubeHalfExtentZ = DiggerToolkit->GetAdvancedCubeHalfExtentZ();
            BrushCache.Offset = DiggerToolkit->GetBrushOffset();

            StrokeSpacing = BrushCache.Radius * 0.5f;
            LastStrokeHitLocation = HitLocation;

            // Apply first stamp
            ApplyBrushWithSettings(Digger, HitLocation, Hit, BrushCache);

            // Continuous setup
            if (bMouseButtonDown)
                StartContinuousApplication(Click);

            return true;
        }
    }
    return false;
}


bool FDiggerEdMode::InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale)
{
    if (bIsContinuouslyApplying && bMouseButtonDown)
    {
        FVector2D CurrentMousePosition = FVector2D(InViewport->GetMouseX(), InViewport->GetMouseY());
        float MouseMoveDelta = FVector2D::Distance(CurrentMousePosition, LastMousePosition);
        if (MouseMoveDelta > 2.0f)
        {
            ApplyContinuousBrush(InViewportClient);
            LastMousePosition = CurrentMousePosition;
        }
    }
    return FEdMode::InputDelta(InViewportClient, InViewport, InDrag, InRot, InScale);
}

bool FDiggerEdMode::StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
    if (bPaintingEnabled)
    {
        bIsDragging = true; // Track that the user is actively dragging
        return true;
    }
    return FEdMode::StartTracking(InViewportClient, InViewport);
}


bool FDiggerEdMode::EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
    if (bIsPainting)
    {
        bIsDragging = false;
        bIsPainting = false;
        StopContinuousApplication();
        return true;
    }
    if (TSharedPtr<FDiggerEdModeToolkit> DiggerToolkit = GetDiggerToolkit())
    {
        DiggerToolkit->SetTemporaryDigOverride(TOptional<bool>());
    }
    return FEdMode::EndTracking(InViewportClient, InViewport);
}

bool FDiggerEdMode::CapturedMouseMove(FEditorViewportClient* InViewportClient, FViewport* InViewport, int32 InMouseX, int32 InMouseY)
{
    if (bIsPainting && bPaintingEnabled)
    {
        FVector2D CurrentMousePos(InMouseX, InMouseY);
        float DistanceMoved = FVector2D::Distance(CurrentMousePos, LastPaintLocation);
        if (DistanceMoved > 5.0f)
        {
            ApplyContinuousBrush(InViewportClient);
            LastPaintLocation = CurrentMousePos;
        }
        return true;
    }
    return false;
}

bool FDiggerEdMode::UsesToolkits() const
{
    return true;
}

void FDiggerEdMode::AddReferencedObjects(FReferenceCollector& Collector)
{
    FEdMode::AddReferencedObjects(Collector);
}

void FDiggerEdMode::StartContinuousApplication(const FViewportClick& Click)
{
    if (!ContinuousSettings.bIsValid || ContinuousSettings.bCtrlPressed)
        return;

    bIsContinuouslyApplying = true;
    ContinuousApplicationTimer = 0.0f;
}

void FDiggerEdMode::StopContinuousApplication()
{
    if (bIsContinuouslyApplying)
    {
        bIsContinuouslyApplying = false;
        ContinuousApplicationTimer = 0.0f;
        ContinuousSettings.bIsValid = false;

        if (TSharedPtr<FDiggerEdModeToolkit> DiggerToolkit = GetDiggerToolkit())
        {
            DiggerToolkit->SetTemporaryDigOverride(TOptional<bool>());
        }
    }
}


void FDiggerEdMode::ApplyBrushWithSettings(ADiggerManager* Digger, const FVector& HitLocation, const FHitResult& Hit, const FBrushCache& Settings)
{
    Digger->EditorBrushRadius = Settings.Radius;
    Digger->EditorBrushDig = Settings.bFinalBrushDig;
    Digger->EditorBrushRotation = Settings.Rotation;
    Digger->EditorBrushIsFilled = Settings.bIsFilled;
    Digger->EditorBrushAngle = Settings.Angle;
    Digger->EditorBrushType = Settings.BrushType;
    Digger->EditorBrushHoleShape = GetHoleShapeForBrush(Settings.BrushType);
    Digger->EditorBrushHiddenSeam = Settings.bHiddenSeam;
    Digger->EditorbUseAdvancedCubeBrush = Settings.bUseAdvancedCube;
    Digger->EditorCubeHalfExtentX = Settings.CubeHalfExtentX;
    Digger->EditorCubeHalfExtentY = Settings.CubeHalfExtentY;
    Digger->EditorCubeHalfExtentZ = Settings.CubeHalfExtentZ;

    FVector OffsetXY(Settings.Offset.X, Settings.Offset.Y, 0.f);
    float ZDistance = Settings.Offset.Z;
    FVector FinalOffset = OffsetXY;

    if (Settings.Rotation.Equals(FRotator::ZeroRotator) == false)
    {
        FinalOffset += Hit.ImpactNormal * ZDistance;
    }
    else
    {
        FinalOffset.Z = ZDistance;
    }

    Digger->EditorBrushOffset = FinalOffset;
    Digger->EditorBrushPosition = HitLocation;
    Digger->ApplyBrushInEditor(Settings.bFinalBrushDig);
}

void FDiggerEdMode::ApplyContinuousBrush(FEditorViewportClient* InViewportClient)
{
    if (!ContinuousSettings.bIsValid) return;

    FVector HitLocation;
    FHitResult Hit;
    if (!GetMouseWorldHit(InViewportClient, HitLocation, Hit))
        return;

    float DistanceSquared = FVector::DistSquared(HitLocation, LastStrokeHitLocation);
    if (DistanceSquared < StrokeSpacing * StrokeSpacing)
        return;

    // Interpolate along the path from last stroke to current to fill gaps
    FVector Direction = (HitLocation - LastStrokeHitLocation).GetSafeNormal();
    float Distance = FMath::Sqrt(DistanceSquared);

    const float StepSize = StrokeSpacing * 0.5f;  // smaller steps for smoothness
    int Steps = FMath::FloorToInt(Distance / StepSize);

    ADiggerManager* Digger = FindDiggerManager();
    if (!IsValid(Digger)) return;

    for (int i = 1; i <= Steps; i++)
    {
        FVector InterpLocation = LastStrokeHitLocation + Direction * StepSize * i;
        // You may want to do a line trace or smart trace here again if precision is critical,
        // but if performance is a concern, just trust this interpolation.

        ApplyBrushWithSettings(Digger, InterpLocation, Hit, BrushCache);
    }

    LastStrokeHitLocation = HitLocation;
}


// Input handling unchanged except bMouseButtonDown management
bool FDiggerEdMode::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
    if (Key == EKeys::P && Event == IE_Pressed)
    {
        bPaintingEnabled = !bPaintingEnabled;
        UE_LOG(LogTemp, Log, TEXT("Painting Mode: %s"), bPaintingEnabled ? TEXT("Enabled") : TEXT("Disabled"));
        return true;
    }

    if (bPaintingEnabled && (Key == EKeys::LeftMouseButton || Key == EKeys::RightMouseButton))
    {
        if (Event == IE_Pressed)
        {
            bMouseButtonDown = true;
            bIsPainting = true;
            ContinuousSettings.bIsValid = true;
            ContinuousSettings.bRightClick = (Key == EKeys::RightMouseButton);
            ContinuousSettings.bFinalBrushDig = ContinuousSettings.bRightClick;

            ApplyContinuousBrush(ViewportClient);
            return true;
        }
        else if (Event == IE_Released)
        {
            bMouseButtonDown = false;
            bIsPainting = false;
            ContinuousSettings.bIsValid = false;
            StopContinuousApplication();
            return true;
        }
    }

    StrokeSpacing = BrushCache.Radius * 0.5f;  // half the brush radius is a good starting point
    
    return FEdMode::InputKey(ViewportClient, Viewport, Key, Event);
}



bool FDiggerEdMode::ShouldApplyContinuously() const
{
    return bIsContinuouslyApplying && bMouseButtonDown && ContinuousSettings.bIsValid;
}

void FDiggerEdMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
    FEdMode::Tick(ViewportClient, DeltaTime);

    if (ShouldApplyContinuously())
    {
        ContinuousApplicationTimer += DeltaTime;

        // Apply every 0.02 seconds (50 times per second) or adjust for your perf needs
        if (ContinuousApplicationTimer >= 0.02f)
        {
            ApplyContinuousBrush(ViewportClient);
            ContinuousApplicationTimer = 0.0f;
        }
    }

    if (GEditor)
    {
        GEditor->RedrawAllViewports();
    }
}


ADiggerManager* FDiggerEdMode::FindDiggerManager()
{
    UWorld* World = GEditor->GetEditorWorldContext().World();
    for (TActorIterator<ADiggerManager> It(World); It; ++It)
        return *It;
    return nullptr;
}

#undef LOCTEXT_NAMESPACE
