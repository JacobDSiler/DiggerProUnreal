#include "VoxelChunk.h"

#include "DiggerDebug.h"
#include "DiggerManager.h"
#include "DynamicHole.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "HLSLTypeAliases.h"
#include "HoleBPHelpers.h"
#include "MarchingCubes.h"
#include "SparseVoxelGrid.h"
#include "Digger/Public/Voxel/VoxelBrushHelpers.h" // or wherever you put SetDigShellVoxels
#include "VoxelBrushTypes.h"
#include "VoxelConversion.h"
#include "VoxelLogManager.h"
#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "Digger/Utilities/FastDebugRenderer.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDeviceNull.h"
#include "Serialization/BufferArchive.h"


struct FSpawnedHoleData;

UVoxelChunk::UVoxelChunk()
	: ChunkCoordinates(FIntVector::ZeroValue), 
	  TerrainGridSize(100), 
	  Subdivisions(4),
	  SectionIndex(0),
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
	if (DiggerDebug::Chunks())
	UE_LOG(LogTemp, Error, TEXT("Chunk created at X: %i Y: %i Z: %i"), ChunkCoordinates.X, ChunkCoordinates.Y, ChunkCoordinates.Z);

	HoleBP=InDiggerManager->HoleBP;
	
	// Now initialize the SparseVoxelGrid with the correct coordinates
	if (SparseVoxelGrid)
	{
		SparseVoxelGrid->Initialize(this);
	}

	if(!DiggerManager)
	{
		DiggerManager = InDiggerManager;
	}

	// Get the terrain and grid settings from the DiggerManager
	if (!DiggerManager)
	{
		for (TActorIterator<ADiggerManager> It(GetWorld()); It; ++It)
		{
			DiggerManager = *It;
			break;
		}
	}

	if (!DiggerManager)
	{
		if (DiggerDebug::Manager() || DiggerDebug::Verbose())
		UE_LOG(LogTemp, Error, TEXT("DiggerManager is null during chunk initialization!"));
		return;
	}
	
	//Set the diggermanager in my SparseVoxelGrid
	SparseVoxelGrid->InitializeDiggerManager(); 

	//Now that diggermanager is set we will set the unique section ID for this chunk.
	SetUniqueSectionIndex();

	MarchingCubesGenerator->SetDiggerManager(DiggerManager);
	ChunkSize=DiggerManager->ChunkSize;
	TerrainGridSize = DiggerManager->TerrainGridSize;
	Subdivisions = DiggerManager->Subdivisions;
	World = DiggerManager->GetWorldFromManager();

	if (!SparseVoxelGrid)
	{
		if (DiggerDebug::Voxels())
		UE_LOG(LogTemp, Error, TEXT("SparseVoxelGrid passed to InitializeChunk is null!"));
		return;
	}
	if (DiggerDebug::Chunks())
	UE_LOG(LogTemp, Warning, TEXT("Initializing new chunk at position X=%d Y=%d Z=%d"), ChunkCoordinates.X, ChunkCoordinates.Y, ChunkCoordinates.Z);

	// Add this chunk to the ChunkMap
	DiggerManager->ChunkMap.Add(ChunkCoordinates, this);
    
	// Log the successful addition
	if (DiggerDebug::Chunks())
	UE_LOG(LogTemp, Warning, TEXT("Chunk added to ChunkMap at position: X=%d Y=%d Z=%d"), ChunkCoordinates.X, ChunkCoordinates.Y, ChunkCoordinates.Z);

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
	if (DiggerDebug::Mesh())
	{	UE_LOG(LogTemp, Warning, TEXT("MeshReady Callback received! Now Setting the ShapeType for the holeBP!"));}
	UStaticMesh* HoleMesh = HoleShapeLibrary->GetMeshForShape(EHoleShapeType::Sphere);
	
}


void UVoxelChunk::SpawnHoleFromData(const FSpawnedHoleData& HoleData)
{
    // 1. Validate Library
    if (!HoleShapeLibrary)
    {
        DiggerManager->EnsureHoleShapeLibrary();
        if (!HoleShapeLibrary)
        {
            if (DiggerDebug::Holes() || DiggerDebug::Error())
            {
                UE_LOG(LogTemp, Error, TEXT("HoleShapeLibrary is not set in SpawnHoleFromData"));
            }
            return;
        }
    }

    // 2. Validate BP Class
    if (!HoleBP)
    {
        EnsureDefaultHoleBP();
        if (!HoleBP)
        {
            if (DiggerDebug::Holes() || DiggerDebug::Error())
            {
                UE_LOG(LogTemp, Error, TEXT("HoleBP is not set in SpawnHoleFromData"));
            }
            return;
        }
    }

    // 3. Validate World
    if (!GetWorld())
    {
        if (DiggerDebug::Context() || DiggerDebug::Holes() || DiggerDebug::Error())
        {
            UE_LOG(LogTemp, Error, TEXT("GetWorld() returned null in SpawnHoleFromData"));
        }
        return;
    }

    // 4. Spawn the Actor
    AActor* SpawnedHole = SpawnTransientActor(GetWorld(), HoleBP, HoleData.Location, HoleData.Rotation, HoleData.Scale);
    if (!SpawnedHole)
    {
        if (DiggerDebug::Holes() || DiggerDebug::Error())
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to spawn hole actor"));
        }
        return;
    }

    // Track the instance
    SpawnedHoleInstances.Add(SpawnedHole);

    // 5. Apply the Mesh (The Fixed Logic)
    UStaticMesh* HoleMesh = HoleShapeLibrary->GetMeshForShape(HoleData.Shape.ShapeType);

    // Cast to the C++ class so we can call functions directly
    ADynamicHole* DynamicHole = Cast<ADynamicHole>(SpawnedHole);

    if (DynamicHole)
    {
        // Inject the DiggerManager reference in case the hole needs it
        DynamicHole->SetDiggerManager(this->DiggerManager);
    	DynamicHole->HoleShapeType = HoleData.Shape.ShapeType; // <--- The memory injection

        if (HoleMesh)
        {
            // DIRECT C++ CALL - Replaces the broken ProcessEvent/InitializeHoleMesh logic
            DynamicHole->SetHoleMesh(HoleMesh);
        }
        else
        {
            if (DiggerDebug::Holes() || DiggerDebug::Error())
            {
                UE_LOG(LogTemp, Warning, TEXT("No mesh found for shape %s - Hole will use default mesh"), *UEnum::GetValueAsString(HoleData.Shape.ShapeType));
            }
        }
    }
    else
    {
        // This Error is critical: It means BP_MeshHole is not parented to ADynamicHole
        UE_LOG(LogTemp, Error, TEXT("CRITICAL ERROR: Spawned Hole is not of type ADynamicHole! Please Reparent BP_MeshHole to ADynamicHole in the Editor."));
    }

#if WITH_EDITOR
    if (GIsEditor)
    {
        FString NewLabel = FString::Printf(TEXT("HoleBP_%s"), *UEnum::GetValueAsString(HoleData.Shape.ShapeType));
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
		UpdateIfDirty(); // Mesh was just rebuilt, time to lock it in
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
    SpawnedHoleInstances.RemoveAll([](AActor* A) { return !IsValid(A); });

    // Wipe old data to rebuild from current world state
    HoleDataArray.Empty();

    for (AActor* Actor : SpawnedHoleInstances)
    {
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
	for (AActor* HoleActor : SpawnedHoleInstances)
	{
		if (HoleActor && IsValid(HoleActor))
		{
			HoleActor->Destroy();
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
		if (SpawnedHoleInstances[i]) continue;

		const FSpawnedHoleData& HoleData = HoleDataArray[i];
		SpawnHoleFromData(HoleData);
	}
}

void UVoxelChunk::RegenerateHolesFromData()
{
	// 1. Clean up any invalid pointers first
	SpawnedHoleInstances.RemoveAll([](AActor* A) { return !IsValid(A); });

	// Validate BP Class
		if (!HoleBP)
		{
			EnsureDefaultHoleBP(); // <--- Make sure this actually works!
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
	if (Hole && !SpawnedHoles.Contains(Hole))
	{
		SpawnedHoles.Add(Hole);
	}
}


int32 UVoxelChunk::GenerateHoleID()
{
	return HoleIDCounter++;
}


void UVoxelChunk::RemoveHoleFromChunk(ADynamicHole* Hole)
{
	if (Hole)
	{
		SpawnedHoles.Remove(Hole);
	}
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

#if WITH_EDITOR
void UVoxelChunk::EnsureDefaultHoleBP()
{
	if (!HoleBP)
	{
		FSoftObjectPath AssetPath(GDefaultHoleBPPath);
		UObject* LoadedObj = AssetPath.TryLoad();

		if (UClass* LoadedClass = Cast<UClass>(LoadedObj))
		{
			HoleBP = LoadedClass;
			if (DiggerDebug::Holes())
			{
				UE_LOG(LogTemp, Log, TEXT("Loaded Default HoleBP from %s"), GDefaultHoleBPPath);
			}
		}
		else
		{
			if (DiggerDebug::Holes())
			{
				UE_LOG(LogTemp, Warning, TEXT("Failed to load HoleBP from %s"), GDefaultHoleBPPath);
			}
		}
	}
}
#endif


void UVoxelChunk::SpawnHole(TSubclassOf<AActor> HoleBPClass, FVector Location, FRotator Rotation, FVector Scale, EHoleShapeType ShapeType)
{
#if WITH_EDITOR
    // Ensure HoleBP is set, even if not passed in
    if (!HoleBPClass)
    {
        EnsureDefaultHoleBP(); // Sets HoleBP internally if missing
        HoleBPClass = HoleBP;
    }

    // Ensure the Hole Shape Library is valid
    DiggerManager->EnsureHoleShapeLibrary(); // Your method to create/load & seed if missing
#endif

    if (!HoleBPClass)
    {
	    if (DiggerDebug::Holes())
	    {
		    UE_LOG(LogTemp, Error, TEXT("SpawnHole: No valid HoleBPClass after ensure"));
	    }
        return;
    }

    HoleBP = HoleBPClass;

    if (!World)
    {
	    if (DiggerDebug::Holes() || DiggerDebug::Context())
	    {
		    UE_LOG(LogTemp, Error, TEXT("SpawnHole: Invalid World"));
	    }
        return;
    }

    AActor* SpawnedHole = nullptr;

#if WITH_EDITOR
    if (GIsEditor && !GWorld->HasBegunPlay())
    {
        if (GEditor && GEditor->GetEditorWorldContext().World())
        {
            UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
            SpawnedHole = SpawnTransientActor(EditorWorld, HoleBPClass, Location, Rotation, Scale);

            if (SpawnedHole)
            {
                FString NewLabel = FString::Printf(TEXT("HoleBP_%d"), FMath::RandRange(0, 999999));
                SpawnedHole->SetActorLabel(NewLabel);
                SpawnedHole->Modify();
            }
        }
    }
    else
#endif
    {
        SpawnedHole = SpawnTransientActor(World, HoleBPClass, Location, Rotation, Scale);
    }

    if (SpawnedHole)
    {
        // Construct HoleShape and assign it
        FHoleShape Shape;
        Shape.ShapeType = ShapeType;

        FSpawnedHoleData HoleData{ Location, Rotation, Scale };
        HoleData.Shape = Shape;

        HoleDataArray.Add(HoleData);

    	if (DiggerDebug::Holes())
        UE_LOG(LogTemp, Log, TEXT("Spawned HoleBP at %s with shape %s"),
               *Location.ToString(),
               *UEnum::GetValueAsString(ShapeType));
    }
    else
    {
    	if (DiggerDebug::Holes())
        UE_LOG(LogTemp, Error, TEXT("Failed to spawn HoleBP at %s"), *Location.ToString());
    }
}




bool UVoxelChunk::RemoveNearestHole(FVector Location, float MaxDistance)
{
	int32 NearestIndex = INDEX_NONE;
	float ClosestDistSqr = MaxDistance * MaxDistance;

	for (int32 i = 0; i < SpawnedHoleInstances.Num(); ++i)
	{
		AActor* HoleActor = SpawnedHoleInstances[i];
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
		AActor* HoleActor = SpawnedHoleInstances[NearestIndex];
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
    // 1. Validation
    UVoxelBrushShape* BrushShape = (DiggerManager) ? DiggerManager->GetBrushShapeForType(Stroke.BrushType) : nullptr;
    if (!DiggerManager || !BrushShape || !SparseVoxelGrid) return;

    FThreadSafeCounter VoxelsDugCounter;
    FThreadSafeCounter VoxelsAddedCounter;

    // 2. Setup Bounds
    const FVector ChunkOrigin = FVoxelConversion::ChunkToWorld(ChunkCoordinates);
    const float CachedVoxelSize = FVoxelConversion::LocalVoxelSize;
    const int32 VoxelsPerChunk = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
    const float HalfChunkSize = (VoxelsPerChunk * CachedVoxelSize) * 0.5f;

    FVector BrushBounds = CalculateBrushBounds(Stroke);
    const float VoxelSpaceBoundsX = BrushBounds.X / CachedVoxelSize;
    const float VoxelSpaceBoundsY = BrushBounds.Y / CachedVoxelSize;
    const float VoxelSpaceBoundsZ = BrushBounds.Z / CachedVoxelSize;

    const FVector LocalBrushPos = Stroke.BrushPosition - ChunkOrigin;
    const FIntVector VoxelCenter = FIntVector(
        FMath::FloorToInt((LocalBrushPos.X + HalfChunkSize) / CachedVoxelSize),
        FMath::FloorToInt((LocalBrushPos.Y + HalfChunkSize) / CachedVoxelSize),
        FMath::FloorToInt((LocalBrushPos.Z + HalfChunkSize) / CachedVoxelSize)
    );

    const int32 Padding = 2; 
    const int32 MinX = FMath::Max(-1, FMath::FloorToInt(VoxelCenter.X - VoxelSpaceBoundsX) - Padding);
    const int32 MaxX = FMath::Min(VoxelsPerChunk, FMath::CeilToInt(VoxelCenter.X + VoxelSpaceBoundsX) + Padding);
    const int32 MinY = FMath::Max(-1, FMath::FloorToInt(VoxelCenter.Y - VoxelSpaceBoundsY) - Padding);
    const int32 MaxY = FMath::Min(VoxelsPerChunk, FMath::CeilToInt(VoxelCenter.Y + VoxelSpaceBoundsY) + Padding);
    const int32 MinZ = FMath::Max(-1, FMath::FloorToInt(VoxelCenter.Z - VoxelSpaceBoundsZ) - Padding);
    const int32 MaxZ = FMath::Min(VoxelsPerChunk, FMath::CeilToInt(VoxelCenter.Z + VoxelSpaceBoundsZ) + Padding);

    const int32 SizeX = MaxX - MinX + 1;
    const int32 SizeY = MaxY - MinY + 1;
    const int32 SizeZ = MaxZ - MinZ + 1;

	const float HalfVoxelSize = CachedVoxelSize * 0.5f; // We need this to use that variable <-<-<-<-|

    if (SizeX <= 0 || SizeY <= 0 || SizeZ <= 0) return;

    // Containers
    TArray<FIntVector> AirVoxelsBelowTerrain;
    struct FVoxelInfo { FIntVector Coords; FVector WorldPos; float TerrainHeight; };
    TArray<FVoxelInfo> ValidVoxels;
    ValidVoxels.Reserve(SizeX * SizeY * SizeZ);

    FVector AdjustedBrushPos = Stroke.BrushPosition + Stroke.BrushOffset;
    float OuterRadius = Stroke.BrushRadius + Stroke.BrushFalloff;
    float OuterRadiusSq = OuterRadius * OuterRadius;

    // 3. Pre-Filter Loop (Game Thread)
    for (int32 X = MinX; X <= MaxX; ++X)
    {
        for (int32 Y = MinY; Y <= MaxY; ++Y)
        {
            FVector ColumnPos = ChunkOrigin + FVector(
                (X * CachedVoxelSize) - HalfChunkSize + HalfVoxelSize,
                (Y * CachedVoxelSize) - HalfChunkSize + HalfVoxelSize,
                0
            );
            
            // LAZY LOAD: This now calls the robust DiggerLandscapeCache
            float ColumnHeight = DiggerManager->GetLandscapeHeightAt(ColumnPos);

            for (int32 Z = MinZ; Z <= MaxZ; ++Z)
            {
                const FVector WorldPos = ColumnPos + FVector(0, 0, (Z * CachedVoxelSize) - HalfChunkSize + HalfVoxelSize);

                if (FVector::DistSquared(WorldPos, AdjustedBrushPos) > OuterRadiusSq) continue;
                if (!BrushShape->IsWithinBounds(WorldPos, Stroke)) continue;

                FVoxelInfo Info;
                Info.Coords = FIntVector(X, Y, Z);
                Info.WorldPos = WorldPos;
                Info.TerrainHeight = ColumnHeight;
                ValidVoxels.Add(Info);
            }
        }
    }

    // 4. Parallel Process
    ParallelFor(ValidVoxels.Num(), [&](int32 VoxelIndex)
    {
        const FVoxelInfo& Info = ValidVoxels[VoxelIndex];
        const float TerrainHeight = Info.TerrainHeight;
        
        bool bDataValid = (TerrainHeight > (UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT + 1.0f));
        bool bIsBelowTerrain = false;

        // Logic: Trust the cache. 
        if (bDataValid)
        {
            // If the voxel center is below the terrain, we are underground.
            bIsBelowTerrain = (Info.WorldPos.Z < TerrainHeight);
        }
        else
        {
            // Fallback: If cache completely failed (unlikely now), assume underground
            // to ensure tunnels work.
            bIsBelowTerrain = true;
        }

        const float SDF = BrushShape->CalculateSDF(Info.WorldPos, Stroke, TerrainHeight);

        if (Stroke.bDig)
        {
            if (SDF > 0.1f)
            {
                // Write Explicit Air
                bool bSet = SparseVoxelGrid->SetVoxel(Info.Coords.X, Info.Coords.Y, Info.Coords.Z, SDF, true);
                if(bSet) VoxelsDugCounter.Increment();

                // Only shell if Valid & Below.
                // This prevents "Air" strokes in the sky from generating floating walls.
                if (bIsBelowTerrain)
                {
                    FScopeLock Lock(&BrushStrokeMutex);
                    AirVoxelsBelowTerrain.Add(Info.Coords);
                }
            }
        }
        else
        {
            if (SDF < -0.1f)
            {
                // Write Explicit Solid
                bool bSet = SparseVoxelGrid->SetVoxel(Info.Coords.X, Info.Coords.Y, Info.Coords.Z, SDF, false);
                if(bSet) VoxelsAddedCounter.Increment();
            }
        }
    });

    // 5. Shell Generation
    if (!AirVoxelsBelowTerrain.IsEmpty())
    {
        CreateSolidShellAroundAirVoxels(AirVoxelsBelowTerrain, Stroke.bHiddenSeam);
    }

    // 6. Broadcast / Dirty
    const int32 Dug = VoxelsDugCounter.GetValue();
    const int32 Added = VoxelsAddedCounter.GetValue();
    
    if (Dug > 0 || Added > 0)
    {
        MarkDirty(); 
        
        if (DiggerManager)
        {
             FVoxelModificationReport Report;
             Report.VoxelsDug = Dug;
             Report.VoxelsAdded = Added;
             Report.ChunkCoordinates = ChunkCoordinates;
             Report.BrushPosition = Stroke.BrushPosition;
             Report.BrushRadius = Stroke.BrushRadius;

             DiggerManager->OnVoxelsModified.Broadcast(Report);
        }
    }
}


void UVoxelChunk::CreateSolidShellAroundAirVoxels(const TArray<FIntVector>& AirVoxels, bool bHiddenSeam)
{
    if (AirVoxels.IsEmpty()) return;

    // Constants
    const FVector ChunkOrigin = FVoxelConversion::ChunkToWorld(ChunkCoordinates);
    const float CachedVoxelSize = FVoxelConversion::LocalVoxelSize;
    const int32 VoxelsPerChunk = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
    const float HalfChunkSize = (VoxelsPerChunk * CachedVoxelSize) * 0.5f;
    const float HalfVoxelSize = CachedVoxelSize * 0.5f;

    // Use Sets
    TSet<FIntVector> AirVoxelSet(AirVoxels);
    TSet<FIntVector> BoundaryPositions;
    
    // 6-Neighbor
    const FIntVector FaceOffsets[] = {
        FIntVector(1,0,0), FIntVector(-1,0,0),
        FIntVector(0,1,0), FIntVector(0,-1,0),
        FIntVector(0,0,1), FIntVector(0,0,-1)
    };

    // Identify Candidates
    for (const FIntVector& AirVoxel : AirVoxels)
    {
        for (const FIntVector& Offset : FaceOffsets)
        {
            const FIntVector Candidate = AirVoxel + Offset;
            if (AirVoxelSet.Contains(Candidate)) continue; 
            
            if (SparseVoxelGrid->VoxelData.Contains(Candidate))
            {
                if (SparseVoxelGrid->VoxelData[Candidate].SDFValue > 0.0f) continue; 
                if (SparseVoxelGrid->VoxelData[Candidate].SDFValue <= FVoxelConversion::SDF_SOLID) continue;
            }
            BoundaryPositions.Add(Candidate);
        }
    }

    // Rim Helper (Slope Aware)
    auto GetRimThickness = [&, CachedVoxelSize](const FVector& WorldPos) -> float
    {
        float CenterH = DiggerManager->GetLandscapeHeightAt(WorldPos);
        if (CenterH <= (UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT + 1.0f)) return 2.0f;

        const TArray<FVector> SampleOffsets = {
            FVector(CachedVoxelSize, 0, 0), FVector(0, CachedVoxelSize, 0),
            FVector(-CachedVoxelSize, 0, 0), FVector(0, -CachedVoxelSize, 0)
        };

        float MaxSlope = 0.0f;
        for (const FVector& Offset : SampleOffsets)
        {
            float NeighborH = DiggerManager->GetLandscapeHeightAt(WorldPos + Offset);
            if (NeighborH > (UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT + 1.0f))
            {
                const float HeightDiff = FMath::Abs(CenterH - NeighborH);
                const float Slope = HeightDiff / CachedVoxelSize;
                MaxSlope = FMath::Max(MaxSlope, Slope);
            }
        }

        if (MaxSlope > 0.1f) return FMath::Clamp(MaxSlope * 4.0f + 2.0f, 3.0f, 12.0f);
        return 2.0f;
    };

    int32 CreatedVoxels = 0;

    for (const FIntVector& Pos : BoundaryPositions)
    {
        if (Pos.X < -1 || Pos.X > VoxelsPerChunk ||
            Pos.Y < -1 || Pos.Y > VoxelsPerChunk ||
            Pos.Z < -1 || Pos.Z > VoxelsPerChunk) continue;

        const FVector WorldPos = ChunkOrigin + FVector(
            (Pos.X * CachedVoxelSize) - HalfChunkSize + HalfVoxelSize,
            (Pos.Y * CachedVoxelSize) - HalfChunkSize + HalfVoxelSize,
            (Pos.Z * CachedVoxelSize) - HalfChunkSize + HalfVoxelSize
        );

        float TerrainHeight = DiggerManager->GetLandscapeHeightAt(WorldPos);

        // Fallback: If cache miss, assume underground (FORCE WALLS)
        if (TerrainHeight <= (UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT + 1.0f))
        {
            TerrainHeight = WorldPos.Z + 10000.0f;
        }

        const float VoxelCenterZ = WorldPos.Z;
        const float VoxelBottomZ = VoxelCenterZ - HalfVoxelSize;
        const float VoxelTopZ    = VoxelCenterZ + HalfVoxelSize;
        
        const float TerrainToVoxelBottom = VoxelBottomZ - TerrainHeight;
        const float TerrainToVoxelCenter = VoxelCenterZ - TerrainHeight;

        bool bShouldCreate = false;

        // LAYER 1: BASE SOLID
        if (bHiddenSeam) {
            if (TerrainToVoxelBottom <= -CachedVoxelSize) bShouldCreate = true;
        } else {
            if (TerrainToVoxelBottom <= HalfVoxelSize) bShouldCreate = true;
        }

        // LAYER 2: RIM LOGIC
        if (!bHiddenSeam && !bShouldCreate)
        {
            float Thickness = GetRimThickness(WorldPos);
            
            if (TerrainToVoxelCenter <= (Thickness * CachedVoxelSize) && 
                TerrainToVoxelBottom <= (CachedVoxelSize * 1.5f))
            {
                // Connectivity Check
                int32 SolidNeighbors = 0;
                int32 TerrainNeighbors = 0;

                for (const auto& Off : FaceOffsets)
                {
                    FIntVector N = Pos + Off;
                    if (SparseVoxelGrid->VoxelData.Contains(N) && SparseVoxelGrid->VoxelData[N].SDFValue <= 0.0f) SolidNeighbors++;
                    else if (BoundaryPositions.Contains(N)) SolidNeighbors++;

                    FVector NPos = WorldPos + FVector(0,0, Off.Z * CachedVoxelSize);
                    float NH = DiggerManager->GetLandscapeHeightAt(NPos);
                    if (NH <= (UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT + 1.0f)) NH = NPos.Z + 10000.0f;

                    if ((NPos.Z - HalfVoxelSize) < NH) TerrainNeighbors++;
                }

                bool bStrong = false;
                if (TerrainNeighbors >= 2) bStrong = true;
                else if (TerrainNeighbors >= 1 && SolidNeighbors >= 3) bStrong = true;
                else if (SolidNeighbors >= 4) bStrong = true;

                // Vertical Support
                bool bSupported = false;
                FIntVector Down = Pos + FIntVector(0,0,-1);
                
                if (SparseVoxelGrid->VoxelData.Contains(Down) && SparseVoxelGrid->VoxelData[Down].SDFValue <= 0.0f) bSupported = true;
                else if (BoundaryPositions.Contains(Down)) bSupported = true;
                else
                {
                    float DownH = DiggerManager->GetLandscapeHeightAt(WorldPos - FVector(0,0,CachedVoxelSize));
                    if (DownH <= (UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT + 1.0f)) DownH = WorldPos.Z + 10000.0f;
                    if ((WorldPos.Z - CachedVoxelSize - HalfVoxelSize) <= DownH) bSupported = true;
                }

                if (bStrong && bSupported) bShouldCreate = true;
            }
        }

        // LAYER 3: PREVENT SKY PILES
        // This stops the volcano effect if the cache is working
        if (VoxelBottomZ > (TerrainHeight + CachedVoxelSize))
        {
            bShouldCreate = false;
        }

        if (bShouldCreate)
        {
            if (SparseVoxelGrid->SetVoxel(Pos, FVoxelConversion::SDF_SOLID, false))
            {
                CreatedVoxels++;
            }
        }
    }
}




void UVoxelChunk::BakeToStaticMesh(bool bEnableCollision, bool bEnableNanite, float DetailReduction,
	const FString& String)
{
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


	if (MarchingCubesGenerator == nullptr)
	{
		if (DiggerDebug::Mesh())
		{UE_LOG(LogTemp, Error, TEXT("MarchingCubesGenerator is nullptr in UVoxelChunk::UpdateIfDirty"));}
		return;
	}

	if (MarchingCubesGenerator != nullptr)
	{
		MarchingCubesGenerator->OnMeshReady.BindLambda([this]()
		{
			this->OnMarchingMeshComplete();
		});

		MarchingCubesGenerator->GenerateMesh(this);
	}

}
