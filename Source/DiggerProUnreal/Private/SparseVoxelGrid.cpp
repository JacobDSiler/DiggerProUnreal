#include "SparseVoxelGrid.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "DiggerManager.h"
#include "EngineUtils.h"
#include "VoxelChunk.h"

USparseVoxelGrid::USparseVoxelGrid(): DiggerManager(nullptr), ParentChunk(nullptr), ParentChunkCoordinates(0, 0, 0),
                                      TerrainGridSize(0),
                                      Subdivisions(0)
{
    // Constructor logic
}

void USparseVoxelGrid::Initialize(UVoxelChunk* ParentChunkReference)
{
    ParentChunk=ParentChunkReference;
    ParentChunkCoordinates = ParentChunk->GetChunkPosition();
}

void USparseVoxelGrid::InitializeDiggerManager()
{
    if (!GetWorld())
    {
        UE_LOG(LogTemp, Warning, TEXT("World is null in InitializeDiggerManager."));
        return; // Ensure we have a valid world context
    }

    UE_LOG(LogTemp, Warning, TEXT("World is not null in InitializeDiggerManager. Grabbing the DiggerManager."));
    // Find and assign DiggerManager if not already assigned
    for (TActorIterator<ADiggerManager> It(GetWorld()); It; ++It)
    {
        DiggerManager = *It;
        if (DiggerManager)
            ChunkSize=DiggerManager->ChunkSize;
            TerrainGridSize=DiggerManager->TerrainGridSize;
            Subdivisions=DiggerManager->Subdivisions;
            VoxelSize=DiggerManager->VoxelSize;
            break;
    }
}


// Converts voxel-local coordinates to world space (using chunk world position)
FVector USparseVoxelGrid::VoxelToWorldSpace(const FIntVector& VoxelCoords)
{
    return ParentChunk->GetWorldPosition() + FVector(
        VoxelCoords.X * VoxelSize,
        VoxelCoords.Y * VoxelSize,
        VoxelCoords.Z * VoxelSize
    );
}

// Converts world space to voxel-local coordinates (for a specific chunk)
FIntVector USparseVoxelGrid::WorldToVoxelSpace(const FVector& WorldCoords)
{
    return FIntVector((WorldCoords - ParentChunk->GetWorldPosition()) / VoxelSize);
}



void USparseVoxelGrid::SetVoxel(FIntVector Position, float SDFValue)
{
    SetVoxel(Position.X, Position.Y, Position.Z, SDFValue);
}

// In USparseVoxelGrid, update SetVoxel to use world coordinates comparison
void USparseVoxelGrid::SetVoxel(int32 X, int32 Y, int32 Z, float SDFValue)
{
    FVector WorldPosition = VoxelToWorldSpace(FIntVector(X, Y, Z));
    
    // Use the world position for chunk comparison
    FIntVector ChunkPosition = ParentChunk->WorldToChunkCoordinates(WorldPosition);

    // Ensure the voxel coordinates match the chunk
  // if (ChunkPosition == ParentChunk->GetChunkPosition()) // Already in chunk space
   // {
        VoxelData.Add(FIntVector(X, Y, Z), FVoxelData(SDFValue));
        UE_LOG(LogTemp, Warning, TEXT("Set voxel at: X=%d Y=%d Z=%d with SDFValue=%f"), X, Y, Z, SDFValue);
   // }
  //  else
   // {
    //    UE_LOG(LogTemp, Error, TEXT("Invalid voxel coordinates: X=%d Y=%d Z=%d"), X, Y, Z);
   // }
}


float USparseVoxelGrid::GetVoxel(int32 X, int32 Y, int32 Z) const
{
    const FVoxelData* Voxel = VoxelData.Find(FIntVector(X, Y, Z));
    if (Voxel)
    {
        UE_LOG(LogTemp, Warning, TEXT("SVG: Voxel found at X=%d, Y=%d, Z=%d with SDF=%f"), X, Y, Z, Voxel->SDFValue);
        return Voxel->SDFValue;
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("No voxel found at X=%d, Y=%d, Z=%d, returning default SDF=1.0f"), X, Y, Z);
        return 1.0f; // Default to air if the voxel doesn't exist
    }
}

TMap<FVector, float> USparseVoxelGrid::GetVoxels() const
{
    TMap<FVector, float> Voxels;
    for (const auto& VoxelPair : VoxelData)
    {
        Voxels.Add(FVector(VoxelPair.Key), VoxelPair.Value.SDFValue);
        UE_LOG(LogTemp, Warning, TEXT("Voxel at %s has SDF value: %f"), *VoxelPair.Key.ToString(), VoxelPair.Value.SDFValue);
    }

    return Voxels;
}

bool USparseVoxelGrid::VoxelExists(int32 X, int32 Y, int32 Z) const
{
    return VoxelData.Contains(FIntVector(X, Y, Z));
}

void USparseVoxelGrid::LogVoxelData() const
{
    if(!VoxelData.IsEmpty())
    {    UE_LOG(LogTemp, Warning, TEXT("Voxel Data isn't empty!"));}
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Voxel Data is empty!"));
    }

    for (const auto& VoxelPair : VoxelData)
    {
        UE_LOG(LogTemp, Warning, TEXT("Voxel at [%s] has SDF value: %f"), *VoxelPair.Key.ToString(), VoxelPair.Value.SDFValue);
    }
}

void USparseVoxelGrid::RenderVoxels()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("World is null within SparseVoxelGrid RenderVoxels()!!"));
        return;
    }

    if (VoxelData.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("VoxelData is empty, no voxels to render!"));
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("VoxelData contains %d voxels."), VoxelData.Num());
    }

    for (const auto& Voxel : VoxelData)
    {
        FIntVector VoxelCoords = Voxel.Key;
        FVoxelData VoxelInfo = Voxel.Value;

        // Convert voxel coordinates to world space
        FVector WorldPos = VoxelToWorldSpace(VoxelCoords);

        // Adjust to the center
        FVector Center = WorldPos + FVector(VoxelSize / 2);

        // Create a cube mesh or similar for rendering
        DrawDebugBox(World, Center, FVector(VoxelSize / 2), FQuat::Identity, FColor::Green, false, 15.f, 0, 2);
        DrawDebugPoint(World, Center, 2.0f, FColor::Green, false, 15.f, 0);
    }
}
git add .







