#include "VoxelBrushShape.h"

#include "DiggerManager.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

UVoxelBrushShape::UVoxelBrushShape()
    : BrushSize(150.0f), SDFChange(0), bDig(false), World(nullptr), DiggerManager(nullptr), TargetChunk(nullptr),
      BrushType(EVoxelBrushType::Sphere)
{

}


void UVoxelBrushShape::InitializeBrush(EVoxelBrushType InBrushType, float InSize, FVector InLocation)
{
    BrushType = InBrushType;
    BrushSize = InSize;
    BrushLocation = InLocation;

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
        return;
    }
    //World
    World = DiggerManager->GetWorldFromManager();
    /*if (World)
    {
        // Start the timer to redraw the debug brush every 5 seconds
        World->GetTimerManager().SetTimer(DebugBrushTimerHandle, this, FTimerDelegate::TMethodPtr<UVoxelBrushShape>(&UVoxelBrushShape::DebugBrush), 5.0f, true);
    }*/
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
    
    switch (BrushType)
    {
        case EVoxelBrushType::Cube:
            ApplyCubeBrush(BrushPosition);
            break;
        case EVoxelBrushType::Sphere:
            ApplySphereBrush(BrushPosition, BrushRadius);
            break;
        case EVoxelBrushType::Cone:
            ApplyConeBrush(BrushPosition);
            break;
        case EVoxelBrushType::Custom:
            ApplyCustomBrush(BrushPosition);
            break;
    }
}


FHitResult UVoxelBrushShape::GetCameraHitLocation()
{
    if(!World)
    { SetWorld( GetWorld());}
    
    APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
    if (!PlayerController) return FHitResult();

    FVector WorldLocation, WorldDirection;
    PlayerController->DeprojectMousePositionToWorld(WorldLocation, WorldDirection);  // Simplified for clarity

    FVector End = WorldLocation + (WorldDirection * 10000.0f);
    FHitResult HitResult;
    FCollisionQueryParams CollisionParams;
    CollisionParams.AddIgnoredActor(PlayerController->GetPawn());  // Ignore the player pawn

    if (World->LineTraceSingleByChannel(HitResult, WorldLocation, End, ECC_Visibility, CollisionParams))
    {
        DrawDebugLine(World, WorldLocation, HitResult.Location, FColor::Red, false, 1.0f, 0, 1.0f);
        BrushLocation = HitResult.Location;  // Update BrushLocation to where the raycast hit
        DebugBrush();
        return HitResult;  // Return the entire FHitResult
    }

    return FHitResult();  // Return an empty FHitResult if nothing is hit
}


void UVoxelBrushShape::ApplyCubeBrush(FVector3d BrushPosition)
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
                    TargetChunk->SetVoxel(X, Y, Z, -1.0f); // Adjust the SDF value to 'cut' into the chunk
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



void UVoxelBrushShape::ApplySphereBrush(FVector3d BrushPosition, float Radius)
{
    FVector3d MinBounds = BrushPosition - FVector3d(Radius);
    FVector3d MaxBounds = BrushPosition + FVector3d(Radius);


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

    // Iterate through active voxels or voxels inside the brush's bounding box
    for (auto& Voxel : TargetChunk->GetActiveVoxels())
    {
        FVector3d VoxelPosition = Voxel.Key;

        // Only affect voxels within the sphere's radius
        if (IsVoxelWithinBounds(VoxelPosition, MinBounds, MaxBounds))
        {
            float Distance = FVector3d::Dist(VoxelPosition, BrushPosition);

            if (Distance < Radius)
            {
                // Calculate the influence of the brush based on distance from the center
                float BrushInfluence = (Radius - Distance) / Radius;

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
                TargetChunk->SetVoxel(VoxelPosition, NewSDFValue);
            }
        }
    }
    
    // Mark the chunk as dirty for mesh regeneration
    TargetChunk->MarkDirty();
}


void UVoxelBrushShape::ApplyConeBrush(FVector3d BrushPosition)
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

   /* UWorld* TheWorld = World();
    if (World)
    {
        // Start the timer to redraw the debug brush every 5 seconds
        TheWorld->GetTimerManager().SetTimer(DebugBrushTimerHandle, this, FTimerDelegate::TMethodPtr<UVoxelBrushShape>(&UVoxelBrushShape::DebugBrush), 5.0f, true);
    }*/
}



