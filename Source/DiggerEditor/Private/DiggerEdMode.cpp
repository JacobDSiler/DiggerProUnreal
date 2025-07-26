// Copyright Epic Games, Inc. All Rights Reserved.

#include "DiggerEdMode.h"
#include "DiggerEdModeToolkit.h"
#include "EditorModeManager.h"
#include "Toolkits/ToolkitManager.h"
#include "DiggerManager.h"
#include "EngineUtils.h"
#include "EditorViewportClient.h"
#include "Engine/Selection.h"
#include "Landscape.h"
#include "LandscapeDataAccess.h"
#include "LandscapeEdit.h"
#include "ScopedTransaction.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"

#define LOCTEXT_NAMESPACE "DiggerEditorMode"

// Static member definitions
const FEditorModeID FDiggerEdMode::EM_DiggerEdModeId = TEXT("EM_DiggerEdMode");
FOnDiggerModeChanged FDiggerEdMode::OnDiggerModeChanged;
bool FDiggerEdMode::bIsDiggerModeCurrentlyActive = false;

FDiggerEdMode::FDiggerEdMode() {}
FDiggerEdMode::~FDiggerEdMode() {}

void FDiggerEdMode::DeselectAllSceneActors()
{
    // Deselect all actors when entering Digger Mode
    GEditor->GetSelectedActors()->DeselectAll();
}

void FDiggerEdMode::Enter()
{
    FEdMode::Enter();

    DeselectAllSceneActors();

    if (!Toolkit.IsValid())
    {
        OnDiggerModeChanged.Broadcast(true);
        bIsDiggerModeCurrentlyActive = true;
        Toolkit = MakeShareable(new FDiggerEdModeToolkit);
        Toolkit->Init(Owner->GetToolkitHost());
    }
}

void FDiggerEdMode::Exit()
{
    if (Toolkit.IsValid())
    {
        OnDiggerModeChanged.Broadcast(false);
        bIsDiggerModeCurrentlyActive = false;
        FToolkitManager::Get().CloseToolkit(Toolkit.ToSharedRef());
        Toolkit.Reset();
        static_cast<FDiggerEdModeToolkit*>(Toolkit.Get())->ShutdownNetworking();
    }
    FEdMode::Exit();
}

bool FDiggerEdMode::GetMouseWorldHit(FEditorViewportClient* ViewportClient, FVector& OutHitLocation, FHitResult& OutHit)
{
    FViewport* Viewport = ViewportClient->Viewport;
    if (!Viewport) return false;

    FIntPoint MousePos;
    Viewport->GetMousePos(MousePos);

    FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
        Viewport,
        ViewportClient->GetScene(),
        ViewportClient->EngineShowFlags
    ));
    FSceneView* SceneView = ViewportClient->CalcSceneView(&ViewFamily);

    FVector WorldOrigin, WorldDirection;
    SceneView->DeprojectFVector2D(MousePos, WorldOrigin, WorldDirection);

    FVector TraceStart = WorldOrigin;
    FVector TraceEnd = WorldOrigin + WorldDirection * 100000.f;

    FHitResult Hit = FindDiggerManager()->ActiveBrush->SmartTrace(TraceStart, TraceEnd);

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
    FEditorViewportClient* ViewportClient = (FEditorViewportClient*)Viewport->GetClient();
    FVector HitLocation;
    FHitResult Hit;
    if (GetMouseWorldHit(ViewportClient, HitLocation, Hit))
    {
        if (ADiggerManager* Digger = FindDiggerManager())
        {
            float PreviewRadius = Digger->EditorBrushRadius;
            EVoxelBrushType BrushType = Digger->EditorBrushType;

            switch (BrushType)
            {
                case EVoxelBrushType::Sphere:
                    DrawDebugSphere(Digger->GetWorld(), HitLocation, PreviewRadius, 32, FColor(0, 255, 255, 64), false, 0.f, 0, 2.f);
                    break;
                case EVoxelBrushType::Cube:
                    DrawDebugBox(Digger->GetWorld(), HitLocation, FVector(PreviewRadius), FColor(0, 255, 0, 64), false, 0.f, 0, 2.f);
                    break;
                case EVoxelBrushType::Cylinder:
                    DrawDebugCylinder(Digger->GetWorld(), HitLocation - FVector(0,0,PreviewRadius), HitLocation + FVector(0,0,PreviewRadius), PreviewRadius, 32, FColor(255, 255, 0, 64), false, 0.f, 0, 2.f);
                    break;
                case EVoxelBrushType::Custom:
                    DrawDebugSphere(Digger->GetWorld(), HitLocation, PreviewRadius, 32, FColor(255, 0, 255, 64), false, 0.f, 0, 2.f);
                    break;
                default:
                    break;
            }
        }
    }
}


bool FDiggerEdMode::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
    FVector HitLocation;
    FHitResult Hit;

    DeselectAllSceneActors();

    if (GetMouseWorldHit(InViewportClient, HitLocation, Hit))
    {
        const bool bCtrlPressed = Click.IsControlDown();
        const bool bRightClick = Click.GetKey() == EKeys::RightMouseButton;

        TSharedPtr<FDiggerEdModeToolkit> DiggerToolkit = StaticCastSharedPtr<FDiggerEdModeToolkit>(Toolkit);

        if (DiggerToolkit.IsValid())
        {
            FVector SurfaceNormal = Hit.ImpactNormal;
            FRotator SurfaceRotation = SurfaceNormal.Rotation();

            if (bCtrlPressed)
            {
                DiggerToolkit->SetBrushRotation(SurfaceRotation);
                return true;
            }

            if (ADiggerManager* Digger = FindDiggerManager())
            {
                EVoxelBrushType BrushType = DiggerToolkit->GetCurrentBrushType();
                Digger->EditorBrushType = BrushType; // ✅ IMPORTANT: assign brush type

                if (BrushType == EVoxelBrushType::Debug)
                {
                    Digger->DebugBrushPlacement(HitLocation);
                }
                if (BrushType == EVoxelBrushType::Cone || BrushType == EVoxelBrushType::Cylinder)
                {
                    Digger->EditorBrushLength = DiggerToolkit->GetBrushLength();
                }

                FRotator FinalRotation = DiggerToolkit->GetBrushRotation();
                if (DiggerToolkit->UseSurfaceNormalRotation())
                {
                    const FVector Normal = Hit.ImpactNormal.GetSafeNormal();
                    const FQuat AlignRotation = FQuat::FindBetweenNormals(FVector::UpVector, Normal);
                    FinalRotation = (AlignRotation * FinalRotation.Quaternion()).Rotator();
                }

                bool bFinalBrushDig = DiggerToolkit->IsDigMode();
                if (bRightClick)
                {
                    bFinalBrushDig = !bFinalBrushDig;
                    DiggerToolkit->SetTemporaryDigOverride(bFinalBrushDig);
                }
                else
                {
                    DiggerToolkit->SetTemporaryDigOverride(TOptional<bool>());
                }

                // Assign all necessary brush settings
                Digger->EditorBrushRadius = DiggerToolkit->GetBrushRadius();
                Digger->EditorBrushDig = bFinalBrushDig;
                Digger->EditorBrushRotation = FinalRotation;
                Digger->EditorBrushAngle = DiggerToolkit->GetBrushAngle();
                Digger->EditorBrushHoleShape = GetHoleShapeForBrush(BrushType);
                Digger->EditorBrushLightType = DiggerToolkit->GetCurrentLightType(); // ✅ Light Type
                Digger->EditorBrushLightColor = DiggerToolkit->GetCurrentLightColor(); // ✅ Light Color
                UE_LOG(LogTemp, Warning, TEXT("Toolkit is passing light type: %d"), (int32)Digger->EditorBrushLightType);

                if (DiggerToolkit->IsUsingAdvancedCubeBrush())
                {
                    Digger->EditorbUseAdvancedCubeBrush = true;
                    Digger->EditorCubeHalfExtentX = DiggerToolkit->GetAdvancedCubeHalfExtentX();
                    Digger->EditorCubeHalfExtentY = DiggerToolkit->GetAdvancedCubeHalfExtentY();
                    Digger->EditorCubeHalfExtentZ = DiggerToolkit->GetAdvancedCubeHalfExtentZ();
                    UE_LOG(LogTemp, Warning, TEXT("DiggerEdMode.cpp: Extent X: %.2f  Extent Y: %.2f  Extent Z: %.2f"),
                        Digger->EditorCubeHalfExtentX,
                        Digger->EditorCubeHalfExtentY,
                        Digger->EditorCubeHalfExtentZ);
                }
                else
                {
                    Digger->EditorbUseAdvancedCubeBrush = false;
                }

                FVector Offset = DiggerToolkit->GetBrushOffset();
                FVector OffsetXY = FVector(Offset.X, Offset.Y, 0.f);
                float ZDistance = Offset.Z;
                FVector FinalOffset = OffsetXY;

                if (DiggerToolkit->RotateToSurfaceNormal())
                {
                    FinalOffset += Hit.ImpactNormal * ZDistance;
                }
                else
                {
                    FinalOffset.Z = ZDistance;
                }

                Digger->EditorBrushOffset = FinalOffset;
                Digger->EditorBrushPosition = HitLocation;
                Digger->ApplyBrushInEditor(bFinalBrushDig);

                FVector ChunkPosition = HitLocation / (Digger->TerrainGridSize * Digger->ChunkSize);
                ChunkPosition = FVector(FMath::FloorToInt(ChunkPosition.X), FMath::FloorToInt(ChunkPosition.Y), FMath::FloorToInt(ChunkPosition.Z));

                if (bMouseButtonDown)
                {
                    StartContinuousApplication(Click);
                }

                return true;
            }
        }
    }

    return false;
}



bool FDiggerEdMode::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
    DeselectAllSceneActors();
    
    if (Key == EKeys::P && Event == IE_Pressed)
    {
        SetPaintMode(!bPaintingEnabled);
        return true;
    }

    if (bPaintingEnabled && (Key == EKeys::LeftMouseButton || Key == EKeys::RightMouseButton))
    {
        if (Event == IE_Pressed)
        {
            bMouseButtonDown = true;
            bIsContinuouslyApplying = true;
            bIsPainting = true;
            ContinuousApplicationTimer = 0.0f;
            LastMousePosition = FVector2D(Viewport->GetMouseX(), Viewport->GetMouseY());

            FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
                ViewportClient->Viewport,
                ViewportClient->GetScene(),
                ViewportClient->EngineShowFlags)
                .SetRealtimeUpdate(true));

            FSceneView* View = ViewportClient->CalcSceneView(&ViewFamily);

            FVector WorldOrigin;
            FVector WorldDirection;
            View->DeprojectFVector2D(LastMousePosition, WorldOrigin, WorldDirection);

            FHitResult Hit;
            FVector HitLocation;
            if (GetMouseWorldHit(ViewportClient, HitLocation, Hit))
            {
                ContinuousSettings.bIsValid = true;
                ContinuousSettings.FinalRotation = ViewportClient->GetViewRotation();
                ContinuousSettings.bCtrlPressed = Viewport->KeyState(EKeys::LeftControl) || Viewport->KeyState(EKeys::RightControl);
                ContinuousSettings.bRightClick = (Key == EKeys::RightMouseButton);
                ContinuousSettings.bFinalBrushDig = ContinuousSettings.bRightClick;
                ApplyContinuousBrush(ViewportClient);
            }

            LastPaintLocation = LastMousePosition;
            return true;
        }
        else if (Event == IE_Released)
        {
            bMouseButtonDown = false;
            bIsPainting = false;
            bIsContinuouslyApplying = false;
            ContinuousSettings.bIsValid = false;
            StopContinuousApplication();
            return true;
        }
    }

    return FEdMode::InputKey(ViewportClient, Viewport, Key, Event);
}

bool FDiggerEdMode::HandleClickSimple(const FVector& RayOrigin, const FVector& RayDirection)
{
    return false;
}

bool FDiggerEdMode::IsDiggerModeActive()
{
    return bIsDiggerModeCurrentlyActive;
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
        return true;
    }
    return FEdMode::StartTracking(InViewportClient, InViewport);
}

bool FDiggerEdMode::EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
    if (bIsPainting)
    {
        bIsPainting = false;
        StopContinuousApplication();
        return true;
    }

    if (Toolkit.IsValid())
    {
        if (TSharedPtr<FDiggerEdModeToolkit> DiggerToolkit = StaticCastSharedPtr<FDiggerEdModeToolkit>(Toolkit))
        {
            DiggerToolkit->SetTemporaryDigOverride(TOptional<bool>());
        }
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

void FDiggerEdMode::StartContinuousApplication(const FViewportClick& Click)
{
    if (!ContinuousSettings.bIsValid || ContinuousSettings.bCtrlPressed)
    {
        return;
    }
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
        TSharedPtr<FDiggerEdModeToolkit> DiggerToolkit = StaticCastSharedPtr<FDiggerEdModeToolkit>(Toolkit);
        if (DiggerToolkit.IsValid())
        {
            DiggerToolkit->SetTemporaryDigOverride(TOptional<bool>());
        }
    }
}

void FDiggerEdMode::ApplyContinuousBrush(FEditorViewportClient* InViewportClient)
{
    if (!ContinuousSettings.bIsValid)
    {
        UE_LOG(LogTemp, Error, TEXT("ContinuousSettings are invalid in DiggerEdMode ApplyContinuousBrush!"));
        return;
    }

    FVector HitLocation;
    FHitResult Hit;
    if (GetMouseWorldHit(InViewportClient, HitLocation, Hit))
    {
        ADiggerManager* Digger = FindDiggerManager();
        if (!Digger)
        {
            UE_LOG(LogTemp, Warning, TEXT("No DiggerManager in in DiggerEdMode ApplyContinuousBrush!"));
            return;
        }

        TSharedPtr<FDiggerEdModeToolkit> DiggerToolkit = StaticCastSharedPtr<FDiggerEdModeToolkit>(Toolkit);
        if (!DiggerToolkit.IsValid())
        {
            UE_LOG(LogTemp, Warning, TEXT("No DiggerToolKit in in DiggerEdMode ApplyContinuousBrush!"));
            return;
        }

        EVoxelBrushType BrushType = DiggerToolkit->GetCurrentBrushType();

        FRotator FinalRotation = ContinuousSettings.FinalRotation;
        if (DiggerToolkit->UseSurfaceNormalRotation())
        {
            const FVector Normal = Hit.ImpactNormal.GetSafeNormal();
            const FQuat AlignRotation = FQuat::FindBetweenNormals(FVector::UpVector, Normal);
            FinalRotation = (AlignRotation * ContinuousSettings.FinalRotation.Quaternion()).Rotator();
        }

        Digger->EditorBrushRadius = DiggerToolkit->GetBrushRadius();
        Digger->EditorBrushDig = ContinuousSettings.bFinalBrushDig;
        Digger->EditorBrushRotation = FinalRotation;
        Digger->EditorBrushIsFilled = DiggerToolkit->GetBrushIsFilled();
        Digger->EditorBrushAngle = DiggerToolkit->GetBrushAngle();
        Digger->EditorBrushHoleShape = GetHoleShapeForBrush(BrushType);

        
        if(DiggerToolkit->IsUsingAdvancedCubeBrush())
        {
            
            Digger->EditorbUseAdvancedCubeBrush = true;
            Digger->EditorCubeHalfExtentX = DiggerToolkit->GetAdvancedCubeHalfExtentX();
            Digger->EditorCubeHalfExtentY = DiggerToolkit->GetAdvancedCubeHalfExtentY();
            Digger->EditorCubeHalfExtentZ = DiggerToolkit->GetAdvancedCubeHalfExtentZ();
            UE_LOG(LogTemp, Warning, TEXT("DiggerEdMode.cpp: Extent X: %.2f  Extent Y: %.2f  Extent Z: %.2f"),
           Digger->EditorCubeHalfExtentX,
           Digger->EditorCubeHalfExtentY,
           Digger->EditorCubeHalfExtentZ);

        }
        else
        {
            Digger->EditorbUseAdvancedCubeBrush = false;
        }
        
        FVector Offset = DiggerToolkit->GetBrushOffset();
        FVector OffsetXY = FVector(Offset.X, Offset.Y, 0.f);
        float ZDistance = Offset.Z;
        FVector FinalOffset = OffsetXY;

        if (DiggerToolkit->RotateToSurfaceNormal())
        {
            FinalOffset += Hit.ImpactNormal * ZDistance;
        }
        else
        {
            FinalOffset.Z = ZDistance;
        }

        Digger->EditorBrushOffset = FinalOffset;
        Digger->EditorBrushPosition = HitLocation;
        Digger->ApplyBrushInEditor(ContinuousSettings.bFinalBrushDig);
    }
}

bool FDiggerEdMode::ShouldApplyContinuously() const
{
    return bIsContinuouslyApplying && bMouseButtonDown && ContinuousSettings.bIsValid;
}

void FDiggerEdMode::AddReferencedObjects(FReferenceCollector& Collector)
{
    FEdMode::AddReferencedObjects(Collector);
}

void FDiggerEdMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
    FEdMode::Tick(ViewportClient, DeltaTime);
    
    if (ShouldApplyContinuously())
    {
        ContinuousApplicationTimer += DeltaTime;
        ApplyContinuousBrush(ViewportClient);
        ContinuousApplicationTimer = 0.0f;
    }

    if (GEditor)
    {
        GEditor->RedrawAllViewports();
    }
}

bool FDiggerEdMode::UsesToolkits() const
{
    return true;
}

ADiggerManager* FDiggerEdMode::FindDiggerManager()
{
    UWorld* World = GEditor->GetEditorWorldContext().World();
    for (TActorIterator<ADiggerManager> It(World); It; ++It)
    {
        return *It;
    }
    UE_LOG(LogTemp, Error, TEXT("DiggerManager not found in: FDiggerEdMode::FindDiggerManager()!!!"));
    return nullptr;
}

#undef LOCTEXT_NAMESPACE
