#include "VoxelBrushShape.h"

#include "DiggerManager.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "GameFramework/GameModeBase.h"
#include "Kismet/GameplayStatics.h"

UVoxelBrushShape::UVoxelBrushShape()
    : BrushSize(150.0f), SDFChange(0), bIsDigging(false), World(nullptr), BrushRotation(FRotator::ZeroRotator),
      BrushLength(1.0f),
      BrushAngle(45.f), DiggerManager(nullptr), TargetChunk(nullptr), BrushType(EVoxelBrushType::Sphere)
{
}



bool UVoxelBrushShape::EnsureDiggerManager()
{
    // Get the terrain and grid settings from the DiggerManager
    if (!DiggerManager)
    {
        for (TActorIterator<ADiggerManager> It(World); It; ++It)
        {
            DiggerManager = *It;
            break;
        }
    }

    if (!DiggerManager)
    {
        UE_LOG(LogTemp, Error, TEXT("DiggerManager is null!"));
        return true;
    }
    return false;
}

void UVoxelBrushShape::InitializeBrush(EVoxelBrushType InBrushType, float InSize, FVector InLocation, ADiggerManager* DiggerManagerRef)
{
    BrushType = InBrushType;
    BrushSize = InSize;
    BrushLocation = InLocation;

    if(!World) World = GetSafeWorld();
    DiggerManager = DiggerManagerRef;
}


// Helper function to retrieve the target chunk from a brush position
UVoxelChunk* UVoxelBrushShape::GetTargetChunkFromBrushPosition(const FVector3d& BrushPosition)
{
    // Get a reference to the Digger Manager (or the system managing your chunks)
    if (!DiggerManager) return nullptr;

    // Calculate the chunk position based on the brush position
    FIntVector ChunkPosition = FVoxelConversion::WorldToChunk(BrushPosition);

    // Retrieve the chunk from the map
    UVoxelChunk* NewTargetChunk = DiggerManager->GetOrCreateChunkAtChunk(ChunkPosition);
    
    // Return the chunk if found, otherwise return nullptr
    return (NewTargetChunk) ? NewTargetChunk : nullptr;
}

UWorld* UVoxelBrushShape::GetSafeWorld() const
{
#if WITH_EDITOR
    if (GIsEditor && GEditor)
    {
        return GEditor->GetEditorWorldContext().World();
    }
#endif
    return GetWorld();
}

// Add this method to VoxelBrushShape
FBrushStroke UVoxelBrushShape::CreateBrushStroke(const FHitResult& HitResult, bool bIsDig) const
{
    FBrushStroke NewStroke;
    NewStroke.BrushPosition = HitResult.Location;  // This is the key - get position from hit
    NewStroke.BrushRadius = BrushSize;
    NewStroke.BrushFalloff = 0.f;
    NewStroke.BrushStrength = 1.f;
    NewStroke.bDig = bIsDig;
    NewStroke.BrushType = BrushType;

    return NewStroke;
}


bool UVoxelBrushShape::GetCameraHitLocation(FHitResult& OutHitResult) const
{
    // Your existing world detection code...
    UWorld* TargetWorld = nullptr;
    
    if (GEngine && GEngine->GameViewport && GEngine->GameViewport->GetWorld())
    {
        TargetWorld = GEngine->GameViewport->GetWorld();
    }
    else if (UWorld* ComponentWorld = GetWorld())
    {
        TargetWorld = ComponentWorld;
    }
    else
    {
        TargetWorld = GetSafeWorld();
    }

    if (!TargetWorld)
    {
        UE_LOG(LogTemp, Warning, TEXT("GetCameraHitLocation:: No valid world found"));
        return false;
    }

    APlayerController* PlayerController = TargetWorld->GetFirstPlayerController();
    if (!PlayerController)
    {
        UE_LOG(LogTemp, Warning, TEXT("GetCameraHitLocation:: No PlayerController found"));
        return false;
    }

    float MouseX, MouseY;
    if (!PlayerController->GetMousePosition(MouseX, MouseY))
    {
        UE_LOG(LogTemp, Warning, TEXT("GetCameraHitLocation:: Could not get mouse position"));
        return false;
    }

    FVector WorldPosition;
    FVector WorldDirection;
    if (PlayerController->DeprojectScreenPositionToWorld(MouseX, MouseY, WorldPosition, WorldDirection))
    {
        FVector TraceStart = WorldPosition;
        FVector TraceEnd = WorldPosition + (WorldDirection * 50000.0f);

        // Use the complex trace system instead of simple line trace
        FHitResult ComplexHit = PerformComplexTrace(TraceStart, TraceEnd, PlayerController->GetPawn());
        
        if (ComplexHit.bBlockingHit)
        {
            OutHitResult = ComplexHit;
            
            // Log what we hit for debugging
            if (OutHitResult.GetActor())
            {
                UE_LOG(LogTemp, Warning, TEXT("GetCameraHitLocation:: Hit Actor: %s, Component: %s"), 
                    *OutHitResult.GetActor()->GetName(), 
                    OutHitResult.GetComponent() ? *OutHitResult.GetComponent()->GetName() : TEXT("None"));
            }
            
            return true;
        }
        
        // Fallback to simple trace if complex trace fails
        FCollisionQueryParams QueryParams;
        QueryParams.bTraceComplex = true;
        QueryParams.AddIgnoredActor(PlayerController->GetPawn());

        if (TargetWorld->LineTraceSingleByChannel(OutHitResult, TraceStart, TraceEnd, ECC_WorldStatic, QueryParams))
        {
            UE_LOG(LogTemp, Warning, TEXT("GetCameraHitLocation:: Fallback Hit Actor: %s"), 
                OutHitResult.GetActor() ? *OutHitResult.GetActor()->GetName() : TEXT("None"));
            return true;
        }
        else if (TargetWorld->LineTraceSingleByChannel(OutHitResult, TraceStart, TraceEnd, ECC_Visibility, QueryParams))
        {
            UE_LOG(LogTemp, Warning, TEXT("GetCameraHitLocation:: Fallback Hit Actor (Visibility): %s"), 
                   OutHitResult.GetActor() ? *OutHitResult.GetActor()->GetName() : TEXT("None"));
            return true;
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("GetCameraHitLocation:: No valid hit found."));
    return false;
}



FHitResult UVoxelBrushShape::PerformComplexTrace(const FVector& Start, const FVector& End, AActor* IgnoredActor) const
{          
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("World is null in PerformComplexTrace!"));
        return FHitResult();
    }

    FCollisionQueryParams CollisionParams;
    CollisionParams.bTraceComplex = true;
    if (IgnoredActor)
    {
        CollisionParams.AddIgnoredActor(IgnoredActor);
    }

    FHitResult InitialHit;
    bool bHit = World->LineTraceSingleByChannel(InitialHit, Start, End, ECC_Visibility, CollisionParams);

    if (!bHit)
    {
        UE_LOG(LogTemp, Warning, TEXT("No hit in PerformComplexTrace."));
        return InitialHit; // Return invalid hit result
    }

    AActor* HitActor = InitialHit.GetActor();
    if (!HitActor)
    {
        UE_LOG(LogTemp, Warning, TEXT("Initial hit actor is null."));
        return InitialHit;
    }

    UE_LOG(LogTemp, Warning, TEXT("Initial hit: %s"), *HitActor->GetName());

    // If we didn't hit a hole, return the initial hit
    if (!IsHoleBPActor(HitActor))
    {
        return InitialHit;
    }

    // We hit a hole, trace through it
    UE_LOG(LogTemp, Warning, TEXT("Hit HoleBP, tracing through..."));
    FHitResult ThroughHoleHit = TraceThroughHole(InitialHit, End, IgnoredActor);
    
    // Return the result from tracing through the hole
    return ThroughHoleHit;
}

bool UVoxelBrushShape::IsHoleBPActor(AActor* Actor) const
{
    if (!Actor)
    {
        UE_LOG(LogTemp, Warning, TEXT("IsHoleBPActor: Actor is null!"));
        return false;
    }
    if (!DiggerManager)
    {
        UE_LOG(LogTemp, Warning, TEXT("IsHoleBPActor: DiggerManager is null!"));
        return false;
    }
    if (!DiggerManager->HoleBP)
    {
        UE_LOG(LogTemp, Warning, TEXT("IsHoleBPActor: HoleBP is null!"));
        return false;
    }

    // Check if the actor is of the HoleBP class
    return Actor->GetClass() == DiggerManager->HoleBP;
}

FHitResult UVoxelBrushShape::TraceThroughHole(const FHitResult& HoleHit, const FVector& End, AActor* IgnoredActor) const
{
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("World is null in TraceThroughHole!"));
        return FHitResult();
    }
    AActor* HoleActor = HoleHit.GetActor();
    if (!HoleActor)
    {
        UE_LOG(LogTemp, Warning, TEXT("HoleHit actor is null in TraceThroughHole!"));
        return HoleHit;
    }

    UE_LOG(LogTemp, Warning, TEXT("Tracing through hole..."));

    FCollisionQueryParams HoleTraceParams;
    HoleTraceParams.AddIgnoredActor(IgnoredActor);
    HoleTraceParams.AddIgnoredActor(HoleActor);

    FVector TraceDirection = (End - HoleHit.Location).GetSafeNormal();
    FVector HoleTraceStart = HoleHit.Location + (TraceDirection * 100.0f);

    FHitResult InsideHoleHit;
    bool bHoleHit = World->LineTraceSingleByChannel(InsideHoleHit, HoleTraceStart, End, ECC_Visibility, HoleTraceParams);

    if (!bHoleHit)
    {
        UE_LOG(LogTemp, Warning, TEXT("No hit inside hole"));
        return bIsDigging ? HoleHit : FHitResult(); // Return the original hit on the hole
    }

    AActor* InsideHitActor = InsideHoleHit.GetActor();
    if (!InsideHitActor)
    {
        UE_LOG(LogTemp, Warning, TEXT("Inside hit actor is null in TraceThroughHole!"));
        return InsideHoleHit;
    }
    UE_LOG(LogTemp, Warning, TEXT("Hit inside hole: %s"), *InsideHitActor->GetName());

    // If we didn't hit landscape, return this hit
    if (!InsideHitActor->IsA(ALandscape::StaticClass()))
    {
        return InsideHoleHit;
    }

    // We hit landscape, perform final trace from behind it
    FHitResult FinalHit = TraceBehindLandscape(InsideHoleHit, End, IgnoredActor, HoleActor);

    // If the final trace didn't hit anything, return the original landscape hit
    if (!FinalHit.bBlockingHit)
    {
        return HoleHit;
    }

    return FinalHit;
}

FHitResult UVoxelBrushShape::TraceBehindLandscape(const FHitResult& LandscapeHit, const FVector& End, AActor* IgnoredActor, AActor* HoleActor) const
{
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("World is null in TraceBehindLandscape!"));
        return FHitResult();
    }
    AActor* LandscapeActor = LandscapeHit.GetActor();
    if (!LandscapeActor)
    {
        UE_LOG(LogTemp, Warning, TEXT("LandscapeHit actor is null in TraceBehindLandscape!"));
        return LandscapeHit;
    }

    UE_LOG(LogTemp, Warning, TEXT("Tracing behind landscape..."));

    FCollisionQueryParams FinalTraceParams;
    FinalTraceParams.AddIgnoredActor(IgnoredActor);
    FinalTraceParams.AddIgnoredActor(HoleActor);
    FinalTraceParams.AddIgnoredActor(LandscapeActor);

    FVector TraceDirection = (End - LandscapeHit.Location).GetSafeNormal();
    FVector FinalTraceStart = LandscapeHit.Location + (TraceDirection * 2.0f);

    FHitResult FinalHit;
    bool bFinalHit = World->LineTraceSingleByChannel(FinalHit, FinalTraceStart, End, ECC_Visibility, FinalTraceParams);

    if (bFinalHit)
    {
        AActor* FinalHitActor = FinalHit.GetActor();
        if (FinalHitActor)
        {
            UE_LOG(LogTemp, Warning, TEXT("Final hit: %s"), *FinalHitActor->GetName());
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Final hit actor is null in TraceBehindLandscape!"));
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("No final hit"));
    }

    return FinalHit;
}
