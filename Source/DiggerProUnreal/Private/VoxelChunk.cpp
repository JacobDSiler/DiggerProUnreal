#include "VoxelChunk.h"

#include "DiggerDebug.h"
#include "DiggerManager.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "HLSLTypeAliases.h"
#include "HoleBPHelpers.h"
#include "MarchingCubes.h"
#include "SparseVoxelGrid.h"
#include "DiggerProUnreal/Public/Voxel/VoxelBrushHelpers.h" // or wherever you put SetDigShellVoxels
#include "VoxelBrushTypes.h"
#include "VoxelConversion.h"
#include "VoxelLogManager.h"
#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "DiggerProUnreal/Utilities/FastDebugRenderer.h"
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
	if (DiggerDebug::Mesh || DiggerDebug::Chunks)
	UE_LOG(LogTemp, Warning, TEXT("SectionID Set to: %i for ChunkCoordinates X=%d Y=%d Z=%d"), SectionIndex, ChunkCoordinates.X, ChunkCoordinates.Y, ChunkCoordinates.Z);

	GlobalChunkID++;
	if (DiggerDebug::Mesh || DiggerDebug::Chunks)
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

void UVoxelChunk::InitializeChunk(const FIntVector& InChunkCoordinates, ADiggerManager* InDiggerManager)
{
	ChunkCoordinates = InChunkCoordinates;
	if (DiggerDebug::Chunks)
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
		if (DiggerDebug::Manager || DiggerDebug::Verbose)
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
		if (DiggerDebug::Voxels)
		UE_LOG(LogTemp, Error, TEXT("SparseVoxelGrid passed to InitializeChunk is null!"));
		return;
	}
	if (DiggerDebug::Chunks)
	UE_LOG(LogTemp, Warning, TEXT("Initializing new chunk at position X=%d Y=%d Z=%d"), ChunkCoordinates.X, ChunkCoordinates.Y, ChunkCoordinates.Z);

	// Add this chunk to the ChunkMap
	DiggerManager->ChunkMap.Add(ChunkCoordinates, this);
    
	// Log the successful addition
	if (DiggerDebug::Chunks)
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
		if (DiggerDebug::Chunks || DiggerDebug::Holes)
		UE_LOG(LogTemp, Warning, TEXT("Chunk HoleShapeLibrary set successfully from the manager!"));
		HoleShapeLibrary = DiggerManager->HoleShapeLibrary;
	}
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
	if (DiggerDebug::Mesh)
	{	UE_LOG(LogTemp, Warning, TEXT("MeshReady Callback received! Now Setting the ShapeType for the holeBP!"));}
	UStaticMesh* HoleMesh = HoleShapeLibrary->GetMeshForShape(EHoleShapeType::Sphere);
	
}


// In UVoxelChunk.cpp
void UVoxelChunk::SpawnHoleFromData(const FSpawnedHoleData& HoleData)
{
	if (!HoleShapeLibrary)
	{
		DiggerManager->EnsureHoleShapeLibrary();
		if (!HoleShapeLibrary)
		{
			if (DiggerDebug::Holes || DiggerDebug::Error)
			{
				UE_LOG(LogTemp, Error, TEXT("HoleShapeLibrary is not set in SpawnHoleFromData"));
			}
			return;
		}
	}

	if (!HoleBP)
	{
		EnsureDefaultHoleBP(); // works now
		if (!HoleBP)
		{
			if (DiggerDebug::Holes || DiggerDebug::Error)
			{
				UE_LOG(LogTemp, Error, TEXT("HoleBP is not set in SpawnHoleFromData"));
			}
			return;
		}
	}

	if (!GetWorld())
	{
		if (DiggerDebug::Context || DiggerDebug::Holes || DiggerDebug::Error)
		{
			UE_LOG(LogTemp, Error, TEXT("GetWorld() returned null in SpawnHoleFromData"));
		}
		return;
	}

	AActor* SpawnedHole = SpawnTransientActor(GetWorld(), HoleBP, HoleData.Location, HoleData.Rotation, HoleData.Scale);
	if (!SpawnedHole)
	{
		if (DiggerDebug::Holes || DiggerDebug::Error)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to spawn hole actor"));
		}
		return;
	}

	SpawnedHoleInstances.Add(SpawnedHole);

	UStaticMesh* HoleMesh = HoleShapeLibrary->GetMeshForShape(HoleData.Shape.ShapeType);
	if (HoleMesh)
	{
		if (UFunction* InitFunc = SpawnedHole->FindFunction(FName("InitializeHoleMesh")))
		{
			struct FInitHoleParams { UStaticMesh* Mesh; } Params{ HoleMesh };
			SpawnedHole->ProcessEvent(InitFunc, &Params);
		}
		else
		{
			if (DiggerDebug::Holes || DiggerDebug::Error)
			{
				UE_LOG(LogTemp, Warning, TEXT("InitializeHoleMesh not found on spawned hole actor"));
			}
		}
	}
	else
	{
		if (DiggerDebug::Holes || DiggerDebug::Error)
		{
			UE_LOG(LogTemp, Warning, TEXT("No mesh found for shape %s"), *UEnum::GetValueAsString(HoleData.Shape.ShapeType));
		}
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
	if (!DiggerDebug::Chunks || !DiggerDebug::Voxels)
		return;
	
	if (!SparseVoxelGrid)
	{
		if (DiggerDebug::Voxels || DiggerDebug::Error)
		{
			UE_LOG(LogTemp, Error, TEXT("SparseVoxelGrid is null in DebugPrintVoxelData"));
		}
		return;
	}

	if (DiggerDebug::Chunks || DiggerDebug::Voxels || DiggerDebug::Error)
	{
		UE_LOG(LogTemp, Log, TEXT("Voxel Data for Chunk at %s:"), *GetChunkCoordinates().ToString());
	}
	for (const auto& Pair : SparseVoxelGrid->VoxelData)
	{
		if (DiggerDebug::Voxels)
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
		if (DiggerDebug::Mesh)
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

	if (DiggerDebug::Mesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("UVoxelChunk::ForceUpdate - Starting mesh regeneration"));
	}
    
	// Perform the update (e.g., regenerate the mesh)
	GenerateMeshSyncronous();
        
	// Reset the dirty flag
	bIsDirty = false;

	if (DiggerDebug::Mesh)
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


void UVoxelChunk::GenerateMeshSyncronous() const
{
    // Verify we're on the game thread
    if (!IsInGameThread())
    {
    	if (DiggerDebug::Mesh)
    	{
    		UE_LOG(LogTemp, Error, TEXT("GenerateMeshSyncronous called from non-game thread! This will cause issues."));
    	}
    	return;
    }

    if (!SparseVoxelGrid)
    {
    	if (DiggerDebug::Mesh || DiggerDebug::Error || DiggerDebug::Voxels)
        UE_LOG(LogTemp, Error, TEXT("SparseVoxelGrid is null!"));
        return;
    }

	if (DiggerDebug::Mesh)
    UE_LOG(LogTemp, Warning, TEXT("GenerateMeshSyncronous - Starting mesh generation"));

    // --- Island Detection ---
    TArray<FIslandData> Islands = SparseVoxelGrid->DetectIslands(0.0f);
    if (Islands.Num() > 0)
    {
        if (DiggerDebug::Islands)
        {
            UE_LOG(LogTemp, Warning, TEXT("Island detection: %d islands found!"), Islands.Num());
        }
        for (int32 i = 0; i < Islands.Num(); ++i)
        {
            if (DiggerDebug::Islands)
            {
                UE_LOG(LogTemp, Warning, TEXT("  Island %d: %d voxels"), i, Islands[i].VoxelCount);
            }
        }
    }

    if (MarchingCubesGenerator == nullptr)
    {
        if (DiggerDebug::Mesh)
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
    	if (DiggerDebug::Mesh)
        UE_LOG(LogTemp, Warning, TEXT("Marching cubes mesh generation completed"));
        this->OnMarchingMeshComplete();
    });

	if (DiggerDebug::Mesh)
    UE_LOG(LogTemp, Warning, TEXT("Starting marching cubes generation"));
    MarchingCubesGenerator->GenerateMeshSyncronous(this);
}


bool UVoxelChunk::SaveChunkData(const FString& FilePath)
{
	FBufferArchive ToBinary;

	// Serialize voxel data first
	if (!SparseVoxelGrid || !SparseVoxelGrid->SerializeToArchive(ToBinary))
	{
		if (DiggerDebug::Chunks || DiggerDebug::Voxels)
		UE_LOG(LogTemp, Error, TEXT("Failed to serialize voxel grid"));
		return false;
	}

	// Serialize hole data count
	int32 HoleCount = SpawnedHoleInstances.Num();
	ToBinary << HoleCount;

	// Serialize each hole
	for (FSpawnedHoleData& Hole : HoleDataArray)
	{
		ToBinary << Hole;  // Use your operator<< for FSpawnedHoleData
	}

	// Save all to file
	if (FFileHelper::SaveArrayToFile(ToBinary, *FilePath))
	{
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
		if (DiggerDebug::IO)
		UE_LOG(LogTemp, Warning, TEXT("LoadChunkData: File does not exist: %s"), *FilePath);
		return false;
	}

	TArray<uint8> BinaryArray;
	if (!FFileHelper::LoadFileToArray(BinaryArray, *FilePath))
	{
		if (DiggerDebug::IO)
		{
			UE_LOG(LogTemp, Warning, TEXT("LoadChunkData: Failed to load file to array"));
		}
		return false;
	}

	FMemoryReader FromBinary = FMemoryReader(BinaryArray, true);
	FromBinary.Seek(0);

	// --- Load voxel grid ---
	if (!SparseVoxelGrid)
	{
		if (DiggerDebug::Voxels || DiggerDebug::IO)
		{
			UE_LOG(LogTemp, Error, TEXT("SparseVoxelGrid is null during load"));
		}
		return false;
	}

	// Temporarily deserialize into a temp voxel grid for merging
	USparseVoxelGrid* TempGrid = NewObject<USparseVoxelGrid>();
	if (!TempGrid->SerializeFromArchive(FromBinary))
	{
		if (DiggerDebug::IO)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to deserialize voxel grid from archive"));
		}
		return false;
	}

	if (bOverwrite)
	{
		SparseVoxelGrid->VoxelData = TempGrid->VoxelData; // Replace voxel data
		ClearSpawnedHoles();                              // Remove any existing hole actors
		HoleDataArray.Empty();                            // Clear existing saved hole data
	}
	else
	{
		for (const auto& Pair : TempGrid->VoxelData)
		{
			SparseVoxelGrid->VoxelData.Add(Pair.Key, Pair.Value); // Merge into existing
		}
	}

	// --- Deserialize hole data ---
	int32 HoleCount = 0;
	FromBinary << HoleCount;

	for (int32 i = 0; i < HoleCount; ++i)
	{
		FSpawnedHoleData Hole;
		FromBinary << Hole;

		HoleDataArray.Add(Hole);

		if (bOverwrite || !bOverwrite) // Always spawn when loading
		{
			SpawnHoleFromData(Hole);
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
			if (DiggerDebug::Holes)
			{
				UE_LOG(LogTemp, Log, TEXT("Loaded Default HoleBP from %s"), GDefaultHoleBPPath);
			}
		}
		else
		{
			if (DiggerDebug::Holes)
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
	    if (DiggerDebug::Holes)
	    {
		    UE_LOG(LogTemp, Error, TEXT("SpawnHole: No valid HoleBPClass after ensure"));
	    }
        return;
    }

    HoleBP = HoleBPClass;

    if (!World)
    {
	    if (DiggerDebug::Holes || DiggerDebug::Context)
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

    	if (DiggerDebug::Holes)
        UE_LOG(LogTemp, Log, TEXT("Spawned HoleBP at %s with shape %s"),
               *Location.ToString(),
               *UEnum::GetValueAsString(ShapeType));
    }
    else
    {
    	if (DiggerDebug::Holes)
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
    // Get the specific brush shape for this stroke type
	UVoxelBrushShape* BrushShape = (DiggerManager)
	? DiggerManager->GetBrushShapeForType(Stroke.BrushType)
	: nullptr;

	if (!BrushShape)
	{
		if (DiggerDebug::Brush || DiggerDebug::Error)
		UE_LOG(LogTemp, Error, TEXT("ApplyBrushStroke: No brush shape for type %d"), (int32)Stroke.BrushType);
		return;
	}


    if (!DiggerManager || !BrushShape || !SparseVoxelGrid)
    {
        if (DiggerDebug::Brush || DiggerDebug::Manager || DiggerDebug::Voxels || DiggerDebug::Error)
        {
            UE_LOG(LogTemp, Error, TEXT("Null pointer in UVoxelChunk::ApplyBrushStroke - DiggerManager: %s, BrushShape: %s, SparseVoxelGrid: %s"), 
                   DiggerManager ? TEXT("Valid") : TEXT("NULL"),
                   BrushShape ? TEXT("Valid") : TEXT("NULL"), 
                   SparseVoxelGrid ? TEXT("Valid") : TEXT("NULL"));
        }
        return;
    }

    // Initialize counters for tracking modifications
    FThreadSafeCounter VoxelsDugCounter;
    FThreadSafeCounter VoxelsAddedCounter;

    // Get chunk origin and voxel size - cache these values
    const FVector ChunkOrigin = FVoxelConversion::ChunkToWorld(ChunkCoordinates);
    const float CachedVoxelSize = FVoxelConversion::LocalVoxelSize;
    const int32 VoxelsPerChunk = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
    const float HalfChunkSize = (VoxelsPerChunk * CachedVoxelSize) * 0.5f;
    const float HalfVoxelSize = CachedVoxelSize * 0.5f;

    // Get brush-specific bounds from the brush shape itself
    FVector BrushBounds = CalculateBrushBounds(Stroke);

    // Convert brush bounds to voxel space
    const float VoxelSpaceBoundsX = BrushBounds.X / CachedVoxelSize;
    const float VoxelSpaceBoundsY = BrushBounds.Y / CachedVoxelSize;
    const float VoxelSpaceBoundsZ = BrushBounds.Z / CachedVoxelSize;

    // Convert brush position to local voxel coordinates
    const FVector LocalBrushPos = Stroke.BrushPosition - ChunkOrigin;
    const FIntVector VoxelCenter = FIntVector(
        FMath::FloorToInt((LocalBrushPos.X + HalfChunkSize) / CachedVoxelSize),
        FMath::FloorToInt((LocalBrushPos.Y + HalfChunkSize) / CachedVoxelSize),
        FMath::FloorToInt((LocalBrushPos.Z + HalfChunkSize) / CachedVoxelSize)
    );

    // Calculate bounding box for THIS chunk's domain INCLUDING overflow slab (-1 to VoxelsPerChunk)
    // This chunk is responsible for voxels from -1 to VoxelsPerChunk in each dimension
    const int32 MinX = FMath::Max(-1, FMath::FloorToInt(VoxelCenter.X - VoxelSpaceBoundsX));
    const int32 MaxX = FMath::Min(VoxelsPerChunk, FMath::CeilToInt(VoxelCenter.X + VoxelSpaceBoundsX));
    const int32 MinY = FMath::Max(-1, FMath::FloorToInt(VoxelCenter.Y - VoxelSpaceBoundsY));
    const int32 MaxY = FMath::Min(VoxelsPerChunk, FMath::CeilToInt(VoxelCenter.Y + VoxelSpaceBoundsY));
    const int32 MinZ = FMath::Max(-1, FMath::FloorToInt(VoxelCenter.Z - VoxelSpaceBoundsZ));
    const int32 MaxZ = FMath::Min(VoxelsPerChunk, FMath::CeilToInt(VoxelCenter.Z + VoxelSpaceBoundsZ));

    // Calculate sizes for each dimension
    const int32 SizeX = MaxX - MinX + 1;
    const int32 SizeY = MaxY - MinY + 1;
    const int32 SizeZ = MaxZ - MinZ + 1;

    // Check for valid sizes before any allocation or work
    if (SizeX <= 0 || SizeY <= 0 || SizeZ <= 0)
    {
        if (DiggerDebug::Error)
        {
            UE_LOG(LogTemp, Warning, TEXT("ApplyBrushStroke: Invalid brush bounds: SizeX=%d SizeY=%d SizeZ=%d (MinX=%d MaxX=%d MinY=%d MaxY=%d MinZ=%d MaxZ=%d)"), 
                SizeX, SizeY, SizeZ, MinX, MaxX, MinY, MaxY, MinZ, MaxZ);
        }
        return;
    }

    // Thread-safe array for air voxels below terrain
    TArray<FIntVector> AirVoxelsBelowTerrain;

    // Pre-filter voxels and compute terrain heights on game thread using precise queries
    struct FVoxelInfo
    {
        FIntVector Coords;
        FVector WorldPos;
        float TerrainHeight;
    };
    
    TArray<FVoxelInfo> ValidVoxels;
    
    for (int32 X = MinX; X <= MaxX; ++X)
    {
        for (int32 Y = MinY; Y <= MaxY; ++Y)
        {
            for (int32 Z = MinZ; Z <= MaxZ; ++Z)
            {
                // Convert voxel coordinates to center-aligned world position
                const FVector WorldPos = ChunkOrigin + FVector(
                    (X * CachedVoxelSize) - HalfChunkSize + HalfVoxelSize,
                    (Y * CachedVoxelSize) - HalfChunkSize + HalfVoxelSize,
                    (Z * CachedVoxelSize) - HalfChunkSize + HalfVoxelSize
                );

                // Let the brush shape itself determine if this voxel is relevant
                // Use the brush shape's bounds checking method
                if (!BrushShape->IsWithinBounds(WorldPos, Stroke))
                {
                    continue;
                }

                // Get precise landscape height using modified DiggerManager method (cache-free)
                TOptional<float> Height = DiggerManager->SampleLandscapeHeight(DiggerManager->GetLandscapeProxyAt(WorldPos), WorldPos);
                float TerrainHeight = Height.IsSet() ? Height.GetValue() : -10000000.f;

                // Store voxel info for parallel processing
                FVoxelInfo VoxelInfo;
                VoxelInfo.Coords = FIntVector(X, Y, Z);
                VoxelInfo.WorldPos = WorldPos;
                VoxelInfo.TerrainHeight = TerrainHeight;
                
                ValidVoxels.Add(VoxelInfo);
            }
        }
    }

    // Process valid voxels in parallel - Let brush shape determine everything
    ParallelFor(ValidVoxels.Num(), [&](int32 VoxelIndex)
    {
        const FVoxelInfo& VoxelInfo = ValidVoxels[VoxelIndex];
        const FIntVector& Coords = VoxelInfo.Coords;
        const FVector& WorldPos = VoxelInfo.WorldPos;
        const float TerrainHeight = VoxelInfo.TerrainHeight;
        const bool bAboveTerrain = WorldPos.Z >= TerrainHeight;

        // Calculate SDF value using the specific brush shape with precise terrain height
        const float SDF = BrushShape->CalculateSDF(WorldPos, Stroke, TerrainHeight);

        // Let the brush shape's SDF completely determine voxel creation
        if (Stroke.bDig)
        {
            // Create air where SDF indicates we're inside the shape
            if (SDF > 0.1f) // Only use SDF threshold, no distance override
            {
                // Add depth validation to prevent far-off subterranean voxels
                const float MaxDepthBelowBrush = Stroke.BrushRadius * 1.5f;
                const float VerticalDistanceFromBrush = FMath::Abs(WorldPos.Z - Stroke.BrushPosition.Z);
                
                if (VerticalDistanceFromBrush <= MaxDepthBelowBrush)
                {
                    bool Removed = SparseVoxelGrid->SetVoxel(Coords.X, Coords.Y, Coords.Z, SDF, true); // true = EXPLICIT AIR
                    Removed? VoxelsDugCounter.Increment() : false; // Handle it if nothing was Removed.
                    
                    // Track air voxels below terrain for solid shell creation
                    if (!bAboveTerrain)
                    {
                        FScopeLock Lock(&BrushStrokeMutex);
                        AirVoxelsBelowTerrain.Add(Coords);
                    }
                }
            }
        }
        else
        {
            // Create solid where SDF indicates
            if (SDF < -0.1f) // Only use SDF threshold, no distance override
            {
                bool Added = SparseVoxelGrid->SetVoxel(Coords.X, Coords.Y, Coords.Z, SDF, false); // false = solid
                Added ? VoxelsAddedCounter.Increment() : false; // Handle it right if nothing was added
            }
        }
    });

    // Track shell voxels separately if needed
    int32 ShellVoxelsAdded = 0;
    
    // Create shell of solid voxels around air voxels below terrain
    // This is ESSENTIAL for marching cubes to detect isosurfaces
    if (!AirVoxelsBelowTerrain.IsEmpty())
    {
    	CreateSolidShellAroundAirVoxels(AirVoxelsBelowTerrain, Stroke.bHiddenSeam);
    }

    // Get final counts
    const int32 FinalVoxelsDug = VoxelsDugCounter.GetValue();
    const int32 FinalVoxelsAdded = VoxelsAddedCounter.GetValue();

    // Create and broadcast modification report
    if (FinalVoxelsDug > 0 || FinalVoxelsAdded > 0)
    {
        FVoxelModificationReport Report;
        Report.VoxelsDug = FinalVoxelsDug;
        Report.VoxelsAdded = FinalVoxelsAdded;
        Report.ChunkCoordinates = ChunkCoordinates;
        Report.BrushPosition = Stroke.BrushPosition;
        Report.BrushRadius = Stroke.BrushRadius;

        // Broadcast the modification report through the DiggerManager
    	if (DiggerManager)
    	{
    		DiggerManager->OnVoxelsModified.Broadcast(Report);
		    //if (DiggerDebug::Chunks || DiggerDebug::Manager)
		    {
    			int TotalVoxelsDug=Report.VoxelsDug;
    			if (DiggerDebug::Chunks || DiggerDebug::VoxelModificationReports)
			    UE_LOG(LogTemp, Warning, TEXT("[ApplyBrushStroke] Voxel Modification report Broadcast on an instance of UVoxelChunk. Voxels Modification Report: %d"), TotalVoxelsDug);
		    }
    	}
    	else
    	{
    		if (DiggerDebug::Chunks || DiggerDebug::Manager)
    		{
    			UE_LOG(LogTemp, Error, TEXT("[ApplyBrushStroke] Manager Missing in ApplyBrushStroke in an instance UVoxelChunk."));
    		}
    	}

        // Optional: Log performance info in debug builds
        #if UE_BUILD_DEBUG
        if (DiggerDebug::Chunks || DiggerDebug::Voxels)
        {
            UE_LOG(LogTemp, Message, TEXT("ApplyBrushStroke in chunk %s using %s brush: Dug %d voxels, Added %d voxels"), 
                   *ChunkCoordinates.ToString(), 
                   *UEnum::GetValueAsString(Stroke.BrushType),
                   FinalVoxelsDug,
                   FinalVoxelsAdded
                   );
        }
        #endif
    }
}



// Update the method signature in your header file (VoxelChunk.h):
// int32 CreateSolidShellAroundAirVoxels(const TArray<FIntVector>& AirVoxels, bool bHiddenSeam);

void UVoxelChunk::CreateSolidShellAroundAirVoxels(const TArray<FIntVector>& AirVoxels, bool bHiddenSeam)
{
    // DEBUG: Log the seam mode being used
	if (DiggerDebug::Seams)
    UE_LOG(LogTemp, Warning, TEXT("CreateSolidShellAroundAirVoxels: Using %s seam for %d air voxels in chunk %s"), 
           bHiddenSeam ? TEXT("HIDDEN") : TEXT("NATURAL"), AirVoxels.Num(), *ChunkCoordinates.ToString());
           
    if (AirVoxels.IsEmpty())
    {
        return;
    }

    const FVector ChunkOrigin = FVoxelConversion::ChunkToWorld(ChunkCoordinates);
    const float CachedVoxelSize = FVoxelConversion::LocalVoxelSize;
    const int32 VoxelsPerChunk = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
    const float HalfChunkSize = (VoxelsPerChunk * CachedVoxelSize) * 0.5f;
    const float HalfVoxelSize = CachedVoxelSize * 0.5f;

    // Cache landscape proxies and heights
    TMap<FIntVector, ALandscapeProxy*> ProxyCache;
    TMap<FVector, float> HeightCache;

    // Convert air voxels array to set for fast lookup
    TSet<FIntVector> AirVoxelSet(AirVoxels);

    // Find boundary positions: neighbors of air voxels that are NOT air voxels
    TSet<FIntVector> BoundaryPositions;
    
    // Extended neighbor offsets for wider shell detection (including diagonal connections)
    const TArray<FIntVector> NeighborOffsets = []()
    {
        TArray<FIntVector> Offsets;
        for (int32 x = -2; x <= 2; x++)
        {
            for (int32 y = -2; y <= 2; y++)
            {
                for (int32 z = -2; z <= 2; z++)
                {
                    if (x == 0 && y == 0 && z == 0) continue; // Skip center
                    Offsets.Add(FIntVector(x, y, z));
                }
            }
        }
        return Offsets;
    }();

    // Find all boundary positions (shell candidates)
    for (const FIntVector& AirVoxel : AirVoxels)
    {
        for (const FIntVector& Offset : NeighborOffsets)
        {
            const FIntVector BoundaryCandidate = AirVoxel + Offset;
            
            // Skip if this position is also an air voxel IN THIS CHUNK
            if (AirVoxelSet.Contains(BoundaryCandidate))
                continue;
                
            // IMPORTANT: Don't skip if it's an air voxel in ANOTHER chunk
            // Check if it's an air voxel in this chunk's sparse grid
            if (SparseVoxelGrid->VoxelData.Contains(BoundaryCandidate))
            {
                const FVoxelData& ExistingData = SparseVoxelGrid->VoxelData[BoundaryCandidate];
                if (ExistingData.SDFValue > 0.0f) // It's air in this chunk
                    continue;
                if (ExistingData.SDFValue <= FVoxelConversion::SDF_SOLID) // Already solid
                    continue;
            }
                
            // This is a valid boundary position
            BoundaryPositions.Add(BoundaryCandidate);
        }
    }

    // DEBUG: Log boundary positions found
<<<<<<< Updated upstream
	if (DiggerDebug::Seams)
    UE_LOG(LogTemp, Warning, TEXT("Found %d boundary positions for %s seam"), 
           BoundaryPositions.Num(), bHiddenSeam ? TEXT("HIDDEN") : TEXT("NATURAL"));
=======

    if (DiggerDebug::Seams)
    {
	    UE_LOG(LogTemp, Warning, TEXT("Found %d boundary positions for %s seam"), 
	           BoundaryPositions.Num(), bHiddenSeam ? TEXT("HIDDEN") : TEXT("NATURAL"));
    }
>>>>>>> Stashed changes

    // Rim thickness calculation function
    auto GetRimThickness = [&, CachedVoxelSize](const FVector& WorldPos) -> float
    {
        // Get cached landscape proxy
        const FIntVector ProxyCacheKey = FIntVector(WorldPos.X / 1000.0f, WorldPos.Y / 1000.0f, 0);
        ALandscapeProxy* LandscapeProxy = nullptr;
        
        if (ALandscapeProxy** CachedProxy = ProxyCache.Find(ProxyCacheKey))
        {
            LandscapeProxy = *CachedProxy;
        }
        else
        {
            LandscapeProxy = DiggerManager->GetLandscapeProxyAt(WorldPos);
            ProxyCache.Add(ProxyCacheKey, LandscapeProxy);
        }

        TOptional<float> TerrainHeightOptional = DiggerManager->SampleLandscapeHeight(
            LandscapeProxy, WorldPos, true);

        if (!TerrainHeightOptional.IsSet())
        {
            return 1.0f;
        }

        float TerrainHeight = TerrainHeightOptional.GetValue();
        HeightCache.Add(WorldPos, TerrainHeight);

        // Multi-directional slope calculation
        const TArray<FVector> SampleOffsets = {
            FVector(CachedVoxelSize, 0, 0),
            FVector(0, CachedVoxelSize, 0),
            FVector(-CachedVoxelSize, 0, 0),
            FVector(0, -CachedVoxelSize, 0)
        };

        float MaxSlope = 0.0f;
        int32 ValidSamples = 0;

        for (const FVector& Offset : SampleOffsets)
        {
            const FVector SamplePos = WorldPos + Offset;
            
            if (float* CachedSampleHeight = HeightCache.Find(SamplePos))
            {
                const float HeightDiff = FMath::Abs(TerrainHeight - *CachedSampleHeight);
                const float Slope = HeightDiff / CachedVoxelSize;
                MaxSlope = FMath::Max(MaxSlope, Slope);
                ValidSamples++;
                continue;
            }

            TOptional<float> SampleHeightOptional = DiggerManager->SampleLandscapeHeight(
                LandscapeProxy, SamplePos, true);

            if (SampleHeightOptional.IsSet())
            {
                float SampleHeight = SampleHeightOptional.GetValue();
                HeightCache.Add(SamplePos, SampleHeight);
                
                const float HeightDiff = FMath::Abs(TerrainHeight - SampleHeight);
                const float Slope = HeightDiff / CachedVoxelSize;
                MaxSlope = FMath::Max(MaxSlope, Slope);
                ValidSamples++;
            }
        }

        if (ValidSamples == 0)
        {
            return 1.0f;
        }

        if (MaxSlope > 0.1f)
        {
            float RimThickness = FMath::Clamp(MaxSlope * MaxSlope * 4.0f + 1.0f, 3.0f, 11.0f);
            return RimThickness;
        }
        
        return 1.0f;
    };

    // Process each boundary position to determine if it should be solid
    int32 CreatedVoxels = 0;
    int32 SkippedRimVoxels = 0;
    
    for (const FIntVector& BoundaryPos : BoundaryPositions)
    {
        // Bounds check
        if (BoundaryPos.X < -1 || BoundaryPos.X >= VoxelsPerChunk + 1 ||
            BoundaryPos.Y < -1 || BoundaryPos.Y >= VoxelsPerChunk + 1 ||
            BoundaryPos.Z < -1 || BoundaryPos.Z >= VoxelsPerChunk + 1)
            continue;

        // Calculate world position
        const FVector WorldPos = ChunkOrigin + FVector(
            (BoundaryPos.X * CachedVoxelSize) - HalfChunkSize + HalfVoxelSize,
            (BoundaryPos.Y * CachedVoxelSize) - HalfChunkSize + HalfVoxelSize,
            (BoundaryPos.Z * CachedVoxelSize) - HalfChunkSize + HalfVoxelSize
        );

        // Get terrain height
        float TerrainHeight = DiggerManager->GetLandscapeHeightAt(WorldPos);
        const float RimThickness = GetRimThickness(WorldPos);
        
        // Enhanced rim height consistency logic with seam type control
        const float VoxelCenterZ = WorldPos.Z;
        const float VoxelBottomZ = VoxelCenterZ - HalfVoxelSize;
        const float VoxelTopZ = VoxelCenterZ + HalfVoxelSize;
        
        // Calculate terrain-relative positioning with consistent thresholds
        const float TerrainToVoxelBottom = VoxelBottomZ - TerrainHeight;
        const float TerrainToVoxelCenter = VoxelCenterZ - TerrainHeight;
        
        bool ShouldCreateVoxel = false;
        
        // Layer 1: Base solid layer - adjust based on seam type
        if (bHiddenSeam)
        {
            // HIDDEN SEAM: Only create if voxel is a full voxel below terrain (floor behavior)
            if (TerrainToVoxelBottom <= -CachedVoxelSize) // Voxel bottom must be a full voxel below terrain
            {
                ShouldCreateVoxel = true;
            }
        }
        else
        {
            // NATURAL SEAM: Create if voxel intersects terrain surface (original behavior)
            if (TerrainToVoxelBottom <= HalfVoxelSize)
            {
                ShouldCreateVoxel = true;
            }
        }
        // Layer 2: Seam type determines rim behavior
        if (!bHiddenSeam && // Only create raised rim for Natural seam
                 TerrainToVoxelCenter <= (RimThickness * CachedVoxelSize) && 
                 TerrainToVoxelBottom <= (CachedVoxelSize * 1.5f))
        {
            // NATURAL SEAM: Create raised rim with enhanced connectivity check
            int32 SolidConnections = 0;
            int32 TerrainConnections = 0;
            
            const TArray<FIntVector> FaceOffsets = {
                FIntVector(1,0,0), FIntVector(-1,0,0),
                FIntVector(0,1,0), FIntVector(0,-1,0),
                FIntVector(0,0,1), FIntVector(0,0,-1)
            };
            
            for (const FIntVector& FaceOffset : FaceOffsets)
            {
                const FIntVector ConnectionCoord = BoundaryPos + FaceOffset;
                
                const FVector ConnectionWorldPos = ChunkOrigin + FVector(
                    (ConnectionCoord.X * CachedVoxelSize) - HalfChunkSize + HalfVoxelSize,
                    (ConnectionCoord.Y * CachedVoxelSize) - HalfChunkSize + HalfVoxelSize,
                    (ConnectionCoord.Z * CachedVoxelSize) - HalfChunkSize + HalfVoxelSize
                );
                
                float ConnectionTerrainHeight = DiggerManager->GetLandscapeHeightAt(ConnectionWorldPos);
                const float ConnectionTerrainDistance = (ConnectionWorldPos.Z - HalfVoxelSize) - ConnectionTerrainHeight;
                
                if (ConnectionTerrainDistance <= HalfVoxelSize)
                {
                    SolidConnections++;
                    TerrainConnections++;
                }
                
                if (SparseVoxelGrid->VoxelData.Contains(ConnectionCoord))
                {
                    const FVoxelData& ExistingData = SparseVoxelGrid->VoxelData[ConnectionCoord];
                    if (ExistingData.SDFValue <= FVoxelConversion::SDF_SOLID)
                    {
                        SolidConnections++;
                    }
                }
                else if (BoundaryPositions.Contains(ConnectionCoord))
                {
                    SolidConnections++;
                }
            }
            
            // Stricter connectivity requirements for rim voxels to eliminate floating
            bool HasStrongConnection = false;
            
            if (TerrainConnections >= 3)
            {
                HasStrongConnection = true;
            }
            else if (TerrainConnections >= 2 && SolidConnections >= 3)
            {
                HasStrongConnection = true;
            }
            else if (SolidConnections >= 4)
            {
                HasStrongConnection = true;
            }
            
            // Additional vertical support check
            bool HasVerticalSupport = false;
            const FIntVector DownNeighbor = BoundaryPos + FIntVector(0, 0, -1);
            
            if (SparseVoxelGrid->VoxelData.Contains(DownNeighbor))
            {
                const FVoxelData& DownData = SparseVoxelGrid->VoxelData[DownNeighbor];
                if (DownData.SDFValue <= FVoxelConversion::SDF_SOLID)
                {
                    HasVerticalSupport = true;
                }
            }
            else if (BoundaryPositions.Contains(DownNeighbor))
            {
                HasVerticalSupport = true;
            }
            
            const FVector DownWorldPos = ChunkOrigin + FVector(
                (DownNeighbor.X * CachedVoxelSize) - HalfChunkSize + HalfVoxelSize,
                (DownNeighbor.Y * CachedVoxelSize) - HalfChunkSize + HalfVoxelSize,
                (DownNeighbor.Z * CachedVoxelSize) - HalfChunkSize + HalfVoxelSize
            );
            float DownTerrainHeight = DiggerManager->GetLandscapeHeightAt(DownWorldPos);
            if ((DownWorldPos.Z - HalfVoxelSize) <= DownTerrainHeight + HalfVoxelSize)
            {
                HasVerticalSupport = true;
            }
            
            if (HasStrongConnection && HasVerticalSupport)
            {
                ShouldCreateVoxel = true;
            }
        }
        // HIDDEN SEAM: No raised rim - only create voxels at/below terrain
        else if (bHiddenSeam && TerrainToVoxelBottom > HalfVoxelSize)
        {
            // Don't create this voxel - it's above terrain and we want hidden seam
            ShouldCreateVoxel = false;
            SkippedRimVoxels++;
        }

        if (ShouldCreateVoxel)
        {
            // Enhanced bounds checking - allow overflow for seamless transitions
            bool InMainChunk = (BoundaryPos.X >= 0 && BoundaryPos.X < VoxelsPerChunk &&
                               BoundaryPos.Y >= 0 && BoundaryPos.Y < VoxelsPerChunk &&
                               BoundaryPos.Z >= 0 && BoundaryPos.Z < VoxelsPerChunk);
            
            bool InOverflowRegion = (BoundaryPos.X >= -1 && BoundaryPos.X <= VoxelsPerChunk &&
                                    BoundaryPos.Y >= -1 && BoundaryPos.Y <= VoxelsPerChunk &&
                                    BoundaryPos.Z >= -1 && BoundaryPos.Z <= VoxelsPerChunk);
            
            if (InMainChunk || InOverflowRegion)
            {
                bool Added = SparseVoxelGrid->SetVoxel(BoundaryPos, FVoxelConversion::SDF_SOLID, false);
				Added? CreatedVoxels++ : false; /*nothing was added.*/
            }
        }
    }
    
    // DEBUG: Final summary
<<<<<<< Updated upstream
	if (DiggerDebug::Seams)
=======
	if (DiggerDebug::Seams || DiggerDebug::Voxels)
>>>>>>> Stashed changes
    UE_LOG(LogTemp, Warning, TEXT("Shell creation complete: %s seam - Created %d voxels, Skipped %d rim voxels"), 
           bHiddenSeam ? TEXT("HIDDEN") : TEXT("NATURAL"), CreatedVoxels, SkippedRimVoxels);
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




void UVoxelChunk::GenerateMesh() const
{
	if (!SparseVoxelGrid)
	{
		if (DiggerDebug::Voxels)
		UE_LOG(LogTemp, Error, TEXT("SparseVoxelGrid is null!"));
		return;
	}

	
	// --- Island Detection ---
	TArray<FIslandData> Islands = SparseVoxelGrid->DetectIslands(0.0f);
	if (Islands.Num() > 0)
	{
		if (DiggerDebug::Islands)
		{UE_LOG(LogTemp, Warning, TEXT("Island detection: %d islands found!"), Islands.Num());}
	    for (int32 i = 0; i < Islands.Num(); ++i)
	    {
	        if (DiggerDebug::Islands)
	        {UE_LOG(LogTemp, Warning, TEXT("  Island %d: %d voxels"), i, Islands[i].VoxelCount);}
	    }
	}


	if (MarchingCubesGenerator == nullptr)
	{
		if (DiggerDebug::Mesh)
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
