#include "DiggerEdMode.h"
#include "DiggerEdModeToolkit.h"
#include "EditorModeManager.h"           // For FEditorModeTools
#include "Toolkits/ToolkitManager.h"     // For GetToolkitHost()
#include "DiggerManager.h" // ✅ Needed for ADiggerManager
#include "EngineUtils.h"

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
    if (GetMouseWorldHit(InViewportClient, HitLocation, Hit))
    {
        const bool bCtrlPressed = Click.IsControlDown();
        const bool bShiftPressed = Click.IsShiftDown();
        const bool bAltPressed = Click.IsAltDown();
        
        TSharedPtr<FDiggerEdModeToolkit> DiggerToolkit = StaticCastSharedPtr<FDiggerEdModeToolkit>(Toolkit);

        if (DiggerToolkit.IsValid())
        {
            if (bCtrlPressed) // 🔥 If holding Ctrl, sample the surface normal
            {
                FVector SurfaceNormal = Hit.ImpactNormal;
                FRotator NewRotation = SurfaceNormal.Rotation();
                
                // Optionally clamp rotation to only pitch/yaw if needed
                // NewRotation.Roll = 0.0f;

                DiggerToolkit->SetBrushRotation(NewRotation);

                UE_LOG(LogTemp, Log, TEXT("[HandleClick] Sampled surface normal and set rotation: %s"), *NewRotation.ToString());
                return true; // ✅ Done sampling, no digging.
            }
            else
            {
                // Regular digging action
                if (ADiggerManager* Digger = FindDiggerManager())
                {
                    if (DiggerToolkit->GetCurrentBrushType() == EVoxelBrushType::Cone || DiggerToolkit->GetCurrentBrushType() == EVoxelBrushType::Cylinder)
                    {
                        Digger->EditorBrushLength = DiggerToolkit->GetBrushLength();
                    }

                    Digger->EditorBrushRadius = DiggerToolkit->GetBrushRadius();
                    Digger->EditorBrushDig = DiggerToolkit->IsDigMode();
                    Digger->EditorBrushRotation = DiggerToolkit->GetBrushRotation();
                    Digger->EditorBrushAngle = DiggerToolkit->GetBrushAngle();

                    FVector VoxelCoords = HitLocation * 0.5f;
                    Digger->EditorBrushPosition = VoxelCoords;

                    Digger->ApplyBrushInEditor();
                    return true;
                }
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
