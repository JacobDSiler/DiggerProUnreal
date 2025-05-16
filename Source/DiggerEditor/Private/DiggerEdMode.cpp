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

    FCollisionQueryParams Params(SCENE_QUERY_STAT(EditorBrushTrace), true);
    UWorld* World = ViewportClient->GetWorld();
    if (World && World->LineTraceSingleByChannel(OutHit, TraceStart, TraceEnd, ECC_Visibility, Params))
    {
        OutHitLocation = OutHit.ImpactPoint;
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

                // Brush digging/building state from UI
                bool bFinalBrushDig = Digger->EditorBrushDig;

                // Override based on right-click
                if (bRightClick)
                {
                    bFinalBrushDig = !bFinalBrushDig;
                }

                // Apply brush settings
                Digger->EditorBrushRadius = DiggerToolkit->GetBrushRadius();
                // Do NOT set Digger->EditorBrushDig here!
                // Instead, pass bFinalBrushDig to the brush application:
                Digger->ApplyBrushInEditor(bFinalBrushDig); // <-- pass as parameter
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
                Digger->EditorBrushPosition = HitLocation;// * 0.5f;  // Adjust the location if needed
                Digger->ApplyBrushInEditor(DiggerToolkit->IsDigMode());

                // Temporary override UI display for visual feedback
                if (bRightClick)
                {
                    DiggerToolkit->SetTemporaryDigOverride(bFinalBrushDig);
                }
                else
                {
                    DiggerToolkit->SetTemporaryDigOverride(TOptional<bool>());
                }

                // Now use the location from the click to perform chunk operations
                // Convert world position (HitLocation) to chunk coordinates
                FVector ChunkPosition = HitLocation / (Digger->TerrainGridSize * Digger->ChunkSize);
                ChunkPosition = FVector(FMath::FloorToInt(ChunkPosition.X), FMath::FloorToInt(ChunkPosition.Y), FMath::FloorToInt(ChunkPosition.Z));

                /*
                // Now use ChunkPosition to retrieve the correct chunk from the manager
                if (DiggerToolkit->GetDetailedDebug())
                {
                    // Get the chunk at this hit location and call the debug method
                    if (ADiggerManager* Manager = FindDiggerManager())
                    {
                        if (UVoxelChunk* Chunk = Manager->GetOrCreateChunkAtChunk(ChunkPosition))
                        {
                            Chunk->DebugPrintVoxelData(); // Show detailed debug
                        }
                    }
                }
                else
                {
                    // Use the basic chunk debug drawing
                    if (ADiggerManager* Manager = FindDiggerManager())
                    {
                        if (UVoxelChunk* Chunk = Manager->GetOrCreateChunkAtChunk(ChunkPosition))
                        {
                            Chunk->DebugDrawChunk(); // Show basic chunk debug
                        }
                    }
                }*/

                return true;
            }
        }
    }

    return false;
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
	// Add any tool update logic here
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

bool FDiggerEdMode::EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
    if (Toolkit.IsValid())
    {
        if (TSharedPtr<FDiggerEdModeToolkit> DiggerToolkit = StaticCastSharedPtr<FDiggerEdModeToolkit>(Toolkit))
        {
            DiggerToolkit->SetTemporaryDigOverride(TOptional<bool>()); // Clear override
        }
    }

    return FEdMode::EndTracking(InViewportClient, InViewport); // Return base result
}

