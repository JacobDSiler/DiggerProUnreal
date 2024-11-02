#include "DiggerManager.h"
#include "SparseVoxelGrid.h"
#include "MarchingCubes.h"
#include "VoxelChunk.h"
#include "ProceduralMeshComponent.h"

ADiggerManager::ADiggerManager()
{
    // Initialize the ProceduralMeshComponent
    ProceduralMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("GeneratedMesh"));
    RootComponent = ProceduralMesh;

    // Initialize the voxel grid and marching cubes
    SparseVoxelGrid = CreateDefaultSubobject<USparseVoxelGrid>(TEXT("SparseVoxelGrid"));
    MarchingCubes = CreateDefaultSubobject<UMarchingCubes>(TEXT("MarchingCubes"));

    // Initialize the brush component
    ActiveBrush = CreateDefaultSubobject<UVoxelBrushShape>(TEXT("ActiveBrush"));

    // Load the material in the constructor
    static ConstructorHelpers::FObjectFinder<UMaterial> Material(TEXT("/Game/Materials/M_ProcGrid.M_ProcGrid"));
    if (Material.Succeeded())
    {
        TerrainMaterial = Material.Object;
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Material M_ProcGrid required in /Content/Materials/ folder. Please ensure it is there."));
    }
}

bool ADiggerManager::EnsureWorldReference()
{
    if (World)
    {
        return true;
    }

    World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("World not found in DiggerManager."));
        return false; // Exit if World is not found
    }
    return true;
}

void ADiggerManager::BeginPlay()
{
    Super::BeginPlay();

    if (!EnsureWorldReference())
    {
        UE_LOG(LogTemp, Error, TEXT("World is null in DiggerManager BeginPlay()! Continuing with default behavior."));
    }

    UpdateVoxelSize();


    // Start the timer to process dirty chunks
    if (World)
    {
        World->GetTimerManager().SetTimer(ChunkProcessTimerHandle, this, &ADiggerManager::ProcessDirtyChunks, 1.0f, true);
    }

    // Set the ProceduralMesh material if it's valid
    if (TerrainMaterial)
    {
        if (ProceduralMesh && ProceduralMesh->GetMaterial(0) != TerrainMaterial)
        {
            ProceduralMesh->SetMaterial(0, TerrainMaterial);
            UE_LOG(LogTemp, Warning, TEXT("Set Material M_ProcGrid at index 0"));
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Material M_ProcGrid is null. Please ensure it is loaded properly."));
    }
}

void ADiggerManager::UpdateVoxelSize()
{
    if (Subdivisions != 0)
    {
        VoxelSize = TerrainGridSize / Subdivisions;
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("Subdivisions value is zero. VoxelSize will not be updated to avoid division by zero."));
        VoxelSize = 1.0f; // Default fallback value
    }
}

void ADiggerManager::InitializeChunks()
{
    for (const auto& ChunkPair : ChunkMap)
    {
        const FIntVector& Coordinates = ChunkPair.Key;
        UVoxelChunk* NewChunk = NewObject<UVoxelChunk>(this);
        if (!NewChunk)
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to create a new chunk for coordinates: %s"), *Coordinates.ToString());
            continue;
        }

        UProceduralMeshComponent* NewMeshComponent = NewObject<UProceduralMeshComponent>(this);
        if (NewMeshComponent)
        {
            NewMeshComponent->RegisterComponent();
            NewMeshComponent->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
            NewChunk->InitializeMeshComponent(NewMeshComponent);
            ChunkMap.Add(Coordinates, NewChunk);
            ProceduralMeshComponents.Add(NewMeshComponent);
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to create a new ProceduralMeshComponent for coordinates: %s"), *Coordinates.ToString());
        }
    }
}

void ADiggerManager::InitializeSingleChunk(UVoxelChunk* Chunk)
{
    if (!Chunk)
    {
        UE_LOG(LogTemp, Error, TEXT("InitializeSingleChunk called with a null chunk."));
        return;
    }

    FIntVector Coordinates = Chunk->GetChunkPosition();
    UProceduralMeshComponent* NewMeshComponent = NewObject<UProceduralMeshComponent>(this);
    if (NewMeshComponent)
    {
        NewMeshComponent->RegisterComponent();
        NewMeshComponent->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
        Chunk->InitializeMeshComponent(NewMeshComponent);
        ChunkMap.Add(Coordinates, Chunk);
        ProceduralMeshComponents.Add(NewMeshComponent);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create a ProceduralMeshComponent for chunk at coordinates: %s"), *Coordinates.ToString());
    }
}

void ADiggerManager::DebugLogVoxelChunkGrid() const
{
    UE_LOG(LogTemp, Warning, TEXT("Logging Voxel Chunk Grid:"));

    if (ChunkMap.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("No chunks in ChunkMap!"));
        return;
    }

    // Assuming you have a map or array of chunks
    for (auto& ChunkPair : ChunkMap)
    {
        UVoxelChunk* Chunk = ChunkPair.Value;
        if (Chunk)
        {
            Chunk->DebugDrawChunk();
        }
    }
}

void ADiggerManager::DebugVoxels()
{
    if (!ChunkMap.IsEmpty())
    {
        for (const auto& ChunkPair : ChunkMap)
        {
            const FIntVector& ChunkCoordinates = ChunkPair.Key;
            UVoxelChunk* Chunk = ChunkPair.Value;

            if (Chunk && Chunk->GetSparseVoxelGrid())
            {
                DebugLogVoxelChunkGrid();
                USparseVoxelGrid* SparseVoxelGridPtr = Chunk->GetSparseVoxelGrid();
                if (SparseVoxelGridPtr)
                {
                    SparseVoxelGridPtr->RenderVoxels();
                } // Render voxels
                UE_LOG(LogTemp, Warning, TEXT("Rendering voxels for chunk at coordinates: %s"), *ChunkCoordinates.ToString());
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("Chunk or SparseVoxelGrid is null for coordinates: %s"), *ChunkCoordinates.ToString());
            }
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("No chunks available for rendering."));
    }
}


void ADiggerManager::ProcessDirtyChunks()
{
    for (auto& Elem : ChunkMap)
    {
        UVoxelChunk* Chunk = Elem.Value;
        if (Chunk)
        {
            Chunk->UpdateIfDirty();
        }
    }

    if (World)
    {
        AsyncTask(ENamedThreads::GameThread, [this]()
        {
            if (World)
            {
                World->GetTimerManager().ClearTimer(ChunkProcessTimerHandle);
                World->GetTimerManager().SetTimer(ChunkProcessTimerHandle, this, &ADiggerManager::ProcessDirtyChunks, 2.0f, true);
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("World is null when resetting the ProcessDirtyChunks timer."));
            }
        });
    }
}

void ADiggerManager::ApplyBrush()
{
    if (!ActiveBrush)
    {
        UE_LOG(LogTemp, Error, TEXT("ActiveBrush is null. Cannot apply brush."));
        return;
    }

    FVector BrushPosition = ActiveBrush->GetBrushLocation();
    float BrushRadius = ActiveBrush->GetBrushSize();
    FBrushStroke BrushStroke;
    BrushStroke.BrushPosition = BrushPosition;
    BrushStroke.BrushRadius = BrushRadius;
    BrushStroke.bDig = ActiveBrush->GetDig();
    BrushStroke.BrushType = ActiveBrush->GetBrushType();

    UVoxelChunk* TargetChunk = GetOrCreateChunkAt(BrushPosition);
    if (TargetChunk)
    {
        TargetChunk->ApplyBrushStroke(BrushStroke);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("No valid chunk found or created for BrushPosition: %s"), *BrushPosition.ToString());
    }
}

FIntVector ADiggerManager::CalculateChunkPosition(const FIntVector& ProposedChunkPosition) const
{
    FVector ChunkWorldPosition = FVector(ProposedChunkPosition) * ChunkSize * TerrainGridSize;
    UE_LOG(LogTemp, Warning, TEXT("CalculateChunkPosition: ProposedChunkPosition = %s, ChunkWorldPosition = %s"),
           *ProposedChunkPosition.ToString(), *ChunkWorldPosition.ToString());
    return ProposedChunkPosition;
}

FIntVector ADiggerManager::CalculateChunkPosition(const FVector3d& WorldPosition) const
{
    int32 ChunkWorldSize = ChunkSize * TerrainGridSize;
    int32 ChunkIndexX = FMath::FloorToInt(WorldPosition.X / ChunkWorldSize);
    int32 ChunkIndexY = FMath::FloorToInt(WorldPosition.Y / ChunkWorldSize);
    int32 ChunkIndexZ = FMath::FloorToInt(WorldPosition.Z / ChunkWorldSize);
    return FIntVector(ChunkIndexX, ChunkIndexY, ChunkIndexZ);
}

UVoxelChunk* ADiggerManager::GetOrCreateChunkAt(const FVector3d& ProposedChunkPosition)
{
    int32 ChunkWorldSize = ChunkSize * TerrainGridSize;
    UpdateVoxelSize();
    int32 FlooredX = FMath::FloorToInt(ProposedChunkPosition.X / ChunkWorldSize);
    int32 FlooredY = FMath::FloorToInt(ProposedChunkPosition.Y / ChunkWorldSize);
    int32 FlooredZ = FMath::FloorToInt(ProposedChunkPosition.Z / ChunkWorldSize);
    FIntVector ChunkCoords(FlooredX, FlooredY, FlooredZ);
    return GetOrCreateChunkAt(ChunkCoords);
}

UVoxelChunk* ADiggerManager::GetOrCreateChunkAt(const float& ProposedChunkX, const float& ProposedChunkY, const float& ProposedChunkZ)
{
    return GetOrCreateChunkAt(FIntVector(ProposedChunkX, ProposedChunkY, ProposedChunkZ));
}

UVoxelChunk* ADiggerManager::GetOrCreateChunkAt(const FIntVector& ProposedChunkPosition)
{
    FIntVector ChunkPosition = CalculateChunkPosition(ProposedChunkPosition);
    UVoxelChunk** ExistingChunk = ChunkMap.Find(ChunkPosition);

    if (ExistingChunk)
    {
        UE_LOG(LogTemp, Log, TEXT("Found existing chunk at position: %s"), *ChunkPosition.ToString());
        return *ExistingChunk;
    }

    UVoxelChunk* NewChunk = NewObject<UVoxelChunk>(this);
    if (NewChunk)
    {
        NewChunk->InitializeChunk(ChunkPosition, this);
        ChunkMap.Add(ChunkPosition, NewChunk);
        UE_LOG(LogTemp, Log, TEXT("Created a new chunk at position: %s"), *ChunkPosition.ToString());
        return NewChunk;
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create a new chunk at position: %s"), *ChunkPosition.ToString());
        return nullptr;
    }
}
