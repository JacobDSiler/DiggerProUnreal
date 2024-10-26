#include "VoxelChunk.h"
#include "DiggerManager.h"
#include "EngineUtils.h"
#include "MarchingCubes.h"
#include "SparseVoxelGrid.h"
#include "VoxelBrushShape.h"
#include "VoxelBrushTypes.h"
#include "Async/Async.h"
#include "Net/Core/Connection/NetConnectionFaultRecoveryBase.h"


UVoxelChunk::UVoxelChunk()
	: ChunkCoordinates(FIntVector::ZeroValue), TerrainGridSize(100), Subdivisions(4), VoxelSize(100),SectionIndex(-1),
	  DiggerManager(nullptr),
	  SparseVoxelGrid(CreateDefaultSubobject<USparseVoxelGrid>(TEXT("SparseVoxelGrid"))),
bIsDirty(false)
{
	// Initialize the SparseVoxelGrid
	SparseVoxelGrid->Initialize( this); 
	SparseVoxelGrid->InitializeDiggerManager(); 
    
	MarchingCubesGenerator = CreateDefaultSubobject<UMarchingCubes>(TEXT("MarchingCubesGenerator"));
}

//Change a world space coordinate into chunk space
FIntVector UVoxelChunk::WorldToChunkCoordinates(const FVector& WorldCoords) const
{return FIntVector(WorldCoords / (ChunkSize * VoxelSize));}

//Change Chunk Coordinates to world space.
FVector UVoxelChunk::ChunkToWorldCoordinates(const FVector& ChunkCoords) const
{    return FVector(ChunkCoords) * ChunkSize * VoxelSize;}

void UVoxelChunk::SetUniqueSectionIndex()
{
	// Format coordinates as positive/negative strings
	auto FormatCoordinate = [](int32 Coordinate) -> FString {
		if (Coordinate < 0)
		{
			return "N" + FString::FromInt(FMath::Abs(Coordinate)); // Prefix with 'N' for negative
		}
		else
		{
			return "P" + FString::FromInt(Coordinate); // Prefix with 'P' for positive
		}
	};

	// Turn the coordinates into unique strings
	FString XString = FormatCoordinate(ChunkCoordinates.X);
	FString YString = FormatCoordinate(ChunkCoordinates.Y);
	FString ZString = FormatCoordinate(ChunkCoordinates.Z);

	// Concatenate the strings
	FString ConcatenatedString = XString + YString + ZString;

	// Convert the concatenated string to a number using CRC32, ensuring the result is positive
	uint32 RawSectionIndex = FCrc::StrCrc32(*ConcatenatedString);

	// Make sure the SectionIndex is within bounds
	SectionIndex = RawSectionIndex % 16770;  // Bound the index to the array size

	UE_LOG(LogTemp, Warning, TEXT("SectionID Set to: %i for ChunkCoordinates X=%d Y=%d Z=%d"), SectionIndex, ChunkCoordinates.X, ChunkCoordinates.Y, ChunkCoordinates.Z);
}



void UVoxelChunk::InitializeChunk(const FIntVector& InChunkCoordinates)
{
	ChunkCoordinates = InChunkCoordinates;
	UE_LOG(LogTemp, Error, TEXT("Chunk created at X: %i Y: %i Z: %i"), ChunkCoordinates.X, ChunkCoordinates.Y, ChunkCoordinates.Z);
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

	ChunkSize=DiggerManager->ChunkSize;
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
	UWorld* World = GetWorld();
	if (!World) return;

	FVector ChunkCenter = FVector(ChunkCoordinates) * ChunkSize * TerrainGridSize;
	FVector ChunkExtent = FVector(ChunkSize * TerrainGridSize / 2.0f);

	UE_LOG(LogTemp, Warning, TEXT("DebugDrawChunk: World is valid, continuing to render the chunk bounds. ChunkCenter: %s, ChunkExtent: %s"),
	   *ChunkCenter.ToString(), *ChunkExtent.ToString());
	
	DrawDebugBox(World, ChunkCenter, ChunkExtent, FColor::Red, false, 15.0f);
}

void UVoxelChunk::DebugPrintVoxelData() const
{
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
	if (!bIsDirty) return;  // Early exit if not dirty
	
		// Perform the update (e.g., regenerate the mesh)
		// Regenerate the mesh only if dirty
		GenerateMeshAsync();
		
		// Reset the dirty flag
		bIsDirty = false;
}

void UVoxelChunk::ForceUpdate()
{
	// Perform the update (e.g., regenerate the mesh)
	// Regenerate the mesh only if dirty
	GenerateMeshAsync();
		
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

void UVoxelChunk::ApplyBrushStroke(FBrushStroke& Stroke)
{
	UE_LOG(LogTemp, Warning, TEXT("BrushType: %d"), (int32)Stroke.BrushType);
	UE_LOG(LogTemp, Warning, TEXT("BrushPosition: X=%f Y=%f Z=%f"), Stroke.BrushPosition.X, Stroke.BrushPosition.Y, Stroke.BrushPosition.Z);
	UE_LOG(LogTemp, Warning, TEXT("BrushRadius: %f"), Stroke.BrushRadius);

	switch (Stroke.BrushType)
	{
	case EVoxelBrushType::Cube:
		ApplyCubeBrush(Stroke.BrushPosition, Stroke.BrushRadius, Stroke.bDig);
		break;
	case EVoxelBrushType::Sphere:
		ApplySphereBrush(Stroke.BrushPosition, Stroke.BrushRadius, Stroke.bDig);
		break;
	case EVoxelBrushType::Cone:
		ApplyConeBrush(Stroke.BrushPosition, Stroke.BrushRadius, 45.0f, Stroke.bDig);
		break;
	default:
		UE_LOG(LogTemp, Warning, TEXT("Invalid BrushType: %d"), (int32)Stroke.BrushType);
		break;
	}

	MarkDirty();
	UE_LOG(LogTemp, Warning, TEXT("BrushStroke applied successfully"));
}

void UVoxelChunk::ApplySphereBrush(FVector3d BrushPosition, float Radius, bool bDig) {
	UE_LOG(LogTemp, Warning, TEXT("Applying Sphere Brush: Position: %s, Radius: %f, Dig: %d"), *BrushPosition.ToString(), Radius, bDig);
	FIntVector VoxelCenter = WorldToVoxelCoordinates(BrushPosition);
	FVector3d MinBounds = FVector3d(VoxelCenter) - FVector3d(Radius + 2);
	FVector3d MaxBounds = FVector3d(VoxelCenter) + FVector3d(Radius + 2);

	for (int32 X = MinBounds.X; X <= MaxBounds.X; ++X) {
		for (int32 Y = MinBounds.Y; Y <= MaxBounds.Y; ++Y) {
			for (int32 Z = MinBounds.Z; Z <= MaxBounds.Z; ++Z) {
				FVector3d VoxelPosition = FVector3d(X, Y, Z);
				FVector3d WorldPosition = VoxelToWorldCoordinates(FIntVector(X, Y, Z));
				float Distance = FVector3d::Dist(WorldPosition, BrushPosition);

				if (Distance <= Radius) {
					float SDFValue = (Distance / Radius) * 2.0f - 1.0f; // Smooth transition between -1.0 and 1.0
					SDFValue = bDig ? -SDFValue : SDFValue;
					SetVoxel(X, Y, Z, SDFValue);
					UE_LOG(LogTemp, Warning, TEXT("Set Voxel at %s with SDFValue: %f"), *VoxelPosition.ToString(), SDFValue);
				}
			}
		}
	}
}




void UVoxelChunk::ApplyCubeBrush(FVector3d BrushPosition, float Size, bool bDig)
{
    FVector3d MinBounds = BrushPosition - FVector3d(Size);
    FVector3d MaxBounds = BrushPosition + FVector3d(Size);

    for (int32 X = MinBounds.X; X <= MaxBounds.X; ++X)
    {
        for (int32 Y = MinBounds.Y; Y <= MaxBounds.Y; ++Y)
        {
            for (int32 Z = MinBounds.Z; Z <= MaxBounds.Z; ++Z)
            {
                FVector3d VoxelPosition = VoxelToWorldCoordinates(FIntVector(X, Y, Z));

                //if (IsVoxelWithinBounds(VoxelPosition, MinBounds, MaxBounds))
               // {
                    SetVoxel(X, Y, Z, bDig ? -1.0f : 1.0f);
               // }
            }
        }
    }
}

void UVoxelChunk::ApplyConeBrush(FVector3d BrushPosition, float Height, float Angle, bool bDig) {
	for (int32 X = 0; X <= ChunkSize; ++X) {
		for (int32 Y = 0; X <= ChunkSize; ++Y) {
			for (int32 Z = 0; Z <= ChunkSize; ++Z) {
				FVector3d VoxelPosition = VoxelToWorldCoordinates(FIntVector(X, Y, Z));
				FVector3d RelativePosition = VoxelPosition - BrushPosition;
				float DistanceXY = FVector2D(RelativePosition.X, RelativePosition.Y).Size();
				float DistanceZ = RelativePosition.Z;

				/*if (FlipOrientation.IsSet() && FlipOrientation.GetValue()) {
					DistanceZ = Height - DistanceZ;  // Flip the cone upside down
				}*/

				if (DistanceZ <= Height && DistanceXY <= (Height - DistanceZ) * FMath::Tan(FMath::DegreesToRadians(Angle))) {
					float SDFValue = bDig ? -1.0f : 1.0f;
					SetVoxel(X, Y, Z, SDFValue);
				}
			}
		}
	}
	MarkDirty();
}


void UVoxelChunk::BakeSingleBrushStroke(FBrushStroke StrokeToBake) {
	// Placeholder for baking SDF values into the chunk
if (SparseVoxelGrid)
{
//	SparseVoxelGrid->BakeBrush();
}
}

bool UVoxelChunk::IsValidChunkLocalCoordinate(FVector Position) const
{
	float LocalX = Position.X;
	float LocalY = Position.Y;
	float LocalZ = Position.Z;

	return IsValidChunkLocalCoordinate(LocalX, LocalY, LocalZ);
}

FVector UVoxelChunk::GetWorldPosition(const FIntVector& VoxelCoordinates) const
{
	// Assuming ChunkPosition is the position of this chunk in the world (in chunk coordinates)
	// and VoxelSize is the size of each voxel in world units.
    
	FVector ChunkWorldPosition = DiggerManager->ChunkToWorldCoordinates(ChunkCoordinates.X, ChunkCoordinates.Y, ChunkCoordinates.Z); // Get chunk world position

	// Calculate the world position by converting the voxel coordinates relative to this chunk
	FVector VoxelWorldPosition = FVector(VoxelCoordinates) * VoxelSize;

	// Add the chunk's world position to the voxel's local position to get the final world position
	return ChunkWorldPosition + VoxelWorldPosition;
}

FVector UVoxelChunk::GetWorldPosition() const
{
	FVector ChunkWorldPosition = DiggerManager->ChunkToWorldCoordinates(ChunkCoordinates.X, ChunkCoordinates.Y, ChunkCoordinates.Z); // Get chunk world position
	return ChunkWorldPosition;
}

// Check if the coordinates are within valid bounds
bool UVoxelChunk::IsValidChunkLocalCoordinate(int32 LocalX, int32 LocalY, int32 LocalZ) const
{
	int32 HalfChunkExtent = (ChunkSize * TerrainGridSize / Subdivisions) / 2;
	FVector ChunkPosition = FVector(GetChunkPosition()) * ChunkSize;

	return (LocalX >= ChunkPosition.X - HalfChunkExtent && LocalX < ChunkPosition.X + HalfChunkExtent) &&
		   (LocalY >= ChunkPosition.Y - HalfChunkExtent && LocalY < ChunkPosition.Y + HalfChunkExtent) &&
		   (LocalZ >= ChunkPosition.Z - HalfChunkExtent && LocalZ < ChunkPosition.Z + HalfChunkExtent);
}


// Converts chunk-local voxel coordinates to world space
FVector UVoxelChunk::VoxelToWorldCoordinates(const FIntVector& VoxelCoords) const
{
	FVector ChunkWorldPos = DiggerManager->ChunkToWorldCoordinates(ChunkCoordinates.X, ChunkCoordinates.Y, ChunkCoordinates.Z);
	return ChunkWorldPos + FVector(VoxelCoords) * VoxelSize;
}

// Converts world coordinates to chunk-local voxel coordinates
FIntVector UVoxelChunk::WorldToVoxelCoordinates(const FVector& WorldCoords) const
{
	FVector ChunkWorldPos = DiggerManager->ChunkToWorldCoordinates(ChunkCoordinates.X, ChunkCoordinates.Y, ChunkCoordinates.Z);
	return FIntVector((WorldCoords - ChunkWorldPos) / VoxelSize);
}


void UVoxelChunk::SetVoxel(int32 X, int32 Y, int32 Z, const float SDFValue) const
{
	if (SparseVoxelGrid) //&& IsValidChunkLocalCoordinate(X, Y, Z))
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
	/*if (!IsValidChunkLocalCoordinate(X, Y, Z))
	{
		UE_LOG(LogTemp, Warning, TEXT("Attempt to access out-of-bounds voxel at X=%d, Y=%d, Z=%d"), X, Y, Z);
		return 1.0f;  // Return default air value for out-of-bounds voxels
	}*/

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
	if (!IsValidChunkLocalCoordinate(X, Y, Z))
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

void UVoxelChunk::GenerateMeshAsync() const
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
