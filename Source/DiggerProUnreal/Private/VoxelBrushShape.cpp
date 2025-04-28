#include "VoxelBrushShape.h"

#include "DiggerManager.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "Kismet/GameplayStatics.h"

UVoxelBrushShape::UVoxelBrushShape()
    : BrushSize(150.0f), SDFChange(0), bDig(false), World(nullptr), BrushRotation(FRotator::ZeroRotator),
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

    if(!World) World = GetWorld();
    DiggerManager = DiggerManagerRef;
}


// Helper function to retrieve the target chunk from a brush position
UVoxelChunk* UVoxelBrushShape::GetTargetChunkFromBrushPosition(const FVector3d& BrushPosition)
{
    // Get a reference to the Digger Manager (or the system managing your chunks)
    if (!DiggerManager) return nullptr;

    // Calculate the chunk position based on the brush position
    FIntVector ChunkPosition = DiggerManager->CalculateChunkPosition(BrushPosition);

    // Retrieve the chunk from the map
    UVoxelChunk* NewTargetChunk = DiggerManager->GetOrCreateChunkAt(ChunkPosition);
    
    // Return the chunk if found, otherwise return nullptr
    return (NewTargetChunk) ? NewTargetChunk : nullptr;
}


void UVoxelBrushShape::ApplyBrushToChunk(UVoxelChunk* BrushChunk, FVector3d BrushPosition, float BrushRadius)
{
    // Use the helper function to get the target chunk if it isn't passed
    if (!TargetChunk)
    {
        TargetChunk = GetTargetChunkFromBrushPosition(BrushPosition);
    }

    if (!TargetChunk)
    {
        // Log error or handle the case where no chunk is found
        return;
    }

    FBrushStroke BrushStroke;
    BrushStroke.BrushPosition = BrushPosition;
    BrushStroke.BrushRadius = BrushRadius;
    BrushStroke.bDig = DiggerManager->ActiveBrush->GetDig();
    BrushStroke.BrushType = DiggerManager->ActiveBrush->GetBrushType();
    
    switch (BrushType)
    {
        case EVoxelBrushType::Cube:
            ApplyCubeBrush(&BrushStroke);
            break;
        case EVoxelBrushType::Sphere:
            ApplySphereBrush(&BrushStroke);
            break;
        case EVoxelBrushType::Cone:
            ApplyConeBrush(&BrushStroke);
            break;
        case EVoxelBrushType::Custom:
            ApplyCustomBrush(BrushPosition);
            break;
    }
}

FHitResult UVoxelBrushShape::GetCameraHitLocation()
{
    if (!World)
    {
        SetWorld(GetWorld());
    }
    
    // Early exit if no player controller
    APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
    if (!PlayerController) return FHitResult();

    // Get mouse ray
    FVector WorldLocation, WorldDirection;
    PlayerController->DeprojectMousePositionToWorld(WorldLocation, WorldDirection);
    FVector End = WorldLocation + (WorldDirection * 10000.0f);

    return PerformComplexTrace(WorldLocation, End, PlayerController->GetPawn());
}

FHitResult UVoxelBrushShape::PerformComplexTrace(const FVector& Start, const FVector& End, AActor* IgnoredActor)
{
    // Setup initial trace parameters
    FCollisionQueryParams CollisionParams;
    CollisionParams.AddIgnoredActor(IgnoredActor);
    
    FHitResult InitialHit;
    bool bHit = World->LineTraceSingleByChannel(InitialHit, Start, End, ECC_Visibility, CollisionParams);
    
    if (!bHit)
    {
        return InitialHit;
    }

    // Log initial hit
    AActor* HitActor = InitialHit.GetActor();
    if (!HitActor)
    {
        return InitialHit;
    }

    UE_LOG(LogTemp, Warning, TEXT("Initial hit: %s"), *HitActor->GetName());

    // Check if we hit a hole
    if (!IsHoleBPActor(HitActor))
    {
        return InitialHit;
    }

    // We hit a hole, perform trace through it
    return TraceThroughHole(InitialHit, End, IgnoredActor);
}

bool UVoxelBrushShape::IsHoleBPActor(AActor* Actor) const
{
    if (!DiggerManager || !DiggerManager->HoleBP)
    {
        return false;
    }

    // Check if the actor is of the HoleBP class
    return Actor->GetClass() == DiggerManager->HoleBP;
}

FHitResult UVoxelBrushShape::TraceThroughHole(const FHitResult& HoleHit, const FVector& End, AActor* IgnoredActor)
{
    UE_LOG(LogTemp, Warning, TEXT("Tracing through hole..."));
    
    // Setup trace parameters for inside hole trace
    FCollisionQueryParams HoleTraceParams;
    HoleTraceParams.AddIgnoredActor(IgnoredActor);
    HoleTraceParams.AddIgnoredActor(HoleHit.GetActor());

    // Start slightly inside the hole
    FVector TraceDirection = (End - HoleHit.Location).GetSafeNormal();
    FVector HoleTraceStart = HoleHit.Location + (TraceDirection * 100.0f);
    
    FHitResult InsideHoleHit;
    bool bHoleHit = World->LineTraceSingleByChannel(InsideHoleHit, HoleTraceStart, End, ECC_Visibility, HoleTraceParams);

    if (!bHoleHit)
    {
        UE_LOG(LogTemp, Warning, TEXT("No hit inside hole"));

        return bDig ? HoleHit : FHitResult();; // Return the original hit on the hole
    }

    AActor* InsideHitActor = InsideHoleHit.GetActor();
    UE_LOG(LogTemp, Warning, TEXT("Hit inside hole: %s"), *InsideHitActor->GetName());

    // If we didn't hit landscape, return this hit
    if (!InsideHitActor->IsA(ALandscape::StaticClass()))
    {
        return InsideHoleHit;
    }

    // We hit landscape, perform final trace from behind it
    FHitResult FinalHit = TraceBehindLandscape(InsideHoleHit, End, IgnoredActor, HoleHit.GetActor());

    // If the final trace didn't hit anything, return the original landscape hit
    if (!FinalHit.bBlockingHit)
    {
        return HoleHit;
    }

    return FinalHit;
}

FHitResult UVoxelBrushShape::TraceBehindLandscape(const FHitResult& LandscapeHit, const FVector& End, AActor* IgnoredActor, AActor* HoleActor)
{
    UE_LOG(LogTemp, Warning, TEXT("Tracing behind landscape..."));
    
    // Setup final trace parameters
    FCollisionQueryParams FinalTraceParams;
    FinalTraceParams.AddIgnoredActor(IgnoredActor);
    FinalTraceParams.AddIgnoredActor(HoleActor);
    FinalTraceParams.AddIgnoredActor(LandscapeHit.GetActor());

    // Start slightly behind the landscape
    FVector TraceDirection = (End - LandscapeHit.Location).GetSafeNormal();
    FVector FinalTraceStart = LandscapeHit.Location + (TraceDirection * 2.0f);
    
    FHitResult FinalHit;
    bool bFinalHit = World->LineTraceSingleByChannel(FinalHit, FinalTraceStart, End, ECC_Visibility, FinalTraceParams);

    if (bFinalHit)
    {
        UE_LOG(LogTemp, Warning, TEXT("Final hit: %s"), *FinalHit.GetActor()->GetName());
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("No final hit"));
    }

    return FinalHit;
}



void UVoxelBrushShape::ApplyCubeBrush(FBrushStroke* BrushStroke)
{
    // Use the helper function to get the target chunk if it isn't passed
    if (!TargetChunk)
    {
        TargetChunk = GetTargetChunkFromBrushPosition(BrushStroke->BrushPosition);
    }

    if (!TargetChunk)
    {
        // Log error or handle the case where no chunk is found
        return;
    }
    // Example logic for cube brush: Iterate over chunk voxels and modify SDF values
    for (int32 X = 0; X < TargetChunk->GetChunkSize(); ++X)
    {
        for (int32 Y = 0; Y < TargetChunk->GetChunkSize(); ++Y)
        {
            for (int32 Z = 0; Z < TargetChunk->GetChunkSize(); ++Z)
            {
                FVector VoxelPosition = FVector(X, Y, Z) * TargetChunk->GetVoxelSize();
                if (FVector::Dist(BrushLocation, VoxelPosition) < BrushSize)
                {
                    TargetChunk->SetVoxel(X, Y, Z, -1.0f, bDig); // Adjust the SDF value to 'cut' into the chunk
                }
            }
        }
    }
}



bool UVoxelBrushShape::IsVoxelWithinBounds(const FVector3d& VoxelPosition, const FVector3d& MinBounds,
                                           const FVector3d& MaxBounds)
{
    return (VoxelPosition.X >= MinBounds.X && VoxelPosition.X <= MaxBounds.X) &&
           (VoxelPosition.Y >= MinBounds.Y && VoxelPosition.Y <= MaxBounds.Y) &&
           (VoxelPosition.Z >= MinBounds.Z && VoxelPosition.Z <= MaxBounds.Z);
}

bool UVoxelBrushShape::IsVoxelWithinSphere(const FVector3d& VoxelPosition, const FVector3d& SphereCenter, double Radius)
{
    double DistanceSquared = (VoxelPosition.X - SphereCenter.X) * (VoxelPosition.X - SphereCenter.X) +
                             (VoxelPosition.Y - SphereCenter.Y) * (VoxelPosition.Y - SphereCenter.Y) +
                             (VoxelPosition.Z - SphereCenter.Z) * (VoxelPosition.Z - SphereCenter.Z);
    return DistanceSquared <= (Radius * Radius);
}



void UVoxelBrushShape::ApplySphereBrush(FBrushStroke* BrushStroke)
{
    FVector3d MinBounds = BrushStroke->BrushPosition - FVector3d(BrushStroke->BrushRadius);
    FVector3d MaxBounds = BrushStroke->BrushPosition + FVector3d(BrushStroke->BrushRadius);


    // Use the helper function to get the target chunk if it isn't passed
    if (!TargetChunk)
    {
        TargetChunk = GetTargetChunkFromBrushPosition(BrushStroke->BrushPosition);
    }

    if (!TargetChunk)
    {
        // Log error or handle the case where no chunk is found
        return;
    }

    // Iterate through active voxels or voxels inside the brush's bounding box
    for (auto& Voxel : TargetChunk->GetActiveVoxels())
    {
        FVector3d VoxelPosition = Voxel.Key;

        // Only affect voxels within the sphere's radius
        if (IsVoxelWithinBounds(VoxelPosition, MinBounds, MaxBounds))
        {
            float Distance = FVector3d::Dist(VoxelPosition, BrushStroke->BrushPosition);

            if (Distance < BrushStroke->BrushRadius)
            {
                // Calculate the influence of the brush based on distance from the center
                float BrushInfluence = (BrushStroke->BrushRadius - Distance) / BrushStroke->BrushRadius;

                // Modify the SDF value of the voxel based on the brush's influence
                float NewSDFValue = TargetChunk->GetVoxel(VoxelPosition);

                if (bDig)
                {
                    NewSDFValue -= SDFChange * BrushInfluence; // Digging (subtract SDF)
                }
                else
                {
                    NewSDFValue += SDFChange * BrushInfluence; // Adding (increase SDF)
                }

                // Update the voxel's SDF value
                TargetChunk->SetVoxel(VoxelPosition, NewSDFValue, bDig);
            }
        }
    }
    
    // Mark the chunk as dirty for mesh regeneration
    TargetChunk->MarkDirty();
}


void UVoxelBrushShape::ApplyConeBrush(FBrushStroke* BrushStroke)
{
    // Use the helper function to get the target chunk if it isn't passed
    if (!TargetChunk)
    {
        TargetChunk = GetTargetChunkFromBrushPosition(BrushStroke->BrushPosition);
    }

    if (!TargetChunk)
    {
        // Log error or handle the case where no chunk is found
        return;
    }
    
    // Cone logic
}

void UVoxelBrushShape::ApplyCustomBrush(FVector3d BrushPosition)
{
    // Use the helper function to get the target chunk if it isn't passed
    if (!TargetChunk)
    {
        TargetChunk = GetTargetChunkFromBrushPosition(BrushPosition);
    }

    if (!TargetChunk)
    {
        // Log error or handle the case where no chunk is found
        return;
    }
    // Custom brush logic
}



void UVoxelBrushShape::DebugBrush()
{
    if (!World)
    {
        SetWorld(GetWorld());
    }

    UE_LOG(LogTemp, Warning, TEXT("BrushType: %d"), (int32)BrushType);
    UE_LOG(LogTemp, Warning, TEXT("BrushLocation: X=%f Y=%f Z=%f"), BrushLocation.X, BrushLocation.Y, BrushLocation.Z);
    UE_LOG(LogTemp, Warning, TEXT("BrushSize: %f"), BrushSize);
    
    switch (BrushType) {
    case EVoxelBrushType::Cube:
        DrawDebugBox(World, BrushLocation, FVector(BrushSize), FColor::Green, false, 5.0f);
        break;
    case EVoxelBrushType::Sphere:
        DrawDebugSphere(World, BrushLocation, BrushSize, 32, FColor::Red, false, 5.0f);
        break;
    case EVoxelBrushType::Cone: {
            float ConeHeight = BrushSize;
            //float ConeAngle = ConeAngle.IsSet() ? ConeAngle.GetValue() : 45.0f;
            //bool bFlip = FlipOrientation.IsSet() ? FlipOrientation.GetValue() : false;
            //FVector ConeDirection = bFlip ? FVector::DownVector * ConeHeight : FVector::UpVector * ConeHeight;
            DrawDebugCone(World, BrushLocation, FVector(0, 1, 0)/*ConeDirection*/, ConeHeight, FMath::DegreesToRadians(45/*ConeAngle*/), FMath::DegreesToRadians(45/*ConeAngle*/), 32, FColor::Blue, false, 5.0f);
            break;
    }
    case EVoxelBrushType::Custom:
        // Debug custom
            break;
    }
    
}



