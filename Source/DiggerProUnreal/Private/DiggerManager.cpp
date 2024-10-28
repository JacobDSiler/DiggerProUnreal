#include "DiggerManager.h"
#include "SparseVoxelGrid.h"
#include "MarchingCubes.h"
#include "VoxelChunk.h"

class UVoxelChunk;
class UVoxelBrushType;
class USparseVoxelGrid;
class UMarchingCubes;
class UVoxelBrushShape;

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
}

bool ADiggerManager::EnsureWorldReference()
{
	if (World)
	{return true;}

	World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("World not found in BeginPlay of DiggerManager."));
		return false; // Exit if World is not found
	}
	return true;
}

void ADiggerManager::BeginPlay()
{
	Super::BeginPlay();
	
	if(!EnsureWorldReference())
	{ UE_LOG(LogTemp,Error,TEXT("World is null in DiggerManager BeginPlay() Continuing!"));}
	
	
	UpdateVoxelSize();
	
	GenerateVoxelsTest();

	// Start the timer to process dirty chunks
	World=GetWorld();
	if (World)
	{	World->GetTimerManager().SetTimer(ChunkProcessTimerHandle, this, &ADiggerManager::ProcessDirtyChunks, 1.0f, true);}
}

void ADiggerManager::UpdateVoxelSize()
{
	VoxelSize=TerrainGridSize/Subdivisions;
}

void ADiggerManager::ProcessDirtyChunks()
{
	// Process chunks on a background thread
	AsyncTask(ENamedThreads::AnyNormalThreadNormalTask, [this]()
	{
		for (auto& Elem : ChunkMap)
		{
			UVoxelChunk* Chunk = Elem.Value;
			if (Chunk)
			{
				Chunk->UpdateIfDirty();
			}
		}

		// Reset the timer on the game thread
		AsyncTask(ENamedThreads::GameThread, [this]()
		{
			World->GetTimerManager().ClearTimer(ChunkProcessTimerHandle);
			World->GetTimerManager().SetTimer(ChunkProcessTimerHandle, this, &ADiggerManager::ProcessDirtyChunks, 2.0f, true);
		});
	});
}




void ADiggerManager::ApplyBrush()
{
	FVector BrushPosition = ActiveBrush->GetBrushLocation();
	float BrushRadius = ActiveBrush->GetBrushSize();
	FBrushStroke BrushStroke;
	BrushStroke.BrushPosition = BrushPosition;
	BrushStroke.BrushRadius = BrushRadius;
	BrushStroke.bDig = ActiveBrush->GetDig();
	BrushStroke.BrushType = ActiveBrush->GetBrushType(); // Ensure it captures the correct type
    BrushStrokeQueue.push(BrushStroke);

    // Use the helper function to get the target chunk if it isn't passed
    UVoxelChunk* TargetChunk = GetOrCreateChunkAt(BrushPosition);

    if (TargetChunk)
    {
    	UE_LOG(LogTemp, Warning, TEXT("BrushPosition: X=%f Y=%f Z=%f BrushRadius: %f"), 
    	BrushPosition.X, BrushPosition.Y,BrushPosition.Z, BrushRadius);
    
        TargetChunk->ApplyBrushStroke(BrushStroke);

        // Check if the queue exceeds the configured limit
    	/*if (BrushStrokeQueue.size() > MaxUndoLength)
    	{
    		FBrushStroke StrokeToBake = BrushStrokeQueue.front();
    		BrushStrokeQueue.pop(); // This removes the oldest element, which is at the front
    		//TargetChunk->BakeSingleBrushStroke(StrokeToBake);
    	}*/
    }
}


/*void ADiggerManager::BakeSDFValues(UVoxelChunk* TargetChunk)
{
    while (!BrushStrokeQueue.empty())
    {
        FBrushStroke Stroke = BrushStrokeQueue.front();
        BrushStrokeQueue.pop();

    	UE_LOG(LogTemp,Warning, TEXT("BakeSDFValues() called"));
        // Logic to bake the stroke to the base SDF
         TargetChunk->ApplyBrushStroke(Stroke);
    }
}*/


FIntVector ADiggerManager::CalculateChunkPosition(const FIntVector& ProposedChunkPosition) const
{
	// Calculate world position based on chunk coordinates
	FVector ChunkWorldPosition = FVector(ProposedChunkPosition) * ChunkSize * TerrainGridSize;

	// Log the chunk position calculation for debugging
	UE_LOG(LogTemp, Warning, TEXT("CalculateChunkPosition: ProposedChunkPosition = %s, ChunkWorldPosition = %s"),
		   *ProposedChunkPosition.ToString(), *ChunkWorldPosition.ToString());

	return ProposedChunkPosition;
}



FIntVector ADiggerManager::CalculateChunkPosition(const FVector3d& WorldPosition) const
{
	int32 ChunkWorldSize=ChunkSize*TerrainGridSize;
	// Calculate the chunk index based on the world position
	int32 ChunkIndexX = FMath::FloorToInt(WorldPosition.X / ChunkWorldSize);
	int32 ChunkIndexY = FMath::FloorToInt(WorldPosition.Y / ChunkWorldSize);
	int32 ChunkIndexZ = FMath::FloorToInt(WorldPosition.Z / ChunkWorldSize);

	// Return the chunk position as an FIntVector
	return FIntVector(ChunkIndexX, ChunkIndexY, ChunkIndexZ);
}

UVoxelChunk* ADiggerManager::GetOrCreateChunkAt(const FVector3d& ProposedChunkPosition)
{
	int32 ChunkWorldSize=ChunkSize*TerrainGridSize;
	//Make sure theVoxelSize Member Variable is up to date.
	UpdateVoxelSize();
	// Convert FVector3d to FIntVector by flooring the coordinates
	int32 FlooredX = FMath::FloorToInt(ProposedChunkPosition.X / ChunkWorldSize);
	int32 FlooredY = FMath::FloorToInt(ProposedChunkPosition.Y / ChunkWorldSize);
	int32 FlooredZ = FMath::FloorToInt(ProposedChunkPosition.Z / ChunkWorldSize);
    
	// Create an FIntVector from the floored coordinates
	FIntVector ChunkCoords(FlooredX, FlooredY, FlooredZ);
    
	// Call the method that gets or creates the chunk at the specified FIntVector
	return GetOrCreateChunkAt(ChunkCoords);
}

UVoxelChunk* ADiggerManager::GetOrCreateChunkAt(const float& ProposedChunkX, const float& ProposedChunkY, const float& ProposedChunkZ)
{ return GetOrCreateChunkAt(FIntVector(ProposedChunkX,ProposedChunkY,ProposedChunkZ));}

UVoxelChunk* ADiggerManager::GetOrCreateChunkAt(const FIntVector& ProposedChunkPosition)
{
	// Determine the chunk position based on ProposedChunkPosition
	FIntVector ChunkPosition = FIntVector(CalculateChunkPosition(ProposedChunkPosition));

	// Check if the chunk already exists in the map
	UVoxelChunk** ExistingChunk = ChunkMap.Find(ChunkPosition);
	if (ExistingChunk)
	{
		UE_LOG(LogTemp, Log, TEXT("Found existing chunk at position: %s"), *ChunkPosition.ToString());
		return *ExistingChunk; // Return the existing chunk
	}

	// Assert that the chunk does not exist yet
	checkf(ChunkMap.Find(ChunkPosition) == nullptr, TEXT("Duplicate chunk creation detected at position: %s"), *ChunkPosition.ToString());

	// Create a new chunk since it does not exist
	UVoxelChunk* NewChunk = NewObject<UVoxelChunk>(this);
	if (NewChunk)
	{
		NewChunk->InitializeChunk(ChunkPosition); // Initialize the new chunk
		ChunkMap.Add(ChunkPosition, NewChunk); // Add the new chunk to the map
		UE_LOG(LogTemp, Log, TEXT("Created a new chunk at position: %s"), *ChunkPosition.ToString());
		return NewChunk; // Return the newly created chunk
	}
	else
	{
		// Log if the chunk couldn't be created
		UE_LOG(LogTemp, Error, TEXT("Failed to create a chunk at position: %s"), *ChunkPosition.ToString());
		return nullptr; // Return nullptr if creation failed
	}
}



void ADiggerManager::UpdateChunks()
{
	// Iterate through all chunks in the ChunkMap
	for (auto& ChunkEntry : ChunkMap)
	{
		UVoxelChunk* Chunk = ChunkEntry.Value;  // Access the chunk using the map's value (UVoxelChunk*)

		if (Chunk && Chunk->IsDirty())  // Check if the chunk is valid and dirty
		{
			Chunk->UpdateIfDirty();  // Regenerate the mesh if dirty
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




void ADiggerManager::CreateSingleHole(FVector3d HoleCenter, int HoleSize)
{
	//UVoxelChunk* NewChunk = GetOrCreateChunkAt(HoleCenter);

	/*if (!NewChunk)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to get or create chunk at hole center: %s"), *HoleCenter.ToString());
		return;
	}*/

	CreateSphereVoxelGrid(ZeroChunk, FVector(0,0,0), 50.f);
/*
	int32 CenterX = FMath::FloorToInt(HoleCenter.X / VoxelSize);
	int32 CenterY = FMath::FloorToInt(HoleCenter.Y / VoxelSize);
	int32 CenterZ = FMath::FloorToInt(HoleCenter.Z / VoxelSize);

	int32 HalfSize = HoleSize / 2;
	float MaxDistanceSquared = 3.0f * FMath::Square(static_cast<float>(HalfSize)); // Precompute max distance squared

	for (int32 OffsetX = -HalfSize; OffsetX < HalfSize; OffsetX++)
	{
		for (int32 OffsetY = -HalfSize; OffsetY < HalfSize; OffsetY++)
		{
			for (int32 OffsetZ = -HalfSize; OffsetZ < HalfSize; OffsetZ++)
			{
				int32 VoxelPosX = CenterX + OffsetX;
				int32 VoxelPosY = CenterY + OffsetY;
				int32 VoxelPosZ = CenterZ + OffsetZ;

				FVector3d CurrentPosition(VoxelPosX, VoxelPosY, VoxelPosZ);
				float DistanceSquared = 
					FMath::Square(static_cast<float>(OffsetX)) +
					FMath::Square(static_cast<float>(OffsetY)) +
					FMath::Square(static_cast<float>(OffsetZ));

				float NormalizedDistance = DistanceSquared / MaxDistanceSquared;
				float SDFValue = NormalizedDistance * 2.0f - 1.0f;

				NewChunk->SetVoxel(CurrentPosition, SDFValue);
#if WITH_EDITOR
				UE_LOG(LogTemp, Warning, TEXT("Voxel set at: %s with SDF: %f"), *CurrentPosition.ToString(), SDFValue);
#endif
			}
		}
	}*/
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

void ADiggerManager::CreateSphereVoxelGrid(UVoxelChunk* Chunk, const FVector& Position, float Radius) const
{
    if (!Chunk) return;

    FVector Center = Position; // Center of the sphere based on the input position
    FIntVector ChunkLocation = Chunk->GetChunkPosition() * TerrainGridSize * Subdivisions * 2;

    // Calculate the size of the grid based on the radius and add 1 for all sides
    int32 SphereDiameter = FMath::CeilToInt(Radius * 2.0f);
    int32 GridSize = SphereDiameter / VoxelSize + 2; // Ensure the sphere is fully contained

    // Iterate through the grid to set voxels in the chunk
    for (int32 x = 0; x < GridSize; x++)
    {
        for (int32 y = 0; y < GridSize; y++)
        {
            for (int32 z = 0; z < GridSize; z++)
            {
                // Calculate the position of the voxel in world space
                FVector VoxelPosition = Center + FVector(
                    (x * VoxelSize) - Radius - VoxelSize,  // X position
                    (y * VoxelSize) - Radius - VoxelSize,  // Y position
                    (z * VoxelSize) - Radius - VoxelSize   // Z position
                );

                // Calculate the distance from the voxel to the center of the sphere
                float DistanceToCenter = FVector::Dist(VoxelPosition, Center);
                
                // Determine raw SDF value: negative inside the sphere, positive outside
                float SDFValue = DistanceToCenter - Radius;

                // Normalize the SDF value to be between -1.0 and 1.0 based on the radius
                float NormalizedSDFValue = SDFValue / Radius;

                // Clamp the SDF value between -1.0 and 1.0
                NormalizedSDFValue = FMath::Clamp(NormalizedSDFValue, -1.0f, 1.0f);

                UE_LOG(LogTemp, Warning, TEXT("Setting voxel at X=%d, Y=%d, Z=%d with SDF=%f"), x, y, z, NormalizedSDFValue);

                // Set the voxel in the chunk using the clamped SDF value
                Chunk->SetVoxel(FVector(ChunkLocation) + FVector3d(x, y, z), NormalizedSDFValue);
            }
        }
    }
}

void ADiggerManager::GenerateAxesAlignedVoxelsInChunk(UVoxelChunk* Chunk) const
{
	if (!Chunk) return;
	
	int32 ChunkVoxels = ChunkSize * Subdivisions;
	int32 HalfChunkVoxels = ChunkVoxels / 2;

	// Get the chunk's world position
	FVector WorldSpaceChunkPosition = Chunk->GetWorldPosition();

	UE_LOG(LogTemp, Warning, TEXT("Generating voxels in chunk at world position: %s"), *WorldSpaceChunkPosition.ToString());

	// Iterate through the center line of each axis from -HalfChunkVoxels to HalfChunkVoxels
	for (int32 X = -HalfChunkVoxels; X < HalfChunkVoxels; ++X)
	{
		FVector LocalVoxelPosition = FVector(X, 0, 0);
		FVector VoxelPosition = LocalVoxelPosition + WorldSpaceChunkPosition;
		//UE_LOG(LogTemp, Warning, TEXT("Placing voxel at: %s in chunk at world position %s"), *VoxelPosition.ToString(), *WorldSpaceChunkPosition.ToString());
		Chunk->SetVoxel(X, 0, 0, 1.0f);  // Center Y and Z
	}
	for (int32 Y = -HalfChunkVoxels; Y < HalfChunkVoxels; ++Y)
	{
		FVector LocalVoxelPosition = FVector(0, Y, 0);
		FVector VoxelPosition = LocalVoxelPosition + WorldSpaceChunkPosition;
		//UE_LOG(LogTemp, Warning, TEXT("Placing voxel at: %s in chunk at world position %s"), *VoxelPosition.ToString(), *WorldSpaceChunkPosition.ToString());
		Chunk->SetVoxel(0, Y, 0, -1.0f);  // Center X and Z
	}
	for (int32 Z = -HalfChunkVoxels; Z < HalfChunkVoxels; ++Z)
	{
		FVector LocalVoxelPosition = FVector(0, 0, Z);
		FVector VoxelPosition = LocalVoxelPosition + WorldSpaceChunkPosition;
		//UE_LOG(LogTemp, Warning, TEXT("Placing voxel at: %s in chunk at world position %s"), *VoxelPosition.ToString(), *WorldSpaceChunkPosition.ToString());
		Chunk->SetVoxel(0, 0, Z, 1.0);  // Center X and Y
	}

	// Generate the mesh after setting all voxels
	Chunk->MarkDirty();
	Chunk->ForceUpdate();
}


void ADiggerManager::FillChunkWithPerlinNoiseVoxels(UVoxelChunk* Chunk) const
{
	if (!Chunk) return;
	
	int32 ChunkVoxels = ChunkSize * Subdivisions;
	int32 HalfChunkVoxels = ChunkVoxels / 2;
	

	// Iterate through all voxels within the chunk
	for (int32 X = -HalfChunkVoxels; X < HalfChunkVoxels; ++X)
	{
		for (int32 Y = -HalfChunkVoxels; Y < HalfChunkVoxels; ++Y)
		{
			for (int32 Z = -HalfChunkVoxels; Z < HalfChunkVoxels; ++Z)
			{
				FVector LocalVoxelPosition = FVector(X, Y, Z);

				// Generate Perlin noise for the voxel position
				float NoiseValue = FMath::PerlinNoise3D(LocalVoxelPosition * 0.5f); // Adjust the scale as needed

				// Map Perlin noise value (-1.0f to 1.0f)
				float SDFValue = FMath::Clamp(NoiseValue, -1.0f, 1.0f);

				// Log the voxel position and SDF value for debugging
				UE_LOG(LogTemp, Warning, TEXT("Placing voxel at: %s with SDF: %f"), *LocalVoxelPosition.ToString(), SDFValue);

				// Set the voxel SDF value in the chunk
				Chunk->SetVoxel(X, Y, Z, SDFValue);
			}
		}
	}

	//OutputAggregatedLogs();
	// Generate the mesh after filling the voxels
	//Chunk->MarkDirty();
	//Chunk->ForceUpdate();
}


void ADiggerManager::GenerateVoxelsTest()
{
	// Use GetOrCreateChunkAt method to retrieve or create the chunk at ChunkLocation
	ZeroChunk = GetOrCreateChunkAt(0,0,0);

	if(!ZeroChunk) return;
	//GenerateAxesAlignedVoxelsInChunk(ZeroChunk);
	FillChunkWithPerlinNoiseVoxels(ZeroChunk);
	//ZeroChunk->GetSparseVoxelGrid()->RenderVoxels();

	FIntVector ChunkLocation = FIntVector(1, 1, 0);
	OneChunk = GetOrCreateChunkAt(ChunkLocation);
	if(!OneChunk) return;
	GenerateAxesAlignedVoxelsInChunk(OneChunk);
	//FillChunkWithPerlinNoiseVoxels(OneChunk);
	OneChunk->GetSparseVoxelGrid()->RenderVoxels();
		//CreateSphereVoxelGrid(OneChunk, FVector(0,0,0), 50.f);
}