#include "VoxelConversionDebugger.h"
#include "DrawDebugHelpers.h"
// Include your voxel conversion header
#include "VoxelConversion.h"

AVoxelConversionDebugger::AVoxelConversionDebugger()
{
    PrimaryActorTick.bCanEverTick = false;
}

void AVoxelConversionDebugger::TestPositionConversion(FVector WorldPosition)
{
    UE_LOG(LogTemp, Warning, TEXT("==== VOXEL CONVERSION DEBUG ===="));
    UE_LOG(LogTemp, Warning, TEXT("Testing position: %s"), *WorldPosition.ToString());
    
    // Current configuration
    UE_LOG(LogTemp, Warning, TEXT("Configuration: ChunkSize=%d, Subdivisions=%d, GridSize=%f, VoxelSize=%f, Origin=%s"),
           FVoxelConversion::ChunkSize, 
           FVoxelConversion::Subdivisions,
           FVoxelConversion::TerrainGridSize,
           FVoxelConversion::LocalVoxelSize,
           *FVoxelConversion::Origin.ToString());
    
    // Test WorldToChunk
    FIntVector ChunkCoords = FVoxelConversion::WorldToChunk(WorldPosition);
    UE_LOG(LogTemp, Warning, TEXT("WorldToChunk: %s"), *ChunkCoords.ToString());
    
    // Test ChunkToWorld
    FVector ChunkOrigin = FVoxelConversion::ChunkToWorld(ChunkCoords);
    UE_LOG(LogTemp, Warning, TEXT("ChunkToWorld: %s"), *ChunkOrigin.ToString());
    
    // Test WorldToLocalVoxel
    FIntVector LocalVoxel = FVoxelConversion::WorldToLocalVoxel(WorldPosition);
    UE_LOG(LogTemp, Warning, TEXT("WorldToLocalVoxel: %s"), *LocalVoxel.ToString());
    
    // Calculate global voxel coordinate for clarity
    int32 VoxelsPerChunk = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
    FIntVector GlobalVoxel(
        ChunkCoords.X * VoxelsPerChunk + LocalVoxel.X,
        ChunkCoords.Y * VoxelsPerChunk + LocalVoxel.Y,
        ChunkCoords.Z * VoxelsPerChunk + LocalVoxel.Z
    );
    UE_LOG(LogTemp, Warning, TEXT("Calculated Global Voxel: %s"), *GlobalVoxel.ToString());
    
    // Test round trip with LocalVoxelToWorld
    FVector ReconstructedWorld = FVoxelConversion::LocalVoxelToWorld(GlobalVoxel);
    UE_LOG(LogTemp, Warning, TEXT("LocalVoxelToWorld: %s"), *ReconstructedWorld.ToString());
    
    // Calculate error
    float Error = FVector::Distance(WorldPosition, ReconstructedWorld);
    UE_LOG(LogTemp, Warning, TEXT("Round-trip error: %f units"), Error);
    
    UE_LOG(LogTemp, Warning, TEXT("================================"));
}

void AVoxelConversionDebugger::VisualizeChunkBoundaries(FVector WorldPosition, float Duration)
{
    FIntVector ChunkCoords = FVoxelConversion::WorldToChunk(WorldPosition);
    
    // Draw the chunk containing the position
    DrawDebugChunk(ChunkCoords, Duration);
    
    // Draw neighboring chunks for context
    for (int32 x = -1; x <= 1; x++)
    for (int32 y = -1; y <= 1; y++)
    for (int32 z = -1; z <= 1; z++)
    {
        if (x == 0 && y == 0 && z == 0) continue;
        
        FIntVector NeighborCoords = ChunkCoords + FIntVector(x, y, z);
        DrawDebugChunk(NeighborCoords, Duration);
    }
}

void AVoxelConversionDebugger::DrawDebugChunk(const FIntVector& ChunkCoords, float Duration)
{
    FVector ChunkOrigin = FVoxelConversion::ChunkToWorld(ChunkCoords);
    float ChunkWorldSize = FVoxelConversion::ChunkSize * FVoxelConversion::TerrainGridSize;
    
    FVector Min = ChunkOrigin;
    FVector Max = ChunkOrigin + FVector(ChunkWorldSize);
    
    // Draw chunk boundaries
    DrawDebugBox(GetWorld(), (Min + Max) * 0.5f, (Max - Min) * 0.5f, 
        ChunkCoords == FVoxelConversion::WorldToChunk(GetActorLocation()) ? FQuat::Identity : FQuat::Identity,
        ChunkCoords == FVoxelConversion::WorldToChunk(GetActorLocation()) ? FColor::Red : FColor::Yellow,
        false, Duration, 0, 2.0f);

    // Draw label for chunk coordinates
    DrawDebugString(GetWorld(), ChunkOrigin, ChunkCoords.ToString(), nullptr, FColor::White, Duration);
}

void AVoxelConversionDebugger::VisualizeVoxelAtPosition(FVector WorldPosition, float Duration)
{
    FIntVector ChunkCoords = FVoxelConversion::WorldToChunk(WorldPosition);
    FIntVector LocalVoxel = FVoxelConversion::WorldToLocalVoxel(WorldPosition);
    
    // Calculate the center of the voxel
    int32 VoxelsPerChunk = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
    FIntVector GlobalVoxel(
        ChunkCoords.X * VoxelsPerChunk + LocalVoxel.X,
        ChunkCoords.Y * VoxelsPerChunk + LocalVoxel.Y,
        ChunkCoords.Z * VoxelsPerChunk + LocalVoxel.Z
    );
    
    FVector VoxelCenter = FVoxelConversion::LocalVoxelToWorld(GlobalVoxel);
    float VoxelSize = FVoxelConversion::LocalVoxelSize;
    
    // Draw the voxel as a cube
    DrawDebugBox(GetWorld(), VoxelCenter, FVector(VoxelSize * 0.5f), FQuat::Identity, FColor::Green, false, Duration, 0, 2.0f);
    
    // Draw a line from world position to voxel center to show any offset
    DrawDebugLine(GetWorld(), WorldPosition, VoxelCenter, FColor::Blue, false, Duration, 0, 3.0f);
    
    // Print debug info
    UE_LOG(LogTemp, Warning, TEXT("World Position: %s"), *WorldPosition.ToString());
    UE_LOG(LogTemp, Warning, TEXT("Voxel Center: %s"), *VoxelCenter.ToString());
    UE_LOG(LogTemp, Warning, TEXT("Distance: %f"), FVector::Distance(WorldPosition, VoxelCenter));
}

void AVoxelConversionDebugger::TestRoundTripConversion(FVector WorldPosition)
{
    // World -> Chunk + Local Voxel -> Global Voxel -> World
    FIntVector ChunkCoords = FVoxelConversion::WorldToChunk(WorldPosition);
    FIntVector LocalVoxel = FVoxelConversion::WorldToLocalVoxel(WorldPosition);
    
    int32 VoxelsPerChunk = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
    FIntVector GlobalVoxel(
        ChunkCoords.X * VoxelsPerChunk + LocalVoxel.X,
        ChunkCoords.Y * VoxelsPerChunk + LocalVoxel.Y,
        ChunkCoords.Z * VoxelsPerChunk + LocalVoxel.Z
    );
    
    FVector ReconstructedWorld = FVoxelConversion::LocalVoxelToWorld(GlobalVoxel);
    
    // Draw debug visualization
    DrawDebugSphere(GetWorld(), WorldPosition, 10.0f, 12, FColor::Red, false, 10.0f);
    DrawDebugSphere(GetWorld(), ReconstructedWorld, 10.0f, 12, FColor::Green, false, 10.0f);
    DrawDebugLine(GetWorld(), WorldPosition, ReconstructedWorld, FColor::Yellow, false, 10.0f, 0, 2.0f);
    
    // Print results
    UE_LOG(LogTemp, Warning, TEXT("Original World Position: %s"), *WorldPosition.ToString());
    UE_LOG(LogTemp, Warning, TEXT("Chunk Coords: %s"), *ChunkCoords.ToString());
    UE_LOG(LogTemp, Warning, TEXT("Local Voxel: %s"), *LocalVoxel.ToString());
    UE_LOG(LogTemp, Warning, TEXT("Global Voxel: %s"), *GlobalVoxel.ToString());
    UE_LOG(LogTemp, Warning, TEXT("Reconstructed World: %s"), *ReconstructedWorld.ToString());
    UE_LOG(LogTemp, Warning, TEXT("Error: %f"), FVector::Distance(WorldPosition, ReconstructedWorld));
}