#include "VoxelChunk.h"
#include "MarchingCubes.h"
#include "SparseVoxelGrid.h"
#include "DiggerManager.h"
#include "EngineUtils.h"
#include "Net/Core/Connection/NetConnectionFaultRecoveryBase.h"


UVoxelChunk::UVoxelChunk()
	: ChunkCoordinates(FIntVector::ZeroValue), TerrainGridSize(100), Subdivisions(4), VoxelSize(100),
	  DiggerManager(nullptr),
	  SparseVoxelGrid(CreateDefaultSubobject<USparseVoxelGrid>(TEXT("SparseVoxelGrid"))),
	  bIsDirty(false), 
	  SectionIndex(-1)
{
	// Initialize the SparseVoxelGrid
	SparseVoxelGrid->Initialize(ChunkCoordinates); 
	SparseVoxelGrid->InitializeDiggerManager(); 
    
	MarchingCubesGenerator = CreateDefaultSubobject<UMarchingCubes>(TEXT("MarchingCubesGenerator"));
}


void UVoxelChunk::SetUniqueSectionIndex()
{
	//Turn the coordinates into a unique index now.
	FString XString = FString::FromInt(ChunkCoordinates.X);
	FString YString = FString::FromInt(ChunkCoordinates.Y);
	FString ZString = FString::FromInt(ChunkCoordinates.Z);

	// Concatenate the strings
	FString ConcatenatedString = XString + YString + ZString;

	// Convert the concatenated string to a number
	SectionIndex = FCString::Atoi(*ConcatenatedString);

	UE_LOG(LogTemp, Error, TEXT("SectionID Set to: %i"), SectionIndex);
}

void UVoxelChunk::InitializeChunk(const FIntVector& InChunkCoordinates)
{
	ChunkCoordinates = InChunkCoordinates;
	SetUniqueSectionIndex();


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
		UE_LOG(LogTemp, Error, TEXT("DiggerManager is null during chunk initialization!"));
		return;
	}
	
	TerrainGridSize = DiggerManager->TerrainGridSize;
	Subdivisions = DiggerManager->Subdivisions;
	VoxelSize = TerrainGridSize/Subdivisions;

	if (!SparseVoxelGrid)
	{
		UE_LOG(LogTemp, Error, TEXT("SparseVoxelGrid passed to InitializeChunk is null!"));
		return;
	}
	
	UE_LOG(LogTemp, Warning, TEXT("Initializing new chunk at position X=%d Y=%d Z=%d"), ChunkCoordinates.X, ChunkCoordinates.Y, ChunkCoordinates.Z);

	// Add this chunk to the ChunkMap
	DiggerManager->ChunkMap.Add(ChunkCoordinates, this);
    
	// Log the successful addition
	UE_LOG(LogTemp, Warning, TEXT("Chunk added to ChunkMap at position: X=%d Y=%d Z=%d"), ChunkCoordinates.X, ChunkCoordinates.Y, ChunkCoordinates.Z);

}


void UVoxelChunk::DebugDrawChunk() const
{
	// Get the world context for the chunk
	UWorld* World = GetWorld();
	if (!World) return;

	UE_LOG(LogTemp, Log, TEXT("DebugDrawChunk: World is valid, continuing..."));
	UE_LOG(LogTemp, Log, TEXT("DebugDrawChunk: World is valid, continuing..."));
	UE_LOG(LogTemp, Log, TEXT("DebugDrawChunk: World is valid, continuing..."));
	UE_LOG(LogTemp, Log, TEXT("DebugDrawChunk: World is valid, continuing..."));
	UE_LOG(LogTemp, Log, TEXT("DebugDrawChunk: World is valid, continuing..."));
	UE_LOG(LogTemp, Log, TEXT("DebugDrawChunk: World is valid, continuing..."));
	UE_LOG(LogTemp, Log, TEXT("DebugDrawChunk: World is valid, continuing..."));
	
	const FVector ChunkCenter = FVector(ChunkCoordinates * ChunkSize * VoxelSize); // Position in world space
	const FVector ChunkExtent = FVector(ChunkSize * VoxelSize / 2.0f); // Half the size of the chunk

	// Draw the bounding box of the chunk
	DrawDebugBox(World, ChunkCenter, ChunkExtent, FColor::Red, false, 10.0f); // Display for 10 seconds
}


void UVoxelChunk::MarkDirty()
{
	bIsDirty = true; // Set the dirty flag
}

void UVoxelChunk::UpdateIfDirty()
{
	if (!bIsDirty) return;  // Early exit if not dirty
	
		// Perform the update (e.g., regenerate the mesh)
		// Regenerate the mesh only if dirty
		GenerateMesh();
		
		// Reset the dirty flag
		bIsDirty = false;
}

void UVoxelChunk::ForceUpdate()
{
	// Perform the update (e.g., regenerate the mesh)
	// Regenerate the mesh only if dirty
	GenerateMesh();
		
	// Reset the dirty flag
	bIsDirty = false;
}


/*void UVoxelChunk::CreateSphereVoxelGrid(float Radius) const
{
	// Calculate the size of the grid based on the radius and add 1 for all sides
	int32 SphereDiameter = FMath::CeilToInt(Radius * 2.0f);
	int32 GridSize = SphereDiameter / VoxelSize + 2; // Add 2 to encapsulate the sphere completely

	FVector Center = FVector(0, 0, 0); // Center of the sphere (can be modified if needed)

	// Iterate through the grid
	for (int32 x = 0; x < GridSize; x++)
	{
		for (int32 y = 0; y < GridSize; y++)
		{
			for (int32 z = 0; z < GridSize; z++)
			{
				// Calculate the position of the voxel
				FVector VoxelPosition = Center + FVector(
					(x * VoxelSize) - Radius - VoxelSize, // Subtract an additional VoxelSize for proper centering
					(y * VoxelSize) - Radius - VoxelSize, // Subtract an additional VoxelSize for proper centering
					(z * VoxelSize) - Radius - VoxelSize  // Subtract an additional VoxelSize for proper centering
				);

				// Calculate the distance from the voxel center to the sphere center
				float DistanceToCenter = FVector::Dist(VoxelPosition, Center);
                
				// Set the SDF value: Negative if inside the sphere, positive if outside
				float SDFValue = DistanceToCenter - Radius;

				// Set the voxel SDF value (assuming you have a method for this)
				SparseVoxelGrid->SetVoxel(FIntVector(x, y, z), SDFValue);
			}
		}
	}

	
}*/



bool UVoxelChunk::IsValidLocalCoordinate(int32 LocalX, int32 LocalY, int32 LocalZ) const
{
	// Check if the coordinates are within valid bounds
	return (LocalX >= 0 && LocalX < ChunkSize) &&
		   (LocalY >= 0 && LocalY < ChunkSize) &&
		   (LocalZ >= 0 && LocalZ < ChunkSize);
}


void UVoxelChunk::SetVoxel(int32 X, int32 Y, int32 Z, const float SDFValue) const
{
	if (SparseVoxelGrid && IsValidLocalCoordinate(X, Y, Z))
	{
		SparseVoxelGrid->SetVoxel(X, Y, Z, SDFValue);
	}
}

void UVoxelChunk::SetVoxel(const FVector& Position, const float SDFValue) const
{
	int32 X = Position.X;
	int32 Y = Position.Y;
	int32 Z = Position.Z;

	SetVoxel(X, Y, Z, SDFValue);
}

float UVoxelChunk::GetVoxel(int32 X, int32 Y, int32 Z) const
{
	// Check if the coordinate is out of bounds
	if (!IsValidLocalCoordinate(X, Y, Z))
	{
		UE_LOG(LogTemp, Warning, TEXT("Attempt to access out-of-bounds voxel at X=%d, Y=%d, Z=%d"), X, Y, Z);
		return 1.0f;  // Return default air value for out-of-bounds voxels
	}

	// Otherwise, proceed to check the sparse grid
	if (SparseVoxelGrid)
	{
		return SparseVoxelGrid->GetVoxel(X, Y, Z);
	}
	return 1.0f;  // Return default air if grid doesn't exist
}

float UVoxelChunk::GetVoxel(const FVector& Position) const
{
	int32 X = Position.X;
	int32 Y = Position.Y;
	int32 Z = Position.Z;
	
	// Check if the coordinate is out of bounds
	if (!IsValidLocalCoordinate(X, Y, Z))
	{
		UE_LOG(LogTemp, Warning, TEXT("Attempt to access out-of-bounds voxel at X=%d, Y=%d, Z=%d"), X, Y, Z);
		return 1.0f;  // Return default air value for out-of-bounds voxels
	}

	// Otherwise, proceed to check the sparse grid
	if (SparseVoxelGrid)
	{
		return SparseVoxelGrid->GetVoxel(X, Y, Z);
	}
	return 1.0f;  // Return default air if grid doesn't exist
}

USparseVoxelGrid* UVoxelChunk::GetSparseVoxelGrid() const
{
		return SparseVoxelGrid;
}

int16& UVoxelChunk::GetVoxelSize()
{
	return VoxelSize;
}

TMap<FVector, float> UVoxelChunk::GetActiveVoxels() const
{
	return GetSparseVoxelGrid()->GetVoxels();
}

//ToDo: Give this proper arguments and structure to function properly, pull data from SparseVoxelGrid for it if necessary.
void UVoxelChunk::GenerateMesh() const
{
	if (!SparseVoxelGrid)
	{
		UE_LOG(LogTemp, Error, TEXT("SparseVoxelGrid is null!"));
		return;
	}

	if (MarchingCubesGenerator != nullptr)
	{
		MarchingCubesGenerator->GenerateMesh(this);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("MarchingCubesGenerator is null!"));
	}

}
