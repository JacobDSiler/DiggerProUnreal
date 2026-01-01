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
#include "AssetRegistry/AssetRegistryModule.h"

#define LOCTEXT_NAMESPACE "DiggerEditorMode"

const FEditorModeID FDiggerEdMode::EM_DiggerEdModeId = TEXT("EM_DiggerEdMode");
FOnDiggerModeChanged FDiggerEdMode::OnDiggerModeChanged;
bool FDiggerEdMode::bIsDiggerModeCurrentlyActive = false;

// --- Tunables (top of FDiggerEdMode.cpp or as static const) ---
static constexpr float RADIUS_MIN = 10.f;
static constexpr float RADIUS_MAX = 4096.f; // feel free to raise
static constexpr float STRENGTH_MIN = 0.f;
static constexpr float STRENGTH_MAX = 1.f;
static constexpr float FALLOFF_MIN = 0.f;
static constexpr float FALLOFF_MAX = 1.f;

// Base step sizes per tick of MouseWheelAxis at neutral speed
static constexpr float BASE_RADIUS_STEP   = 8.f;
static constexpr float BASE_STRENGTH_STEP = 0.02f;
static constexpr float BASE_FALLOFF_STEP  = 0.02f;

// Acceleration gain: how strongly scroll speed scales the step
static constexpr float ACCEL_GAIN = 0.35f;   // 0.25–0.5 is a nice range
static constexpr float SPEED_CLAMP = 30.f;   // cap insane wheels

// Optional snapping (set to 0 to disable)
static constexpr float RADIUS_SNAP_STEP = 0.f;  // e.g. set to your voxel size to snap
static constexpr float FALLOFF_SNAP_STEP = 0.f; // e.g. 0.05f
static constexpr float STRENGTH_SNAP_STEP = 0.f;// e.g. 0.05f

// Helper: snap if step > 0
static FORCEINLINE float SnapIf(float Value, float Step)
{
    if (Step <= KINDA_SMALL_NUMBER) return Value;
    const float q = FMath::RoundToFloat(Value / Step);
    return q * Step;
}

// Compute accelerated step for this wheel event.
static FORCEINLINE float WheelStep(float BaseStep, float Delta, float DeltaTime, bool bFine, bool bCoarse)
{
    // Speed in "ticks/sec"
    const float speed = FMath::Clamp(FMath::Abs(Delta) / FMath::Max(DeltaTime, KINDA_SMALL_NUMBER), 0.f, SPEED_CLAMP);
    // Acceleration multiplier (1.0 at rest, grows with speed)
    float accel = 1.f + speed * ACCEL_GAIN;

    // Modifier scaling: Fine (Ctrl) shrinks; Coarse (Alt) grows
    if (bFine)   accel *= 0.25f;   // precise
    if (bCoarse) accel *= 2.5f;    // chunky

    // Keep sign of wheel
    const float sgn = (Delta >= 0.f) ? 1.f : -1.f;
    return sgn * BaseStep * accel;
}



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
    
    // 1. Get the Correct World (Handle PIE vs Editor)
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World) return;

    // 2. Find or Spawn Manager
    ADiggerManager* Mgr = FindExistingManager(World);
    if (!Mgr)
    {
        FActorSpawnParameters S;
        S.Name = FName(TEXT("DiggerManager"));
        S.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        // Make sure it's transient so it doesn't get saved into the map permanently if you don't want it to
        S.ObjectFlags = RF_Transactional; 
        
        Mgr = World->SpawnActor<ADiggerManager>(ADiggerManager::StaticClass(), FTransform::Identity, S);
    }

    if (Mgr)
    {
        // 3. Check if Library is missing
        if (!IsValid(Mgr->HoleShapeLibrary))
        {
            // USE THE CORRECT PATH FROM YOUR LOGS
            const TCHAR* LibPath = TEXT("/Digger/Digger/BluePrints/HoleShapeLibrary.HoleShapeLibrary");
            
            UHoleShapeLibrary* LoadedLib = Cast<UHoleShapeLibrary>(StaticLoadObject(UHoleShapeLibrary::StaticClass(), nullptr, LibPath));

            if (LoadedLib)
            {
                // ASSIGN IT! This was missing in your previous code.
                Mgr->HoleShapeLibrary = LoadedLib;
                UE_LOG(LogTemp, Log, TEXT("DiggerPrereqs: Successfully assigned existing HoleShapeLibrary."));
            }
            else
            {
                // Fallback: Create a Transient one
                // CRITICAL: Pass 'Mgr' as the Outer (1st arg). 
                // If you pass GetTransientPackage(), the GC might eat it because the Manager doesn't "own" it.
                Mgr->HoleShapeLibrary = NewObject<UHoleShapeLibrary>(Mgr, UHoleShapeLibrary::StaticClass());
                
                // If you have a function to seed it, call it now
                // SeedHoleShapesFromFolder(Mgr->HoleShapeLibrary); 
                
                UE_LOG(LogTemp, Warning, TEXT("DiggerPrereqs: Could not load Library at %s. Created a new transient one."), LibPath);
            }

            // 4. Update the Manager
            // Only call this if you are sure it doesn't crash on nulls
            // Mgr->EnsureHoleShapeLibrary(); 
            
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
        ////////// This commented code is to  check what objects are actually registered in the file system.
        ////////// Specifically, it helped me know that my plugin objects were loaded.
        // // 1. Get the Asset Registry
        // FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
        //
        // // 2. Define the search root. 
        // // We suspect it is "/Digger", but let's scan the root to be safe.
        // // If this returns nothing, change "/Digger" to "/" to scan the whole project (lots of logs, but definitive).
        // FName SearchPath = FName("/Digger"); 
        //
        // TArray<FAssetData> AssetDataList;
        // AssetRegistryModule.Get().GetAssetsByPath(SearchPath, AssetDataList, true); // 'true' = Recursive
        //
        // if (AssetDataList.Num() == 0)
        // {
        //     UE_LOG(LogTemp, Error, TEXT("PATH DEBUG: No assets found under path '%s'. The Plugin might not be mounted or 'CanContainContent' is false."), *SearchPath.ToString());
        // }
        // else
        // {
        //     UE_LOG(LogTemp, Warning, TEXT("PATH DEBUG: Found %d assets. Printing correct paths below:"), AssetDataList.Num());
        //     for (const FAssetData& Data : AssetDataList)
        //     {
        //         // This prints the exact Package Path you need to use in your code
        //         UE_LOG(LogTemp, Warning, TEXT("FOUND: %s"), *Data.PackageName.ToString());
        //     }
        // }
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
        // Scrap the worklight if there is one.
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



bool FDiggerEdMode::GetMouseWorldHit(FEditorViewportClient* ViewportClient, FVector& OutHitLocation, FHitResult& OutHit)
{
    if (!ViewportClient || !ViewportClient->Viewport)
        return false;

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
    const FVector TraceEnd = WorldOrigin + WorldDirection * 100000.f;

    ADiggerManager* Digger = FindDiggerManager();
    if (!IsValid(Digger))
    {
        UE_LOG(LogTemp, Error, TEXT("No DiggerManager found in DiggerEdMode::GetMouseWorldHit!"));
        return false;
    }

    // Ensure brush exists and is initialized
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

    // ✅ Prefer SmartTrace if brush is valid
    if (IsValid(Digger->ActiveBrush))
    {
        const FHitResult SmartHit = Digger->ActiveBrush->SmartTrace(TraceStart, TraceEnd);
        if (SmartHit.bBlockingHit)
        {
            OutHit = SmartHit;
            OutHitLocation = SmartHit.ImpactPoint;
            if (DiggerDebug::Casts())
            UE_LOG(LogTemp, Warning, TEXT("SmartTrace returned: %s"), *SmartHit.ImpactPoint.ToString());
            return true;
        }
    }

    // ❌ Fallback: basic line trace if SmartTrace fails or brush is invalid
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (World)
    {
        FCollisionQueryParams Params(SCENE_QUERY_STAT(DiggerEdMode_MouseTrace), true);
        FHitResult FallbackHit;
        if (World->LineTraceSingleByChannel(FallbackHit, TraceStart, TraceEnd, ECC_Visibility, Params) && FallbackHit.bBlockingHit)
        {
            OutHit = FallbackHit;
            OutHitLocation = FallbackHit.ImpactPoint;
            if (DiggerDebug::Casts())
            UE_LOG(LogTemp, Error, TEXT("Line Trace Fallback returned hit: %s"), *FallbackHit.ImpactPoint.ToString());
            return true;
        }
    }

    return false;
}


// ----- Spawning / Destroying -----

void FDiggerEdMode::UpdateBrushSettingsFromUI(const FHitResult& TraceHit, bool bRightClick)
{
    TSharedPtr<FDiggerEdModeToolkit> DiggerToolkit = GetDiggerToolkit();
    if (!DiggerToolkit.IsValid()) return;

    // 1. Cache Basic Settings
    BrushCache.Radius = DiggerToolkit->GetBrushRadius();
    BrushCache.Falloff = DiggerToolkit->GetBrushFalloff();
    BrushCache.Strength = DiggerToolkit->GetBrushStrength();

    BrushCache.LightType = DiggerToolkit->GetCurrentLightType();
    if (DiggerDebug::Lights())
        UE_LOG(LogTemp, Warning, TEXT("Light set as: %i"), DiggerToolkit->GetCurrentLightType());
    
    // Logic: Left Click = UI Setting, Right Click = Invert UI Setting
    bool bUiDig = DiggerToolkit->IsDigMode();
    BrushCache.bFinalBrushDig = bRightClick ? !bUiDig : bUiDig;

    // 2. Handle Rotation (Normal Alignment)
    BrushCache.Rotation = DiggerToolkit->GetBrushRotation();
    if (DiggerToolkit->UseSurfaceNormalRotation())
    {
        const FVector Normal = TraceHit.ImpactNormal.GetSafeNormal();
        const FQuat AlignRotation = FQuat::FindBetweenNormals(FVector::UpVector, Normal);
        BrushCache.Rotation = (AlignRotation * BrushCache.Rotation.Quaternion()).Rotator();
    }

    // 3. Cache Advanced Settings
    BrushCache.bIsFilled = DiggerToolkit->GetBrushIsFilled();
    BrushCache.Angle = DiggerToolkit->GetBrushAngle();
    BrushCache.BrushType = DiggerToolkit->GetCurrentBrushType();
    BrushCache.bHiddenSeam = DiggerToolkit->GetHiddenSeam();
    
    // Advanced Cube
    BrushCache.bUseAdvancedCube = DiggerToolkit->IsUsingAdvancedCubeBrush();
    BrushCache.CubeHalfExtentX = DiggerToolkit->GetAdvancedCubeHalfExtentX();
    BrushCache.CubeHalfExtentY = DiggerToolkit->GetAdvancedCubeHalfExtentY();
    BrushCache.CubeHalfExtentZ = DiggerToolkit->GetAdvancedCubeHalfExtentZ();
    
    BrushCache.Offset = DiggerToolkit->GetBrushOffset();

    // 4. Update Spacing
    float SafeRadius = FMath::Max(BrushCache.Radius, 10.0f);
    StrokeSpacing = SafeRadius * 0.5f;
    // CRITICAL: Reset the tracker so we don't sweep from the previous stroke's end point
    LastStrokeHitLocation = TraceHit.Location; 
}

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
    UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Digger/Digger/DiggerEditor/M_DiggerBrushPreview.M_DiggerBrushPreview")); // your path
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

bool FDiggerEdMode::TraceUnderCursor(FEditorViewportClient* InViewportClient, FHitResult& OutHit)
{
    if (!InViewportClient || !InViewportClient->Viewport)
        return false;

    FIntPoint MousePos;
    InViewportClient->Viewport->GetMousePos(MousePos);

    FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
        InViewportClient->Viewport,
        InViewportClient->GetScene(),
        InViewportClient->EngineShowFlags)
        .SetRealtimeUpdate(true));

    FSceneView* View = InViewportClient->CalcSceneView(&ViewFamily);
    if (!View)
        return false;

    FVector WorldOrigin, WorldDirection;
    View->DeprojectFVector2D(FVector2D(MousePos), WorldOrigin, WorldDirection);

    const FVector TraceStart = WorldOrigin;
    const FVector TraceEnd = TraceStart + WorldDirection * 100000.f;

    ADiggerManager* Digger = FindDiggerManager();
    if (!IsValid(Digger))
    {
        if (DiggerDebug::Casts() || DiggerDebug::Manager())
        UE_LOG(LogTemp, Error, TEXT("No DiggerManager found in DiggerEdMode::TraceUnderCursor!"));
        return false;
    }

    // Ensure brush exists and is initialized
    if (!IsValid(Digger->ActiveBrush))
    {
        if (UWorld* World = InViewportClient->GetWorld())
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

    // ✅ Prefer SmartTrace if brush is valid
    if (IsValid(Digger->ActiveBrush))
    {
        const FHitResult SmartHit = Digger->ActiveBrush->SmartTrace(TraceStart, TraceEnd);
        if (SmartHit.bBlockingHit)
        {
            OutHit = SmartHit;
            if (DiggerDebug::Casts())
            UE_LOG(LogTemp, Warning, TEXT("SmartTrace returned hit: %s"), *SmartHit.ImpactPoint.ToString());
            return true;
        }
    }

    // ❌ Fallback: basic line trace if SmartTrace fails or brush is invalid
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (World)
    {
        FCollisionQueryParams Params(SCENE_QUERY_STAT(DiggerPreviewTrace), true);
        Params.bReturnPhysicalMaterial = false;
        Params.AddIgnoredActor(Preview.IsValid() ? Preview.Get() : nullptr);

        FHitResult FallbackHit;
        if (World->LineTraceSingleByChannel(FallbackHit, TraceStart, TraceEnd, ECC_Visibility, Params) && FallbackHit.bBlockingHit)
        {
            OutHit = FallbackHit;
            if (DiggerDebug::Casts())
            UE_LOG(LogTemp, Error, TEXT("Line Trace Fallback returned hit: %s"), *FallbackHit.ImpactPoint.ToString());
            return true;
        }
    }

    return false;
}


FDiggerEdMode::FBrushUIParams FDiggerEdMode::GetCurrentBrushUI() const
{
    FBrushUIParams P;
    
    if (auto DiggerToolkit = GetDiggerToolkit())
    {
        P.RadiusXYZ  = FVector(DiggerToolkit ? DiggerToolkit->GetBrushRadius() : 64.f);
        P.Falloff    = DiggerToolkit ? DiggerToolkit->GetBrushFalloff() : 0.25f;
        P.bAdd       = DiggerToolkit ? DiggerToolkit->IsDigMode() : true;
        P.Rotation   = DiggerToolkit->GetBrushRotation();
        P.Offset     = DiggerToolkit->GetBrushOffset();
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
    FBrushUIParams P = GetCurrentBrushUI();

    // Align rotation to surface normal if the user has that option enabled
    if (TSharedPtr<FDiggerEdModeToolkit> DiggerToolkit = GetDiggerToolkit())
    {
        if (DiggerToolkit->UseSurfaceNormalRotation())
        {
            const FVector Normal = Hit.ImpactNormal.GetSafeNormal();
            const FQuat AlignRotation = FQuat::FindBetweenNormals(FVector::UpVector, Normal);
            P.Rotation = (AlignRotation * P.Rotation.Quaternion()).Rotator();
        }
    }

    // The shape type is provided by GetCurrentBrushUI via the toolkit.
    const EBrushPreviewShape Shape = static_cast<EBrushPreviewShape>(P.ShapeType);

    FVector OffsetXY(P.Offset.X, P.Offset.Y, 0.f);
    float ZDist = P.Offset.Z;
    FVector FinalOffset = OffsetXY;

    if (!P.Rotation.IsNearlyZero())
    {
        FinalOffset += Hit.ImpactNormal * ZDist;
    }
    else
    {
        FinalOffset.Z = ZDist;
    }

    const FVector PreviewCenter = Hit.Location + FinalOffset;
    const FQuat   PreviewRot    = P.Rotation.Quaternion();


    Preview->UpdatePreview(
        PreviewCenter,
        P.RadiusXYZ,
        P.Falloff,
        P.bAdd,
        P.CellSize,
        Shape,
        PreviewRot);


    if (DiggerDebug::Brush())
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
        // Use the new helper
        bool bRightClick = (Click.GetKey() == EKeys::RightMouseButton);
        UpdateBrushSettingsFromUI(Hit, bRightClick);

        // Initialize Tracker (Just in case they drag after a single click without Paint Mode)
        LastStrokeHitLocation = HitLocation;

        // Apply
        ApplyBrushWithSettings(Digger, HitLocation, Hit, BrushCache);

        return true;
    }
    return false;
}

bool FDiggerEdMode::InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale)
{
    // Remove the painting logic from here. 
    // Let CapturedMouseMove handle it to avoid double-processing.
    
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
    UpdatePreviewAtCursor(InViewportClient);

    if (bIsPainting && bPaintingEnabled)
    {
        // Screen space distance check (Optimization)
        // Only raycast if the mouse actually moved 2 pixels on screen
        FVector2D CurrentMousePos(InMouseX, InMouseY);
        float DistanceMoved = FVector2D::Distance(CurrentMousePos, LastPaintLocation);
        
        if (DistanceMoved > 2.0f) 
        {
            // Now we check World Distance inside this function
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
    // --- CRITICAL FIX: Push Cache value to Manager ---
    // This overwrites whatever was there, so it MUST be correct (from Step 1)
    Digger->EditorBrushLightType = Settings.LightType;
    // ------------------------------------------------
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
    if (!GetMouseWorldHit(InViewportClient, HitLocation, Hit)) return;

    // 1. Check Distance
    float DistanceSquared = FVector::DistSquared(HitLocation, LastStrokeHitLocation);
    if (DistanceSquared < 1.0f) return; // Moved virtually nowhere

    ADiggerManager* Digger = FindDiggerManager();
    if (!IsValid(Digger)) return;

    // 2. Prepare the Stroke
    // We start with the user's current settings (Radius, Falloff, etc.)
    FBrushCache CurrentSettings = BrushCache; 
    
    // But we OVERRIDE the Shape/Transform to create the sweep
    if (CurrentSettings.BrushType == EVoxelBrushType::Sphere || 
        CurrentSettings.BrushType == EVoxelBrushType::Capsule) // Works best for round brushes
    {
        // --- THE OPTIMIZATION: CAPSULE SWEEP ---
        
        // Create a temporary brush shape to calculate the struct
        // (Or access the existing one if safe)
        if (Digger->ActiveBrush)
        {
            FBrushStroke SweptStroke;
        
            // --- CRITICAL DATA COPY ---
            SweptStroke.bDig = BrushCache.bFinalBrushDig;       // Must copy this!
            SweptStroke.BrushStrength = BrushCache.Strength;    // Must copy this!
            SweptStroke.BrushFalloff = BrushCache.Falloff;      // Must copy this!
            // ---------------------------

            // Now setup the geometry (Radius, Length, Pos, Rot)
            Digger->ActiveBrush->SetupSweptStroke(
                SweptStroke, 
                LastStrokeHitLocation, 
                HitLocation, 
                BrushCache.Radius
            );
        
            // Debug Log to prove it fired
            if (DiggerDebug::Brush())
            {
                UE_LOG(LogTemp, Warning, TEXT("Sweeping Capsule: Start=%s End=%s Dig=%d"), 
                    *LastStrokeHitLocation.ToString(), 
                    *HitLocation.ToString(), 
                    SweptStroke.bDig);
            }

            Digger->ApplyBrushToAllChunks(SweptStroke);
        }
    }
    else
    {
        // --- FALLBACK FOR NON-ROUND SHAPES (Cube, etc.) ---
        // Cubes don't sweep into capsules cleanly. 
        // For these, we still have to use the interpolation loop, but maybe cap it tighter.
        
        float StepSize = CurrentSettings.Radius * 0.5f;
        float Distance = FMath::Sqrt(DistanceSquared);
        int32 Steps = FMath::FloorToInt(Distance / StepSize);
        
        // Cap steps to prevent freeze
        Steps = FMath::Min(Steps, 10); 

        FVector Direction = (HitLocation - LastStrokeHitLocation).GetSafeNormal();
        
        for (int i = 1; i <= Steps; ++i)
        {
            FVector Pos = LastStrokeHitLocation + Direction * StepSize * i;
            ApplyBrushWithSettings(Digger, Pos, Hit, CurrentSettings);
        }
    }

    // 3. Update Tracker
    LastStrokeHitLocation = HitLocation;
}


// Add this method to your header file and implement it to handle mouse hover
bool FDiggerEdMode::MouseEnter(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y)
{
    bool bResult = FEdMode::MouseEnter(ViewportClient, Viewport, x, y);
    
    // Auto-focus when mouse enters if we're in an active state
    if (ViewportClient && Viewport)
    {
        Viewport->SetUserFocus(true);
    }
    
    return bResult;
}



bool FDiggerEdMode::InputAxis(
    FEditorViewportClient* ViewportClient,
    FViewport* Viewport,
    int32 ControllerId,
    FKey Key,
    float Delta,
    float DeltaTime)
{
    TSharedPtr<FDiggerEdModeToolkit> DiggerToolkit = GetDiggerToolkit();
    if (!DiggerToolkit.IsValid())
    {
        if (DiggerDebug::Error())
        UE_LOG(LogTemp, Error, TEXT("Toolkit absent in FDiggerEdMode::InputAxis!!!"));
        return false;
    }

    const bool bShift = Viewport->KeyState(EKeys::LeftShift) || Viewport->KeyState(EKeys::RightShift);
    const bool bCtrl  = Viewport->KeyState(EKeys::LeftControl) || Viewport->KeyState(EKeys::RightControl);
    const bool bAlt   = Viewport->KeyState(EKeys::LeftAlt) || Viewport->KeyState(EKeys::RightAlt);

    // Only handle wheel when SHIFT is down (your chosen gesture)
    if (Key == EKeys::MouseWheelAxis && bShift)
    {
        // ----- Choose which property with modifiers -----
        // Shift+Ctrl  = Radius
        // Shift+Alt   = Falloff
        // Shift only  = Strength
        // (feel free to swap these; logic below matches your earlier intent)
        if (bCtrl && !bAlt)
        {
            // Radius
            const float step = WheelStep(BASE_RADIUS_STEP, Delta, DeltaTime, /*bFine=*/true, /*bCoarse=*/false);
            float v = DiggerToolkit->GetBrushRadius();
            v = FMath::Clamp(v + step, RADIUS_MIN, RADIUS_MAX);
            v = SnapIf(v, RADIUS_SNAP_STEP);
            DiggerToolkit->SetBrushRadius(v);

            if (DiggerDebug::Brush())
                UE_LOG(LogTemp, Log, TEXT("Brush Radius: %.1f"), v);
        }
        else if (bAlt && !bCtrl)
        {
            // Falloff
            const float step = WheelStep(BASE_FALLOFF_STEP, Delta, DeltaTime, /*bFine=*/false, /*bCoarse=*/true);
            float v = DiggerToolkit->GetBrushFalloff();
            v = FMath::Clamp(v + step, FALLOFF_MIN, FALLOFF_MAX);
            v = SnapIf(v, FALLOFF_SNAP_STEP);
            DiggerToolkit->SetBrushFalloff(v);

            if (DiggerDebug::Brush())
                UE_LOG(LogTemp, Log, TEXT("Brush Falloff: %.3f"), v);
        }
        else
        {
            // Strength (Shift only)
            const float step = WheelStep(BASE_STRENGTH_STEP, Delta, DeltaTime, /*bFine=*/false, /*bCoarse=*/false);
            float v = DiggerToolkit->GetBrushStrength();
            v = FMath::Clamp(v + step, STRENGTH_MIN, STRENGTH_MAX);
            v = SnapIf(v, STRENGTH_SNAP_STEP);
            DiggerToolkit->SetBrushStrength(v);

            if (DiggerDebug::Brush())
                UE_LOG(LogTemp, Log, TEXT("Brush Strength: %.3f"), v);
        }

        // Tell the Toolkit to refresh its widgets (see section 2)
        DiggerToolkit->RequestBrushUIRefresh();

        // CRITICAL: consume so the viewport camera doesn’t zoom
        return true;
    }

    return FEdMode::InputAxis(ViewportClient, Viewport, ControllerId, Key, Delta, DeltaTime);
}
// Input handling unchanged except bMouseButtonDown management
bool FDiggerEdMode::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
    const bool bShift = Viewport->KeyState(EKeys::LeftShift) || Viewport->KeyState(EKeys::RightShift);

    // While holding Shift for adjustments, do not start/continue brush strokes
    if (bShift && Key.IsMouseButton())
    {
        return true; // consume mouse buttons so no stroke begins
    }
    
    // Handle mouse enter to automatically focus viewport for our custom input
    if (Key == EKeys::MouseX || Key == EKeys::MouseY)
    {
        if (ViewportClient && ViewportClient->Viewport)
        {
            const bool bShiftDown = Viewport->KeyState(EKeys::LeftShift) || Viewport->KeyState(EKeys::RightShift);
            
            // If shift is held and mouse moves over viewport, prepare for input capture
            if (bShiftDown && Event == IE_Axis)
            {
                ViewportClient->Viewport->CaptureMouse(false); // Don't fully capture yet
                ViewportClient->Viewport->SetUserFocus(true);
            }
        }
    }
    
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
            // --- THE FIX STARTS HERE ---
            
            // 1. Get the hit result NOW so we can setup the brush
            FVector HitLocation;
            FHitResult Hit;
            GetMouseWorldHit(ViewportClient, HitLocation, Hit); 
            // Note: Even if this fails (sky hit), we still want to init settings, 
            // but we might default the normal to UpVector.

            // 2. Update Cache BEFORE painting
            bool bRightClick = (Key == EKeys::RightMouseButton);
            UpdateBrushSettingsFromUI(Hit, bRightClick);

            // 3. Initialize Location Tracker
            LastStrokeHitLocation = HitLocation;
            LastPaintLocation = FVector2D(Viewport->GetMouseX(), Viewport->GetMouseY());

            // 4. Set State
            bMouseButtonDown = true;
            bIsPainting = true;
            ContinuousSettings.bIsValid = true;
            ContinuousSettings.bRightClick = bRightClick;
            
            // 5. Apply Initial Stamp
            ApplyContinuousBrush(ViewportClient);
            
            // --- THE FIX ENDS HERE ---

            return true; // Consume input (HandleClick will NOT fire)
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

    UpdatePreviewAtCursor(ViewportClient);

    // Keep your focus logic
    if (ViewportClient && ViewportClient->Viewport)
    {
        const bool bShiftDown = ViewportClient->Viewport->KeyState(EKeys::LeftShift) || 
                               ViewportClient->Viewport->KeyState(EKeys::RightShift);
        if (bShiftDown && ViewportClient->Viewport->HasMouseCapture())
        {
            ViewportClient->Viewport->SetUserFocus(true);
        }
    }

    // Keep Toolkit logic
    if (TSharedPtr<FDiggerEdModeToolkit> DiggerToolkit = GetDiggerToolkit())
    {
        DiggerToolkit->SpawnOrUpdateWorklight(ViewportClient);
        DiggerToolkit->SetViewportClient(ViewportClient);
        
        if (DiggerToolkit->GetAutoUnderLandscape())
        {
            float AbsoluteZ, RelativeZ;
            DiggerToolkit->GetElevationInfo(AbsoluteZ, RelativeZ);

            const bool bShouldEnable = RelativeZ < 0.f;
            if (DiggerToolkit->GetWorklightEnabled() != bShouldEnable)
            {
                DiggerToolkit->ToggleWorklight(bShouldEnable);
            }
        }
    }

    // --- REMOVED THE CONTINUOUS TIMER LOGIC ---
    // We handle painting in CapturedMouseMove / InputDelta now.
    // Doing it here caused the "Drill in place" lag.
    
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
