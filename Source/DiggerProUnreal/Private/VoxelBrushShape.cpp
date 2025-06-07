#include "VoxelBrushShape.h"

#include "DiggerManager.h"
#include "EngineUtils.h"
#include "Landscape.h"
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


FHitResult UVoxelBrushShape::GetCameraHitLocation()
{
    if (!World)
    {
        SetWorld(GetSafeWorld());
    }

    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("World is null in GetCameraHitLocation!"));
        return FHitResult();
    }

    // Early exit if no player controller
    APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
    if (!PlayerController)
    {
        UE_LOG(LogTemp, Warning, TEXT("PlayerController is null in GetCameraHitLocation!"));
        return FHitResult();
    }

    // Get mouse ray
    FVector WorldLocation, WorldDirection;
    if (!PlayerController->DeprojectMousePositionToWorld(WorldLocation, WorldDirection))
    {
        UE_LOG(LogTemp, Warning, TEXT("DeprojectMousePositionToWorld failed!"));
        return FHitResult();
    }
    FVector End = WorldLocation + (WorldDirection * 10000.0f);

    // Perform initial simple trace
    FHitResult HitResult;
    FCollisionQueryParams QueryParams;
    QueryParams.bTraceComplex = false; // Simple trace first
    QueryParams.AddIgnoredActor(PlayerController->GetPawn());

    bool bHit = World->LineTraceSingleByChannel(
        HitResult,
        WorldLocation,
        End,
        ECC_Visibility, // or whatever channel you use
        QueryParams
    );

    if (!bHit)
    {
        return FHitResult(); // No hit at all
    }

    // Check if the hit actor is a hole blueprint
    AActor* HitActor = HitResult.GetActor();
    if (!HitActor)
    {
        return HitResult; // Hit something but no actor, return simple trace result
    }

    // Check if this is a hole blueprint - adjust this condition to match your hole BP detection
    // Option 1: Check by class name
    if (IsHoleBPActor(HitActor))
    {
        // This is a hole blueprint, perform complex trace
        return PerformComplexTrace(WorldLocation, End, PlayerController->GetPawn());
    }

    // Option 2: Check by tag (if your hole BPs have a specific tag)
    /*
    if (HitActor->Tags.Contains(FName("HoleBlueprint")))
    {
        return PerformComplexTrace(WorldLocation, End, PlayerController->GetPawn());
    }
    */

    // Option 3: Check by interface or component (if your hole BPs implement a specific interface)
    /*
    if (HitActor->GetClass()->ImplementsInterface(UYourHoleInterface::StaticClass()))
    {
        return PerformComplexTrace(WorldLocation, End, PlayerController->GetPawn());
    }
    */

    // Option 4: Check by component (if your hole BPs have a specific component)
    /*
    if (HitActor->FindComponentByClass<UYourHoleComponent>())
    {
        return PerformComplexTrace(WorldLocation, End, PlayerController->GetPawn());
    }
    */

    // Not a hole blueprint, return the simple trace result
    return HitResult;
}

FHitResult UVoxelBrushShape::PerformComplexTrace(const FVector& Start, const FVector& End, AActor* IgnoredActor)
{
    World=GetSafeWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("World is null in PerformComplexTrace!"));
        return FHitResult();
    }

    FCollisionQueryParams CollisionParams;
    CollisionParams.AddIgnoredActor(IgnoredActor);

    FHitResult InitialHit;
    bool bHit = World->LineTraceSingleByChannel(InitialHit, Start, End, ECC_Visibility, CollisionParams);

    if (!bHit)
    {
        UE_LOG(LogTemp, Warning, TEXT("No hit in PerformComplexTrace."));
        return InitialHit;
    }

    AActor* HitActor = InitialHit.GetActor();
    if (!HitActor)
    {
        UE_LOG(LogTemp, Warning, TEXT("Initial hit actor is null."));
        return InitialHit;
    }

    UE_LOG(LogTemp, Warning, TEXT("Initial hit: %s"), *HitActor->GetName());

    if (!IsHoleBPActor(HitActor))
    {
        return InitialHit;
    }

    // Defensive: TraceThroughHole may return an invalid hit, so check inside that method too
    return TraceThroughHole(InitialHit, End, IgnoredActor);
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

FHitResult UVoxelBrushShape::TraceThroughHole(const FHitResult& HoleHit, const FVector& End, AActor* IgnoredActor)
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

FHitResult UVoxelBrushShape::TraceBehindLandscape(const FHitResult& LandscapeHit, const FVector& End, AActor* IgnoredActor, AActor* HoleActor)
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
