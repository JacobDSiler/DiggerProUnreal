#include "VoxelChunk.h"

#include "DiggerDebug.h"
#include "DiggerManager.h"
#include "DynamicHole.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "HLSLTypeAliases.h"
#include "DynamicHolesHelpers.h"
#include "MarchingCubes.h"
#include "SparseVoxelGrid.h"
#include "VoxelBrushTypes.h"
#include "VoxelConversion.h"
#include "VoxelLogManager.h"
#include "Async/Async.h"
#include "Async/ParallelFor.h"
//#include "VoxelBrushHelpers.h" // or wherever you put SetDigShellVoxels
#include "Utils/FastDebugRenderer.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDeviceNull.h"
#include "Serialization/BufferArchive.h"

struct FSpawnedHoleData;

// Near the top of VoxelChunk.cpp, before ApplyBrushStroke

struct FVoxelStrokeInfo
{
	FIntVector Coords;
	FVector WorldPos;
	float TerrainHeight;
};


UVoxelChunk::UVoxelChunk()
	: ChunkCoordinates(FIntVector::ZeroValue), 
	  SectionIndex(0), 
	  TerrainGridSize(100),
	  Subdivisions(4),
	  bIsDirty(false),
	  DiggerManager(nullptr),
	  SparseVoxelGrid(CreateDefaultSubobject<USparseVoxelGrid>(TEXT("SparseVoxelGrid")))
{
	MarchingCubesGenerator = CreateDefaultSubobject<UMarchingCubes>(TEXT("MarchingCubesGenerator"));

}

void UVoxelChunk::Tick(float DeltaTime)
{
	// Trigger Needed mesh updates.
	UpdateIfDirty();
}


void UVoxelChunk::SetUniqueSectionIndex() {
	// Ensure unique IDs are assigned correctly
	static int32 GlobalChunkID = 0;

	// Set the section index to the length of the ChunkMap plus one
	SectionIndex = GlobalChunkID;
	if (DiggerDebug::Mesh() || DiggerDebug::Chunks())
	UE_LOG(LogTemp, Warning, TEXT("SectionID Set to: %i for ChunkCoordinates X=%d Y=%d Z=%d"), SectionIndex, ChunkCoordinates.X, ChunkCoordinates.Y, ChunkCoordinates.Z);

	GlobalChunkID++;
	if (DiggerDebug::Mesh() || DiggerDebug::Chunks())
	UE_LOG(LogTemp, Warning, TEXT("Chunk Section ID set with GlobalChunkID#: %i"), GlobalChunkID);
}


void UVoxelChunk::ReportVoxelModification(const FVoxelModificationReport& Report)
{
	// Per-chunk broadcast
	OnVoxelsModified.Broadcast(Report);

	// OPTIONAL: temporary backward-compat forwarding to manager’s global event.
	// Remove this once you fully migrate listeners to per-chunk subscriptions.
	if (ADiggerManager* Mgr = GetDiggerManager())
	{
		// If you want zero duplicates now, comment the next line out:
		Mgr->OnVoxelsModified.Broadcast(Report);
	}
}

void UVoxelChunk::UpdateMeshFromData(const TArray<FVector>& Vertices, const TArray<int32>& Triangles, const TArray<FVector>& Normals)
{
    // 1. Thread Safety: This MUST run on the Game Thread because it touches a UObject Component
    if (!IsInGameThread())
    {
        // If we are somehow still on a background thread, dispatch back to Game Thread
        AsyncTask(ENamedThreads::GameThread, [this, Vertices, Triangles, Normals]()
        {
            UpdateMeshFromData(Vertices, Triangles, Normals);
        });
        return;
    }

    // 2. Component Validation
    if (!ProceduralMeshComponent)
    {
        // Try to recover from DiggerManager if local pointer is lost
        if (DiggerManager && DiggerManager->ProceduralMesh)
        {
            ProceduralMeshComponent = DiggerManager->ProceduralMesh;
        }
        else
        {
            if (DiggerDebug::Mesh())
            {
                UE_LOG(LogTemp, Error, TEXT("UpdateMeshFromData: No ProceduralMeshComponent found for Chunk %s"), *ChunkCoordinates.ToString());
            }
            return;
        }
    }

    // 3. Apply Geometry
    // We assume Triplanar mapping in the material, so we pass empty UVs/Colors for performance.
    TArray<FVector2D> EmptyUVs;
    TArray<FColor> EmptyColors;
    TArray<FProcMeshTangent> EmptyTangents;

    // Critical: CreateMeshSection automatically clears the old section if it exists
    ProceduralMeshComponent->CreateMeshSection(
        SectionIndex,
        Vertices,
        Triangles,
        Normals,
        EmptyUVs,       // UV0
        EmptyColors,    // Vertex Colors
        EmptyTangents,  // Tangents
        true            // bCreateCollision (Enable Physics)
    );

    // 4. Configure Collision (Physics)
    if (Vertices.Num() > 0)
    {
        ProceduralMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        // Important for "Falling through holes":
        ProceduralMeshComponent->SetCollisionObjectType(ECC_WorldDynamic); 
        ProceduralMeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
        // Ensure complex collision is used so the character walks on the exact mesh shape
        ProceduralMeshComponent->bUseComplexAsSimpleCollision = true;
    }

    // 5. Apply Material
    if (DiggerManager)
    {
        UMaterialInterface* Mat = DiggerManager->GetTerrainMaterial();
        if (Mat)
        {
            ProceduralMeshComponent->SetMaterial(SectionIndex, Mat);
        }
    }

    if (DiggerDebug::Mesh())
    {
        UE_LOG(LogTemp, Log, TEXT("Updated Mesh for Chunk %s: %d Verts, %d Tris"), 
            *ChunkCoordinates.ToString(), Vertices.Num(), Triangles.Num() / 3);
    }
}

void UVoxelChunk::InitializeChunk(const FIntVector& InChunkCoordinates, ADiggerManager* InDiggerManager)
{
    ChunkCoordinates = InChunkCoordinates;

    // --- 1. Initialize voxel conversion math immediately ---
    if (InDiggerManager)
    {
        FVoxelConversion::InitFromConfig(
            InDiggerManager->ChunkSize,
            InDiggerManager->Subdivisions,
            InDiggerManager->TerrainGridSize,
            InDiggerManager->GetActorLocation()
        );
    }

    if (DiggerDebug::Chunks())
        UE_LOG(LogTemp, Warning, TEXT("Chunk created at X: %i Y: %i Z: %i"),
            ChunkCoordinates.X, ChunkCoordinates.Y, ChunkCoordinates.Z);

    HoleBP = InDiggerManager ? InDiggerManager->DynamicHoleClass : nullptr;

    // --- 2. Initialize SparseVoxelGrid with this chunk ---
    if (SparseVoxelGrid)
    {
        SparseVoxelGrid->Initialize(this);
    }

    // --- 3. Assign or recover DiggerManager ---
    if (!DiggerManager)
        DiggerManager = InDiggerManager;

	if (!DiggerManager)
	{
		DiggerManager = ADiggerManager::FindDiggerManager(World);
	}


    if (!DiggerManager)
    {
        if (DiggerDebug::Manager() || DiggerDebug::Verbose())
            UE_LOG(LogTemp, Error, TEXT("DiggerManager is null during chunk initialization!"));
        return;
    }

    // --- 4. Now that manager exists, initialize grid + section index ---
    SparseVoxelGrid->InitializeDiggerManager();
    SetUniqueSectionIndex();

    // --- 5. Pass manager to Marching Cubes ---
    MarchingCubesGenerator->SetDiggerManager(DiggerManager);

    // --- 6. SYNC SETTINGS FROM FVoxelConversion (authoritative source) ---
    ChunkSize       = FVoxelConversion::ChunkSize;
    TerrainGridSize = FVoxelConversion::TerrainGridSize;
    Subdivisions    = FVoxelConversion::Subdivisions;

    // Voxel size is derived from conversion math
    VoxelSize = FVoxelConversion::LocalVoxelSize;
    if (VoxelSize <= 0)
        VoxelSize = 25; // safety fallback

    // // Optional: if you want chunk world size cached locally
    // ChunkWorldSize = FVoxelConversion::ChunkWorldSize;

    // --- 7. Cache world reference ---
    World = DiggerManager->GetWorldFromManager();

    // --- 8. Final SparseVoxelGrid safety check ---
    if (!SparseVoxelGrid)
    {
        if (DiggerDebug::Voxels())
            UE_LOG(LogTemp, Error, TEXT("SparseVoxelGrid passed to InitializeChunk is null!"));
        return;
    }

    if (DiggerDebug::Chunks())
        UE_LOG(LogTemp, Warning, TEXT("Initializing new chunk at position X=%d Y=%d Z=%d"),
            ChunkCoordinates.X, ChunkCoordinates.Y, ChunkCoordinates.Z);

    // --- 9. Register chunk in manager map ---
    DiggerManager->ChunkMap.Add(ChunkCoordinates, this);

    if (DiggerDebug::Chunks())
        UE_LOG(LogTemp, Warning, TEXT("Chunk added to ChunkMap at position: X=%d Y=%d Z=%d"),
            ChunkCoordinates.X, ChunkCoordinates.Y, ChunkCoordinates.Z);
}


void UVoxelChunk::InitializeMeshComponent(UProceduralMeshComponent* MeshComponent)
{
	ProceduralMeshComponent = MeshComponent;
}


void UVoxelChunk::InitializeDiggerManager(ADiggerManager* InDiggerManager)
{
	if(!DiggerManager) DiggerManager = InDiggerManager;
	if (!HoleShapeLibrary && DiggerManager->HoleShapeLibrary)
	{
		if (DiggerDebug::Chunks() || DiggerDebug::Holes())
		UE_LOG(LogTemp, Warning, TEXT("Chunk HoleShapeLibrary set successfully from the manager!"));
		HoleShapeLibrary = DiggerManager->HoleShapeLibrary;
	}
}

void UVoxelChunk::CaptureLightForSave(AActor* LightActor)
{
	// Create a new data struct
	FSavedLightData Data;
	// Capture the data from the actor
	Data.CaptureFromLightActor(LightActor);
	// Add to our list
	SavedLights.Add(Data);
}

void UVoxelChunk::ClearSavedLights()
{
	SavedLights.Empty();
}

void UVoxelChunk::RestoreAllHoles()
{
	for (const FSpawnedHoleData& HoleData : HoleDataArray)
	{
		SpawnHoleFromData(HoleData);
	}
}

void UVoxelChunk::OnMarchingMeshComplete() const
{
	if (DiggerDebug::Mesh() || DiggerDebug::Holes())
	{
		UE_LOG(LogTemp, Warning, TEXT("Mesh Generation Complete for Chunk %s. Updating Hole Meshes."), *ChunkCoordinates.ToString());
	}

	// --- FIX START: ACTUALLY UPDATE THE HOLES ---
	// Iterate over all tracked holes for this chunk and update them.
	for (const TWeakObjectPtr<ADynamicHole>& HolePtr : SpawnedHoleInstances)
	{
		if (ADynamicHole* Hole = HolePtr.Get())
		{
			Hole->UpdateHoleMesh();
		}
	}

	// --- FIX END ---
}


void UVoxelChunk::SpawnHoleFromData(const FSpawnedHoleData& HoleData)
{
    // 0. Validate DiggerManager
    if (!DiggerManager)
    {
        if (DiggerDebug::Context() || DiggerDebug::Holes() || DiggerDebug::Error())
        {
            UE_LOG(LogTemp, Error, TEXT("SpawnHoleFromData: DiggerManager is null"));
        }
        return;
    }

    // 1. Validate / acquire HoleShapeLibrary
    if (!HoleShapeLibrary)
    {
        DiggerManager->EnsureHoleShapeLibrary();
        HoleShapeLibrary = DiggerManager->GetHoleShapeLibrary();

        if (!HoleShapeLibrary)
        {
            if (DiggerDebug::Holes() || DiggerDebug::Error())
            {
                UE_LOG(LogTemp, Error, TEXT("SpawnHoleFromData: HoleShapeLibrary is not set"));
            }
            return;
        }
    }

    // 2. Resolve the Hole Actor Class
    TSubclassOf<AActor> HoleClass = DiggerManager->GetDynamicHoleClass();

    if (!HoleClass)
    {
        const UDiggerSettings* Settings = UDiggerSettings::Get();
        if (Settings)
        {
            HoleClass = Settings->DefaultHoleActorClass.LoadSynchronous();
        }
    }

    if (!HoleClass)
    {
        if (DiggerDebug::Holes() || DiggerDebug::Error())
        {
            UE_LOG(LogTemp, Error,
                TEXT("SpawnHoleFromData: Cannot spawn hole. No Class specified in Digger Settings or DiggerManager!"));
        }
        return;
    }

    // 3. Validate World
    if (!GetWorld())
    {
        if (DiggerDebug::Context() || DiggerDebug::Holes() || DiggerDebug::Error())
        {
            UE_LOG(LogTemp, Error, TEXT("SpawnHoleFromData: GetWorld() returned null"));
        }
        return;
    }

    // 4. Spawn the Actor
    AActor* SpawnedHole = SpawnTransientActor(
        GetWorld(), HoleClass,
        HoleData.Location, HoleData.Rotation, HoleData.Scale);

    if (!SpawnedHole)
    {
        if (DiggerDebug::Holes() || DiggerDebug::Error())
        {
            UE_LOG(LogTemp, Error, TEXT("SpawnHoleFromData: Failed to spawn hole actor"));
        }
        return;
    }

    // 5. Cast to ADynamicHole
    ADynamicHole* DynamicHole = Cast<ADynamicHole>(SpawnedHole);
    if (!DynamicHole)
    {
        UE_LOG(LogTemp, Error,
            TEXT("CRITICAL ERROR: Spawned Hole is not of type ADynamicHole (class=%s). ")
            TEXT("Please reparent BP_MeshHole to ADynamicHole in the Editor."),
            *SpawnedHole->GetClass()->GetName());
        return;
    }

    // 6. Restore data onto the hole
    DynamicHole->SetDiggerManager(DiggerManager);
    DynamicHole->HoleShape     = HoleData.Shape;
    DynamicHole->HoleShapeType = HoleData.Shape.ShapeType;

    DynamicHole->SetActorLocation(HoleData.Location);
    DynamicHole->SetActorRotation(HoleData.Rotation);
    DynamicHole->SetActorScale3D(HoleData.Scale);

    // 7. CRITICAL: assign owning chunk (this will internally register it via AddHoleToChunk)
    DynamicHole->SetOwningChunk(this);

    if (DiggerDebug::Holes())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("SpawnHoleFromData: Spawned hole %s at %s for chunk %s (Shape=%s)"),
            *DynamicHole->GetName(),
            *HoleData.Location.ToString(),
            *ChunkCoordinates.ToString(),
            *UEnum::GetValueAsString(HoleData.Shape.ShapeType));
    }

#if WITH_EDITOR
    if (GIsEditor)
    {
        FString NewLabel = FString::Printf(
            TEXT("HoleBP_%s"),
            *UEnum::GetValueAsString(HoleData.Shape.ShapeType));
        SpawnedHole->SetActorLabel(NewLabel);
    }
#endif
}





// In UVoxelChunk.cpp
void UVoxelChunk::SaveHoleData(const FVector& Location, const FRotator& Rotation, const FVector& Scale)
{
	HoleDataArray.Add(FSpawnedHoleData(Location, Rotation, Scale));
}


// Updated UVoxelChunk::DebugDrawChunk()
void UVoxelChunk::DebugDrawChunk()
{
	if (!World) World = DiggerManager->GetWorldFromManager();
	if (!World) return;
    
	const float DebugDuration = 35.0f;
	// Core chunk bounds
	const FVector ChunkCenter = FVector(ChunkCoordinates) * ChunkSize * TerrainGridSize;
	const FVector ChunkExtent = FVector(ChunkSize * TerrainGridSize / 2.0f);
    
	// Draw core chunk (red) using fast debug system
	FAST_DEBUG_BOX(ChunkCenter, ChunkExtent, FLinearColor::Red);
}

void UVoxelChunk::DebugPrintVoxelData() const
{
	if (!DiggerDebug::Chunks() || !DiggerDebug::Voxels())
		return;
	
	if (!SparseVoxelGrid)
	{
		if (DiggerDebug::Voxels() || DiggerDebug::Error())
		{
			UE_LOG(LogTemp, Error, TEXT("SparseVoxelGrid is null in DebugPrintVoxelData"));
		}
		return;
	}

	if (DiggerDebug::Chunks() || DiggerDebug::Voxels() || DiggerDebug::Error())
	{
		UE_LOG(LogTemp, Log, TEXT("Voxel Data for Chunk at %s:"), *GetChunkCoordinates().ToString());
	}
	for (const auto& Pair : SparseVoxelGrid->VoxelData)
	{
		if (DiggerDebug::Voxels())
		{
			UE_LOG(LogTemp, Log, TEXT("Voxel at (%d,%d,%d): Value = %f"),
			       Pair.Key.X, Pair.Key.Y, Pair.Key.Z, Pair.Value.SDFValue);
		}
	}
}


void UVoxelChunk::MarkDirty()
{
	bIsDirty = true; // Set the dirty flag
}

void UVoxelChunk::UpdateIfDirty()
{
	if (bIsDirty)
	{
			GenerateMesh();
			bIsDirty = false; // Reset dirty flag
	}
}

void UVoxelChunk::ForceUpdate()
{
	// Ensure we're on the game thread for mesh updates
	if (!IsInGameThread())
	{
		if (DiggerDebug::Mesh())
		{
			UE_LOG(LogTemp, Warning, TEXT("ForceUpdate called from non-game thread, dispatching to game thread"));
		}
		AsyncTask(ENamedThreads::GameThread, [this]()
		{
			if (IsValid(this))
			{
				this->ForceUpdate();
			}
		});
		return;
	}

	if (DiggerDebug::Mesh())
	{
		UE_LOG(LogTemp, Warning, TEXT("UVoxelChunk::ForceUpdate - Starting mesh regeneration"));
	}
    
	// Perform the update (e.g., regenerate the mesh)
	GenerateMeshSyncronous();
        
	// Reset the dirty flag
	bIsDirty = false;

	if (DiggerDebug::Mesh())
	{
		UE_LOG(LogTemp, Warning, TEXT("UVoxelChunk::ForceUpdate - Completed"));
	}
}

void UVoxelChunk::RefreshSectionMesh()
{
	UMarchingCubes* Cubes = GetMarchingCubesGenerator();
	if (Cubes)
	{
		FIntVector ChunkCoords = ChunkCoordinates;
		Cubes->ClearSectionAndRebuildMesh(GetSectionIndex(), ChunkCoords);
	}

	// Mark self dirty and force rebuild when mesh is ready
	bIsDirty = true;
}

void UVoxelChunk::OnMeshReady(FIntVector Coord, int32 SectionIdx)
{
    UE_LOG(LogTemp, Warning,
        TEXT("OnMeshReady CALLED for chunk %s (incoming=%s, section=%d)"),
        *ChunkCoordinates.ToString(),
        *Coord.ToString(),
        SectionIdx);

    if (Coord != ChunkCoordinates)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("OnMeshReady IGNORED: Coord does not match this chunk (%s != %s)"),
            *Coord.ToString(),
            *ChunkCoordinates.ToString());
        return;
    }

    if (!HoleShapeLibrary && DiggerManager)
        HoleShapeLibrary = DiggerManager->GetHoleShapeLibrary();

    if (!HoleShapeLibrary)
    {
        UE_LOG(LogTemp, Error,
            TEXT("OnMeshReady FAILED: HoleShapeLibrary is NULL"));
        return;
    }

    UE_LOG(LogTemp, Warning,
        TEXT("OnMeshReady: Processing %d registered holes for chunk %s"),
        SpawnedHoleInstances.Num(),
        *ChunkCoordinates.ToString());

    for (const TWeakObjectPtr<ADynamicHole>& HolePtr : SpawnedHoleInstances)
    {
        ADynamicHole* Hole = HolePtr.Get();
        if (!Hole)
        {
            UE_LOG(LogTemp, Error,
                TEXT("OnMeshReady: Found invalid hole pointer (GC'd)"));
            continue;
        }

        UStaticMeshComponent* MeshComp = Hole->GetHoleMeshComponent();
        if (!MeshComp)
        {
            UE_LOG(LogTemp, Error,
                TEXT("OnMeshReady: Hole %s has NO HoleMeshComponent"),
                *Hole->GetName());
            continue;
        }

        if (MeshComp->GetStaticMesh() != nullptr)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("OnMeshReady: Hole %s already has mesh %s, skipping"),
                *Hole->GetName(),
                *MeshComp->GetStaticMesh()->GetName());
            continue;
        }

        EHoleShapeType ShapeType = Hole->HoleShape.ShapeType;
        UStaticMesh* HoleMesh = HoleShapeLibrary->GetMeshForShape(ShapeType);

        if (!HoleMesh)
        {
            UE_LOG(LogTemp, Error,
                TEXT("OnMeshReady: Hole %s: No mesh for shape %s"),
                *Hole->GetName(),
                *UEnum::GetValueAsString(ShapeType));
            continue;
        }

        UE_LOG(LogTemp, Warning,
            TEXT("OnMeshReady: Assigning mesh %s to hole %s"),
            *HoleMesh->GetName(),
            *Hole->GetName());

        MeshComp->SetStaticMesh(HoleMesh);

        if (Hole->WriterMaterial)
        {
            const int32 SlotCount = HoleMesh->GetStaticMaterials().Num();
            for (int32 i = 0; i < SlotCount; i++)
                MeshComp->SetMaterial(i, Hole->WriterMaterial);

            UE_LOG(LogTemp, Warning,
                TEXT("OnMeshReady: Applied WriterMaterial to hole %s (%d slots)"),
                *Hole->GetName(), SlotCount);
        }
        else
        {
            UE_LOG(LogTemp, Warning,
                TEXT("OnMeshReady: Hole %s has no WriterMaterial"),
                *Hole->GetName());
        }
    }

    UE_LOG(LogTemp, Warning,
        TEXT("OnMeshReady FINISHED for chunk %s"),
        *ChunkCoordinates.ToString());
}







void UVoxelChunk::GenerateMeshSyncronous()
{
    // Verify we're on the game thread
    if (!IsInGameThread())
    {
    	if (DiggerDebug::Mesh())
    	{
    		UE_LOG(LogTemp, Error, TEXT("GenerateMeshSyncronous called from non-game thread! This will cause issues."));
    	}
    	return;
    }

    if (!SparseVoxelGrid)
    {
    	if (DiggerDebug::Mesh() || DiggerDebug::Error() || DiggerDebug::Voxels())
        UE_LOG(LogTemp, Error, TEXT("SparseVoxelGrid is null!"));
        return;
    }

	if (DiggerDebug::Mesh())
    UE_LOG(LogTemp, Warning, TEXT("GenerateMeshSyncronous - Starting mesh generation"));

    // --- Island Detection ---
    TArray<FIslandData> Islands = SparseVoxelGrid->DetectIslands(0.0f);
    if (Islands.Num() > 0)
    {
        if (DiggerDebug::Islands())
        {
            UE_LOG(LogTemp, Warning, TEXT("Island detection: %d islands found!"), Islands.Num());
        }
        for (int32 i = 0; i < Islands.Num(); ++i)
        {
            if (DiggerDebug::Islands())
            {
                UE_LOG(LogTemp, Warning, TEXT("  Island %d: %d voxels"), i, Islands[i].VoxelCount);
            }
        }
    }

    if (MarchingCubesGenerator == nullptr)
    {
        if (DiggerDebug::Mesh())
        {
            UE_LOG(LogTemp, Error, TEXT("MarchingCubesGenerator is nullptr in UVoxelChunk::GenerateMeshSyncronous"));
        }
        return;
    }

    // Clear any previous binding to avoid multiple bindings
    MarchingCubesGenerator->OnMeshReady.Unbind();
    
    // Bind the completion callback
    MarchingCubesGenerator->OnMeshReady.BindLambda([this]()
    {
    	if (DiggerDebug::Mesh())
        UE_LOG(LogTemp, Warning, TEXT("Marching cubes mesh generation completed"));
        this->OnMarchingMeshComplete();
    });

	if (DiggerDebug::Mesh())
    UE_LOG(LogTemp, Warning, TEXT("Starting marching cubes generation"));
    MarchingCubesGenerator->GenerateMeshSyncronous(this);
}


bool UVoxelChunk::SaveChunkData(const FString& FilePath)
{
    // --- STEP 1: CLEANUP & SYNC ---
    SpawnedHoleInstances.RemoveAll([](const TWeakObjectPtr<ADynamicHole>& HolePtr)
    {
	    return !HolePtr.IsValid();
    });


    // Wipe old data to rebuild from current world state
    HoleDataArray.Empty();

	for (const TWeakObjectPtr<ADynamicHole>& HolePtr : SpawnedHoleInstances)
	{
		if (!HolePtr.IsValid())
			continue;

		AActor* Actor = HolePtr.Get();

        FSpawnedHoleData NewData;
        NewData.Location = Actor->GetActorLocation();
        NewData.Rotation = Actor->GetActorRotation();
        NewData.Scale = Actor->GetActorScale3D();

        // Try to get specific shape data
        if (ADynamicHole* DynamicHole = Cast<ADynamicHole>(Actor))
        {
            // Success: Save the correct shape
            NewData.Shape.ShapeType = DynamicHole->HoleShapeType;
        }
        else
        {
            // Fallback: It's a valid actor but not our C++ class yet.
            // Save it as a Sphere (Default) so we don't lose the hole entirely.
            // Also log a warning so you know to reparent the BP.
            NewData.Shape.ShapeType = EHoleShapeType::Sphere;
            UE_LOG(LogTemp, Warning, TEXT("SaveChunkData: Hole actor '%s' is not ADynamicHole! Saved as default Sphere."), *Actor->GetName());
        }

        HoleDataArray.Add(NewData);
    }

    // --- STEP 2: SERIALIZE ---
    FBufferArchive ToBinary;

    // A. Voxels
    if (!SparseVoxelGrid || !SparseVoxelGrid->SerializeToArchive(ToBinary))
    {
        return false;
    }

    // B. Holes
    int32 HoleCount = HoleDataArray.Num();
    ToBinary << HoleCount;
    for (FSpawnedHoleData& Hole : HoleDataArray)
    {
        ToBinary << Hole;
    }

    // C. Lights (Always save count, even if 0)
    int32 LightCount = SavedLights.Num();
    ToBinary << LightCount;
    for (FSavedLightData& Light : SavedLights)
    {
        ToBinary << Light;
    }

    // --- STEP 3: WRITE ---
    if (FFileHelper::SaveArrayToFile(ToBinary, *FilePath))
    {
        SavedLights.Empty(); // Clear temp light data
        ToBinary.FlushCache();
        return true;
    }

    return false;
}

bool UVoxelChunk::LoadChunkData(const FString& FilePath)
{return LoadChunkData( FilePath, false);}


bool UVoxelChunk::LoadChunkData(const FString& FilePath, bool bOverwrite)
{
    if (!FPaths::FileExists(FilePath))
    {
        // Silent fail or log verbose only
        return false;
    }

    TArray<uint8> BinaryArray;
    if (!FFileHelper::LoadFileToArray(BinaryArray, *FilePath)) return false;

    FMemoryReader FromBinary(BinaryArray, true);
    FromBinary.Seek(0);

    // --- 1. Load Voxels ---
    USparseVoxelGrid* TempGrid = NewObject<USparseVoxelGrid>();
    if (!TempGrid->SerializeFromArchive(FromBinary)) return false;

    if (bOverwrite && SparseVoxelGrid)
    {
        SparseVoxelGrid->VoxelData = TempGrid->VoxelData;
        
        // IMPORTANT: If we are overwriting, we MUST kill the old actors first
        ClearSpawnedHoles(); 
        HoleDataArray.Empty();
    }
    else if (SparseVoxelGrid)
    {
        for (const auto& Pair : TempGrid->VoxelData)
        {
            SparseVoxelGrid->VoxelData.Add(Pair.Key, Pair.Value);
        }
    }

    // --- 2. Load Holes ---
    int32 HoleCount = 0;
    FromBinary << HoleCount;

    if (DiggerDebug::IO())
    {
        UE_LOG(LogTemp, Log, TEXT("LoadChunkData: Found %d holes in file"), HoleCount);
    }

    for (int32 i = 0; i < HoleCount; ++i)
    {
        FSpawnedHoleData Hole;
        FromBinary << Hole;
        
        HoleDataArray.Add(Hole);

        // --- THE FIX: ALWAYS SPAWN ---
        // Previously, this was inside 'if (bOverwrite)'.
        // But if we successfully loaded a hole from the file, we implicitly want to see it!
        SpawnHoleFromData(Hole);
    }

    // --- 3. Load Lights ---
    // Handle EOF for legacy files
    if (FromBinary.AtEnd()) 
    {
        return true; 
    }

    int32 LightCount = 0;
    FromBinary << LightCount;

    UWorld* CurrentWorld = GetWorld();
    if (!CurrentWorld && DiggerManager) CurrentWorld = DiggerManager->GetWorld();

    for (int32 i = 0; i < LightCount; ++i)
    {
        if (FromBinary.AtEnd()) break;

        FSavedLightData LightData;
        FromBinary << LightData;

        // Same logic for lights: If we loaded it, spawn it.
        // (Assuming we haven't already spawned it in a non-overwrite scenario, 
        //  but duplicate lights are better than NO lights for now).
        if (CurrentWorld)
        {
            AActor* NewLight = LightData.SpawnLightActor(CurrentWorld);
            if (DiggerManager && NewLight)
            {
                DiggerManager->SpawnedLights.Add(NewLight);
            }
        }
    }

    return true;
}


void UVoxelChunk::ClearSpawnedHoles()
{
	for (const TWeakObjectPtr<ADynamicHole>& HolePtr : SpawnedHoleInstances)
	{
		if (HolePtr.IsValid())
		{
			HolePtr->Destroy();
		}
	}
	SpawnedHoleInstances.Empty();
}



void UVoxelChunk::SpawnHoleMeshes()
{
	if (!World || !HoleBP) return;

	while (SpawnedHoleInstances.Num() < HoleDataArray.Num())
	{
		SpawnedHoleInstances.Add(nullptr);
	}

	for (int32 i = 0; i < HoleDataArray.Num(); ++i)
	{
		ADynamicHole* HoleActor = SpawnedHoleInstances[i].Get();
		if (!HoleActor) continue;

		const FSpawnedHoleData& HoleData = HoleDataArray[i];
		SpawnHoleFromData(HoleData);
	}
}

void UVoxelChunk::RegenerateHolesFromData()
{
	// 1. Clean up any invalid pointers first
	SpawnedHoleInstances.RemoveAll([](const TWeakObjectPtr<ADynamicHole>& HolePtr)
	{
		return !HolePtr.IsValid();
	});


	// Validate BP Class
		if (!HoleBP)
		{
			DiggerManager->EnsureDefaultHoleBP(); // <--- Make sure this actually works!
			if (!HoleBP)
			{
				// If this logs, your path to BP_MeshHole is wrong in the C++ global constant
				UE_LOG(LogTemp, Error, TEXT("HoleBP is not set in SpawnHoleFromData")); 
				return;
			}
		}
	
	// 2. If we already have actors matching the data count, we assume we are good
	if (SpawnedHoleInstances.Num() >= HoleDataArray.Num()) 
	{
		return; 
	}

	UE_LOG(LogTemp, Log, TEXT("Regenerating %d holes for Chunk %s"), HoleDataArray.Num(), *ChunkCoordinates.ToString());

	// 3. Respawn missing holes
	for (const FSpawnedHoleData& Data : HoleDataArray)
	{
		// Check if a hole exists at this location roughly (optional deduplication)
		// For now, we just spawn. SpawnHoleFromData handles the setup.
		SpawnHoleFromData(Data);
	}
}

void UVoxelChunk::AddHoleToChunk(ADynamicHole* Hole)
{
	if (!Hole)
		return;

	SpawnedHoleInstances.AddUnique(Hole);

	UE_LOG(LogTemp, Warning,
		TEXT("AddHoleToChunk: Registered hole %s to chunk %s"),
		*Hole->GetName(),
		*ChunkCoordinates.ToString());
}

void UVoxelChunk::RemoveHoleFromChunk(ADynamicHole* Hole)
{
	if (!Hole)
		return;

	SpawnedHoleInstances.Remove(Hole);

	UE_LOG(LogTemp, Warning,
		TEXT("RemoveHoleFromChunk: Unregistered hole %s from chunk %s"),
		*Hole->GetName(),
		*ChunkCoordinates.ToString());
}




int32 UVoxelChunk::GenerateHoleID()
{
	return HoleIDCounter++;
}


AActor* UVoxelChunk::SpawnTransientActor(UWorld* InWorld, TSubclassOf<AActor> ActorClass, FVector Location, FRotator Rotation, FVector Scale)
{
	if (!InWorld || !ActorClass) return nullptr;
	
#if WITH_EDITOR
	// Store the current dirty state to restore it later
	bool bWasLevelDirty = false;
	ULevel* Level = InWorld->GetCurrentLevel();
	if (Level && GIsEditor)
	{
		bWasLevelDirty = Level->GetPackage()->IsDirty();
	}
#endif

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.bNoFail = true;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bDeferConstruction = false;
	
	// Spawn the actor
	AActor* Spawned = InWorld->SpawnActor<AActor>(ActorClass, Location, Rotation, SpawnParams);
	
	if (Spawned)
	{
		Spawned->SetActorScale3D(Scale);
		
		// Set all the transient flags
		Spawned->SetFlags(RF_Transient);
		Spawned->ClearFlags(RF_Transactional | RF_Public);
		
		// Mark as preview actor and disable various systems
		Spawned->SetActorHiddenInGame(false);
		Spawned->bIsEditorPreviewActor = true;
		Spawned->SetReplicates(false);
		Spawned->SetCanBeDamaged(false);
		
		// Additional flags to prevent serialization
		Spawned->SetFlags(RF_DuplicateTransient | RF_NonPIEDuplicateTransient);
		
#if WITH_EDITOR
		if (GIsEditor)
		{
			// Clear transactional flag and prevent modification tracking
			Spawned->ClearFlags(RF_Transactional);
			Spawned->Modify(false);
			
			// Mark all components as transient too
			for (UActorComponent* Component : Spawned->GetComponents().Array())
			{
				if (Component)
				{
					Component->SetFlags(RF_Transient | RF_DuplicateTransient | RF_NonPIEDuplicateTransient);
					Component->ClearFlags(RF_Transactional | RF_Public);
				}
			}
			
			// CRITICAL: Reset the level's dirty state if it wasn't dirty before
			if (Level && !bWasLevelDirty)
			{
				Level->GetPackage()->SetDirtyFlag(false);
			}
		}
#endif
		
		Spawned->SetActorLabel(TEXT("Runtime Hole"));
	}
	
	return Spawned;
}


FVector UVoxelChunk::SnapToVoxelGrid(const FVector& WorldLocation) const
{
	const float GridSize = FVoxelConversion::LocalVoxelSize; // e.g. 50.f or whatever your voxel size is
	return FVector(
		FMath::GridSnap(WorldLocation.X, GridSize),
		FMath::GridSnap(WorldLocation.Y, GridSize),
		FMath::GridSnap(WorldLocation.Z, GridSize)
	);
}



void UVoxelChunk::SpawnHole(
    TSubclassOf<AActor> HoleBPClass,
    FVector Location,
    FRotator Rotation,
    FVector Scale,
    EHoleShapeType ShapeType)
{
    // ---------------------------------------------------------
    // 0. Validate DiggerManager
    // ---------------------------------------------------------
    if (!DiggerManager)
    {
        DiggerManager = ADiggerManager::FindDiggerManager(World);;
        if (!DiggerManager)
        {
            UE_LOG(LogTemp, Error, TEXT("SpawnHole: No DiggerManager found"));
            return;
        }
    }

    // ---------------------------------------------------------
    // 1. Resolve Hole Class
    // ---------------------------------------------------------
    if (!HoleBPClass)
    {
        // Ask the manager to ensure its cached class is loaded
        DiggerManager->EnsureDefaultHoleBP();
        HoleBPClass = DiggerManager->GetDynamicHoleClass();
    }

    if (!HoleBPClass)
    {
        UE_LOG(LogTemp, Error, TEXT("SpawnHole: No valid HoleBPClass after manager ensure"));
        return;
    }

    // Cache locally if you still use HoleBP internally
    HoleBP = HoleBPClass;

    // ---------------------------------------------------------
    // 2. Validate World
    // ---------------------------------------------------------
    UWorld* UseWorld = World ? World : GetWorld();

    if (!UseWorld)
    {
        UE_LOG(LogTemp, Error, TEXT("SpawnHole: Invalid World"));
        return;
    }

    // ---------------------------------------------------------
    // 3. Editor‑only: ensure HoleShapeLibrary
    // ---------------------------------------------------------
#if WITH_EDITOR
    if (GIsEditor)
    {
        DiggerManager->EnsureHoleShapeLibrary();
    }
#endif

    // ---------------------------------------------------------
    // 4. Spawn the actor (Editor vs Runtime)
    // ---------------------------------------------------------
    AActor* SpawnedHole = nullptr;

#if WITH_EDITOR
    const bool bIsEditorPreview = (GIsEditor && !UseWorld->HasBegunPlay());

    if (bIsEditorPreview)
    {
        // Use the editor world context safely
        if (GEditor)
        {
            UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
            if (EditorWorld)
            {
                SpawnedHole = SpawnTransientActor(EditorWorld, HoleBPClass, Location, Rotation, Scale);

                if (SpawnedHole)
                {
                    FString NewLabel = FString::Printf(TEXT("HoleBP_%d"), FMath::RandRange(0, 999999));
                    SpawnedHole->SetActorLabel(NewLabel);
                    SpawnedHole->Modify();
                }
            }
        }
    }
    else
#endif
    {
        // Runtime or PIE
        SpawnedHole = SpawnTransientActor(UseWorld, HoleBPClass, Location, Rotation, Scale);
    }

    // ---------------------------------------------------------
    // 5. Handle spawn result
    // ---------------------------------------------------------
    if (!SpawnedHole)
    {
        UE_LOG(LogTemp, Error, TEXT("SpawnHole: Failed to spawn HoleBP at %s"), *Location.ToString());
        return;
    }

    // ---------------------------------------------------------
    // 6. Record hole data
    // ---------------------------------------------------------
    FHoleShape Shape;
    Shape.ShapeType = ShapeType;

    FSpawnedHoleData HoleData{ Location, Rotation, Scale };
    HoleData.Shape = Shape;

    HoleDataArray.Add(HoleData);

    if (DiggerDebug::Holes())
    {
        UE_LOG(LogTemp, Log, TEXT("Spawned HoleBP at %s with shape %s"),
            *Location.ToString(),
            *UEnum::GetValueAsString(ShapeType));
    }
}




bool UVoxelChunk::RemoveNearestHole(FVector Location, float MaxDistance)
{
	int32 NearestIndex = INDEX_NONE;
	float ClosestDistSqr = MaxDistance * MaxDistance;

	for (int32 i = 0; i < SpawnedHoleInstances.Num(); ++i)
	{
		ADynamicHole* HoleActor = SpawnedHoleInstances[i].Get();
		if (!HoleActor) continue;

		float DistSqr = FVector::DistSquared(HoleActor->GetActorLocation(), Location);
		if (DistSqr < ClosestDistSqr)
		{
			ClosestDistSqr = DistSqr;
			NearestIndex = i;
		}
	}

	if (NearestIndex != INDEX_NONE)
	{
		ADynamicHole* HoleActor = SpawnedHoleInstances[NearestIndex].Get();
		if (IsValid(HoleActor))
		{
			HoleActor->Destroy();
		}
		

		// Also remove the serialized hole data
		if (HoleDataArray.IsValidIndex(NearestIndex))
		{
			HoleDataArray.RemoveAt(NearestIndex);
		}

		SpawnedHoleInstances.RemoveAt(NearestIndex);

		return true;
	}

	return false;
}


void UVoxelChunk::WriteToOverflows(const FIntVector& LocalVoxelCoords, 
                                   int32 StorageX, int32 StorageY, int32 StorageZ, 
                                   float SDF, bool bDig)
{
    const int32 VoxelsPerChunk = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
    const int32 HalfVoxelsPerChunk = VoxelsPerChunk / 2;
    
    // Write to current chunk's overflow regions based on voxel position
    
    // Check if voxel is in the -X overflow region (before chunk start)
    if (LocalVoxelCoords.X < -HalfVoxelsPerChunk) {
        // Write to -X overflow (storage index 0)
        SparseVoxelGrid->SetVoxel(0, StorageY, StorageZ, SDF, bDig);
    }
    
    // Check if voxel is in the -Y overflow region  
    if (LocalVoxelCoords.Y < -HalfVoxelsPerChunk) {
        // Write to -Y overflow
        SparseVoxelGrid->SetVoxel(StorageX, 0, StorageZ, SDF, bDig);
    }
    
    // Check if voxel is in the -Z overflow region
    if (LocalVoxelCoords.Z < -HalfVoxelsPerChunk) {
        // Write to -Z overflow  
        SparseVoxelGrid->SetVoxel(StorageX, StorageY, 0, SDF, bDig);
    }
    
    // Handle corner overflow cases
    if (LocalVoxelCoords.X < -HalfVoxelsPerChunk && LocalVoxelCoords.Y < -HalfVoxelsPerChunk) {
        SparseVoxelGrid->SetVoxel(0, 0, StorageZ, SDF, bDig);
    }
    
    if (LocalVoxelCoords.X < -HalfVoxelsPerChunk && LocalVoxelCoords.Z < -HalfVoxelsPerChunk) {
        SparseVoxelGrid->SetVoxel(0, StorageY, 0, SDF, bDig);
    }
    
    if (LocalVoxelCoords.Y < -HalfVoxelsPerChunk && LocalVoxelCoords.Z < -HalfVoxelsPerChunk) {
        SparseVoxelGrid->SetVoxel(StorageX, 0, 0, SDF, bDig);
    }
    
    // Triple corner overflow
    if (LocalVoxelCoords.X < -HalfVoxelsPerChunk && 
        LocalVoxelCoords.Y < -HalfVoxelsPerChunk && 
        LocalVoxelCoords.Z < -HalfVoxelsPerChunk) {
        SparseVoxelGrid->SetVoxel(0, 0, 0, SDF, bDig);
    }
}

void UVoxelChunk::MulticastApplyBrushStroke_Implementation(const FBrushStroke& Stroke)
{
	ApplyBrushStroke(Stroke);
}



void UVoxelChunk::ApplyBrushStroke(const FBrushStroke& Stroke)
{
	// CRITICAL FOR UNDO:
	// This tells Unreal: "Save the current state of this object before I change it."
	Modify();
	
    // 1. Validation
    UVoxelBrushShape* BrushShape = (DiggerManager) ? DiggerManager->GetBrushShapeForType(Stroke.BrushType) : nullptr;
    if (!DiggerManager || !BrushShape || !SparseVoxelGrid) return;

    FThreadSafeCounter VoxelsDugCounter;
    FThreadSafeCounter VoxelsAddedCounter;

    // 2. Setup Metrics
    // ChunkOrigin is the World Space position of the chunk's (0,0,0) corner
    const FVector ChunkOrigin = FVoxelConversion::ChunkToWorld(ChunkCoordinates);
    const float CachedVoxelSize = FVoxelConversion::LocalVoxelSize;
    if (CachedVoxelSize <= SMALL_NUMBER) return;

    const int32 VoxelsPerChunk = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
    
    // Half Voxel offset is only needed to center the sample point inside the voxel grid cell
    const float HalfVoxelSize = CachedVoxelSize * 0.5f;

    // 3. Bounds Calculation (World Space -> Local Voxel Space)
    FVector BrushBounds = CalculateBrushBounds(Stroke); 
    
    // Convert Brush World Position to Local Space relative to the Chunk Corner
    const FVector LocalBrushPos = Stroke.BrushPosition - ChunkOrigin;
    
    // Convert Radius/Bounds to Voxel Count
    const int32 RangeX = FMath::CeilToInt(BrushBounds.X / CachedVoxelSize);
    const int32 RangeY = FMath::CeilToInt(BrushBounds.Y / CachedVoxelSize);
    const int32 RangeZ = FMath::CeilToInt(BrushBounds.Z / CachedVoxelSize);

    // Get the center voxel index
    const int32 CenterX = FMath::FloorToInt(LocalBrushPos.X / CachedVoxelSize);
    const int32 CenterY = FMath::FloorToInt(LocalBrushPos.Y / CachedVoxelSize);
    const int32 CenterZ = FMath::FloorToInt(LocalBrushPos.Z / CachedVoxelSize);

    // Calculate Min/Max and CLAMP strictly to the chunk size (0 to N)
    // This prevents negative sizes which caused your crash.
    const int32 MinX = FMath::Clamp(CenterX - RangeX, 0, VoxelsPerChunk);
    const int32 MaxX = FMath::Clamp(CenterX + RangeX, 0, VoxelsPerChunk);
    
    const int32 MinY = FMath::Clamp(CenterY - RangeY, 0, VoxelsPerChunk);
    const int32 MaxY = FMath::Clamp(CenterY + RangeY, 0, VoxelsPerChunk);
    
    const int32 MinZ = FMath::Clamp(CenterZ - RangeZ, 0, VoxelsPerChunk);
    const int32 MaxZ = FMath::Clamp(CenterZ + RangeZ, 0, VoxelsPerChunk);

    // Calculate Sizes
    // Since we clamped Min and Max, Size cannot be negative, preventing the TArray crash.
    const int32 SizeX = MaxX - MinX; 
    const int32 SizeY = MaxY - MinY;
    const int32 SizeZ = MaxZ - MinZ;

    // Safety: If the brush is completely outside the chunk, Size will be 0.
    if (SizeX <= 0 || SizeY <= 0 || SizeZ <= 0) return;

    // 4. Pre-Filter
    TArray<FIntVector> AirVoxelsBelowTerrain;
    struct FVoxelInfo { FIntVector Coords; FVector WorldPos; float TerrainHeight; };
    TArray<FVoxelInfo> ValidVoxels;
    
    // Now this Reserve is safe because SizeX/Y/Z are guaranteed positive
    int64 TotalVoxelsToProcess = (int64)SizeX * (int64)SizeY * (int64)SizeZ;
    if (TotalVoxelsToProcess > 0)
    {
        ValidVoxels.Reserve(TotalVoxelsToProcess);
    }

    // Loop through the bounds
    for (int32 X = MinX; X < MaxX; ++X)
    {
        for (int32 Y = MinY; Y < MaxY; ++Y)
        {
            // Calculate Column World Position
            // Origin + (Index * Size) + HalfVoxelOffset
            const FVector ColumnWorldPos = ChunkOrigin + FVector(
                (X * CachedVoxelSize) + HalfVoxelSize,
                (Y * CachedVoxelSize) + HalfVoxelSize,
                0 
            );

            float TerrainHeight = DiggerManager->GetLandscapeHeightAt(ColumnWorldPos);

            // Skip if underground is invalid/bedrock
            if (TerrainHeight <= (UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT + 1.0f)) continue; 

            for (int32 Z = MinZ; Z < MaxZ; ++Z)
            {
                FVector WorldPos = ColumnWorldPos;
                WorldPos.Z = ChunkOrigin.Z + (Z * CachedVoxelSize) + HalfVoxelSize;

                // Let the Brush Shape decide if we are inside the precise shape
                if (!BrushShape->IsWithinBounds(WorldPos, Stroke)) continue;

                FVoxelInfo Info;
                Info.Coords = FIntVector(X, Y, Z);
                Info.WorldPos = WorldPos;
                Info.TerrainHeight = TerrainHeight;
                ValidVoxels.Add(Info);
            }
        }
    }

    // 5. Parallel Process
    ParallelFor(ValidVoxels.Num(), [&](int32 VoxelIndex)
    {
        const FVoxelInfo& Info = ValidVoxels[VoxelIndex];
        const bool bAboveTerrain = Info.WorldPos.Z >= Info.TerrainHeight;

        // Calculate SDF
        float SDF = BrushShape->CalculateSDF(Info.WorldPos, Stroke, Info.TerrainHeight);

        // Apply Smoothing
        if (Stroke.BrushFalloff > SMALL_NUMBER)
        {
            const float DistParams = FMath::Clamp(FMath::Abs(SDF) / Stroke.BrushFalloff, 0.0f, 1.0f);
            const float SmoothedFactor = FMath::SmoothStep(0.0f, 1.0f, DistParams);
            SDF = FMath::Sign(SDF) * (SmoothedFactor * Stroke.BrushFalloff);
        }

        if (Stroke.bDig)
        {
            if (SDF > 0.1f) // Air
            {
                // Safety depth check
                if (FMath::Abs(Info.WorldPos.Z - Stroke.BrushPosition.Z) <= Stroke.BrushRadius * 2.0f)
                {
                    SparseVoxelGrid->SetVoxel(Info.Coords.X, Info.Coords.Y, Info.Coords.Z, SDF, true);
                    VoxelsDugCounter.Increment();

                    if (!bAboveTerrain)
                    {
                        FScopeLock Lock(&BrushStrokeMutex);
                        AirVoxelsBelowTerrain.Add(Info.Coords);
                    }
                }
            }
        }
        else // Add
        {
            if (SDF < -0.1f) // Solid
            {
                SparseVoxelGrid->SetVoxel(Info.Coords.X, Info.Coords.Y, Info.Coords.Z, SDF, false);
                VoxelsAddedCounter.Increment();
            }
        }
    });

    // 6. Solid Shell
    if (!AirVoxelsBelowTerrain.IsEmpty())
    {
    //	CreateSolidShellAroundAirVoxels(AirVoxelsBelowTerrain, Stroke.bHiddenSeam);
    }

    // 7. Broadcast
    if (VoxelsDugCounter.GetValue() > 0 || VoxelsAddedCounter.GetValue() > 0)
    {
        if (DiggerManager)
        {
            FVoxelModificationReport Report;
            Report.VoxelsDug = VoxelsDugCounter.GetValue();
            Report.VoxelsAdded = VoxelsAddedCounter.GetValue();
            Report.ChunkCoordinates = ChunkCoordinates;
            Report.BrushPosition = Stroke.BrushPosition;
            Report.BrushRadius = Stroke.BrushRadius;
            DiggerManager->OnVoxelsModified.Broadcast(Report);
        }
    }
}

void UVoxelChunk::CreateSolidShellAroundAirVoxels(const TArray<FIntVector>& AirVoxels, bool bHiddenSeam)
{
    // If we want a hidden seam, we do NOTHING. 
    // The new Marching Cubes generator handles the invisible seal automatically.
    if (AirVoxels.IsEmpty() || bHiddenSeam) return;

    // --- SETUP ---
    const FVector ChunkOrigin = FVoxelConversion::ChunkToWorld(ChunkCoordinates);
    const float LocalVoxelSize = FVoxelConversion::LocalVoxelSize;
    const int32 VoxelsPerChunk = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
    const float HalfVoxelSize = LocalVoxelSize * 0.5f;
    const float HalfChunkSize = (VoxelsPerChunk * LocalVoxelSize) * 0.5f;

    // 1. Identify Boundary Candidates
    // We only care about neighbors that are theoretically "Above Ground" or near the surface.
    TSet<FIntVector> BoundaryPositions;
    TSet<FIntVector> AirSet(AirVoxels);

    // Standard 6-neighbor check is usually enough for rims, but 26 (Box) gives thicker corners.
    // Let's stick to your existing offset logic for consistency.
    const TArray<FIntVector> NeighborOffsets = []()
    {
        TArray<FIntVector> Offsets;
        for (int32 x = -1; x <= 1; x++)
            for (int32 y = -1; y <= 1; y++)
                for (int32 z = -1; z <= 1; z++) // Only check immediate neighbors
                    if ((x|y|z) != 0) Offsets.Add(FIntVector(x, y, z));
        return Offsets;
    }();

    for (const FIntVector& Air : AirVoxels)
    {
        for (const FIntVector& Off : NeighborOffsets)
        {
            FIntVector Candidate = Air + Off;
            
            // Don't overwrite the hole we just dug
            if (AirSet.Contains(Candidate)) continue;
            
            // Don't overwrite existing solid voxels (waste of time)
            if (SparseVoxelGrid->VoxelData.Contains(Candidate))
            {
                if (SparseVoxelGrid->VoxelData[Candidate].SDFValue <= 0.0f) continue;
            }
            BoundaryPositions.Add(Candidate);
        }
    }

    // 2. Height Cache (Optimization)
    TMap<FIntPoint, float> LocalHeightCache;
    auto GetHeightFast = [&](const FVector& Pos) -> float
    {
        FIntPoint Key(FMath::FloorToInt(Pos.X / LocalVoxelSize), FMath::FloorToInt(Pos.Y / LocalVoxelSize));
        if (float* Val = LocalHeightCache.Find(Key)) return *Val;
        
        // Ensure DiggerManager is valid
        if (!DiggerManager) return -1.0e30f;

        float H = DiggerManager->GetLandscapeHeightAt(Pos);
        LocalHeightCache.Add(Key, H);
        return H;
    };

    // 3. Rim Calculation
    auto GetRimThickness = [&](const FVector& WorldPos, float TerrainH) -> float
    {
        float MaxSlope = 0.0f;
        const TArray<FVector> SampleOffsets = {
            FVector(LocalVoxelSize, 0, 0), FVector(0, LocalVoxelSize, 0),
            FVector(-LocalVoxelSize, 0, 0), FVector(0, -LocalVoxelSize, 0)
        };

        for (const FVector& Offset : SampleOffsets)
        {
            float SampleHeight = GetHeightFast(WorldPos + Offset);
            if (SampleHeight > -1.0e30f)
            {
                float Slope = FMath::Abs(TerrainH - SampleHeight) / LocalVoxelSize;
                MaxSlope = FMath::Max(MaxSlope, Slope);
            }
        }

        if (MaxSlope > 0.1f)
        {
            // Range: 3.0 to 11.0 (The "Globular" look)
            float Thickness = MaxSlope * MaxSlope * 4.0f + 1.0f;
            return FMath::Clamp(Thickness, 3.0f, 11.0f);
        }
        return 1.0f;
    };

    // 4. Processing
    for (const FIntVector& Pos : BoundaryPositions)
    {
        // Safety Bounds Check
        if (Pos.X < -1 || Pos.X > VoxelsPerChunk || 
            Pos.Y < -1 || Pos.Y > VoxelsPerChunk || 
            Pos.Z < -1 || Pos.Z > VoxelsPerChunk) continue;

        // Calculate World Position
        // Note: Check if your ChunkOrigin is Center or Corner. 
        // Based on previous code, assuming Corner (Min):
        FVector WorldPos = ChunkOrigin + FVector(
            (Pos.X * LocalVoxelSize) + HalfVoxelSize,
            (Pos.Y * LocalVoxelSize) + HalfVoxelSize,
            (Pos.Z * LocalVoxelSize) + HalfVoxelSize
        );

        float TerrainHeight = GetHeightFast(WorldPos);
        if (TerrainHeight <= -1.0e30f) continue;

        // --- THE CRITICAL FIX ---
        // If this voxel is BELOW the terrain, IGNORE IT.
        // The Marching Cubes "Underside Snap" will handle the seal.
        // We only want to create the "Mound" above the grass.
        if (WorldPos.Z <= TerrainHeight) continue;

        // --- RIM LOGIC ---
        float RimThickness = GetRimThickness(WorldPos, TerrainHeight);
        float RimHeight = RimThickness * LocalVoxelSize;
        float RimTopZ = TerrainHeight + RimHeight;

        // If we are above the rim height, skip
        if (WorldPos.Z > RimTopZ) continue;

        // --- SDF GENERATION ---
        // Distance from the top of the rim
        float DistFromTop = RimTopZ - WorldPos.Z;
        
        // Normalize 0..1 (Top to Bottom of rim part)
        float Alpha = FMath::Clamp(DistFromTop / (LocalVoxelSize * 2.0f), 0.0f, 1.0f);
        Alpha = Alpha * Alpha * (3.0f - 2.0f * Alpha); // Smoothstep
        
        // Create the voxel
        // -0.05 is "Just barely solid", -1.0 is "Hard solid"
        float SmoothSDF = FMath::Lerp(-0.05f, -1.0f, Alpha);

        // Update the grid
        SparseVoxelGrid->SetVoxel(Pos, SmoothSDF, false);
    }
}


void UVoxelChunk::BakeToStaticMesh(bool bEnableCollision, bool bEnableNanite, float DetailReduction,
	const FString& String)
{
}


void UVoxelChunk::DedupHoles()
{
	TSet<uint64> Seen;
	TArray<ADynamicHole*> Unique;
	
	for (ADynamicHole* Hole : SpawnedHoles)
	{
		if (!Hole) continue;
	
		uint64 Hash = HashCombine(
			GetTypeHash(Hole->GetActorLocation()),
			GetTypeHash(Hole->GetActorScale3D())
		);
	
		Hash = HashCombine(Hash, GetTypeHash((int32)Hole->HoleShape.ShapeType));
	
		if (!Seen.Contains(Hash))
		{
			Seen.Add(Hash);
			Unique.Add(Hole);
		}
		else
		{
			Hole->Destroy();
		}
	}
	
	SpawnedHoles = Unique;
}

void UVoxelChunk::DeclutterHoles()
{
	// for (int32 i = SpawnedHoles.Num() - 1; i >= 0; --i)
	// {
	// 	ADynamicHole* A = SpawnedHoles[i];
	// 	if (!A) continue;
	//
	// 	const FVector ALoc = A->GetActorLocation();
	// 	const float ARadius = A->GetEffectiveRadius();
	//
	// 	bool bRemoveA = false;
	//
	// 	for (int32 j = 0; j < SpawnedHoles.Num(); ++j)
	// 	{
	// 		if (i == j) continue;
	//
	// 		ADynamicHole* B = SpawnedHoles[j];
	// 		if (!B) continue;
	//
	// 		const float BRadius = B->GetEffectiveRadius();
	// 		const float Dist = FVector::Dist(ALoc, B->GetActorLocation());
	//
	// 		if (Dist + ARadius <= BRadius)
	// 		{
	// 			bRemoveA = true;
	// 			break;
	// 		}
	// 	}
	//
	// 	if (bRemoveA)
	// 	{
	// 		A->Destroy();
	// 		SpawnedHoles.RemoveAt(i);
	// 	}
	// }
}

void UVoxelChunk::MergeHoles()
{
	// bool bMerged = true;
	//
	// while (bMerged)
	// {
	// 	bMerged = false;
	//
	// 	for (int32 i = 0; i < SpawnedHoles.Num(); ++i)
	// 	{
	// 		ADynamicHole* A = SpawnedHoles[i];
	// 		if (!A) continue;
	//
	// 		for (int32 j = i + 1; j < SpawnedHoles.Num(); ++j)
	// 		{
	// 			ADynamicHole* B = SpawnedHoles[j];
	// 			if (!B) continue;
	//
	// 			if (A->HoleShape.ShapeType != B->HoleShape.ShapeType)
	// 				continue; // no shape merging here
	//
	// 			const float Dist = FVector::Dist(
	// 				A->GetActorLocation(),
	// 				B->GetActorLocation()
	// 			);
	//
	// 			const float AR = A->GetEffectiveRadius();
	// 			const float BR = B->GetEffectiveRadius();
	//
	// 			if (Dist < AR + BR)
	// 			{
	// 				// Merge into a new hole
	// 				FVector NewCenter = (A->GetActorLocation() + B->GetActorLocation()) * 0.5f;
	// 				float NewRadius = FMath::Max(AR, BR) + Dist * 0.5f;
	//
	// 				SpawnMergedHole(NewCenter, NewRadius, A->Shape.ShapeType);
	//
	// 				A->Destroy();
	// 				B->Destroy();
	//
	// 				Holes.RemoveAt(j);
	// 				Holes.RemoveAt(i);
	//
	// 				bMerged = true;
	// 				break;
	// 			}
	// 		}
	//
	// 		if (bMerged)
	// 			break;
	// 	}
	// }
}


FVector UVoxelChunk::CalculateBrushBounds(const FBrushStroke& Stroke) const
{
	auto CalculateRotatedBounds = [](const FVector& HalfExtents, const FRotator& Rotation, float Falloff) -> FVector
	{
		static constexpr float EPSILON = 0.01f;

		if (Rotation.IsNearlyZero())
		{
			return HalfExtents + Falloff + FVector(EPSILON);
		}

		TArray<FVector> Corners = {
			FVector(-1, -1, -1), FVector(-1, -1, 1),
			FVector(-1,  1, -1), FVector(-1,  1, 1),
			FVector( 1, -1, -1), FVector( 1, -1, 1),
			FVector( 1,  1, -1), FVector( 1,  1, 1)
		};

		FBox RotatedBox(EForceInit::ForceInit);
		for (const FVector& Corner : Corners)
		{
			RotatedBox += Rotation.RotateVector(Corner * HalfExtents);
		}

		return RotatedBox.GetExtent() + Falloff + FVector(EPSILON);
	};


	switch (Stroke.BrushType)
	{
	case EVoxelBrushType::Sphere:
	case EVoxelBrushType::Icosphere:
	case EVoxelBrushType::Smooth:
	case EVoxelBrushType::Noise:
		// Rotation doesn't matter for true spherical brushes
		return FVector(Stroke.BrushRadius + Stroke.BrushFalloff);

	case EVoxelBrushType::Cube:
		{
			const FVector HalfExtents = Stroke.bUseAdvancedCubeBrush
				? FVector(
					Stroke.AdvancedCubeHalfExtentX,
					Stroke.AdvancedCubeHalfExtentY,
					Stroke.AdvancedCubeHalfExtentZ)
				: FVector(Stroke.BrushRadius);

			return CalculateRotatedBounds(HalfExtents, Stroke.BrushRotation, Stroke.BrushFalloff);
		}

	case EVoxelBrushType::Cylinder:
	case EVoxelBrushType::Capsule:
		{
			const FVector HalfExtents = FVector(
				Stroke.BrushRadius,
				Stroke.BrushRadius,
				Stroke.BrushLength * 0.5f
			);
			return CalculateRotatedBounds(HalfExtents, Stroke.BrushRotation, Stroke.BrushFalloff);
		}

	case EVoxelBrushType::Cone:
	case EVoxelBrushType::Pyramid:
		{
			const float AngleRad = FMath::DegreesToRadians(Stroke.BrushAngle);
			const float RadiusAtBase = Stroke.BrushLength * FMath::Tan(AngleRad);
			const FVector HalfExtents = FVector(RadiusAtBase, RadiusAtBase, Stroke.BrushLength * 0.5f);
			return CalculateRotatedBounds(HalfExtents, Stroke.BrushRotation, Stroke.BrushFalloff);
		}

	case EVoxelBrushType::Torus:
		{
			const float OuterRadius = Stroke.BrushRadius + Stroke.TorusInnerRadius;
			const FVector HalfExtents = FVector(OuterRadius, OuterRadius, Stroke.BrushRadius);
			return CalculateRotatedBounds(HalfExtents, Stroke.BrushRotation, Stroke.BrushFalloff);
		}

	default:
		return FVector(Stroke.BrushRadius + Stroke.BrushFalloff);
	}
}






// --- Smooth/Sharpen Fix ---
void UVoxelChunk::ApplySmoothBrush(const FVector& Center, float Radius, bool bDig, int NumIterations)
{
	TArray<FIntVector> VoxelsToSmooth;
	FIntVector CenterVoxel = FVoxelConversion::WorldToLocalVoxel(Center);
	int32 VoxelRadius = FMath::CeilToInt(Radius / FVoxelConversion::LocalVoxelSize);

	for (int32 X = CenterVoxel.X - VoxelRadius; X <= CenterVoxel.X + VoxelRadius; ++X)
		for (int32 Y = CenterVoxel.Y - VoxelRadius; Y <= CenterVoxel.Y + VoxelRadius; ++Y)
			for (int32 Z = CenterVoxel.Z - VoxelRadius; Z <= CenterVoxel.Z + VoxelRadius; ++Z)
			{
				FVector Pos = FVoxelConversion::LocalVoxelToWorld(FIntVector(X, Y, Z));
				if (FVector::Dist(Pos, Center) <= Radius)
				{
					VoxelsToSmooth.Add(FIntVector(X, Y, Z));
				}
			}

	TMap<FIntVector, float> CurrentSDF;
	for (const FIntVector& Voxel : VoxelsToSmooth)
	{
		CurrentSDF.Add(Voxel, SparseVoxelGrid->GetVoxel(Voxel.X, Voxel.Y, Voxel.Z));
	}

	for (int Iter = 0; Iter < NumIterations; ++Iter)
	{
		TMap<FIntVector, float> NextSDF;
		for (const FIntVector& Voxel : VoxelsToSmooth)
		{
			float Sum = 0.f;
			int Count = 0;
			for (int dx = -1; dx <= 1; ++dx)
				for (int dy = -1; dy <= 1; ++dy)
					for (int dz = -1; dz <= 1; ++dz)
					{
						FIntVector N = Voxel + FIntVector(dx, dy, dz);
						float V = CurrentSDF.Contains(N) ? CurrentSDF[N] : SparseVoxelGrid->GetVoxel(N.X, N.Y, N.Z);
						Sum += V;
						++Count;
					}

			float Avg = Sum / Count;
			float CenterVal = CurrentSDF[Voxel];

			float Result = bDig ? CenterVal + (CenterVal - Avg) * 0.5f : FMath::Lerp(CenterVal, Avg, 0.5f);
			NextSDF.Add(Voxel, Result);
		}
		CurrentSDF = NextSDF;
	}

	for (const auto& Pair : CurrentSDF)
	{
		SetVoxel(Pair.Key.X, Pair.Key.Y, Pair.Key.Z, Pair.Value, bDig);
	}
	MarkDirty();
}



float UVoxelChunk::ComputeSDFValue(float NormalizedDist, bool bDig, float TransitionStart, float TransitionEnd)
{
    if (bDig)
    {
        if (NormalizedDist <= TransitionStart)
            return 1.0f; // Air
        else if (NormalizedDist <= TransitionEnd)
        {
            float T = (NormalizedDist - TransitionStart) / (TransitionEnd - TransitionStart);
            T = FMath::SmoothStep(0.0f, 1.0f, T);
            return FMath::Lerp(1.0f, -1.0f, T);
        }
        else
            return -1.0f;
    }
    else
    {
        if (NormalizedDist <= TransitionStart)
            return -1.0f;
        else if (NormalizedDist <= TransitionEnd)
        {
            float T = (NormalizedDist - TransitionStart) / (TransitionEnd - TransitionStart);
            T = FMath::SmoothStep(0.0f, 1.0f, T);
            return FMath::Lerp(-1.0f, 1.0f, T);
        }
        else
            return 1.0f;
    }
}




void UVoxelChunk::BakeSingleBrushStroke(FBrushStroke StrokeToBake) {
	// Placeholder for baking SDF values into the chunk
if (SparseVoxelGrid)
{
//	SparseVoxelGrid->BakeBrush();
}
}


void UVoxelChunk::SetVoxel(int32 X, int32 Y, int32 Z, const float SDFValue, bool &bDig) const
{
	if (SparseVoxelGrid) //&& IsValidChunkLocalCoordinate(X, Y, Z))
	{
		SparseVoxelGrid->SetVoxel(X, Y, Z, SDFValue, bDig);
	}
}

void UVoxelChunk::SetVoxel(const FVector& Position, const float SDFValue, bool &bDig) const
{
	int32 X = Position.X;
	int32 Y = Position.Y;
	int32 Z = Position.Z;

	SetVoxel(X, Y, Z, SDFValue, bDig);
}


USparseVoxelGrid* UVoxelChunk::GetSparseVoxelGrid() const
{
		return SparseVoxelGrid;
}

TMap<FIntVector, float> UVoxelChunk::GetActiveVoxels() const
{
	return GetSparseVoxelGrid()->GetAllVoxelsSDF();
}




void UVoxelChunk::GenerateMesh()
{
	if (!SparseVoxelGrid)
	{
		if (DiggerDebug::Voxels())
		UE_LOG(LogTemp, Error, TEXT("SparseVoxelGrid is null!"));
		return;
	}

	
	// --- Island Detection ---
	TArray<FIslandData> Islands = SparseVoxelGrid->DetectIslands(0.0f);
	if (Islands.Num() > 0)
	{
		if (DiggerDebug::Islands())
		{UE_LOG(LogTemp, Warning, TEXT("Island detection: %d islands found!"), Islands.Num());}
	    for (int32 i = 0; i < Islands.Num(); ++i)
	    {
	        if (DiggerDebug::Islands())
	        {UE_LOG(LogTemp, Warning, TEXT("  Island %d: %d voxels"), i, Islands[i].VoxelCount);}
	    }
	}


	 if (!MarchingCubesGenerator)
    {
        if (DiggerDebug::Mesh())
            UE_LOG(LogTemp, Error, TEXT("MarchingCubesGenerator is nullptr"));
        return;
    }

    // 1. Snapshot OWN Data
    TMap<FIntVector, FVoxelData> CombinedData = SparseVoxelGrid->GetAllVoxels();

    // 2. Snapshot NEIGHBOR Data (26-Way Blend)
    if (DiggerManager) 
    {
        // Grid Dimensions (Points, not Cells)
        const int32 N = ChunkSize * Subdivisions; 

        // Iterate -1 to 1 on all axes (3x3x3 = 27 blocks)
        for (int32 x = -1; x <= 1; x++)
        {
            for (int32 y = -1; y <= 1; y++)
            {
                for (int32 z = -1; z <= 1; z++)
                {
                    // Skip (0,0,0) - that is 'this' chunk
                    if (x == 0 && y == 0 && z == 0) continue;

                    FIntVector Offset(x, y, z);
                    FIntVector NeighborCoords = ChunkCoordinates + Offset;

                    // Look up neighbor chunk
                    if (UVoxelChunk** NeighborPtr = DiggerManager->ChunkMap.Find(NeighborCoords))
                    {
                        if (UVoxelChunk* Neighbor = *NeighborPtr)
                        {
                            if (USparseVoxelGrid* NeighborGrid = Neighbor->GetSparseVoxelGrid())
                            {
                                // Calculate the coordinate shift.
                                // If Neighbor is at X+1, its local '0' maps to our local 'N'.
                                // Shift = (1 * N) = N.
                                FIntVector CoordShift = Offset * N;

                                // Helper: Only copy data relevant to the shared boundary.
                                // This prevents copying the entire neighbor (slow) and only grabs the touching face/edge/corner.
                                for (const auto& Pair : NeighborGrid->VoxelData)
                                {
                                    const FIntVector& Loc = Pair.Key;

                                    // Relevance Check:
                                    // If Offset.X is  1, we need Neighbor's X=0.
                                    // If Offset.X is -1, we need Neighbor's X=N.
                                    // If Offset.X is  0, we take Any X (provided Y or Z matches).
                                    bool bMatchX = (x == 0) || (x == 1 && Loc.X == 0) || (x == -1 && Loc.X == N);
                                    bool bMatchY = (y == 0) || (y == 1 && Loc.Y == 0) || (y == -1 && Loc.Y == N);
                                    bool bMatchZ = (z == 0) || (z == 1 && Loc.Z == 0) || (z == -1 && Loc.Z == N);

                                    // Only add if it matches the boundary condition for this specific neighbor direction
                                    if (bMatchX && bMatchY && bMatchZ)
                                    {
                                        FIntVector ShiftedKey = Loc + CoordShift;
                                        CombinedData.Add(ShiftedKey, Pair.Value);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // 3. PREPARE DATA FOR THREAD
    FVector Origin = FVoxelConversion::ChunkToWorld(ChunkCoordinates);
    const float VoxelSizeSnapshot = VoxelSize; // <--- Your excellent fix
    int32 N = ChunkSize * Subdivisions;

    // Capture Heights (Game Thread)
    TArray<float> LocalHeights = MarchingCubesGenerator->CaptureHeightMap(Origin, VoxelSizeSnapshot, N);

    // 4. Launch Async Task
    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, 
        [this, Generator=MarchingCubesGenerator, CombinedData, Origin, VoxelSizeSnapshot, LocalHeights]()
    {
        TArray<FVector> Verts;
        TArray<int32> Tris;
        TArray<FVector> Normals;

        // Use the Combined Map
        Generator->GenerateMeshFromGrid(
            CombinedData, 
            Origin, 
            VoxelSizeSnapshot, 
            LocalHeights, 
            Verts, Tris, Normals
        );

        // Return to Game Thread
        AsyncTask(ENamedThreads::GameThread, [this, Verts, Tris, Normals]()
        {
            if (IsValid(this))
            {
                this->UpdateMeshFromData(Verts, Tris, Normals);
                this->OnMarchingMeshComplete();
            }
        });
    });
}
