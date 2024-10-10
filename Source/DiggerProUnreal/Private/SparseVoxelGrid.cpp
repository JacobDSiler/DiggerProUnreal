#include "SparseVoxelGrid.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "DiggerManager.h"
#include "EngineUtils.h"

USparseVoxelGrid::USparseVoxelGrid(): DiggerManager(nullptr), ParentChunkCoordinates(0, 0, 0)
{
    // Constructor logic
}

void USparseVoxelGrid::Initialize(FIntVector ParentChunkPosition)
{
    ParentChunkCoordinates = ParentChunkPosition; // Store parent chunk position
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


FIntVector USparseVoxelGrid::WorldToVoxelSpace(const FVector& WorldPosition, float SubdividedVoxelSize) const
{
    FVector ChunkMinCorner = GetParentChunkCoordinatesV3D() * ChunkSize * TerrainGridSize / 2.0f;
    FVector AdjustedPosition = WorldPosition - ChunkMinCorner;
    return FIntVector(
        FMath::FloorToInt(AdjustedPosition.X / SubdividedVoxelSize),
        FMath::FloorToInt(AdjustedPosition.Y / SubdividedVoxelSize),
        FMath::FloorToInt(AdjustedPosition.Z / SubdividedVoxelSize)
    );
}


// Converts voxel-space coordinates to world-space coordinates
FVector USparseVoxelGrid::VoxelToWorldSpace(const FIntVector& VoxelPosition, float SubdividedVoxelSize) const
{
    FVector ChunkMinCorner = GetParentChunkCoordinatesV3D() * ChunkSize * TerrainGridSize / 2.0f;
    return ChunkMinCorner + FVector(VoxelPosition) * SubdividedVoxelSize;
}


void USparseVoxelGrid::SetVoxel(FIntVector Position, float SDFValue)
{
    FVoxelData NewVoxelData(SDFValue);
    VoxelData.Add(Position, NewVoxelData); // Add the voxel to the map

    UE_LOG(LogTemp, Warning, TEXT("Set voxel at coordinates: X=%d Y=%d Z=%d with SDFValue: %f"), 
    Position.X, Position.Y, Position.Z, SDFValue);

    // Verify if the voxel was added
    if (VoxelData.Contains(Position))
    {
        UE_LOG(LogTemp, Warning, TEXT("Successfully added a voxel at: %s"), *Position.ToString());
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to add a voxel at: %s"), *Position.ToString());
    }
}


void USparseVoxelGrid::SetVoxel(int32 X, int32 Y, int32 Z, float SDFValue)
{
    SetVoxel(FIntVector(X, Y, Z), SDFValue);
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

    for (const auto& VoxelPair : VoxelData)
    {
        FVector Position = VoxelToWorldSpace(VoxelPair.Key, TerrainGridSize / Subdivisions);
        FVector VoxelExtent = FVector(TerrainGridSize / (2.0f * Subdivisions));

        float SDFValue = VoxelPair.Value.SDFValue;
        FColor VoxelColor = (SDFValue < 0.0f) ? FColor::Red : (SDFValue > 0.0f) ? FColor::Green : FColor::Yellow;

        UE_LOG(LogTemp, Warning, TEXT("Voxel at position: X=%f Y=%f Z=%f | SDFValue=%f"), Position.X, Position.Y, Position.Z, SDFValue);

        DrawDebugPoint(World, Position, 10.0f, VoxelColor, false, 10.0f);
        DrawDebugBox(World, Position, VoxelExtent, VoxelColor, false, 10.0f);
    }
}






