#include "DiggerEdMode.h"
#include "DiggerEdModeToolkit.h"
#include "EditorModeManager.h"           // For FEditorModeTools
#include "Toolkits/ToolkitManager.h"     // For GetToolkitHost()
#include "DiggerManager.h" // ✅ Needed for ADiggerManager
#include "EngineUtils.h"

enum class EVoxelBrushType : FPlatformTypes::uint8;
class FEditorViewportClient;
class UWorld;
// Define the Editor Mode ID
const FEditorModeID FDiggerEdMode::EM_DiggerEdModeId = TEXT("EM_DiggerEdMode");

FDiggerEdMode::FDiggerEdMode() {}
FDiggerEdMode::~FDiggerEdMode() {}

void FDiggerEdMode::Enter()
{
	FEdMode::Enter();

	if (!Toolkit.IsValid())
	{
		Toolkit = MakeShareable(new FDiggerEdModeToolkit);
		Toolkit->Init(Owner->GetToolkitHost());
	}
}

// DiggerEdMode.cpp

bool FDiggerEdMode::GetMouseWorldHit(FEditorViewportClient* ViewportClient, FVector& OutHitLocation, FHitResult& OutHit)
{
    FViewport* Viewport = ViewportClient->Viewport;
    if (!Viewport) return false;

    // Get mouse position
    FIntPoint MousePos;
    Viewport->GetMousePos(MousePos);

    // Deproject screen to world
    FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
        Viewport,
        ViewportClient->GetScene(),
        ViewportClient->EngineShowFlags
    ));
    FSceneView* SceneView = ViewportClient->CalcSceneView(&ViewFamily);

    FVector WorldOrigin, WorldDirection;
    SceneView->DeprojectFVector2D(MousePos, WorldOrigin, WorldDirection);

    // Raycast into the world
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
                    DrawDebugSphere(
                        Digger->GetWorld(),
                        HitLocation,
                        PreviewRadius,
                        32,
                        FColor(0, 255, 255, 64),
                        false, 0.f, 0,
                        2.f
                    );
                    break;
                case EVoxelBrushType::Cube:
                    DrawDebugBox(
                        Digger->GetWorld(),
                        HitLocation,
                        FVector(PreviewRadius),
                        FColor(0, 255, 0, 64),
                        false, 0.f, 0,
                        2.f
                    );
                    break;
                case EVoxelBrushType::Cylinder:
                    DrawDebugCylinder(
                        Digger->GetWorld(),
                        HitLocation - FVector(0,0,PreviewRadius),
                        HitLocation + FVector(0,0,PreviewRadius),
                        PreviewRadius,
                        32,
                        FColor(255, 255, 0, 64),
                        false, 0.f, 0,
                        2.f
                    );
                    break;
                case EVoxelBrushType::Custom:
                    // For now, just draw a magenta sphere for custom
                    DrawDebugSphere(
                        Digger->GetWorld(),
                        HitLocation,
                        PreviewRadius,
                        32,
                        FColor(255, 0, 255, 64),
                        false, 0.f, 0,
                        2.f
                    );
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

    // Retrieve the world space hit location
    if (GetMouseWorldHit(InViewportClient, HitLocation, Hit))
    {
        // Log the hit location for debugging
        UE_LOG(LogTemp, Log, TEXT("Click Location: %s"), *HitLocation.ToString());

        const bool bCtrlPressed = Click.IsControlDown();
        const bool bRightClick = Click.GetKey() == EKeys::RightMouseButton;

        TSharedPtr<FDiggerEdModeToolkit> DiggerToolkit = StaticCastSharedPtr<FDiggerEdModeToolkit>(Toolkit);

        if (DiggerToolkit.IsValid())
        {
            FVector SurfaceNormal = Hit.ImpactNormal;
            FRotator SurfaceRotation = SurfaceNormal.Rotation();

            // Handle Ctrl + Click to rotate the brush based on surface normal
            if (bCtrlPressed)
            {
                DiggerToolkit->SetBrushRotation(SurfaceRotation);
                UE_LOG(LogTemp, Log, TEXT("[HandleClick] Sampled surface normal (Ctrl): %s"), *SurfaceRotation.ToString());
                return true;
            }

            if (ADiggerManager* Digger = FindDiggerManager())
            {
                EVoxelBrushType BrushType = DiggerToolkit->GetCurrentBrushType();

                // Apply brush settings based on selected brush type
                if (BrushType == EVoxelBrushType::Debug)
                {
                    // Add verification log
                    UE_LOG(LogTemp, Error, TEXT("Found DiggerManager, calling DebugBrushPlacement with: %s"), *HitLocation.ToString());
                
                    Digger->DebugBrushPlacement(HitLocation);
                }
                
                // Apply brush settings based on selected brush type
                if (BrushType == EVoxelBrushType::Cone || BrushType == EVoxelBrushType::Cylinder)
                {
                    Digger->EditorBrushLength = DiggerToolkit->GetBrushLength();
                }

                // Handle brush rotation
                FRotator FinalRotation = DiggerToolkit->GetBrushRotation();
                if (DiggerToolkit->UseSurfaceNormalRotation())
                {
                    const FVector Normal = Hit.ImpactNormal.GetSafeNormal();
                    const FQuat AlignRotation = FQuat::FindBetweenNormals(FVector::UpVector, Normal);
                    FinalRotation = (AlignRotation * FinalRotation.Quaternion()).Rotator();
                }

                // Determine brush dig mode - base from UI setting
                bool bFinalBrushDig = DiggerToolkit->IsDigMode();

                // Override based on right-click
                if (bRightClick)
                {
                    bFinalBrushDig = !bFinalBrushDig;
                    // Temporary override UI display for visual feedback
                    DiggerToolkit->SetTemporaryDigOverride(bFinalBrushDig);
                }
                else
                {
                    DiggerToolkit->SetTemporaryDigOverride(TOptional<bool>());
                }

                // Store settings for continuous application
                ContinuousSettings.bFinalBrushDig = bFinalBrushDig;
                ContinuousSettings.FinalRotation = FinalRotation;
                ContinuousSettings.bCtrlPressed = bCtrlPressed;
                ContinuousSettings.bRightClick = bRightClick;
                ContinuousSettings.bIsValid = true;

                // Apply brush settings
                Digger->EditorBrushRadius = DiggerToolkit->GetBrushRadius();
                Digger->EditorBrushDig = bFinalBrushDig;
                Digger->EditorBrushRotation = FinalRotation;
                Digger->EditorBrushAngle = DiggerToolkit->GetBrushAngle();

                // Brush offset calculation
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

                // Set final brush position
                Digger->EditorBrushOffset = FinalOffset;
                Digger->EditorBrushPosition = HitLocation;
                
                // Apply brush ONCE with the correct dig mode
                Digger->ApplyBrushInEditor(bFinalBrushDig);

                // Now use the location from the click to perform chunk operations
                FVector ChunkPosition = HitLocation / (Digger->TerrainGridSize * Digger->ChunkSize);
                ChunkPosition = FVector(FMath::FloorToInt(ChunkPosition.X), FMath::FloorToInt(ChunkPosition.Y), FMath::FloorToInt(ChunkPosition.Z));
                
                // Start continuous application if mouse button is down
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
    // Toggle paint mode
    if (Key == EKeys::P && Event == IE_Pressed)
    {
        SetPaintMode(!bPaintingEnabled);
        UE_LOG(LogTemp, Log, TEXT("Paint mode %s"), bPaintingEnabled ? TEXT("enabled") : TEXT("disabled"));
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

            // Construct scene view and get deprojected ray
            FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
                ViewportClient->Viewport,
                ViewportClient->GetScene(),
                ViewportClient->EngineShowFlags)
                .SetRealtimeUpdate(true));

            FSceneView* View = ViewportClient->CalcSceneView(&ViewFamily);

            FVector WorldOrigin;
            FVector WorldDirection;
            View->DeprojectFVector2D(LastMousePosition, WorldOrigin, WorldDirection);

            // Perform raycast to get hit info
            FHitResult Hit;
            FVector HitLocation;
            if (GetMouseWorldHit(ViewportClient, HitLocation, Hit))
            {
                ContinuousSettings.bIsValid = true;
                ContinuousSettings.FinalRotation = ViewportClient->GetViewRotation();
                ContinuousSettings.bCtrlPressed = Viewport->KeyState(EKeys::LeftControl) || Viewport->KeyState(EKeys::RightControl);
                ContinuousSettings.bRightClick = (Key == EKeys::RightMouseButton);
                ContinuousSettings.bFinalBrushDig = ContinuousSettings.bRightClick;

                // Optional: store brush decal location if using visual
                // UpdateBrushVisual(HitLocation);

                // 👉 Immediate paint stroke here
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
    // FHitResult HitResult;
    // FVector End = RayOrigin + RayDirection * 10000.0f;

    // Use your brush shape's smart trace
    //HitResult = FindDiggerManager()->ActiveBrush->SmartTrace(RayOrigin, End);

    // if (HitResult.bBlockingHit)
    // {
    //     UE_LOG(LogTemp, Log, TEXT("Hit at: %s"), *HitResult.ImpactPoint.ToString());
    //     return true;
    // }

    return false;
}





bool FDiggerEdMode::InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale)
{
    // If we're in continuous mode and mouse is moving, apply brush
    if (bIsContinuouslyApplying && bMouseButtonDown)
    {
        FVector2D CurrentMousePosition = FVector2D(InViewport->GetMouseX(), InViewport->GetMouseY());
        
        // Check if mouse has moved significantly
        float MouseMoveDelta = FVector2D::Distance(CurrentMousePosition, LastMousePosition);
        if (MouseMoveDelta > 2.0f) // Minimum movement threshold
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
        return true; // We handle our own tracking
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
            DiggerToolkit->SetTemporaryDigOverride(TOptional<bool>()); // Clear override
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
        
        // Only apply if mouse moved enough (prevents over-application)
        if (DistanceMoved > 5.0f) // Adjust threshold as needed
        {
            ApplyContinuousBrush(InViewportClient);
            LastPaintLocation = CurrentMousePos;
        }
        
        return true; // Consume the mouse move
    }
    
    return false; // Let viewport handle camera movement
}



void FDiggerEdMode::StartContinuousApplication(const FViewportClick& Click)
{
    if (!ContinuousSettings.bIsValid || ContinuousSettings.bCtrlPressed)
    {
        return; // Don't start continuous mode for Ctrl+Click or invalid settings
    }

    bIsContinuouslyApplying = true;
    ContinuousApplicationTimer = 0.0f;
    
    UE_LOG(LogTemp, Log, TEXT("Started continuous brush application"));
}

void FDiggerEdMode::StopContinuousApplication()
{
    if (bIsContinuouslyApplying)
    {
        bIsContinuouslyApplying = false;
        ContinuousApplicationTimer = 0.0f;
        ContinuousSettings.bIsValid = false;
        
        // Clear any temporary overrides
        TSharedPtr<FDiggerEdModeToolkit> DiggerToolkit = StaticCastSharedPtr<FDiggerEdModeToolkit>(Toolkit);
        if (DiggerToolkit.IsValid())
        {
            DiggerToolkit->SetTemporaryDigOverride(TOptional<bool>());
        }
        
        UE_LOG(LogTemp, Log, TEXT("Stopped continuous brush application"));
    }
}

void FDiggerEdMode::ApplyContinuousBrush(FEditorViewportClient* InViewportClient)
{
    if (!ContinuousSettings.bIsValid)
    {
        return;
    }

    FVector HitLocation;
    FHitResult Hit;

    
    // Get current mouse world hit location
    if (GetMouseWorldHit(InViewportClient, HitLocation, Hit))
    {
        ADiggerManager* Digger = FindDiggerManager();
        if (!Digger)
        {
            return;
        }

        TSharedPtr<FDiggerEdModeToolkit> DiggerToolkit = StaticCastSharedPtr<FDiggerEdModeToolkit>(Toolkit);
        if (!DiggerToolkit.IsValid())
        {
            return;
        }

        // Reapply all the same logic from HandleClick for the current mouse position
        FRotator FinalRotation = ContinuousSettings.FinalRotation;
        if (DiggerToolkit->UseSurfaceNormalRotation())
        {
            const FVector Normal = Hit.ImpactNormal.GetSafeNormal();
            const FQuat AlignRotation = FQuat::FindBetweenNormals(FVector::UpVector, Normal);
            FinalRotation = (AlignRotation * ContinuousSettings.FinalRotation.Quaternion()).Rotator();
        }

        // Apply brush settings with stored continuous settings
        Digger->EditorBrushRadius = DiggerToolkit->GetBrushRadius();
        Digger->EditorBrushDig = ContinuousSettings.bFinalBrushDig;
        Digger->EditorBrushRotation = FinalRotation;
        Digger->EditorBrushAngle = DiggerToolkit->GetBrushAngle();

        // Brush offset calculation
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

        // Set final brush position and apply
        Digger->EditorBrushOffset = FinalOffset;
        Digger->EditorBrushPosition = HitLocation;
        
        // Apply brush with stored settings
        Digger->ApplyBrushInEditor(ContinuousSettings.bFinalBrushDig);

        UE_LOG(LogTemp, VeryVerbose, TEXT("Applied continuous brush at: %s"), *HitLocation.ToString());
    }
}

bool FDiggerEdMode::ShouldApplyContinuously() const
{
    return bIsContinuouslyApplying && bMouseButtonDown && ContinuousSettings.bIsValid;
}


void FDiggerEdMode::Exit()
{
	if (Toolkit.IsValid())
	{
		FToolkitManager::Get().CloseToolkit(Toolkit.ToSharedRef());
		Toolkit.Reset();
	}

	FEdMode::Exit();
}

void FDiggerEdMode::AddReferencedObjects(FReferenceCollector& Collector)
{
	FEdMode::AddReferencedObjects(Collector);
	// Add UObject references here if you start using them
}

void FDiggerEdMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	FEdMode::Tick(ViewportClient, DeltaTime);

    if (GEditor)
    {
        GEditor->RedrawAllViewports();
    }
    
    // Handle time-based continuous application
    if (bIsContinuouslyApplying && bMouseButtonDown)
    {
        ContinuousApplicationTimer += DeltaTime;
        
       // if (ContinuousApplicationTimer >= ContinuousApplicationInterval)
        {
            ApplyContinuousBrush(ViewportClient);
            ContinuousApplicationTimer = 0.0f;
        }
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
	return nullptr;
}


