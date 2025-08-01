#include "VoxelChunk.h"

#include "DiggerDebug.h"
#include "DiggerManager.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "HLSLTypeAliases.h"
#include "MarchingCubes.h"
#include "SparseVoxelGrid.h"
#include "DiggerProUnreal/Public/Voxel/VoxelBrushHelpers.h" // or wherever you put SetDigShellVoxels
#include "VoxelBrushShape.h"
#include "VoxelBrushTypes.h"
#include "VoxelConversion.h"
#include "VoxelLogManager.h"
#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDeviceNull.h"
#include "Serialization/BufferArchive.h"
#include "Voxel/BrushShapes/CapsuleBrushShape.h"
#include "Voxel/BrushShapes/ConeBrushShape.h"
#include "Voxel/BrushShapes/CubeBrushShape.h"
#include "Voxel/BrushShapes/CylinderBrushShape.h"
#include "Voxel/BrushShapes/IcosphereBrushShape.h"
#include "Voxel/BrushShapes/NoiseBrushShape.h"
#include "Voxel/BrushShapes/PyramidBrushShape.h"
#include "Voxel/BrushShapes/SmoothBrushShape.h"
#include "Voxel/BrushShapes/SphereBrushShape.h"
#include "Voxel/BrushShapes/TorusBrushShape.h"


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
		UE_LOG(LogTemp, Error, TEXT("HoleShapeLibrary is not set in SpawnHoleFromData"));
		return;
	}
	
    if (!HoleBP)
    {
        UE_LOG(LogTemp, Error, TEXT("HoleBP is not set in SpawnHoleFromData"));
    }
    if (!GetWorld())
    {
        UE_LOG(LogTemp, Error, TEXT("GetWorld() returned null in SpawnHoleFromData"));
    }

    if (!HoleBP || !GetWorld())
    {
    	if (DiggerDebug::Chunks || DiggerDebug::Holes)
        UE_LOG(LogTemp, Error, TEXT("Missing dependencies in SpawnHoleFromData"));
        return;
    }

    AActor* SpawnedHole = SpawnTransientActor(GetWorld(), HoleBP, HoleData.Location, HoleData.Rotation, HoleData.Scale);
    if (!SpawnedHole)
    {
    	if (DiggerDebug::Holes)
        UE_LOG(LogTemp, Error, TEXT("Failed to spawn hole actor"));
        return;
    }

    // Add the spawned actor to the tracking list
    SpawnedHoleInstances.Add(SpawnedHole);

    // Get the corresponding mesh for the hole shape
    UStaticMesh* HoleMesh = HoleShapeLibrary->GetMeshForShape(HoleData.Shape.ShapeType);
    if (HoleMesh)
    {
        // Call the InitializeHoleMesh function on the BP actor
        if (UFunction* InitFunc = SpawnedHole->FindFunction(FName("InitializeHoleMesh")))
        {
            struct FInitHoleParams
            {
                UStaticMesh* Mesh;
            };

        	if (DiggerDebug::Holes)
            UE_LOG(LogTemp, Warning, TEXT("Spawned a hole!"));

            FInitHoleParams Params{ HoleMesh };
            SpawnedHole->ProcessEvent(InitFunc, &Params);
        }
        else
        {
        	if (DiggerDebug::Holes)
            UE_LOG(LogTemp, Warning, TEXT("InitializeHoleMesh not found on spawned hole actor"));
        }
    }
    else
    {
    	if (DiggerDebug::Holes)
        UE_LOG(LogTemp, Warning, TEXT("No mesh found for shape %s"), *UEnum::GetValueAsString(HoleData.Shape.ShapeType));
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


void UVoxelChunk::DebugDrawChunk()
{
	if (!World) World = DiggerManager->GetWorldFromManager();
	if (!World) return;

	const float DebugDuration = 35.0f;

	// Core chunk bounds
	const FVector ChunkCenter = FVector(ChunkCoordinates) * ChunkSize * TerrainGridSize;
	const FVector ChunkExtent = FVector(ChunkSize * TerrainGridSize / 2.0f);
	

	// Draw core chunk (red)
	DrawDebugBox(World, ChunkCenter, ChunkExtent, FColor::Red, false, DebugDuration);
}




void UVoxelChunk::DebugPrintVoxelData() const
{
	if (!DiggerDebug::Chunks || !DiggerDebug::Voxels)
		return;
	
	if (!SparseVoxelGrid)
	{
		UE_LOG(LogTemp, Error, TEXT("SparseVoxelGrid is null in DebugPrintVoxelData"));
		return;
	}
 
	UE_LOG(LogTemp, Log, TEXT("Voxel Data for Chunk at %s:"), *GetChunkPosition().ToString());
	for (const auto& Pair : SparseVoxelGrid->VoxelData)
	{
		UE_LOG(LogTemp, Log, TEXT("Voxel at (%d,%d,%d): Value = %f"),
			Pair.Key.X, Pair.Key.Y, Pair.Key.Z, Pair.Value.SDFValue);
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
		UE_LOG(LogTemp, Warning, TEXT("ForceUpdate called from non-game thread, dispatching to game thread"));
		AsyncTask(ENamedThreads::GameThread, [this]()
		{
			if (IsValid(this))
			{
				this->ForceUpdate();
			}
		});
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("UVoxelChunk::ForceUpdate - Starting mesh regeneration"));
    
	// Perform the update (e.g., regenerate the mesh)
	GenerateMeshSyncronous();
        
	// Reset the dirty flag
	bIsDirty = false;
    
	UE_LOG(LogTemp, Warning, TEXT("UVoxelChunk::ForceUpdate - Completed"));
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
        UE_LOG(LogTemp, Error, TEXT("GenerateMeshSyncronous called from non-game thread! This will cause issues."));
        return;
    }

    if (!SparseVoxelGrid)
    {
        UE_LOG(LogTemp, Error, TEXT("SparseVoxelGrid is null!"));
        return;
    }

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
        UE_LOG(LogTemp, Warning, TEXT("Marching cubes mesh generation completed"));
        this->OnMarchingMeshComplete();
    });

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

void UVoxelChunk::SpawnHole(TSubclassOf<AActor> HoleBPClass, FVector Location, FRotator Rotation, FVector Scale, EHoleShapeType ShapeType)
{
	if (!HoleBPClass)
	{
		if (DiggerDebug::Holes)
		{
			UE_LOG(LogTemp, Error, TEXT("SpawnHole: Invalid HoleBPClass"));
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
		// ✅ Construct HoleShape and assign it to HoleData
		FHoleShape Shape;
		Shape.ShapeType = ShapeType;

		FSpawnedHoleData HoleData{ Location, Rotation, Scale };
		HoleData.Shape = Shape;

		HoleDataArray.Add(HoleData);


		if (DiggerDebug::Holes || DiggerDebug::Chunks)
		{
			UE_LOG(LogTemp, Log, TEXT("Spawned HoleBP in Chunk at %s with shape %s"),
				*Location.ToString(),
				*UEnum::GetValueAsString(ShapeType));
		}
	}
	else
	{
		if (DiggerDebug::Holes || DiggerDebug::Chunks)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to spawn HoleBP in Chunk at %s"), *Location.ToString());
		}
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

void UVoxelChunk::InitializeBrushShapes()
{
	if (CachedBrushShapes.Num() == 0)
	{
		CachedBrushShapes.Add(EVoxelBrushType::Sphere, NewObject<USphereBrushShape>(this));
		CachedBrushShapes.Add(EVoxelBrushType::Cube, NewObject<UCubeBrushShape>(this));
		CachedBrushShapes.Add(EVoxelBrushType::Cylinder, NewObject<UCylinderBrushShape>(this));
		CachedBrushShapes.Add(EVoxelBrushType::Cone, NewObject<UConeBrushShape>(this));
		CachedBrushShapes.Add(EVoxelBrushType::Capsule, NewObject<UCapsuleBrushShape>(this));
		CachedBrushShapes.Add(EVoxelBrushType::Torus, NewObject<UTorusBrushShape>(this));
		CachedBrushShapes.Add(EVoxelBrushType::Pyramid, NewObject<UPyramidBrushShape>(this));
		CachedBrushShapes.Add(EVoxelBrushType::Icosphere, NewObject<UIcosphereBrushShape>(this));
		CachedBrushShapes.Add(EVoxelBrushType::Smooth, NewObject<USmoothBrushShape>(this));
		CachedBrushShapes.Add(EVoxelBrushType::Noise, NewObject<UNoiseBrushShape>(this));
	}
}

UVoxelBrushShape* UVoxelChunk::GetBrushShapeForType(EVoxelBrushType BrushType)
{
	InitializeBrushShapes();
    
	UVoxelBrushShape** FoundShape = CachedBrushShapes.Find(BrushType);
	if (FoundShape && *FoundShape)
	{
		return *FoundShape;
	}
    
	UE_LOG(LogTemp, Warning, TEXT("Unknown brush type %d, defaulting to Sphere"), (int32)BrushType);
	return CachedBrushShapes[EVoxelBrushType::Sphere];
}

void UVoxelChunk::MulticastApplyBrushStroke_Implementation(const FBrushStroke& Stroke)
{
	ApplyBrushStroke(Stroke);
}

void UVoxelChunk::ApplyBrushStroke(const FBrushStroke& Stroke)
{
    // Get the specific brush shape for this stroke type
    UVoxelBrushShape* BrushShape = GetBrushShapeForType(Stroke.BrushType);

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

    // Get chunk origin and voxel size - cache these values
    const FVector ChunkOrigin = FVoxelConversion::ChunkToWorld(ChunkCoordinates);
    const float CachedVoxelSize = FVoxelConversion::LocalVoxelSize;
    const int32 VoxelsPerChunk = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
    const float HalfChunkSize = (VoxelsPerChunk * CachedVoxelSize) * 0.5f;
    const float HalfVoxelSize = CachedVoxelSize * 0.5f;

    // NEW: Get brush-specific bounds instead of just using radius
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

    // Calculate bounding box with overflow support using brush-specific bounds
    const int32 MinX = FMath::Max(-1, FMath::FloorToInt(VoxelCenter.X - VoxelSpaceBoundsX));
    const int32 MaxX = FMath::Min(VoxelsPerChunk, FMath::CeilToInt(VoxelCenter.X + VoxelSpaceBoundsX));
    const int32 MinY = FMath::Max(-1, FMath::FloorToInt(VoxelCenter.Y - VoxelSpaceBoundsY));
    const int32 MaxY = FMath::Min(VoxelsPerChunk, FMath::CeilToInt(VoxelCenter.Y + VoxelSpaceBoundsY));
    const int32 MinZ = FMath::Max(-1, FMath::FloorToInt(VoxelCenter.Z - VoxelSpaceBoundsZ));
    const int32 MaxZ = FMath::Min(VoxelsPerChunk, FMath::CeilToInt(VoxelCenter.Z + VoxelSpaceBoundsZ));

    // Calculate safe sizes for each dimension
    const int32 SizeX = MaxX - MinX + 1;
    const int32 SizeY = MaxY - MinY + 1;
    const int32 SizeZ = MaxZ - MinZ + 1;

    // Bulletproof: Check for valid sizes before any allocation or work
    if (SizeX <= 0 || SizeY <= 0 || SizeZ <= 0)
    {
        if (DiggerDebug::Error)
        {
            UE_LOG(LogTemp, Warning, TEXT("ApplyBrushStroke: Invalid brush bounds: SizeX=%d SizeY=%d SizeZ=%d (MinX=%d MaxX=%d MinY=%d MaxY=%d MinZ=%d MaxZ=%d)"), 
                SizeX, SizeY, SizeZ, MinX, MaxX, MinY, MaxY, MinZ, MaxZ);
        }
        return;
    }

    // Pre-calculate values for optimization - use the largest bound for fast distance check
    const float MaxBrushBound = FMath::Max3(BrushBounds.X, BrushBounds.Y, BrushBounds.Z);
    const float MaxBrushBoundSq = MaxBrushBound * MaxBrushBound;
    const float SDF_AIR_Strength = FVoxelConversion::SDF_AIR * Stroke.BrushStrength;

    // Thread-safe counter and array
    FThreadSafeCounter ModifiedVoxels;
    TArray<FIntVector> AirVoxelsBelowTerrain;

    // --- Precompute terrain heights on the game thread ---
    int64 TotalVoxels = static_cast<int64>(SizeX) * static_cast<int64>(SizeY) * static_cast<int64>(SizeZ);
    if (TotalVoxels > 100000000) // Arbitrary sanity limit, adjust as needed
    {
        UE_LOG(LogTemp, Error, TEXT("ApplyBrushStroke: Refusing to allocate TerrainHeights for %lld voxels (SizeX=%d SizeY=%d SizeZ=%d)"), TotalVoxels, SizeX, SizeY, SizeZ);
        return;
    }

    TArray<float> TerrainHeights;
    TerrainHeights.SetNumUninitialized(static_cast<int32>(TotalVoxels));

    // Helper lambda for safe flat index calculation
    auto GetFlatIndex = [=](int32 X, int32 Y, int32 Z) -> int32
    {
        int32 x = X - MinX;
        int32 y = Y - MinY;
        int32 z = Z - MinZ;
        check(x >= 0 && x < SizeX);
        check(y >= 0 && y < SizeY);
        check(z >= 0 && z < SizeZ);
        return (x * SizeY * SizeZ) + (y * SizeZ) + z;
    };

    for (int32 X = MinX; X <= MaxX; ++X)
    {
        for (int32 Y = MinY; Y <= MaxY; ++Y)
        {
            for (int32 Z = MinZ; Z < MaxZ-1; ++Z)
            {
                const FVector WorldPos = ChunkOrigin + FVector(
                    (X * CachedVoxelSize) - HalfChunkSize + HalfVoxelSize,
                    (Y * CachedVoxelSize) - HalfChunkSize + HalfVoxelSize,
                    (Z * CachedVoxelSize) - HalfChunkSize + HalfVoxelSize
                );
                TOptional<float> Height = DiggerManager->SampleLandscapeHeight(DiggerManager->GetLandscapeProxyAt(WorldPos), WorldPos);
                float TerrainHeight = Height.IsSet() ? Height.GetValue() : -10000000.f;
                int32 FlatIndex = GetFlatIndex(X, Y, Z);
                TerrainHeights[FlatIndex] = TerrainHeight;
            }
        }
    }

    // Only run ParallelFor if all sizes are positive (already checked above)
    ParallelFor(SizeX, [&](int32 XIndex)
    {
        int32 X = MinX + XIndex;
        TArray<FIntVector> LocalAirVoxelsBelowTerrain;
        int32 LocalModifiedVoxels = 0;

        for (int32 Y = MinY; Y <= MaxY; ++Y)
        {
            for (int32 Z = MinZ; Z < MaxZ-1; ++Z)
            {
                // Convert voxel coordinates to center-aligned world position
                const FVector WorldPos = ChunkOrigin + FVector(
                    (X * CachedVoxelSize) - HalfChunkSize + HalfVoxelSize,
                    (Y * CachedVoxelSize) - HalfChunkSize + HalfVoxelSize,
                    (Z * CachedVoxelSize) - HalfChunkSize + HalfVoxelSize
                );

                // Calculate Bounds Check
                if (!BrushShape->IsWithinBounds(WorldPos, Stroke))
                {
                    continue;
                }

                // Get precomputed terrain height
                int32 FlatIndex = GetFlatIndex(X, Y, Z);
                float TerrainHeight = TerrainHeights[FlatIndex];
                const bool bAboveTerrain = WorldPos.Z >= TerrainHeight;

                // Calculate SDF value using the specific brush shape
                const float SDF = BrushShape->CalculateSDF(
                    WorldPos,
                    Stroke,
                    TerrainHeight
                );

                // Proper digging logic that respects the SDF shape
                if (Stroke.bDig)
                {
                    // For digging, we want to create air where the SDF indicates we're inside the shape
                    if (SDF > 0) // SDF > 0 means we want air here
                    {
                        SparseVoxelGrid->SetVoxel(X, Y, Z, SDF, true);
                        if (!bAboveTerrain)
                        {
                            LocalAirVoxelsBelowTerrain.Add(FIntVector(X, Y, Z));
                        }
                    }
                }
                else
                {
                    // For adding, we want to create solid where SDF is negative
                    if (SDF < 0)
                    {
                        SparseVoxelGrid->SetVoxel(X, Y, Z, SDF, false);
                    }
                }

                LocalModifiedVoxels++;
            }
        }

        // Lock and append local results to shared arrays/counters
        if (LocalAirVoxelsBelowTerrain.Num() > 0)
        {
            FScopeLock Lock(&BrushStrokeMutex);
            AirVoxelsBelowTerrain.Append(LocalAirVoxelsBelowTerrain);
        }
        if (LocalModifiedVoxels > 0)
        {
            ModifiedVoxels.Add(LocalModifiedVoxels);
        }
    });

    // Create shell of solid voxels around air voxels below terrain
    if (!AirVoxelsBelowTerrain.IsEmpty())
    {
        CreateSolidShellAroundAirVoxels(AirVoxelsBelowTerrain);
    }

    // Optional: Log performance info in debug builds
    #if UE_BUILD_DEBUG
    if (ModifiedVoxels.GetValue() > 0)
    {
        if (DiggerDebug::Chunks || DiggerDebug::Voxels)
        {
            UE_LOG(LogTemp, Message, TEXT("ApplyBrushStroke modified %d voxels in chunk %s using %s brush"), 
                   ModifiedVoxels.GetValue(), *ChunkCoordinates.ToString(), *UEnum::GetValueAsString(Stroke.BrushType));
        }
    }
    #endif
}



void UVoxelChunk::CreateSolidShellAroundAirVoxels(const TArray<FIntVector>& AirVoxels)
{
    // Generate all 26 neighbor offsets (corners, edges, and faces)
    TArray<FIntVector> NeighborOffsets;
    for (int32 x = -2; x <= 1; x++)
    {
        for (int32 y = -2; y <= 1; y++)
        {
            for (int32 z = -2; z <= 1; z++)
            {
                if (x == 0 && y == 0 && z == 0) continue; // Skip center
                NeighborOffsets.Add(FIntVector(x, y, z));
            }
        }
    }
    
    const FVector ChunkOrigin = FVoxelConversion::ChunkToWorld(ChunkCoordinates);
    const float CachedVoxelSize = FVoxelConversion::LocalVoxelSize;
    const int32 VoxelsPerChunk = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
    const float HalfChunkSize = (VoxelsPerChunk * CachedVoxelSize) * 0.5f;
    const float HalfVoxelSize = CachedVoxelSize * 0.5f;

   // Function to dynamically adjust rim thickness based on slope
    auto GetRimThickness = [this, CachedVoxelSize](const FVector& WorldPos) -> float
    {
	    // Retrieve the terrain height at the world position
	    TOptional<float> TerrainHeightOptional = DiggerManager->SampleLandscapeHeight(
		    DiggerManager->GetLandscapeProxyAt(WorldPos), WorldPos, true);

	    // Check if the terrain height was successfully retrieved
	    if (!TerrainHeightOptional.IsSet())
	    {
		    // If not, handle the error by logging or using a fallback value
		    UE_LOG(LogTemp, Warning, TEXT("Failed to retrieve terrain height at position: %s"), *WorldPos.ToString());
		    return 1.0f; // Default thickness or handle as needed
	    }

	    // Retrieve the height value
	    float TerrainHeight = TerrainHeightOptional.GetValue();

	    // Get the neighboring position to calculate slope (in this case, we add X offset)
	    FVector NeighborPos = WorldPos + FVector(CachedVoxelSize, 0, 0); // Neighbor to calculate slope

	    // Retrieve the height at the neighboring position
	    TOptional<float> NeighborHeightOptional = DiggerManager->SampleLandscapeHeight(
		    DiggerManager->GetLandscapeProxyAt(NeighborPos), NeighborPos, true);

	    // Check if the neighbor height was successfully retrieved
	    if (!NeighborHeightOptional.IsSet())
	    {
		    // If not, handle the error by logging or using a fallback value
		    UE_LOG(LogTemp, Warning, TEXT("Failed to retrieve neighbor terrain height at position: %s"),
		           *NeighborPos.ToString());
		    return 1.0f; // Default thickness or handle as needed
	    }

	    // Retrieve the height value for the neighbor
	    float NeighborHeight = NeighborHeightOptional.GetValue();

	    // Calculate the slope or height difference
	    float Slope = FMath::Abs(TerrainHeight - NeighborHeight) / CachedVoxelSize;

	    // Dynamically adjust rim thickness based on the slope
	    // Increase the rim thickness more for steeper slopes
	    if (Slope > 0.1f) // Arbitrary threshold for detecting a slope
	    {
		    // Ramp up the rim thickness more as slope increases
		    float RimThickness = FMath::Clamp(Slope * 3.0f, 3.0f, 10.0f);
		    // Increase rim thickness with slope (max cap of 10)
		    return RimThickness;
	    }
	    return 1.0f; // Keep it thin or zero on flat terrain
    };


    // Loop through all air voxels
    for (const FIntVector& AirVoxel : AirVoxels)
    {
        // Check all 26 neighbors
        for (const FIntVector& Offset : NeighborOffsets)
        {
            FIntVector NeighborCoord = AirVoxel + Offset;

            // Allow one voxel negative overflow, clamp at chunk size
            if (NeighborCoord.X < -1 || NeighborCoord.X >= VoxelsPerChunk + 1 ||
                NeighborCoord.Y < -1 || NeighborCoord.Y >= VoxelsPerChunk + 1 ||
                NeighborCoord.Z < -1 || NeighborCoord.Z >= VoxelsPerChunk + 1)
                continue;

            // Skip if this neighbor is already explicitly set
            if (SparseVoxelGrid->VoxelData.Contains(NeighborCoord))
                continue;

            // Convert to world position to check if below terrain
            const FVector WorldPos = ChunkOrigin + FVector(
                (NeighborCoord.X * CachedVoxelSize) - HalfChunkSize + HalfVoxelSize,
                (NeighborCoord.Y * CachedVoxelSize) - HalfChunkSize + HalfVoxelSize,
                (NeighborCoord.Z * CachedVoxelSize) - HalfChunkSize + HalfVoxelSize
            );

            // Get the rim thickness based on the terrain slope
            float RimThickness = GetRimThickness(WorldPos);

            const float TerrainHeight = DiggerManager->GetLandscapeHeightAt(WorldPos);
            
            // Skip the voxel if it's above the landscape surface (allowing spill-down under the terrain)
            if (WorldPos.Z > TerrainHeight)
            {
                continue;
            }
            // Ensure we add the rim correctly for voxels below terrain and at the appropriate thickness
            if (WorldPos.Z < TerrainHeight - RimThickness)
            {
                // If this neighbor is below terrain (and adjusted by rim thickness), make it solid
                SparseVoxelGrid->SetVoxel(NeighborCoord.X, NeighborCoord.Y, NeighborCoord.Z, FVoxelConversion::SDF_SOLID, false);
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
	return GetSparseVoxelGrid()->GetAllVoxels();
}




void UVoxelChunk::GenerateMesh() const
{
	if (!SparseVoxelGrid)
	{
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
