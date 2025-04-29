#include "DiggerManager.h"
#include "CoreMinimal.h"
#include "LandscapeEdit.h"
#include "LandscapeInfo.h"
#include "LandscapeProxy.h"
#include "MarchingCubes.h"
#include "ProceduralMeshComponent.h"
#include "SparseVoxelGrid.h"
#include "TimerManager.h"
#include "VoxelChunk.h"
#include "Async/Async.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

//Editor Tool Specific Includes
#if WITH_EDITOR
#include "DiggerEditor/DiggerEdModeToolkit.h"
class Editor;
class FDiggerEdModeToolkit;
class FDiggerEdMode;



void ADiggerManager::ApplyBrushInEditor()
{
    FBrushStroke BrushStroke;
    BrushStroke.BrushPosition = EditorBrushPosition;
    BrushStroke.BrushRadius = EditorBrushRadius;
   // UE_LOG(LogTemp, Error, TEXT("[DiggerManager] BrushRadius set to: %f"), EditorBrushRadius);
    BrushStroke.BrushRotation = EditorBrushRotation;
    //UE_LOG(LogTemp, Error, TEXT("[ApplyBrushInEditor] Using rotation: %s"), *EditorBrushRotation.ToString());
    BrushStroke.BrushType = EditorBrushType;
    BrushStroke.bDig = EditorBrushDig; // or expose as property
    BrushStroke.BrushLength = EditorBrushLength; // or expose as property
    BrushStroke.BrushAngle = EditorBrushAngle; // or expose as property
    

    UE_LOG(LogTemp, Warning, TEXT("[ApplyBrushInEditor] EditorBrushPosition (World): %s"), *EditorBrushPosition.ToString());

    UVoxelChunk* TargetChunk = FindOrCreateNearestChunk(EditorBrushPosition);
    if (TargetChunk)
    {
        TargetChunk->ApplyBrushStroke(BrushStroke);
        MarkNearbyChunksDirty(EditorBrushPosition, EditorBrushRadius);
        ProcessDirtyChunks(); // Or whatever triggers your mesh update
    }
    
    if (GEditor)
    {
        GEditor->RedrawAllViewports();
    }
}
#endif

ADiggerManager::ADiggerManager()
{
#if WITH_EDITOR  
    this->SetFlags(RF_Transactional); // Enable undo/redo support in editor
#endif

    // Initialize the ProceduralMeshComponent
    ProceduralMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("GeneratedMesh"));
    RootComponent = ProceduralMesh;

    // Initialize the voxel grid and marching cubes
    SparseVoxelGrid = CreateDefaultSubobject<USparseVoxelGrid>(TEXT("SparseVoxelGrid"));
    MarchingCubes = CreateDefaultSubobject<UMarchingCubes>(TEXT("MarchingCubes"));

    // Initialize the brush component
    ActiveBrush = CreateDefaultSubobject<UVoxelBrushShape>(TEXT("ActiveBrush"));

    // Load the material in the constructor
    static ConstructorHelpers::FObjectFinder<UMaterial> Material(TEXT("/Game/Materials/M_VoxelMat.M_VoxelMat"));
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
    ActiveBrush->InitializeBrush(ActiveBrush->GetBrushType(),ActiveBrush->GetBrushSize(),ActiveBrush->GetBrushLocation(),this);

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


void ADiggerManager::BakeToStaticMesh(bool bEnableCollision, bool bEnableNanite, float DetailReduction)
{
    UE_LOG(LogTemp, Warning, TEXT("BakeToStaticMesh called with:\n  Collision: %s\n  Nanite: %s\n  DetailReduction: %.2f"),
        bEnableCollision ? TEXT("True") : TEXT("False"),
        bEnableNanite ? TEXT("True") : TEXT("False"),
        DetailReduction);
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

float ADiggerManager::GetLandscapeHeightAt(FVector WorldPosition)
{
    // Get all landscape actors in the level
    TArray<AActor*> FoundLandscapes;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), ALandscapeProxy::StaticClass(), FoundLandscapes);

    if (FoundLandscapes.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("No landscape found in level"));
        return 0.0f;
    }

    // Cast the first found landscape to ALandscapeProxy
    ALandscapeProxy* LandscapeProxy = Cast<ALandscapeProxy>(FoundLandscapes[0]);
    if (!LandscapeProxy)
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to cast landscape actor"));
        return 0.0f;
    }

    // Try getting height using different sources in order of complexity
    TOptional<float> HeightResult;

    // Try Simple first as it's most efficient
    HeightResult = LandscapeProxy->GetHeightAtLocation(WorldPosition, EHeightfieldSource::Simple);

    // If Simple fails, try Complex
    if (!HeightResult.IsSet())
    {
        HeightResult = LandscapeProxy->GetHeightAtLocation(WorldPosition, EHeightfieldSource::Complex);
    }

    // If Complex fails and we're in editor, try Editor source
    if (!HeightResult.IsSet() && GIsEditor)
    {
        HeightResult = LandscapeProxy->GetHeightAtLocation(WorldPosition, EHeightfieldSource::Editor);
    }

    if (!HeightResult.IsSet())
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to get height at location: %s"), *WorldPosition.ToString());
        return 0.0f;
    }

    return static_cast<float>(HeightResult.GetValue());
}

bool ADiggerManager::GetHeightAtLocation(ALandscapeProxy* LandscapeProxy, const FVector& Location, float& OutHeight)
{
    if (!LandscapeProxy || !LandscapeProxy->GetLandscapeInfo())
    {
        return false;
    }

    // Convert world location to landscape local space
    FVector LocalPosition = LandscapeProxy->GetTransform().InverseTransformPosition(Location);

    // Try getting height using different sources
    TOptional<float> HeightResult = LandscapeProxy->GetHeightAtLocation(LocalPosition, EHeightfieldSource::Simple);

    if (!HeightResult.IsSet())
    {
        HeightResult = LandscapeProxy->GetHeightAtLocation(LocalPosition, EHeightfieldSource::Complex);
    }

    if (HeightResult.IsSet())
    {
        // Convert the height to world space
        OutHeight = static_cast<float>(HeightResult.GetValue() * LandscapeProxy->GetActorScale3D().Z +
            LandscapeProxy->GetActorLocation().Z);
        return true;
    }

    return false;
}

void ADiggerManager::SpawnTerrainHole(const FVector& Location)
{
    if (HoleBP)
    {
        FActorSpawnParameters SpawnParams;
        GetWorld()->SpawnActor<AActor>(HoleBP, Location, FRotator::ZeroRotator, SpawnParams);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("HoleBP is not set in AMyClass!"));
    }
}

void ADiggerManager::DuplicateLandscape(ALandscapeProxy* Landscape)
{
    if (!Landscape) return;

    ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
    if (!LandscapeInfo) return;

    // Get landscape extent
    int32 LandscapeMinX, LandscapeMinY, LandscapeMaxX, LandscapeMaxY;
    LandscapeInfo->GetLandscapeExtent(LandscapeMinX, LandscapeMinY, LandscapeMaxX, LandscapeMaxY);

    // Create data access interface
    FLandscapeEditDataInterface LandscapeData(LandscapeInfo);

    int32 ChunkVoxelCount = ChunkSize / VoxelSize;

    // Get landscape transform for proper world position calculation
    FTransform LandscapeTransform = Landscape->GetActorTransform();
    float LandscapeScale = LandscapeTransform.GetScale3D().Z;

    // Iterate over the landscape extent in chunks
    for (int32 Y = LandscapeMinY; Y <= LandscapeMaxY; Y += ChunkVoxelCount)
    {
        for (int32 X = LandscapeMinX; X <= LandscapeMaxX; X += ChunkVoxelCount)
        {
            FIntVector ChunkPosition(X, Y, 0);
            UVoxelChunk* Chunk = GetOrCreateChunkAt(ChunkPosition);

            if (Chunk)
            {
                // Calculate chunk boundaries in landscape space
                int32 ChunkEndX = FMath::Min(X + ChunkVoxelCount, LandscapeMaxX);
                int32 ChunkEndY = FMath::Min(Y + ChunkVoxelCount, LandscapeMaxY);

                // Get height data for the entire chunk area
                TMap<FIntPoint, uint16> HeightData;
                int32 StartX = X;
                int32 StartY = Y;
                int32 EndX = ChunkEndX;
                int32 EndY = ChunkEndY;
                LandscapeData.GetHeightData(StartX, StartY, EndX, EndY, HeightData);

                for (int32 VoxelY = 0; VoxelY < ChunkVoxelCount; ++VoxelY)
                {
                    for (int32 VoxelX = 0; VoxelX < ChunkVoxelCount; ++VoxelX)
                    {
                        int32 LandscapeX = X + VoxelX;
                        int32 LandscapeY = Y + VoxelY;

                        // Skip if we're outside landscape bounds
                        if (LandscapeX > LandscapeMaxX || LandscapeY > LandscapeMaxY)
                            continue;

                        // Get height value from the height data map
                        FIntPoint Key(LandscapeX, LandscapeY);
                        float Height = 0.0f;

                        if (const uint16* HeightValue = HeightData.Find(Key))
                        {
                            // Convert from uint16 to world space height
                            // LandscapeDataAccess.h: Landscape uses USHRT_MAX as "32768" = 0.0f world space
                            const float ScaleFactor = LandscapeScale / 128.0f; // Landscape units to world units
                            Height = ((float)*HeightValue - 32768.0f) * ScaleFactor;
                            Height += LandscapeTransform.GetLocation().Z; // Add landscape base height
                        }

                        for (int32 VoxelZ = 0; VoxelZ < ChunkVoxelCount; ++VoxelZ)
                        {
                            // Convert voxel coordinates to world space
                            FVector WorldPosition = Chunk->VoxelToWorldCoordinates(FIntVector(VoxelX, VoxelY, VoxelZ));

                            // Calculate SDF value (distance from surface)
                            // Positive values are above the surface, negative values are below
                            float SDFValue = WorldPosition.Z - Height;

                            // Optional: Normalize SDF value by voxel size
                            SDFValue /= VoxelSize;

                            // Set the voxel value
                            bool bDig = false;
                            Chunk->SetVoxel(VoxelX, VoxelY, VoxelZ, SDFValue, bDig);
                        }
                    }
                }

                // Mark the chunk for update
                Chunk->MarkDirty();
            }
        }
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
        DebugDrawChunkSectionIDs();
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

int32 ADiggerManager::GetHitSectionIndex(const FHitResult& HitResult)
{
    if (!HitResult.GetComponent()) return -1;

    UProceduralMeshComponent* HitProceduralMesh = Cast<UProceduralMeshComponent>(HitResult.GetComponent());
    if (!HitProceduralMesh) return -1;

    const FVector HitLocation = HitResult.ImpactPoint;

    for (int32 SectionIndex = 0; SectionIndex < HitProceduralMesh->GetNumSections(); ++SectionIndex)
    {
        const FProcMeshSection* Section = HitProceduralMesh->GetProcMeshSection(SectionIndex);
        if (!Section || Section->ProcVertexBuffer.Num() == 0) continue;

        FBox SectionBox(Section->ProcVertexBuffer[0].Position, Section->ProcVertexBuffer[0].Position);
        for (const FProcMeshVertex& Vertex : Section->ProcVertexBuffer)
        {
            SectionBox += Vertex.Position;
        }

        if (SectionBox.IsInside(HitLocation))
        {
            return SectionIndex;
        }
    }

    return -1; // Section not found
}

UVoxelChunk* ADiggerManager::GetChunkBySectionIndex(int32 SectionIndex)
{
    for (auto& Entry : ChunkMap)
    {
        if (Entry.Value->GetSectionIndex() == SectionIndex)
        {
            return Entry.Value;
        }
    }

    return nullptr; // Chunk not found
}

void ADiggerManager::UpdateChunkFromSectionIndex(const FHitResult& HitResult)
{
    int32 SectionIndex = GetHitSectionIndex(HitResult);
    if (SectionIndex == -1) return;

    UVoxelChunk* HitChunk = GetChunkBySectionIndex(SectionIndex);
    if (HitChunk)
    {
        HitChunk->MarkDirty();
    }
}

void ADiggerManager::DebugDrawChunkSectionIDs()
{
    for (auto& Entry : ChunkMap)
    {
        UVoxelChunk* Chunk = Entry.Value;
        if (Chunk)
        {
            Chunk->GetSparseVoxelGrid()->RenderVoxels();
            const FVector ChunkPosition = Chunk->GetWorldPosition();
            const FString SectionIDText = FString::Printf(TEXT("ID: %d"), Chunk->GetSectionIndex());
            DrawDebugString(GetWorld(), ChunkPosition, SectionIDText, nullptr, FColor::Green, 5.0f, true);
        }
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

    UVoxelChunk* TargetChunk = FindOrCreateNearestChunk(BrushPosition);
    if (TargetChunk)
    {
        TargetChunk->ApplyBrushStroke(BrushStroke);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("No valid chunk found or created for BrushPosition: %s"), *BrushPosition.ToString());
    }

    if (BrushStroke.bDig)
    {
        MarkNearbyChunksDirty(BrushPosition, BrushRadius);
    }
}

UVoxelChunk* ADiggerManager::FindOrCreateNearestChunk(const FVector& Position)
{
    FIntVector ChunkCoords = WorldToChunkCoordinates(Position.X, Position.Y, Position.Z);
    UVoxelChunk** ExistingChunk = ChunkMap.Find(ChunkCoords);
    if (ExistingChunk && *ExistingChunk)
    {
        UE_LOG(LogTemp, Warning, TEXT("Chunk found at %s"), *ChunkCoords.ToString());
        return *ExistingChunk;
    }

    
    // Find nearest chunk if no exact match found
    UVoxelChunk* NearestChunk = nullptr;
    float MinDistance = FLT_MAX;
    for (auto& Entry : ChunkMap)
    {
        float Distance = FVector::Dist(Position, Entry.Value->GetWorldPosition());
        if (Distance < MinDistance)
        {
            MinDistance = Distance;
            NearestChunk = Entry.Value;
        }
    }

    // If no nearby chunk is found, create a new one
    if (!NearestChunk)
    {
        UE_LOG(LogTemp, Warning, TEXT("[FindOrCreateNearestChunk] Input Position (World): %s"), *Position.ToString());
        ChunkCoords = WorldToChunkCoordinates(Position.X, Position.Y, Position.Z);
        UE_LOG(LogTemp, Warning, TEXT("[FindOrCreateNearestChunk] ChunkCoords: %s"), *ChunkCoords.ToString());

        
        UVoxelChunk* NewChunk = GetOrCreateChunkAt(ChunkCoords.X, ChunkCoords.Y, ChunkCoords.Z);
        if (NewChunk)
        {
            NewChunk->InitializeChunk(ChunkCoords, this);
            ChunkMap.Add(ChunkCoords, NewChunk);
            return NewChunk;
        }
    }

    return NearestChunk;
}

UVoxelChunk* ADiggerManager::FindNearestChunk(const FVector& Position)
{
    FIntVector ChunkCoords = WorldToChunkCoordinates(Position.X, Position.Y, Position.Z);
    UVoxelChunk** ExistingChunk = ChunkMap.Find(ChunkCoords);
    if (ExistingChunk && *ExistingChunk)
    {
        return *ExistingChunk;
    }

    // Find nearest chunk if no exact match found
    UVoxelChunk* NearestChunk = nullptr;
    float MinDistance = FLT_MAX;
    for (auto& Entry : ChunkMap)
    {
        float Distance = FVector::Dist(Position, Entry.Value->GetWorldPosition());
        if (Distance < MinDistance)
        {
            MinDistance = Distance;
            NearestChunk = Entry.Value;
        }
    }

    return NearestChunk;
}

/*
void ADiggerManager::PreviewVoxelBrush(const FVoxelBrush& Brush, const FVector& WorldLocation)
{
    UWorld* World = GetWorld();
    if (!World)
        return;

    const float VoxelSize = Brush.VoxelSize;

    for (const FIntVector& VoxelCoord : Brush.BrushVoxels)
    {
        FVector LocalPosition = FVector(
            VoxelCoord.X * VoxelSize,
            VoxelCoord.Y * VoxelSize,
            VoxelCoord.Z * VoxelSize
        );

        FVector DrawPosition = WorldLocation + LocalPosition;

        DrawDebugBox(
            World,
            DrawPosition,
            FVector(VoxelSize * 0.5f), // Half extents
            FColor::Cyan,
            false, // Persistent
            -1.0f, // Lifetime (only for one frame unless refreshed)
            0,
            1.0f   // Thickness
        );
    }
}
*/

void ADiggerManager::MarkNearbyChunksDirty(const FVector& CenterPosition, float Radius)
{
    int32 Reach = FMath::CeilToInt(Radius / (ChunkSize * VoxelSize));
    FIntVector CenterChunkCoords = WorldToChunkCoordinates(CenterPosition.X, CenterPosition.Y, CenterPosition.Z);

    for (int32 X = -Reach; X <= Reach; ++X)
    {
        for (int32 Y = -Reach; Y <= Reach; ++Y)
        {
            for (int32 Z = -Reach; Z <= Reach; ++Z)
            {
                FIntVector NearbyChunkCoords = CenterChunkCoords + FIntVector(X, Y, Z);
                UVoxelChunk** NearbyChunk = ChunkMap.Find(NearbyChunkCoords);
                if (NearbyChunk && *NearbyChunk)
                {
                    (*NearbyChunk)->MarkDirty();
                    UE_LOG(LogTemp, Log, TEXT("Marked nearby chunk dirty at position: %s"), *NearbyChunkCoords.ToString());
                }
            }
        }
    }
}

FIntVector ADiggerManager::CalculateChunkPosition(const FIntVector& ProposedChunkPosition) const
{
    FVector ChunkWorldPosition = FVector(ProposedChunkPosition) * ChunkSize * TerrainGridSize;
    UE_LOG(LogTemp, Warning, TEXT("CalculateChunkPosition: ProposedChunkPosition = %s, ChunkWorldPosition = %s"),
        *ProposedChunkPosition.ToString(), *ChunkWorldPosition.ToString());
    return ProposedChunkPosition;
}

FIntVector ADiggerManager::CalculateChunkPosition(const FVector& WorldPosition) const
{
    int32 ChunkWorldSize = ChunkSize * TerrainGridSize;
    return FIntVector(
        FMath::FloorToInt(WorldPosition.X / ChunkWorldSize),
        FMath::FloorToInt(WorldPosition.Y / ChunkWorldSize),
        FMath::FloorToInt(WorldPosition.Z / ChunkWorldSize)
    );
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

// In ADiggerManager.cpp
TArray<FIslandData> ADiggerManager::GetAllIslands() const
{
#if WITH_EDITOR
    OnIslandsDetectionStarted.Broadcast(); // Only clear UI in editor
#endif

    TArray<FIslandData> AllIslands;
    for (const auto& ChunkPair : ChunkMap)
    {
        UVoxelChunk* Chunk = ChunkPair.Value;
        if (Chunk)
        {
            TArray<FIslandData> ChunkIslands = Chunk->GetSparseVoxelGrid()->DetectIslands(0.0f);
            AllIslands.Append(ChunkIslands);
        }
    }
    return AllIslands;
}

#if WITH_EDITOR

void ADiggerManager::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    // Respond to property changes (e.g., ChunkSize, TerrainGridSize, etc.)
    EditorUpdateChunks();
}

void ADiggerManager::PostEditMove(bool bFinished)
{
    Super::PostEditMove(bFinished);

    // Optionally update chunks if the actor is moved in the editor
    if (bFinished)
    {
        EditorUpdateChunks();
    }
}

void ADiggerManager::PostEditUndo()
{
    Super::PostEditUndo();
    // Respond to undo/redo
    EditorUpdateChunks();
}

void ADiggerManager::EditorUpdateChunks()
{
    // Call your chunk update logic here
    ProcessDirtyChunks();
}

void ADiggerManager::EditorRebuildAllChunks()
{
    // Optionally clear and rebuild all chunks
    InitializeChunks();
}


#if WITH_EDITOR
/*void ADiggerManager::UpdateAllIslandsInToolkit() const
{
    if (EditorToolkit) // Set by toolkit on construction
    {
        EditorToolkit->SetIslands(GetAllIslands());
    }
}*/
#endif


#endif // WITH_EDITOR
