// Copyright Epic Games, Inc. All Rights Reserved.

#include "DiggerEdMode.h"
#include "BrushPreviewActor.h"
#include "CollisionQueryParams.h"
#include "DiggerEditorAccess.h"
#include "DiggerEdModeToolkit.h"
#include "DiggerManager.h"
#include "DrawDebugHelpers.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "EditorViewportClient.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "SceneManagement.h" // Required for DrawWireSphere, DrawWireBox, etc.
#include "Selection.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Toolkits/ToolkitManager.h"

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

    EnsureDiggerPrereqs();

    // NEW: create preview once
    EnsurePreviewExists();
}




void FDiggerEdMode::Exit()
{
    // NEW: Clean up preview
    DestroyPreview();

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

// ----- Spawning / Destroying -----

void FDiggerEdMode::EnsurePreviewExists()
{
    if (Preview.IsValid())
        return;

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World) return;

    ABrushPreviewActor* Actor = World->SpawnActor<ABrushPreviewActor>();
    if (!Actor) return;

    // Use engine built-in sphere for quick testing (no custom material yet)
    UStaticMesh* UnitSphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/DiggerEditor/M_DiggerBrushPreview.M_DiggerBrushPreview")); // your path
    Actor->Initialize(UnitSphere, BaseMat);
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

// ----- Mouse Hit → Preview Update -----

bool FDiggerEdMode::TraceUnderCursor(FEditorViewportClient* InViewportClient, FHitResult& OutHit) const
{
    if (!InViewportClient) return false;

    UWorld* World = InViewportClient->GetWorld();
    if (!World) return false;

    // Build a ray from screen to world
    FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
        InViewportClient->Viewport, InViewportClient->GetScene(), InViewportClient->EngineShowFlags)
        .SetRealtimeUpdate(true));

    FSceneView* View = InViewportClient->CalcSceneView(&ViewFamily);
    if (!View) return false;

    FIntPoint MousePos;
    InViewportClient->Viewport->GetMousePos(MousePos);

    FVector WorldOrigin, WorldDirection;
    View->DeprojectFVector2D(FVector2D(MousePos), /*out*/WorldOrigin, /*out*/WorldDirection);

    const FVector Start = WorldOrigin;
    const FVector End   = Start + WorldDirection * 100000.f;

    FCollisionQueryParams Params(SCENE_QUERY_STAT(DiggerPreviewTrace), /*bTraceComplex*/ true);
    Params.bReturnPhysicalMaterial = false;
    Params.AddIgnoredActor(Preview.IsValid() ? Preview.Get() : nullptr);

    // Trace against visibility by default; include landscape + static meshes
    return World->LineTraceSingleByChannel(OutHit, Start, End, ECollisionChannel::ECC_Visibility, Params);
}

FDiggerEdMode::FBrushUIParams FDiggerEdMode::GetCurrentBrushUI() const
{
    FBrushUIParams P;
    
    if (auto DiggerToolkit = GetDiggerToolkit())
    {
        P.RadiusXYZ  = FVector(DiggerToolkit ? DiggerToolkit->GetBrushRadius() : 64.f);
        P.Falloff    = DiggerToolkit ? DiggerToolkit->GetBrushFalloff() : 0.25f;
        P.bAdd       = DiggerToolkit ? DiggerToolkit->IsDigMode() : true;
        //P.CellSize   = (DM && DM->Subdivisions>0) ? DM->TerrainGridSize / float(DM->Subdivisions) : 50.f;

        // Map the active editor brush type to our preview enum so the
        // correct preview mesh is displayed for the selected tool.
        switch (DiggerToolkit->GetCurrentBrushType())
        {
        case EVoxelBrushType::Cube:
        case EVoxelBrushType::AdvancedCube:
            P.ShapeType = static_cast<uint8>(EBrushPreviewShape::Box);
            break;
        case EVoxelBrushType::Cylinder:
            P.ShapeType = static_cast<uint8>(EBrushPreviewShape::Cylinder);
            break;
        case EVoxelBrushType::Capsule:
            P.ShapeType = static_cast<uint8>(EBrushPreviewShape::Capsule);
            break;
        case EVoxelBrushType::Cone:
        case EVoxelBrushType::Pyramid:
            P.ShapeType = static_cast<uint8>(EBrushPreviewShape::Cone);
            break;
        case EVoxelBrushType::Torus:
            P.ShapeType = static_cast<uint8>(EBrushPreviewShape::Torus);
            break;
        case EVoxelBrushType::Sphere:
        case EVoxelBrushType::Icosphere:
        default:
            P.ShapeType = static_cast<uint8>(EBrushPreviewShape::Sphere);
            break;
        }
    }

    return P;
}

void FDiggerEdMode::UpdatePreviewAtCursor(FEditorViewportClient* InViewportClient)
{
    EnsurePreviewExists();
    if (!Preview.IsValid()) return;

    FHitResult Hit;
    const bool bHasHit = TraceUnderCursor(InViewportClient, Hit);

    // If no hit, don’t hide—just keep last position so you can at least see the actor:
    if (!bHasHit)
    {
        Preview->SetVisible(true);
        return;
    }

    Preview->SetVisible(true);
    const FBrushUIParams P = GetCurrentBrushUI();

    // The shape type is provided by GetCurrentBrushUI via the toolkit.
    const EBrushPreviewShape Shape = static_cast<EBrushPreviewShape>(P.ShapeType);

    Preview->UpdatePreview(
        Hit.Location,
        P.RadiusXYZ,
        P.Falloff,
        P.bAdd,
        P.CellSize,
        Shape, {}
    );

    if (DiggerDebug::Brush)
        UE_LOG(LogTemp, Warning, TEXT("Brush Radius: %s"), *P.RadiusXYZ.ToString());
}



void FDiggerEdMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
    if (!Viewport || !PDI) return;
    return;

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

    const FColor BrushColor = FColor(0, 255, 255); // Customize per brush type
    const uint8 DepthPriority = SDPG_Foreground;

    switch (BrushType)
    {
    case EVoxelBrushType::Sphere:
        DrawWireSphere(PDI, HitLocation, BrushColor, PreviewRadius, 32, DepthPriority);
        break;

    case EVoxelBrushType::Cube:
        {
            const FVector BoxExtent(PreviewRadius);
            const FBox Box(HitLocation - BoxExtent, HitLocation + BoxExtent);
            DrawWireBox(PDI, Box, BrushColor, DepthPriority);
        }
        break;

    case EVoxelBrushType::Cylinder:
        {
            const FVector Base = HitLocation - FVector(0, 0, PreviewRadius);
            const FVector Top = HitLocation + FVector(0, 0, PreviewRadius);
            DrawWireCylinder(PDI, Base, FVector(0, 0, 1), FVector(1, 0, 0), FVector(0, 1, 0), BrushColor, PreviewRadius, PreviewRadius, 32, DepthPriority);
        }
        break;

    case EVoxelBrushType::Custom:
        DrawWireSphere(PDI, HitLocation, FColor(255, 0, 255), PreviewRadius, 32, DepthPriority);
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
    // NEW: Always update the preview to follow the cursor
    UpdatePreviewAtCursor(InViewportClient);

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

    UpdatePreviewAtCursor(ViewportClient); // always refresh the brush preview

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

TSharedPtr<FDiggerEdModeToolkit> FDiggerEdMode::GetDiggerToolkit()
{
    return StaticCastSharedPtr<FDiggerEdModeToolkit>(Toolkit);
}

TSharedPtr<FDiggerEdModeToolkit> FDiggerEdMode::GetDiggerToolkit() const
{
    return StaticCastSharedPtr<FDiggerEdModeToolkit>(Toolkit);
}

#undef LOCTEXT_NAMESPACE
