#include "TestVoxelConversion.h"
#include "DrawDebugHelpers.h"
#include "VoxelConversion.h"
#include "UObject/ConstructorHelpers.h"

ATestVoxelConversion::ATestVoxelConversion()
{
    PrimaryActorTick.bCanEverTick = false;
    
    // Create a simple visual component
    UStaticMeshComponent* MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TestMesh"));
    SetRootComponent(MeshComp);
    
    // Make it visible in the editor
    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMeshFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if (SphereMeshFinder.Succeeded())
    {
        MeshComp->SetStaticMesh(SphereMeshFinder.Object);
        MeshComp->SetRelativeScale3D(FVector(0.25f));
    }
}

void ATestVoxelConversion::BeginPlay()
{
    Super::BeginPlay();
    
    // Initialize the voxel conversion system with our settings
    FVoxelConversion::InitFromConfig(ChunkSize, Subdivisions, TerrainGridSize, VoxelOrigin);
    
    // Run basic tests
    RunTests();
}

void ATestVoxelConversion::RunTests()
{
    UE_LOG(LogTemp, Display, TEXT("======= VOXEL CONVERSION SYSTEM TEST ======="));
    UE_LOG(LogTemp, Display, TEXT("Configuration:"));
    UE_LOG(LogTemp, Display, TEXT("  ChunkSize: %d"), FVoxelConversion::ChunkSize);
    UE_LOG(LogTemp, Display, TEXT("  Subdivisions: %d"), FVoxelConversion::Subdivisions);
    UE_LOG(LogTemp, Display, TEXT("  TerrainGridSize: %f"), FVoxelConversion::TerrainGridSize);
    UE_LOG(LogTemp, Display, TEXT("  LocalVoxelSize: %f"), FVoxelConversion::LocalVoxelSize);
    UE_LOG(LogTemp, Display, TEXT("  Origin: %s"), *FVoxelConversion::Origin.ToString());
    
    // Test current position
    FVector ActorPos = GetActorLocation();
    UE_LOG(LogTemp, Display, TEXT("Testing position: %s"), *ActorPos.ToString());
    
    // Test conversions
    FIntVector ChunkCoords = FVoxelConversion::WorldToChunk(ActorPos);
    FVector ChunkWorldPos = FVoxelConversion::ChunkToWorld(ChunkCoords);
    FIntVector LocalVoxel = FVoxelConversion::WorldToLocalVoxel(ActorPos);
    
    UE_LOG(LogTemp, Display, TEXT("  World -> Chunk: %s"), *ChunkCoords.ToString());
    UE_LOG(LogTemp, Display, TEXT("  Chunk -> World: %s"), *ChunkWorldPos.ToString());
    UE_LOG(LogTemp, Display, TEXT("  World -> Local Voxel: %s"), *LocalVoxel.ToString());
    
    // Calculate global voxel coordinate
    int32 VoxelsPerChunk = ChunkSize * Subdivisions;
    FIntVector GlobalVoxel(
        ChunkCoords.X * VoxelsPerChunk + LocalVoxel.X,
        ChunkCoords.Y * VoxelsPerChunk + LocalVoxel.Y,
        ChunkCoords.Z * VoxelsPerChunk + LocalVoxel.Z
    );
    UE_LOG(LogTemp, Display, TEXT("  Calculated Global Voxel: %s"), *GlobalVoxel.ToString());
    
    // Test round-trip conversion
    FVector ReconstructedPos = FVoxelConversion::LocalVoxelToWorld(GlobalVoxel);
    UE_LOG(LogTemp, Display, TEXT("  Global Voxel -> World: %s"), *ReconstructedPos.ToString());
    
    float Error = FVector::Distance(ActorPos, ReconstructedPos);
    UE_LOG(LogTemp, Display, TEXT("  Round-trip error: %f units"), Error);
    
    // Visual debug
    DrawDebugSphere(GetWorld(), ActorPos, 10.0f, 16, FColor::Red, false, 30.0f);
    DrawDebugSphere(GetWorld(), ReconstructedPos, 10.0f, 16, FColor::Green, false, 30.0f);
    DrawDebugSphere(GetWorld(), ChunkWorldPos, 15.0f, 8, FColor::Blue, false, 30.0f);
    
    // Draw chunk boundary
    float ChunkWorldSize = FVoxelConversion::ChunkSize * FVoxelConversion::TerrainGridSize;
    DrawDebugBox(GetWorld(), 
                ChunkWorldPos + FVector(ChunkWorldSize * 0.5f),
                FVector(ChunkWorldSize * 0.5f),
                FQuat::Identity,
                FColor::Yellow,
                false,
                30.0f,
                0,
                2.0f);
    
    UE_LOG(LogTemp, Display, TEXT("=========================================="));
    
    // Test at various distances
    TestPositionAtDistance(50.0f);
    TestPositionAtDistance(100.0f);
    TestPositionAtDistance(500.0f);
    
    // Visualize chunks
    VisualizeChunksAround(200.0f);
}

void ATestVoxelConversion::TestPositionAtDistance(float Distance)
{
    // Test in X direction
    FVector TestPos = GetActorLocation() + FVector(Distance, 0.0f, 0.0f);
    FIntVector ChunkCoords = FVoxelConversion::WorldToChunk(TestPos);
    FIntVector LocalVoxel = FVoxelConversion::WorldToLocalVoxel(TestPos);
    
    UE_LOG(LogTemp, Display, TEXT("Test at +X %f: Chunk=%s, LocalVoxel=%s"), 
           Distance, *ChunkCoords.ToString(), *LocalVoxel.ToString());
    
    // Test in Y direction
    TestPos = GetActorLocation() + FVector(0.0f, Distance, 0.0f);
    ChunkCoords = FVoxelConversion::WorldToChunk(TestPos);
    LocalVoxel = FVoxelConversion::WorldToLocalVoxel(TestPos);
    
    UE_LOG(LogTemp, Display, TEXT("Test at +Y %f: Chunk=%s, LocalVoxel=%s"), 
           Distance, *ChunkCoords.ToString(), *LocalVoxel.ToString());
    
    // Test in Z direction
    TestPos = GetActorLocation() + FVector(0.0f, 0.0f, Distance);
    ChunkCoords = FVoxelConversion::WorldToChunk(TestPos);
    LocalVoxel = FVoxelConversion::WorldToLocalVoxel(TestPos);
    
    UE_LOG(LogTemp, Display, TEXT("Test at +Z %f: Chunk=%s, LocalVoxel=%s"), 
           Distance, *ChunkCoords.ToString(), *LocalVoxel.ToString());
}

static FVector LocalVoxelToWorld(const FIntVector& GlobalVoxelCoords)
{
    // Calculate voxels per chunk dimension
    int32 VoxelsPerChunk = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
    
    // Determine which chunk this voxel belongs to
    const FIntVector ChunkCoords(
        FMath::FloorToInt((float)GlobalVoxelCoords.X / VoxelsPerChunk),
        FMath::FloorToInt((float)GlobalVoxelCoords.Y / VoxelsPerChunk),
        FMath::FloorToInt((float)GlobalVoxelCoords.Z / VoxelsPerChunk)
    );
    
    // Calculate local coordinates within the chunk
    FIntVector LocalInChunk(
        GlobalVoxelCoords.X - ChunkCoords.X * VoxelsPerChunk,
        GlobalVoxelCoords.Y - ChunkCoords.Y * VoxelsPerChunk,
        GlobalVoxelCoords.Z - ChunkCoords.Z * VoxelsPerChunk
    );
    
    // Get the world position of the chunk's origin
    const FVector ChunkOrigin = FVoxelConversion::ChunkToWorld(ChunkCoords);
    
    // Calculate final world position (center of voxel)
    FVector WorldPos = ChunkOrigin 
        + FVector(LocalInChunk) * FVoxelConversion::LocalVoxelSize 
        + FVector(FVoxelConversion::LocalVoxelSize * 0.5f);  // Center offset
        
    UE_LOG(LogTemp, Verbose, TEXT("[LocalVoxelToWorld] GlobalVoxelCoords: %s, ChunkCoords: %s, LocalInChunk: %s, WorldPos: %s"),
           *GlobalVoxelCoords.ToString(), *ChunkCoords.ToString(), *LocalInChunk.ToString(), *WorldPos.ToString());
           
    return WorldPos;
}


void ATestVoxelConversion::VisualizeChunksAround(float Radius)
{
    FVector ActorPos = GetActorLocation();
    
    // Convert to chunk coordinates with padding
    FIntVector MinChunk = FVoxelConversion::WorldToChunk(ActorPos - FVector(Radius));
    FIntVector MaxChunk = FVoxelConversion::WorldToChunk(ActorPos + FVector(Radius));
    
    // Visualize each chunk
    for (int32 X = MinChunk.X; X <= MaxChunk.X; ++X)
    for (int32 Y = MinChunk.Y; Y <= MaxChunk.Y; ++Y)
    for (int32 Z = MinChunk.Z; Z <= MaxChunk.Z; ++Z)
    {
        FIntVector ChunkCoords(X, Y, Z);
        FVector ChunkOrigin = FVoxelConversion::ChunkToWorld(ChunkCoords);
        float ChunkWorldSize = FVoxelConversion::ChunkSize * FVoxelConversion::TerrainGridSize;
        
        // Draw chunk boundaries
        DrawDebugBox(GetWorld(), 
                    ChunkOrigin + FVector(ChunkWorldSize * 0.5f),
                    FVector(ChunkWorldSize * 0.5f),
                    FQuat::Identity,
                    ChunkCoords == FVoxelConversion::WorldToChunk(ActorPos) ? FColor::Red : FColor::Yellow,
                    false,
                    30.0f,
                    0,
                    1.0f);
        
        // Add chunk coordinate label
        DrawDebugString(GetWorld(), 
                       ChunkOrigin, 
                       ChunkCoords.ToString(),
                       nullptr,
                       FColor::White,
                       30.0f);
    }
}