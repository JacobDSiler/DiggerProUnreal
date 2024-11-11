#include "VoxelChunk.h"
#include "DiggerManager.h"
#include "EngineUtils.h"
#include "MarchingCubes.h"
#include "SparseVoxelGrid.h"
#include "VoxelBrushTypes.h"
#include "Async/Async.h"


UVoxelChunk::UVoxelChunk()
	: ChunkCoordinates(FIntVector::ZeroValue), TerrainGridSize(100), Subdivisions(4), VoxelSize(100),SectionIndex(0),
	  bIsDirty(false),
	  DiggerManager(nullptr),
SparseVoxelGrid(CreateDefaultSubobject<USparseVoxelGrid>(TEXT("SparseVoxelGrid")))
{
	// Initialize the SparseVoxelGrid
	SparseVoxelGrid->Initialize( this); 
    
	MarchingCubesGenerator = CreateDefaultSubobject<UMarchingCubes>(TEXT("MarchingCubesGenerator"));
}

//Change a world space coordinate into chunk space
FIntVector UVoxelChunk::WorldToChunkCoordinates(const FVector& WorldCoords) const
{return FIntVector(WorldCoords / (ChunkSize * VoxelSize));}

//Change Chunk Coordinates to world space.
FVector UVoxelChunk::ChunkToWorldCoordinates(const FVector& ChunkCoords) const
{    return FVector(ChunkCoords) * ChunkSize * VoxelSize;}

void UVoxelChunk::SetUniqueSectionIndex() {
	// Ensure unique IDs are assigned correctly
	static int32 GlobalChunkID = 0;

	// Set the section index to the length of the ChunkMap plus one
	SectionIndex = GlobalChunkID;
	UE_LOG(LogTemp, Warning, TEXT("SectionID Set to: %i for ChunkCoordinates X=%d Y=%d Z=%d"), SectionIndex, ChunkCoordinates.X, ChunkCoordinates.Y, ChunkCoordinates.Z);

	GlobalChunkID++;
	UE_LOG(LogTemp, Warning, TEXT("Chunk Section ID set with GlobalChunkID#: %i"), GlobalChunkID);
}



void UVoxelChunk::InitializeChunk(const FIntVector& InChunkCoordinates, ADiggerManager* InDiggerManager)
{
	ChunkCoordinates = InChunkCoordinates;
	UE_LOG(LogTemp, Error, TEXT("Chunk created at X: %i Y: %i Z: %i"), ChunkCoordinates.X, ChunkCoordinates.Y, ChunkCoordinates.Z);

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
	VoxelSize = TerrainGridSize/Subdivisions;
	World = DiggerManager->GetWorldFromManager();

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

void UVoxelChunk::InitializeMeshComponent(UProceduralMeshComponent* MeshComponent)
{
	ProceduralMeshComponent = MeshComponent;
}


void UVoxelChunk::InitializeDiggerManager(ADiggerManager* InDiggerManager)
{
	if(!DiggerManager) DiggerManager = InDiggerManager;
}

void UVoxelChunk::DebugDrawChunk()
{
	if (!World) World = DiggerManager->GetWorldFromManager();
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
	if (bIsDirty)
	{
			GenerateMesh();
			bIsDirty = false; // Reset dirty flag
	}
}

void UVoxelChunk::ForceUpdate()
{
	// Perform the update (e.g., regenerate the mesh)
	// Regenerate the mesh only if dirty
	GenerateMesh();
		
	// Reset the dirty flag
	bIsDirty = false;
}



void UVoxelChunk::ApplyBrushStroke(FBrushStroke& Stroke)
{
	
	if (!DiggerManager)
	{
		UE_LOG(LogTemp, Error, TEXT("DiggerManager is null in UVoxelChunk::ApplyBrushStroke"));
		return;
	}
	
	UE_LOG(LogTemp, Warning, TEXT("Applying Brush Stroke. Chunk ID: %d, Dirty: %d"), SectionIndex, bIsDirty);

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

	UE_LOG(LogTemp, Warning, TEXT("BrushStroke applied successfully. Chunk ID: %d"), SectionIndex);
}

// Improved sphere brush with correct SDF gradients
void UVoxelChunk::ApplySphereBrush(FVector3d BrushPosition, float Radius, bool bDig)
{
    FIntVector VoxelCenter = WorldToVoxelCoordinates(BrushPosition);
    float RadiusSquared = Radius * Radius;

    // Set bounds with transition space
    int32 MinX = FMath::FloorToInt(VoxelCenter.X - Radius - 1)-2;
    int32 MaxX = FMath::CeilToInt(VoxelCenter.X + Radius + 1)+2;
    int32 MinY = FMath::FloorToInt(VoxelCenter.Y - Radius - 1)-2;
    int32 MaxY = FMath::CeilToInt(VoxelCenter.Y + Radius + 1)+2;
    int32 MinZ = FMath::FloorToInt(VoxelCenter.Z - Radius - 1)-2;
    int32 MaxZ = FMath::CeilToInt(VoxelCenter.Z + Radius + 1)+2;

    for (int32 X = MinX; X <= MaxX; ++X)
    {
        for (int32 Y = MinY; Y <= MaxY; ++Y)
        {
            for (int32 Z = MinZ; Z <= MaxZ; ++Z)
            {
                FVector3d WorldPosition = VoxelToWorldCoordinates(FIntVector(X, Y, Z));
                float DistanceSquared = FVector3d::DistSquared(WorldPosition, BrushPosition);

                if (DistanceSquared <= RadiusSquared)
                {
                    float Distance = FMath::Sqrt(DistanceSquared);
                    float SignedDistance = Distance - Radius; // Distance from surface

                    float SDFValue;

                    if (bDig)
                    {
                        bool bIsAboveLandscape = !SparseVoxelGrid->IsPointAboveLandscape(WorldPosition);

                        if (bIsAboveLandscape)
                        {
                            // Original logic for above the landscape
                            if (SignedDistance < -Radius * 0.3f)
                            {
                                SDFValue = 1.0f; // Solid
                            }
                            else if (SignedDistance < Radius * 0.3f)
                            {
                                float T = (SignedDistance + Radius * 0.3f) / (2 * Radius * 0.3f);
                                T = FMath::SmoothStep(0.0f, 1.0f, T);
                                SDFValue = FMath::Lerp(1.0f, -1.0f, T);
                            }
                            else
                            {
                                SDFValue = -1.0f; // Air
                            }
                        }
                        else
                        {
                            // New logic for under the landscape
                            if (SignedDistance < -Radius * 0.3f)
                            {
                                SDFValue = -1.0f; // Air
                            }
                            else if (SignedDistance < Radius * 0.3f)
                            {
                                float T = (SignedDistance + Radius * 0.3f) / (2 * Radius * 0.3f);
                                T = FMath::SmoothStep(0.0f, 1.0f, T);
                                SDFValue = FMath::Lerp(-1.0f, 1.0f, T);
                            }
                            else
                            {
                                SDFValue = 1.0f; // Solid
                            }
                        }
                    }
                    else
                    {
                        // Additive mode logic
                        float NormalizedDist = Distance / Radius;
                        if (NormalizedDist <= 1.0f)
                        {
                            SDFValue = -1.0f;
                        }
                        else if (NormalizedDist <= 1.1f)
                        {
                            float T = (NormalizedDist - 1.0f) / 0.1f;
                            T = FMath::SmoothStep(0.0f, 1.0f, T);
                            SDFValue = FMath::Lerp(-1.0f, 1.0f, T);
                        }
                        else
                        {
                            SDFValue = 1.0f;
                        }
                    }

                    SetVoxel(X, Y, Z, SDFValue, bDig);
                }
            }
        }
    }
}


void UVoxelChunk::ApplyCubeBrush(FVector3d BrushPosition, float Size, bool bDig)
{
    float HalfSize = Size;
    FIntVector VoxelCenter = WorldToVoxelCoordinates(BrushPosition);

    int32 VoxelRadius = FMath::CeilToInt(HalfSize / VoxelSize);

    // Adjust bounds to ensure full enclosure
    int32 MinX = VoxelCenter.X - VoxelRadius;
    int32 MaxX = VoxelCenter.X + VoxelRadius;
    int32 MinY = VoxelCenter.Y - VoxelRadius;
    int32 MaxY = VoxelCenter.Y + VoxelRadius;
    int32 MinZ = VoxelCenter.Z - VoxelRadius;
    int32 MaxZ = VoxelCenter.Z + VoxelRadius;

    // Include an offset to ensure boundaries are covered
    MinX -= 3;
    MinY -= 3;
    MinZ -= 3;

    for (int32 X = MinX; X <= MaxX; ++X)
    {
        for (int32 Y = MinY; Y <= MaxY; ++Y)
        {
            for (int32 Z = MinZ; Z <= MaxZ; ++Z)
            {
                FVector3d WorldPosition = VoxelToWorldCoordinates(FIntVector(X, Y, Z));
                float DistanceX = FMath::Abs(WorldPosition.X - BrushPosition.X);
                float DistanceY = FMath::Abs(WorldPosition.Y - BrushPosition.Y);
                float DistanceZ = FMath::Abs(WorldPosition.Z - BrushPosition.Z);
                float MaxDistance = FMath::Max3(DistanceX, DistanceY, DistanceZ);
                float NormalizedDist = MaxDistance / HalfSize;
                float SDFValue;

                if (bDig)
                {
                    // Dig mode: completely clear voxels within the brush
                    if (NormalizedDist <= 1.0f)
                    {
                        SDFValue = 1.0f; // Air
                    }
                    else
                    {
                        // Skip setting voxel outside the brush
                        continue;
                    }
                }
                else
                {
                    // Additive mode: keep original logic with smoother transitions
                    if (NormalizedDist <= 1.0f)
                    {
                        SDFValue = -1.0f;
                    }
                    else if (NormalizedDist <= 1.3f)
                    {
                        float T = (NormalizedDist - 1.0f) / 0.3f;
                        T = FMath::SmoothStep(0.0f, 1.0f, T);
                        SDFValue = FMath::Lerp(-1.0f, 1.0f, T);
                    }
                    else
                    {
                        SDFValue = 1.0f;
                    }
                }

                SetVoxel(X, Y, Z, SDFValue, bDig);
            }
        }
    }
}


void UVoxelChunk::ApplyConeBrush(FVector3d BrushPosition, float Height, float Angle, bool bDig)
{
    FIntVector VoxelCenter = WorldToVoxelCoordinates(BrushPosition);
    float RadiusAtBase = Height * FMath::Tan(FMath::DegreesToRadians(Angle));
    
    // Set bounds with additional transition space
    int32 MinX = FMath::FloorToInt(VoxelCenter.X - RadiusAtBase - 2);
    int32 MaxX = FMath::CeilToInt(VoxelCenter.X + RadiusAtBase + 2);
    int32 MinY = FMath::FloorToInt(VoxelCenter.Y - RadiusAtBase - 2);
    int32 MaxY = FMath::CeilToInt(VoxelCenter.Y + RadiusAtBase + 2);
    int32 MinZ = VoxelCenter.Z - 1;
    int32 MaxZ = FMath::CeilToInt(VoxelCenter.Z + Height + 2);

    for (int32 X = MinX-1; X <= MaxX+1; ++X)
    {
        for (int32 Y = MinY-1; Y <= MaxY+1; ++Y)
        {
            for (int32 Z = MinZ-1; Z <= MaxZ+1; ++Z)
            {
                FVector3d VoxelPosition = VoxelToWorldCoordinates(FIntVector(X, Y, Z));
                FVector3d RelativePosition = VoxelPosition - BrushPosition;
                float DistanceXY = FVector2D(RelativePosition.X, RelativePosition.Y).Size();
                float DistanceZ = RelativePosition.Z;

                if (DistanceZ <= Height && DistanceXY <= (Height - DistanceZ) * FMath::Tan(FMath::DegreesToRadians(Angle)))
                {
                    float NormalizedDist = DistanceXY / RadiusAtBase;
                    float SDFValue;

                    if (bDig)
                    {
                        // Dig mode: create air voxels
                        if (NormalizedDist <= 1.2f)
                        {
                            SDFValue = 1.0f; // Air
                        }
                        else if (NormalizedDist <= 1.0f)
                        {
                            float T = (NormalizedDist - 1.0f) / 0.3f;
                            T = FMath::SmoothStep(0.0f, 1.0f, T);
                            SDFValue = FMath::Lerp(1.0f, -1.0f, T);
                        }
                        else
                        {
                            SDFValue = -1.0f;
                        }
                    }
                    else
                    {
                        // Additive mode: smooth transitions
                        if (NormalizedDist <= 1.0f)
                        {
                            SDFValue = -1.0f;
                        }
                        else if (NormalizedDist <= 1.3f)
                        {
                            float T = (NormalizedDist - 1.0f) / 0.3f;
                            T = FMath::SmoothStep(0.0f, 1.0f, T);
                            SDFValue = FMath::Lerp(-1.0f, 1.0f, T);
                        }
                        else
                        {
                            SDFValue = 1.0f;
                        }
                    }

                    SetVoxel(X, Y, Z, SDFValue, bDig);
                }
            }
        }
    }
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
FVector UVoxelChunk::VoxelToWorldCoordinates(const FIntVector& VoxelCoords) const {
	FVector ChunkWorldPos = DiggerManager->ChunkToWorldCoordinates(ChunkCoordinates.X, ChunkCoordinates.Y, ChunkCoordinates.Z);
	return ChunkWorldPos + FVector(VoxelCoords) * VoxelSize;
}

// Converts world coordinates to chunk-local voxel coordinates
FIntVector UVoxelChunk::WorldToVoxelCoordinates(const FVector& WorldCoords) const {
	FVector ChunkWorldPos = DiggerManager->ChunkToWorldCoordinates(ChunkCoordinates.X, ChunkCoordinates.Y, ChunkCoordinates.Z);
	return FIntVector((WorldCoords - ChunkWorldPos) / VoxelSize);
}



void UVoxelChunk::SetVoxel(int32 X, int32 Y, int32 Z, const float SDFValue,const bool bDig) const
{
	if (SparseVoxelGrid) //&& IsValidChunkLocalCoordinate(X, Y, Z))
	{
		SparseVoxelGrid->SetVoxel(X, Y, Z, SDFValue, bDig);
	}
}

void UVoxelChunk::SetVoxel(const FVector& Position, const float SDFValue,const bool bDig) const
{
	int32 X = Position.X;
	int32 Y = Position.Y;
	int32 Z = Position.Z;

	SetVoxel(X, Y, Z, SDFValue, bDig);
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

	if (MarchingCubesGenerator == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("MarchingCubesGenerator is nullptr in UVoxelChunk::UpdateIfDirty"));
		return;
	}

	if (MarchingCubesGenerator != nullptr)
	{
		MarchingCubesGenerator->GenerateMesh(this);
	}

}
