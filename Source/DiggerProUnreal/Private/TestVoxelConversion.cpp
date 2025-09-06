#include "TestVoxelConversion.h"
#include "DrawDebugHelpers.h"
#include "VoxelConversion.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

ATestVoxelConversion::ATestVoxelConversion()
{
    PrimaryActorTick.bCanEverTick = false;

    UStaticMeshComponent* MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TestMesh"));
    SetRootComponent(MeshComp);

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

    // Initialize consolidated conversion config
    FVoxelConversion::InitFromConfig(ChunkSize, Subdivisions, TerrainGridSize, VoxelOrigin);

    RunTests();
}

void ATestVoxelConversion::RunTests()
{
    UE_LOG(LogTemp, Display, TEXT("======= VOXEL CONVERSION SYSTEM TEST ======="));
    UE_LOG(LogTemp, Display, TEXT("Config: ChunkSize=%d Subdivisions=%d Grid=%g Voxel=%g Origin=%s"),
        FVoxelConversion::ChunkSize,
        FVoxelConversion::Subdivisions,
        FVoxelConversion::TerrainGridSize,
        FVoxelConversion::LocalVoxelSize,
        *FVoxelConversion::Origin.ToString());

    const FVector ActorPos = GetActorLocation();
    UE_LOG(LogTemp, Display, TEXT("Testing position: %s"), *ActorPos.ToString());

    // World -> Chunk (min-corner)
    const FIntVector ChunkCoords = FVoxelConversion::WorldToChunk_Min(ActorPos);

    // Chunk -> World (min-corner)
    const FVector ChunkWorldPos = FVoxelConversion::ChunkToWorld_Min(ChunkCoords);

    // World -> (Chunk, Local) (min-corner)
    FIntVector OutChunk, LocalVoxel;
    FVoxelConversion::WorldToChunkAndLocal_Min(ActorPos, OutChunk, LocalVoxel);

    UE_LOG(LogTemp, Display, TEXT("  World -> Chunk: %s"), *ChunkCoords.ToString());
    UE_LOG(LogTemp, Display, TEXT("  Chunk -> World(min-corner): %s"), *ChunkWorldPos.ToString());
    UE_LOG(LogTemp, Display, TEXT("  World -> (Chunk=%s, Local=%s)"),
        *OutChunk.ToString(), *LocalVoxel.ToString());

    // Global voxel = Chunk * VPC + Local
    const int32 VPC = FVoxelConversion::VoxelsPerChunk();
    const FIntVector GlobalVoxel(
        OutChunk.X * VPC + LocalVoxel.X,
        OutChunk.Y * VPC + LocalVoxel.Y,
        OutChunk.Z * VPC + LocalVoxel.Z);

    UE_LOG(LogTemp, Display, TEXT("  Global Voxel: %s"), *GlobalVoxel.ToString());

    // Global voxel center -> World
    const FVector ReconstructedPos = FVoxelConversion::GlobalVoxelCenterToWorld(GlobalVoxel);
    UE_LOG(LogTemp, Display, TEXT("  Global Voxel -> World(center): %s"), *ReconstructedPos.ToString());

    const float Error = FVector::Distance(ActorPos, ReconstructedPos);
    UE_LOG(LogTemp, Display, TEXT("  Round-trip error: %f units"), Error);

    // Visual debug
    DrawDebugSphere(GetWorld(), ActorPos,         10.0f, 16, FColor::Red,   false, 30.0f);
    DrawDebugSphere(GetWorld(), ReconstructedPos, 10.0f, 16, FColor::Green, false, 30.0f);
    DrawDebugSphere(GetWorld(), ChunkWorldPos,    15.0f,  8, FColor::Blue,  false, 30.0f);

    // Draw chunk boundary (min-corner box)
    const float CWS = FVoxelConversion::ChunkWorldSize();
    DrawDebugBox(GetWorld(),
                 ChunkWorldPos + FVector(CWS * 0.5f),
                 FVector(CWS * 0.5f),
                 FQuat::Identity,
                 FColor::Yellow,
                 false,
                 30.0f,
                 0,
                 2.0f);

    UE_LOG(LogTemp, Display, TEXT("=========================================="));

    TestPositionAtDistance(50.0f);
    TestPositionAtDistance(100.0f);
    TestPositionAtDistance(500.0f);

    VisualizeChunksAround(200.0f);
}

void ATestVoxelConversion::TestPositionAtDistance(float Distance)
{
    const FVector Base = GetActorLocation();

    auto TestOne = [&](const FVector& Pos, const TCHAR* Tag)
    {
        FIntVector Chunk, Local;
        FVoxelConversion::WorldToChunkAndLocal_Min(Pos, Chunk, Local);
        UE_LOG(LogTemp, Display, TEXT("Test %s %6.1f: Chunk=%s Local=%s"),
            Tag, Distance, *Chunk.ToString(), *Local.ToString());
    };

    TestOne(Base + FVector(Distance, 0, 0), TEXT("+X"));
    TestOne(Base + FVector(0, Distance, 0), TEXT("+Y"));
    TestOne(Base + FVector(0, 0, Distance), TEXT("+Z"));
}

void ATestVoxelConversion::VisualizeChunksAround(float Radius)
{
    const FVector ActorPos = GetActorLocation();

    // Chunk range around actor (min-corner)
    const FIntVector MinChunk = FVoxelConversion::WorldToChunk_Min(ActorPos - FVector(Radius));
    const FIntVector MaxChunk = FVoxelConversion::WorldToChunk_Min(ActorPos + FVector(Radius));

    const float CWS = FVoxelConversion::ChunkWorldSize();
    const FIntVector ActorChunk = FVoxelConversion::WorldToChunk_Min(ActorPos);

    for (int32 X = MinChunk.X; X <= MaxChunk.X; ++X)
    for (int32 Y = MinChunk.Y; Y <= MaxChunk.Y; ++Y)
    for (int32 Z = MinChunk.Z; Z <= MaxChunk.Z; ++Z)
    {
        const FIntVector ChunkCoords(X, Y, Z);
        const FVector ChunkOrigin = FVoxelConversion::ChunkToWorld_Min(ChunkCoords);

        const bool bActorChunk = (ChunkCoords == ActorChunk);
        const FColor BoxColor = bActorChunk ? FColor::Red : FColor::Yellow;

        // Box centered on chunk center, sized to chunk extent
        DrawDebugBox(GetWorld(),
                     ChunkOrigin + FVector(CWS * 0.5f),
                     FVector(CWS * 0.5f),
                     FQuat::Identity,
                     BoxColor,
                     false,
                     30.0f,
                     0,
                     1.0f);

        DrawDebugString(GetWorld(),
                        ChunkOrigin,
                        ChunkCoords.ToString(),
                        nullptr,
                        FColor::White,
                        30.0f);
    }
}
