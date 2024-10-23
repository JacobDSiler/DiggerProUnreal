#include "SparseVoxelGrid.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "DiggerManager.h"
#include "EngineUtils.h"
#include "SWarningOrErrorBox.h"
#include "VoxelChunk.h"

USparseVoxelGrid::USparseVoxelGrid(): LocalVoxelSize(0), DiggerManager(nullptr), ParentChunk(nullptr),
                                      ParentChunkCoordinates(0, 0, 0),
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
    // Find and assign DiggerManager if not already assigned and set the member variables from it
    EnsureDiggerManager();
        if (DiggerManager)
        {
            ChunkSize=DiggerManager->ChunkSize;
            TerrainGridSize=DiggerManager->TerrainGridSize;
            Subdivisions=DiggerManager->Subdivisions;
            LocalVoxelSize=DiggerManager->VoxelSize;
        }
}


bool USparseVoxelGrid::EnsureDiggerManager()
{
    if (!DiggerManager)
    {
        DiggerManager = ParentChunk->GetDiggerManager();
        
        if (!DiggerManager)
        {
            UE_LOG(LogTemp, Error, TEXT("DiggerManager is null in VoxelToWorldSpace"));
            return false;
        }
        UE_LOG(LogTemp, Warning, TEXT("DiggerManager set correctly in an instance of SVG!"));
        LocalVoxelSize = DiggerManager->VoxelSize;
    }
    return true;
}

// Converts voxel-local coordinates to world space (using chunk world position)
FVector USparseVoxelGrid::VoxelToWorldSpace(const FIntVector& VoxelCoords)
{
    if (!EnsureDiggerManager()) return FVector::ZeroVector;

    return ParentChunk->GetWorldPosition() + FVector(
        VoxelCoords.X * LocalVoxelSize,
        VoxelCoords.Y * LocalVoxelSize,
        VoxelCoords.Z * LocalVoxelSize
    );
}

// Converts world space to voxel-local coordinates (for a specific chunk)
FIntVector USparseVoxelGrid::WorldToVoxelSpace(const FVector& WorldCoords)
{
    if (!EnsureDiggerManager()) return FIntVector::ZeroValue;
       
    FVector LocalCoords = WorldCoords - ParentChunk->GetWorldPosition();
    
    FIntVector VoxelCoords = FIntVector(
           FMath::FloorToInt(LocalCoords.X / LocalVoxelSize),
           FMath::FloorToInt(LocalCoords.Y / LocalVoxelSize),
           FMath::FloorToInt(LocalCoords.Z / LocalVoxelSize)
       );

    UE_LOG(LogTemp, Log, TEXT("WorldToVoxelSpace: World(%f,%f,%f) -> Voxel(%d,%d,%d)"),
        WorldCoords.X, WorldCoords.Y, WorldCoords.Z,
        VoxelCoords.X, VoxelCoords.Y, VoxelCoords.Z);

    return VoxelCoords;
}



void USparseVoxelGrid::SetVoxel(FIntVector Position, float SDFValue)
{
    SetVoxel(Position.X, Position.Y, Position.Z, SDFValue);
}

// In USparseVoxelGrid, update SetVoxel to use world coordinates comparison
void USparseVoxelGrid::SetVoxel(int32 X, int32 Y, int32 Z, float SDFValue)
{
    // Calculate the valid range for voxel coordinates
    int32 HalfChunkSize = ChunkSize * Subdivisions / 2;
    
    // Validate the voxel coordinates
    /*if (X >= -HalfChunkSize && X < HalfChunkSize &&
        Y >= -HalfChunkSize && Y < HalfChunkSize &&
        Z >= -HalfChunkSize && Z < HalfChunkSize)
    {*/
        VoxelData.Add(FIntVector(X, Y, Z), FVoxelData(SDFValue));
        UE_LOG(LogTemp, Warning, TEXT("Set voxel at: X=%d Y=%d Z=%d with SDFValue=%f"), X, Y, Z, SDFValue);
    /*}
    else
    {
    UE_LOG(LogTemp, Error, TEXT("Invalid voxel coordinates: X=%d Y=%d Z=%d"), X, Y, Z);
    }*/
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

    if(!EnsureDiggerManager()) return;

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
        FVoxelData VoxelDataValue = Voxel.Value;

        // Convert voxel coordinates to world space
        FVector WorldPos = VoxelToWorldSpace(VoxelCoords);

        // Adjust to the center of the voxel
        FVector Center = WorldPos + FVector(LocalVoxelSize / 2);

        // Access the SDF value from the VoxelData
        float SDFValue = VoxelDataValue.SDFValue;

        // Determine color based on SDF value
        FColor VoxelColor;
        if (SDFValue > 0)
        {
            VoxelColor = FColor::Green; // Air (SDF > 0)
        }
        else if (SDFValue < 0)
        {
            VoxelColor = FColor::Red; // Solid (SDF < 0)
        }
        else
        {
            VoxelColor = FColor::Yellow; // Surface (SDF == 0)
        }

        // Draw the voxel cube with the correct color
        DrawDebugBox(World, Center, FVector(LocalVoxelSize / 2), FQuat::Identity, VoxelColor, false, 15.f, 0, 2);

        // Optionally, draw a point at the center of the voxel
        DrawDebugPoint(World, Center, 2.0f, VoxelColor, false, 15.f, 0);
    }
}

// Method to bake the current voxel data to BaseSDF
void USparseVoxelGrid::BakeSdf()
{
    BakedSDF = VoxelData;
    UE_LOG(LogTemp, Warning, TEXT("Baked current voxel data to BakedSDF."));
}

// Method to apply the BaseSDF to VoxelData
void USparseVoxelGrid::ApplyBakedSDF()
{
    VoxelData = BakedSDF;
    UE_LOG(LogTemp, Warning, TEXT("Applied BakedSDF to current voxel data."));
}

