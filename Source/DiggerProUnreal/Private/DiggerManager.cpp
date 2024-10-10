#include "DiggerManager.h"

#include "InputBehavior.h"
#include "SparseVoxelGrid.h"
#include "MarchingCubes.h"
#include "VoxelChunk.h"
#include "VoxelBrushShape.h"
#include "Chaos/ImplicitQRSVD.h"

ADiggerManager::ADiggerManager()
{
	// Initialize the ProceduralMeshComponent
	ProceduralMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("GeneratedMesh"));
	RootComponent = ProceduralMesh;

	// Initialize the voxel grid and marching cubes
	SparseVoxelGrid = CreateDefaultSubobject<USparseVoxelGrid>(TEXT("SparseVoxelGrid"));
	MarchingCubes = CreateDefaultSubobject<UMarchingCubes>(TEXT("MarchingCubes"));

	//Initialize the brush component
}

void ADiggerManager::BeginPlay()
{
	Super::BeginPlay();
	GenerateVoxelsTest();
}



void ADiggerManager::ApplyBrush(FVector BrushPosition, float BrushRadius)
{
	UVoxelChunk* TargetChunk = nullptr;
	// Use the helper function to get the target chunk if it isn't passed
	if (!TargetChunk)
	{
		TargetChunk = ActiveBrush->GetTargetChunkFromBrushPosition(BrushPosition);
	}

	if (!TargetChunk)
	{
		// Log error or handle the case where no chunk is found
		return;
	}
	// Assuming ActiveBrush is properly initialized
	if (ActiveBrush)
	{
		if (TargetChunk)
		{
			ActiveBrush->ApplyBrushToChunk(BrushPosition, BrushRadius);
		}
	}
}

FIntVector ADiggerManager::CalculateChunkPosition(const FIntVector3& WorldPosition) const
{

	// Calculate the chunk index based on the world position
	int32 ChunkIndexX = WorldPosition.X / ChunkSize;
	int32 ChunkIndexY = WorldPosition.Y / ChunkSize;
	int32 ChunkIndexZ = WorldPosition.Z / ChunkSize;

	// Return the chunk position as an FIntVector
	return FIntVector(ChunkIndexX, ChunkIndexY, ChunkIndexZ);
}

FIntVector ADiggerManager::CalculateChunkPosition(const FVector3d& WorldPosition) const
{

	// Calculate the chunk index based on the world position
	int32 ChunkIndexX = FMath::FloorToInt(WorldPosition.X / ChunkSize);
	int32 ChunkIndexY = FMath::FloorToInt(WorldPosition.Y / ChunkSize);
	int32 ChunkIndexZ = FMath::FloorToInt(WorldPosition.Z / ChunkSize);

	// Return the chunk position as an FIntVector
	return FIntVector(ChunkIndexX, ChunkIndexY, ChunkIndexZ);
}

UVoxelChunk* ADiggerManager::GetOrCreateChunkAt(const FVector3d& ProposedChunkPosition)
{
	// Convert FVector3d to FIntVector by flooring the coordinates
	int32 FlooredX = FMath::FloorToInt(ProposedChunkPosition.X / TerrainGridSize);
	int32 FlooredY = FMath::FloorToInt(ProposedChunkPosition.Y / TerrainGridSize);
	int32 FlooredZ = FMath::FloorToInt(ProposedChunkPosition.Z / TerrainGridSize);
    
	// Create an FIntVector from the floored coordinates
	FIntVector ChunkCoords(FlooredX, FlooredY, FlooredZ);
    
	// Call the method that gets or creates the chunk at the specified FIntVector
	return GetOrCreateChunkAt(ChunkCoords);
}


UVoxelChunk* ADiggerManager::GetOrCreateChunkAt(const FIntVector& ProposedChunkPosition)
{
	// Determine the chunk position based on ProposedChunkPosition
	FIntVector ChunkPosition = CalculateChunkPosition(ProposedChunkPosition);

	// Check if the chunk already exists in the map
	UVoxelChunk** ExistingChunk = ChunkMap.Find(ChunkPosition);
	if (ExistingChunk)
	{
		// Chunk already exists
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
	

	for (auto& Elem : ChunkMap)
	{
		FIntVector ChunkCoords = Elem.Key;
		UVoxelChunk* Chunk = Elem.Value;

		// Calculate the chunk's world size using ChunkSize and VoxelSize
		int32 ChunkWorldSizeX = ChunkSize * VoxelSize;
		int32 ChunkWorldSizeY = ChunkSize * VoxelSize;
		int32 ChunkWorldSizeZ = ChunkSize * VoxelSize;

		UE_LOG(LogTemp, Warning, TEXT("Chunk at Grid X=%d Y=%d Z=%d | World Size: (%d, %d, %d)"),
			ChunkCoords.X, ChunkCoords.Y, ChunkCoords.Z, 
			ChunkWorldSizeX, ChunkWorldSizeY, ChunkWorldSizeZ);
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

	CreateSphereVoxelGrid(ZeroChunk, 50.f);
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
				Chunk->DebugDrawChunk(); // Visualize chunk boundaries
				Chunk->MarkDirty();
				Chunk->ForceUpdate();
				USparseVoxelGrid* SparseVoxelGridPtr = Chunk->GetSparseVoxelGrid();
				if (SparseVoxelGridPtr)
				{
					UE_LOG(LogTemp, Warning, TEXT("Rendering voxels for SparseVoxelGrid..."));
					SparseVoxelGridPtr->LogVoxelData();
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


void ADiggerManager::CreateSphereVoxelGrid(UVoxelChunk* Chunk, float Radius)
{
	if (!Chunk) return;
    // Get or create the chunk at the desired position
    FVector Center = FVector(0, 0, 0); // Center of the sphere (can be modified if needed)
    FIntVector ChunkLocation = Chunk->GetSparseVoxelGrid()->GetParentChunkCoordinates();

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
                Chunk->SetVoxel(FVector3d(x, y, z), NormalizedSDFValue);
            }
        }
    }

}

void ADiggerManager::PlaceVoxelForVoxelLine(UVoxelChunk* Chunk, USparseVoxelGrid* VoxelGrid, int32 X, int32 Y, int32 Z)
{
	// Calculate world position of the current voxel based on the chunk's world position
	FVector VoxelPosition = VoxelGrid->GetParentChunkCoordinatesV3D() + FVector(X, Y, Z) * VoxelSize;

	// Optionally: Check if voxel is inside a valid area, apply any function to decide whether it's solid or air
	bool bIsSolid = true;  // Here you can use any rule to determine if a voxel is solid or air

	// Add voxel to chunk grid (implement SetVoxel method in your chunk to handle voxel data)
	Chunk->SetVoxel(X, Y, Z, bIsSolid);
}

void ADiggerManager::GenerateAxesAlignedVoxelsInChunk(UVoxelChunk* Chunk)
{
	if (!Chunk) return;

	USparseVoxelGrid* VoxelGrid = Chunk->GetSparseVoxelGrid();
	int32 ChunkVoxels = ChunkSize;  // No need for additional scaling factors
	int32 HalfChunkVoxels = ChunkVoxels / 2;

	// Iterate through the center line of each axis from -HalfChunkVoxels to HalfChunkVoxels
	for (int32 X = -HalfChunkVoxels; X < HalfChunkVoxels; ++X)
	{
		VoxelGrid->SetVoxel(FIntVector(X, 0, 0), -1.0f);  // Center Y and Z
	}
	for (int32 Y = -HalfChunkVoxels; Y < HalfChunkVoxels; ++Y)
	{
		VoxelGrid->SetVoxel(FIntVector(0, Y, 0), 1.0f);
	}
	for (int32 Z = -HalfChunkVoxels; Z < HalfChunkVoxels; ++Z)
	{
		VoxelGrid->SetVoxel(FIntVector(0, 0, Z), -1.0f);
	}

	// Generate the mesh after setting all voxels
	//Chunk->GenerateMesh();
}







void ADiggerManager::GenerateVoxelsTest()
{
	// Use GetOrCreateChunkAt method to retrieve or create the chunk at ChunkLocation
	FIntVector ChunkLocation = FIntVector(0,0,0);
	ZeroChunk = GetOrCreateChunkAt(ChunkLocation);
	if(!ZeroChunk) return;
	GenerateAxesAlignedVoxelsInChunk(ZeroChunk);
	ZeroChunk->GetSparseVoxelGrid()->RenderVoxels();
		//CreateSphereVoxelGrid(ZeroChunk, 50.f);
}
