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
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "IslandActor.h"
#include "Engine/StaticMesh.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "VoxelConversion.h"
#include "Engine/StaticMesh.h"



//Editor Tool Specific Includes
#if WITH_EDITOR
#include "DiggerEditor/DiggerEdModeToolkit.h"
class Editor;
class FDiggerEdModeToolkit;
class FDiggerEdMode;
class MeshDescriptors;
class StaticMeshAttributes;




void ADiggerManager::ApplyBrushInEditor()
{
    FBrushStroke BrushStroke;
    BrushStroke.BrushPosition = EditorBrushPosition + EditorBrushOffset;
    BrushStroke.BrushRadius = EditorBrushRadius;
   // UE_LOG(LogTemp, Error, TEXT("[DiggerManager] BrushRadius set to: %f"), EditorBrushRadius);
    BrushStroke.BrushRotation = EditorBrushRotation;
    //UE_LOG(LogTemp, Error, TEXT("[ApplyBrushInEditor] Using rotation: %s"), *EditorBrushRotation.ToString());
    BrushStroke.BrushType = EditorBrushType;
    BrushStroke.bDig = EditorBrushDig; // or expose as property
    BrushStroke.BrushLength = EditorBrushLength; // or expose as property
    BrushStroke.BrushAngle = EditorBrushAngle; // or expose as property
    

    UE_LOG(LogTemp, Warning, TEXT("[ApplyBrushInEditor] EditorBrushPosition (World): %s"), *EditorBrushPosition.ToString());

    ApplyBrushToAllChunks(BrushStroke);
    //MarkNearbyChunksDirty(EditorBrushPosition, EditorBrushRadius);
    ProcessDirtyChunks();

    
    
    if (GEditor)
    {
        GEditor->RedrawAllViewports();
    }

    // Update islands for the UI/toolkit
   TArray<FIslandData> DetectedIslands = GetAllIslands();
    
}




#endif

void ADiggerManager::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    FVoxelConversion::InitFromConfig(ChunkSize,Subdivisions, TerrainGridSize, GetActorLocation());
}


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

void ADiggerManager::ApplyBrushToAllChunks(FBrushStroke& BrushStroke)
{
    // Add padding to ensure we catch chunks at the boundaries
    float PaddedRadius = BrushStroke.BrushRadius + FVoxelConversion::LocalVoxelSize * 2.0f;
    
    // Compute the world-space AABB of the brush with padding
    FVector Min = BrushStroke.BrushPosition - FVector(PaddedRadius);
    FVector Max = BrushStroke.BrushPosition + FVector(PaddedRadius);

    // Convert to chunk coordinates
    FIntVector MinChunk = FVoxelConversion::WorldToChunk(Min);
    FIntVector MaxChunk = FVoxelConversion::WorldToChunk(Max);

    // Debug output
    UE_LOG(LogTemp, Warning, TEXT("ApplyBrushToAllChunks: BrushPosition=%s, Radius=%f"),
           *BrushStroke.BrushPosition.ToString(), BrushStroke.BrushRadius);
    UE_LOG(LogTemp, Warning, TEXT("Affected Chunks: X=[%d,%d], Y=[%d,%d], Z=[%d,%d]"),
           MinChunk.X, MaxChunk.X, MinChunk.Y, MaxChunk.Y, MinChunk.Z, MaxChunk.Z);

    // Loop over all affected chunks
    for (int32 X = MinChunk.X; X <= MaxChunk.X; ++X)
    for (int32 Y = MinChunk.Y; Y <= MaxChunk.Y; ++Y)
    for (int32 Z = MinChunk.Z; Z <= MaxChunk.Z; ++Z)
    {
        // Apply to the calculated chunk
        FIntVector ChunkCoords(X, Y, Z);
        UVoxelChunk* Chunk = GetOrCreateChunkAtChunk(ChunkCoords);
        if (Chunk)
        {
            FVector ChunkWorldPos = FVoxelConversion::ChunkToWorld(ChunkCoords);
            UE_LOG(LogTemp, Warning, TEXT("Applying brush to chunk at %s (world pos: %s)"),
                   *ChunkCoords.ToString(), *ChunkWorldPos.ToString());
            
            Chunk->ApplyBrushStroke(BrushStroke);
            Chunk->MarkDirty(); // Make sure to mark the chunk as dirty
        }
    }
}




UStaticMesh* ADiggerManager::ConvertIslandToStaticMesh(const FIslandData& Island, bool bWorldOrigin, FString AssetName)
{
    UE_LOG(LogTemp, Warning, TEXT("Converting island %d to static mesh..."), Island.IslandID);

    // 1. Generate mesh data for the island using Marching Cubes
    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FColor> Colors;
    TArray<FProcMeshTangent> Tangents;

/* Single Island Mesh Generation in DiggerManager
            // You may need to create a temporary SparseVoxelGrid for just this island
            USparseVoxelGrid* TempGrid = NewObject<USparseVoxelGrid>();
            
            //TempGrid->AddVoxels(IslandVoxels);

            // Generate mesh for just this island
            MarchingCubes->GenerateMeshForIsland(TempGrid, Vertices, Triangles, Normals, UVs, Colors, Tangents);

            // 4. Remove island voxels from the main grid
            //TempGrid->RemoveVoxels(IslandVoxels);
    */

    // For demonstration, we'll log and return nullptr if not implemented
    if (Vertices.Num() == 0 || Triangles.Num() == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("No mesh data generated for island!"));
        return nullptr;
    }

    // 2. Create a temporary ProceduralMeshComponent
    UProceduralMeshComponent* TempProcMesh = NewObject<UProceduralMeshComponent>(this);
    TempProcMesh->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, true);

    // 3. Convert ProceduralMeshComponent to StaticMesh
    FString PackageName = FString::Printf(TEXT("/Game/Islands/%s"), *AssetName);
    UPackage* MeshPackage = CreatePackage(*PackageName);

    UStaticMesh* NewStaticMesh = NewObject<UStaticMesh>(MeshPackage, *AssetName, RF_Public | RF_Standalone);
    if (!NewStaticMesh)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create UStaticMesh object!"));
        return nullptr;
    }

    // Use the built-in conversion utility (UE 4.23+)
    bool bSuccess = false;//TempProcMesh->CreateStaticMesh(NewStaticMesh, MeshPackage, AssetName, true);
    if (!bSuccess)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to convert procedural mesh to static mesh!"));
        return nullptr;
    }

    // 4. Register and save the asset
    FAssetRegistryModule::AssetCreated(NewStaticMesh);
    NewStaticMesh->MarkPackageDirty();

#if WITH_EDITOR
    // Notify the content browser
    TArray<UObject*> ObjectsToSync;
    ObjectsToSync.Add(NewStaticMesh);
    GEditor->SyncBrowserToObjects(ObjectsToSync);
#endif

    UE_LOG(LogTemp, Warning, TEXT("Static mesh asset created: %s"), *PackageName);

    return NewStaticMesh;
}


FIslandMeshData ADiggerManager::ExtractAndGenerateIslandMesh(const FVector& IslandCenter)
{
    UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Starting ExtractAndGenerateIslandMesh at IslandCenter: %s"), *IslandCenter.ToString());

    FIslandMeshData Result;

    // Step 1: Find or create the voxel chunk at the given center
    UVoxelChunk* Chunk = FindOrCreateNearestChunk(IslandCenter);
    if (!Chunk)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] No chunk found or created near IslandCenter."));
        return Result;
    }

    FVector ChunkOrigin = Chunk->GetWorldPosition();
    FVector LocalPosition = IslandCenter - ChunkOrigin;

    FIntVector VoxelCoords = FIntVector(
        FMath::FloorToInt(LocalPosition.X / VoxelSize),
        FMath::FloorToInt(LocalPosition.Y / VoxelSize),
        FMath::FloorToInt(LocalPosition.Z / VoxelSize)
    );

    FVector VoxelWorldCenter = ChunkOrigin + FVector(VoxelCoords) * VoxelSize + FVector(VoxelSize * 0.5f);

    UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Chunk Origin: %s | LocalPosition: %s | VoxelCoords: %s | VoxelWorldCenter: %s"),
        *ChunkOrigin.ToString(),
        *LocalPosition.ToString(),
        *VoxelCoords.ToString(),
        *VoxelWorldCenter.ToString()
    );

    // Draw debug string above search origin
    DrawDebugString(
        GetWorld(),
        VoxelWorldCenter + FVector(0, 0, VoxelSize),
        TEXT("Search Origin"),
        nullptr,
        FColor::White,
        60.0f,
        false
    );

    // RED: Voxel being searched
    DrawDebugBox(
        GetWorld(),
        VoxelWorldCenter,
        FVector(VoxelSize * 0.5f),
        FColor::Red,
        false,
        60.0f,
        0,
        3.0f
    );

    // GREEN: 3x3x3 area around it
    for (int32 dx = -1; dx <= 1; dx++)
    {
        for (int32 dy = -1; dy <= 1; dy++)
        {
            for (int32 dz = -1; dz <= 1; dz++)
            {
                FIntVector Offset = VoxelCoords + FIntVector(dx, dy, dz);
                FVector OffsetWorldCenter = ChunkOrigin + FVector(Offset) * VoxelSize + FVector(VoxelSize * 0.5f);

                DrawDebugBox(
                    GetWorld(),
                    OffsetWorldCenter,
                    FVector(VoxelSize * 0.5f),
                    FColor::Green,
                    false,
                    60.0f,
                    0,
                    1.5f
                );
            }
        }
    }

    // Step 2: Get voxel grid
    USparseVoxelGrid* VoxelGrid = Chunk->GetSparseVoxelGrid();
    if (!VoxelGrid)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] No SparseVoxelGrid found on chunk."));
        return Result;
    }

    // Step 3: Extract the island from the voxel grid using correct voxel coordinates
    USparseVoxelGrid* ExtractedGrid = nullptr;
    TArray<FIntVector> IslandVoxels;

    if (!VoxelGrid->ExtractIslandAtPosition(FVector(VoxelCoords), ExtractedGrid, IslandVoxels) || !ExtractedGrid)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Failed to extract island or ExtractedGrid is null."));
        return Result;
    }

    UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Extracted island with %d voxels."), IslandVoxels.Num());

    // Step 4: Generate mesh
    FVector Origin = Chunk->GetWorldPosition(); // Used to offset vertices into world space
    UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Starting mesh generation using Marching Cubes."));

    MarchingCubes->GenerateMeshFromGrid(
        ExtractedGrid, Origin, VoxelSize,
        Result.Vertices, Result.Triangles, Result.Normals
    );

    // Step 5: Finalize result
    Result.MeshOrigin = Origin;
    Result.bValid = (Result.Vertices.Num() > 0 && Result.Triangles.Num() > 0);

    UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Mesh generation complete. Valid: %s, Vertices: %d, Triangles: %d"),
        Result.bValid ? TEXT("true") : TEXT("false"),
        Result.Vertices.Num(),
        Result.Triangles.Num() / 3
    );

    return Result;
}


AIslandActor* ADiggerManager::SpawnIslandActorWithMeshData(
    const FVector& SpawnLocation,
    const FIslandMeshData& MeshData,
    bool bEnablePhysics
)
{
    if (!MeshData.bValid) return nullptr;
    

    AIslandActor* IslandActor = World->SpawnActor<AIslandActor>(AIslandActor::StaticClass(), SpawnLocation, FRotator::ZeroRotator);
    if (IslandActor && IslandActor->ProcMesh)
    {
        IslandActor->ProcMesh->CreateMeshSection_LinearColor(
            0, MeshData.Vertices, MeshData.Triangles, MeshData.Normals, {}, {}, {}, true
        );
        if (bEnablePhysics)
            IslandActor->ApplyPhysics();
        else
            IslandActor->RemovePhysics();
    }
    return IslandActor;
}

void ADiggerManager::ConvertIslandAtPositionToPhysicsObject(const FVector& IslandCenter)
{
    UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] ConvertIslandAtPositionToPhysicsObject called at %s"), *IslandCenter.ToString());
    
    FIslandMeshData MeshData = ExtractAndGenerateIslandMesh(IslandCenter);
    if (MeshData.bValid)
    {
        SpawnIslandActorWithMeshData(IslandCenter, MeshData, /*bEnablePhysics=*/true);
    }
}


void ADiggerManager::ConvertIslandAtPositionToStaticMesh(const FVector& IslandCenter)
{
    UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] ConvertIslandAtPositionToStaticMesh called at %s"), *IslandCenter.ToString());
    FIslandMeshData MeshData = ExtractAndGenerateIslandMesh(IslandCenter);
    if (MeshData.bValid)
    {
        FString AssetName = FString::Printf(TEXT("Island_%s_StaticMesh"), *IslandCenter.ToString());
        SaveIslandMeshAsStaticMesh(AssetName, MeshData);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] ConvertIslandAtPositionToStaticMesh called with invalid static mesh data at %s"), *IslandCenter.ToString());
    }
}



void ADiggerManager::SaveIslandMeshAsStaticMesh(
    const FString& AssetName,
    const FIslandMeshData& MeshData
)
{
    if (!MeshData.bValid) return;

    TArray<FVector3f> FloatVerts = ConvertArray<FVector, FVector3f>(MeshData.Vertices);
    TArray<FVector3f> FloatNormals = ConvertArray<FVector, FVector3f>(MeshData.Normals);
    
    CreateStaticMeshFromRawData(
        GetTransientPackage(),
        AssetName,
        FloatVerts,
        MeshData.Triangles,
        FloatNormals,
        {}, {}, {}
    );
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


UStaticMesh* ADiggerManager::CreateStaticMeshFromRawData(
    UObject* Outer,
    const FString& AssetName,
    const TArray<FVector3f>& Vertices,
    const TArray<int32>& Triangles,
    const TArray<FVector3f>& Normals,
    const TArray<FVector2d>& UVs,
    const TArray<FColor>& Colors,
    const TArray<FProcMeshTangent>& Tangents
)
{
    UE_LOG(LogTemp, Warning, TEXT("Starting mesh creation for asset: %s"), *AssetName);
    UE_LOG(LogTemp, Warning, TEXT("Vertices: %d, Triangles: %d"), Vertices.Num(), Triangles.Num());

    FString PackageName = FString::Printf(TEXT("/Game/Islands/%s"), *AssetName);
    UPackage* MeshPackage = CreatePackage(*PackageName);
    UE_LOG(LogTemp, Warning, TEXT("Created package: %s"), *PackageName);

    UStaticMesh* StaticMesh = NewObject<UStaticMesh>(MeshPackage, *AssetName, RF_Public | RF_Standalone);
    if (!StaticMesh)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create UStaticMesh object!"));
        return nullptr;
    }

    FMeshDescription MeshDescription;
    FStaticMeshAttributes Attributes(MeshDescription);
    Attributes.Register();
    UE_LOG(LogTemp, Warning, TEXT("Registered mesh attributes"));

    FPolygonGroupID PolyGroupID = MeshDescription.CreatePolygonGroup();
    UE_LOG(LogTemp, Warning, TEXT("Created polygon group"));

    TMap<int32, FVertexID> IndexToVertexID;
    for (int32 i = 0; i < Vertices.Num(); ++i)
    {
        FVertexID VertexID = MeshDescription.CreateVertex();
        Attributes.GetVertexPositions()[VertexID] = Vertices[i];
        IndexToVertexID.Add(i, VertexID);
    }
    UE_LOG(LogTemp, Warning, TEXT("Created %d vertex positions"), Vertices.Num());

    for (int32 i = 0; i < Triangles.Num(); i += 3)
    {
        TArray<FVertexInstanceID> VertexInstanceIDs;
        for (int32 j = 0; j < 3; ++j)
        {
            int32 VertexIndex = Triangles[i + j];
            if (!IndexToVertexID.Contains(VertexIndex))
            {
                UE_LOG(LogTemp, Error, TEXT("Vertex index %d not found in vertex map!"), VertexIndex);
                continue;
            }

            FVertexInstanceID InstanceID = MeshDescription.CreateVertexInstance(IndexToVertexID[VertexIndex]);

            if (Normals.IsValidIndex(VertexIndex))
                Attributes.GetVertexInstanceNormals()[InstanceID] = Normals[VertexIndex];

            if (UVs.IsValidIndex(VertexIndex))
                Attributes.GetVertexInstanceUVs().Set(InstanceID, 0, FVector2f(UVs[VertexIndex]));

            if (Colors.IsValidIndex(VertexIndex))
                Attributes.GetVertexInstanceColors()[InstanceID] = FVector4f(Colors[VertexIndex]);

            if (Tangents.IsValidIndex(VertexIndex))
            {
                Attributes.GetVertexInstanceTangents()[InstanceID] = (FVector3f)Tangents[VertexIndex].TangentX;
                Attributes.GetVertexInstanceBinormalSigns()[InstanceID] = Tangents[VertexIndex].bFlipTangentY ? -1.0f : 1.0f;
            }

            VertexInstanceIDs.Add(InstanceID);
        }

        if (VertexInstanceIDs.Num() == 3)
        {
            MeshDescription.CreatePolygon(PolyGroupID, VertexInstanceIDs);
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to create triangle at index %d (missing vertex instances)"), i);
        }
    }

#if WITH_EDITORONLY_DATA
    StaticMesh->BuildFromMeshDescriptions({ &MeshDescription });
    UE_LOG(LogTemp, Warning, TEXT("Built mesh description"));
#endif

    StaticMesh->CommitMeshDescription(0);
    UE_LOG(LogTemp, Warning, TEXT("Committed mesh description"));

    StaticMesh->Build(false);
    UE_LOG(LogTemp, Warning, TEXT("Built static mesh"));

    FAssetRegistryModule::AssetCreated(StaticMesh);
    StaticMesh->MarkPackageDirty();

    UE_LOG(LogTemp, Warning, TEXT("Static mesh asset created successfully: %s"), *PackageName);
    return StaticMesh;
}



void ADiggerManager::BakeToStaticMesh(bool bEnableCollision, bool bEnableNanite, float DetailReduction)
{
    UE_LOG(LogTemp, Warning, TEXT("BakeToStaticMesh called with:\n  Collision: %s\n  Nanite: %s\n  DetailReduction: %.2f"),
        bEnableCollision ? TEXT("True") : TEXT("False"),
        bEnableNanite ? TEXT("True") : TEXT("False"),
        DetailReduction);

/*#if WITH_EDITOR
    FString AssetName = FString::Printf(TEXT("SM_Baked_%s_%s_%.2f"),
        bEnableCollision ? TEXT("Col") : TEXT("NoCol"),
        bEnableNanite ? TEXT("Nanite") : TEXT("NoNanite"),
        DetailReduction);

    UStaticMesh* NewMesh = CreateStaticMeshFromProceduralMesh(
        GetTransientPackage(), // Or another suitable Outer
        AssetName,
        MyProceduralMeshComponent // <-- Replace with your actual component
    );

    if (NewMesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("Static mesh created: %s"), *AssetName);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create static mesh!"));
    }
#endif*/
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

FVector ADiggerManager::GetLandscapeNormalAt(const FVector& WorldPosition)
{
    // Get all landscape actors in the level
    TArray<AActor*> FoundLandscapes;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), ALandscapeProxy::StaticClass(), FoundLandscapes);

    if (FoundLandscapes.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("No landscape found in level"));
        return FVector::UpVector;
    }

    ALandscapeProxy* LandscapeProxy = Cast<ALandscapeProxy>(FoundLandscapes[0]);
    if (!LandscapeProxy)
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to cast landscape actor"));
        return FVector::UpVector;
    }

    // Use a small offset for sampling (in cm)
    const float Delta = 5.0f;

    FVector PosX = WorldPosition + FVector(Delta, 0, 0);
    FVector NegX = WorldPosition - FVector(Delta, 0, 0);
    FVector PosY = WorldPosition + FVector(0, Delta, 0);
    FVector NegY = WorldPosition - FVector(0, Delta, 0);

    float HeightX1 = GetLandscapeHeightAt(PosX);
    float HeightX0 = GetLandscapeHeightAt(NegX);
    float HeightY1 = GetLandscapeHeightAt(PosY);
    float HeightY0 = GetLandscapeHeightAt(NegY);

    // Calculate gradient
    FVector Gradient;
    Gradient.X = (HeightX1 - HeightX0) / (2 * Delta);
    Gradient.Y = (HeightY1 - HeightY0) / (2 * Delta);
    Gradient.Z = 1.0f;

    // The normal is the inverse of the gradient in X and Y, with Z up
    FVector Normal = FVector(-Gradient.X, -Gradient.Y, 1.0f).GetSafeNormal();
    return Normal;
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
            UVoxelChunk* Chunk = GetOrCreateChunkAtChunk(ChunkPosition);

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
                            FVector WorldPosition = FVoxelConversion::LocalVoxelToWorld(FIntVector(VoxelX, VoxelY, VoxelZ));

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
        DebugLogVoxelChunkGrid();
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

    ApplyBrushToAllChunks(BrushStroke);


    if (BrushStroke.bDig)
    {
        MarkNearbyChunksDirty(BrushPosition, BrushRadius);
    }
}

UVoxelChunk* ADiggerManager::FindOrCreateNearestChunk(const FVector& Position)
{
    FIntVector ChunkCoords = FVoxelConversion::WorldToChunk(Position);
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
        ChunkCoords = FVoxelConversion::WorldToChunk(Position);
        UE_LOG(LogTemp, Warning, TEXT("[FindOrCreateNearestChunk] ChunkCoords: %s"), *ChunkCoords.ToString());

        
        UVoxelChunk* NewChunk = GetOrCreateChunkAtCoordinates(ChunkCoords.X, ChunkCoords.Y, ChunkCoords.Z);
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
    FIntVector ChunkCoords = FVoxelConversion::WorldToChunk(Position);
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
    int32 Reach = FMath::CeilToInt(Radius / (ChunkSize * TerrainGridSize));

    UE_LOG(LogTemp, Warning, TEXT("MarkNearbyChunksDirty: CenterPosition=%s, Radius=%f"), *CenterPosition.ToString(), Radius);
    UE_LOG(LogTemp, Warning, TEXT("FVoxelConversion: ChunkSize=%d, TerrainGridSize=%f, Origin=%s"), FVoxelConversion::ChunkSize, FVoxelConversion::TerrainGridSize, *FVoxelConversion::Origin.ToString());
    FIntVector CenterChunkCoords = FVoxelConversion::WorldToChunk(CenterPosition);
    UE_LOG(LogTemp, Warning, TEXT("WorldToChunk: CenterPosition=%s -> CenterChunkCoords=%s"), *CenterPosition.ToString(), *CenterChunkCoords.ToString());


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


// Main function: always works in chunk coordinates
UVoxelChunk* ADiggerManager::GetOrCreateChunkAtChunk(const FIntVector& ChunkCoords)
{
    //Run Init on the static FVoxelCoversion struct so all of our conversions work properly even if the settings have changed!
    FVoxelConversion::InitFromConfig(ChunkSize,Subdivisions,TerrainGridSize, GetActorLocation());
    
    if (UVoxelChunk** ExistingChunk = ChunkMap.Find(ChunkCoords))
    {
        UE_LOG(LogTemp, Log, TEXT("Found existing chunk at position: %s"), *ChunkCoords.ToString());
        return *ExistingChunk;
    }

    UVoxelChunk* NewChunk = NewObject<UVoxelChunk>(this);
    if (NewChunk)
    {
        NewChunk->InitializeChunk(ChunkCoords, this);
        ChunkMap.Add(ChunkCoords, NewChunk);
        UE_LOG(LogTemp, Log, TEXT("Created a new chunk at position: %s"), *ChunkCoords.ToString());
        return NewChunk;
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create a new chunk at position: %s"), *ChunkCoords.ToString());
        return nullptr;
    }
}

UVoxelChunk* ADiggerManager::GetOrCreateChunkAtCoordinates(const float& ProposedChunkX, const float& ProposedChunkY, const float& ProposedChunkZ)
{
    return GetOrCreateChunkAtWorld(FVector(ProposedChunkX, ProposedChunkY, ProposedChunkZ));
}

// Thin wrapper: converts world position to chunk coordinates
UVoxelChunk* ADiggerManager::GetOrCreateChunkAtWorld(const FVector& WorldPosition)
{
    FIntVector ChunkCoords = FVoxelConversion::WorldToChunk(WorldPosition);
    return GetOrCreateChunkAtChunk(ChunkCoords);
}


// In ADiggerManager.cpp
TArray<FIslandData> ADiggerManager::GetAllIslands() const
{
#if WITH_EDITOR
    UE_LOG(LogTemp, Warning, TEXT("Broadcasting OnIslandsDetectionStarted"));
    OnIslandsDetectionStarted.Broadcast();
#endif

    TMap<FIntVector, FIslandData> UniqueIslands; // Key: rounded center position

    for (const auto& ChunkPair : ChunkMap)
    {
        UVoxelChunk* Chunk = ChunkPair.Value;
        if (Chunk)
        {
            TArray<FIslandData> ChunkIslands = Chunk->GetSparseVoxelGrid()->DetectIslands(0.0f);
            for (const FIslandData& Island : ChunkIslands)
            {
                // Round the center position to nearest int for deduplication
                FIntVector Center = FIntVector(
                    FMath::RoundToInt(Island.Location.X),
                    FMath::RoundToInt(Island.Location.Y),
                    FMath::RoundToInt(Island.Location.Z)
                );
                // Only add if not already present
                if (!UniqueIslands.Contains(Center))
                {
                    UniqueIslands.Add(Center, Island);
                }
            }
        }
    }

    // Now broadcast only unique islands
    TArray<FIslandData> AllIslands;
    UniqueIslands.GenerateValueArray(AllIslands);
    for (const FIslandData& Island : AllIslands)
    {
        OnIslandDetected.Broadcast(Island);
    }
    return AllIslands;
}


#if WITH_EDITOR
void ADiggerManager::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    // Sync updated settings to FVoxelConversion using the actor's position as origin
    FVoxelConversion::InitFromConfig(ChunkSize,Subdivisions, TerrainGridSize, GetActorLocation());

    // Respond to property changes (e.g., ChunkSize, TerrainGridSize, etc.)
    EditorUpdateChunks();
}

void ADiggerManager::PostEditMove(bool bFinished)
{
    Super::PostEditMove(bFinished);

    // Update FVoxelConversion origin if the actor is moved in the editor
    FVoxelConversion::InitFromConfig(ChunkSize,Subdivisions, TerrainGridSize, GetActorLocation());

    if (bFinished)
    {
        EditorUpdateChunks();
    }
}

void ADiggerManager::PostEditUndo()
{
    Super::PostEditUndo();

    // Update FVoxelConversion in case undo/redo changed position or settings
    FVoxelConversion::InitFromConfig(ChunkSize, Subdivisions, TerrainGridSize, GetActorLocation());

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


#endif // WITH_EDITOR
