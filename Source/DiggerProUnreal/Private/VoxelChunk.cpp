#include "VoxelChunk.h"
#include "DiggerManager.h"
#include "EngineUtils.h"
#include "MarchingCubes.h"
#include "SparseVoxelGrid.h"
#include "VoxelBrushTypes.h"
#include "Async/Async.h"
#include "Kismet/GameplayStatics.h"


UVoxelChunk::UVoxelChunk()
	: ChunkCoordinates(FIntVector::ZeroValue), 
	  TerrainGridSize(100), 
	  Subdivisions(4), 
	  VoxelSize(TerrainGridSize/Subdivisions),  // Calculate correctly from the start
	  SectionIndex(0),
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
{
    const float ChunkWorldSize = ChunkSize * TerrainGridSize;
    return FIntVector(
        FMath::FloorToInt(WorldCoords.X / ChunkWorldSize),
        FMath::FloorToInt(WorldCoords.Y / ChunkWorldSize),
        FMath::FloorToInt(WorldCoords.Z / ChunkWorldSize)
    );
}


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

// Improved sphere brush with correct SDF gradients and fixed maximal corner handling
void UVoxelChunk::ApplySphereBrush(FVector3d BrushPosition, float Radius, bool bDig)
{
    FIntVector VoxelCenter = WorldToVoxelCoordinates(BrushPosition);

    // Define radii
    float InnerRadius = Radius * 0.7f;
    float TransitionZone = Radius * 0.2f;
    float OuterRadius = Radius * 1.2f;

    float InnerRadiusSquared = InnerRadius * InnerRadius;
    float RadiusSquared = Radius * Radius;
    float OuterRadiusSquared = OuterRadius * OuterRadius;

    // Calculate voxel bounds (+1 to avoid boundary gaps)
    int32 MinX = FMath::FloorToInt(VoxelCenter.X - OuterRadius);
    int32 MaxX = FMath::CeilToInt(VoxelCenter.X + OuterRadius) + 1;
    int32 MinY = FMath::FloorToInt(VoxelCenter.Y - OuterRadius);
    int32 MaxY = FMath::CeilToInt(VoxelCenter.Y + OuterRadius) + 1;
    int32 MinZ = FMath::FloorToInt(VoxelCenter.Z - OuterRadius);
    int32 MaxZ = FMath::CeilToInt(VoxelCenter.Z + OuterRadius) + 1;

    // Terrain height for this X/Y
    float TerrainHeight = DiggerManager->GetLandscapeHeightAt(FVector(BrushPosition));
    int32 TerrainHeightVoxel = WorldToVoxelCoordinates(FVector3d(BrushPosition.X, BrushPosition.Y, TerrainHeight)).Z;
    MinZ = FMath::Min(MinZ, TerrainHeightVoxel - 5);

    // Debug counters
    int32 ModifiedVoxels = 0, FloorVoxels = 0, AirVoxels = 0, WallVoxels = 0;

    float ClearCavityValue = 3.0f;

    for (int32 X = MinX; X <= MaxX; ++X)
    {
        for (int32 Y = MinY; Y <= MaxY; ++Y)
        {
            for (int32 Z = MinZ; Z <= MaxZ; ++Z)
            {
                FVector3d WorldPos = VoxelToWorldCoordinates(FIntVector(X, Y, Z));
                float HorizontalDistSquared = FMath::Square(WorldPos.X - BrushPosition.X) + FMath::Square(WorldPos.Y - BrushPosition.Y);
                float HorizontalDist = FMath::Sqrt(HorizontalDistSquared);
                float VerticalOffset = WorldPos.Z - BrushPosition.Z;
                float DistanceSquared = HorizontalDistSquared + FMath::Square(VerticalOffset);

                if (DistanceSquared > OuterRadiusSquared)
                    continue;

                float Distance = FMath::Sqrt(DistanceSquared);
                float ExistingSDF = GetVoxel(X, Y, Z);
                float LocalTerrainHeight = DiggerManager->GetLandscapeHeightAt(FVector(WorldPos));
                bool isAboveTerrain = WorldPos.Z >= LocalTerrainHeight;

                float SDFValue = ExistingSDF;

                if (bDig)
                {
                    float FloorHeight = BrushPosition.Z - (InnerRadius * 0.7f);
                    bool isBelowFloor = WorldPos.Z < FloorHeight;

                    if (Distance <= InnerRadius)
                    {
                        float NormalizedHeight = (WorldPos.Z - (BrushPosition.Z - InnerRadius)) / (2.0f * InnerRadius);

                        if (NormalizedHeight > 0.2f && NormalizedHeight < 0.9f)
                        {
                            SDFValue = ClearCavityValue;
                            AirVoxels++;
                        }
                        else
                        {
                            SDFValue = 2.0f;
                            AirVoxels++;
                        }

                        if (!isAboveTerrain && isBelowFloor)
                        {
                            float DepthFactor = (FloorHeight - WorldPos.Z) / (FloorHeight - (BrushPosition.Z - InnerRadius));
                            DepthFactor = FMath::Clamp(DepthFactor, 0.0f, 1.0f);

                            if (DepthFactor > 0.2f)
                            {
                                float MaxHoriz = InnerRadius * (1.0f - DepthFactor * 0.6f);

                                if (HorizontalDist < MaxHoriz)
                                {
                                    float NormalizedDepth = (DepthFactor - 0.2f) / 0.8f;
                                    SDFValue = FMath::Lerp(1.5f, -1.0f, NormalizedDepth);

                                    if (NormalizedDepth > 0.8f)
                                    {
                                        SDFValue = -1.0f;
                                        FloorVoxels++;
                                    }
                                }
                            }
                        }
                    }
                    else if (Distance <= Radius)
                    {
                        float T = FMath::SmoothStep(0.0f, 1.0f, (Distance - InnerRadius) / (Radius - InnerRadius));

                        if (isAboveTerrain)
                        {
                            SDFValue = FMath::Lerp(2.0f, ExistingSDF, T * 0.5f);
                        }
                        else
                        {
                            if (HorizontalDist > InnerRadius * 0.8f)
                            {
                                SDFValue = FMath::Lerp(0.5f, -1.0f, T);
                                WallVoxels++;
                            }
                            else if (isBelowFloor)
                            {
                                SDFValue = FMath::Lerp(0.0f, -1.0f, T);
                            }
                            else
                            {
                                SDFValue = FMath::Lerp(2.0f, -0.5f, T);
                            }
                        }
                    }
                    else if (Distance <= OuterRadius)
                    {
                        float T = FMath::SmoothStep(0.0f, 1.0f, (Distance - Radius) / TransitionZone);

                        if (isAboveTerrain)
                        {
                            SDFValue = FMath::Lerp(ExistingSDF * 0.5f, ExistingSDF, T);
                        }
                        else
                        {
                            SDFValue = FMath::Lerp(-0.5f, -1.0f, T);
                            WallVoxels++;
                        }
                    }

                    if (HorizontalDist < InnerRadius * 0.8f &&
                        VerticalOffset > 0 &&
                        WorldPos.Z <= LocalTerrainHeight &&
                        WorldPos.Z >= BrushPosition.Z)
                    {
                        SDFValue = 2.0f;
                        AirVoxels++;
                    }
                }
                else // Additive mode
                {
                    if (Distance <= InnerRadius)
                    {
                        SDFValue = -1.0f;
                    }
                    else if (Distance <= Radius)
                    {
                        float T = FMath::SmoothStep(0.0f, 1.0f, (Distance - InnerRadius) / (Radius - InnerRadius));
                        SDFValue = FMath::Lerp(-1.0f, 1.0f, T);
                    }
                    else if (Distance <= OuterRadius)
                    {
                        float T = FMath::SmoothStep(0.0f, 1.0f, (Distance - Radius) / TransitionZone);
                        SDFValue = FMath::Lerp(ExistingSDF, 1.0f, T);
                    }
                }

                SetVoxel(X, Y, Z, SDFValue,bDig);
                ModifiedVoxels++;
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("Sphere Brush: Modified=%d, Air=%d, Floor=%d, Wall=%d"), ModifiedVoxels, AirVoxels, FloorVoxels, WallVoxels);
}







// Helper function for digging SDF calculation
float UVoxelChunk::CalculateDiggingSDF(
    float Distance,
    float InnerRadius,
    float Radius,
    float OuterRadius,
    float TransitionZone,
    float ExistingSDF,
    bool bIsAboveTerrain,
    float HeightDifferenceFromTerrain)
{
    // Core region
    if (Distance <= InnerRadius)
    {
        return bIsAboveTerrain ? 1.0f : -1.0f;
    }
    
    // Primary transition zone
    if (Distance <= Radius)
    {
        if (bIsAboveTerrain)
            return 1.0f;
            
        float T = (Distance - InnerRadius) / (Radius - InnerRadius);
        T = FMath::SmoothStep(0.0f, 1.0f, T);
        
        // Adjust transition based on height difference from terrain
        float TerrainFactor = FMath::Clamp(HeightDifferenceFromTerrain / (Radius * 0.5f), 0.0f, 1.0f);
        T = FMath::Lerp(T, 1.0f, TerrainFactor);
        
        return FMath::Lerp(1.0f, -1.0f, T);
    }
    
    // Outer transition zone
    if (Distance <= OuterRadius)
    {
        float T = (Distance - Radius) / TransitionZone;
        T = FMath::SmoothStep(0.0f, 1.0f, T);
        
        if (bIsAboveTerrain)
        {
            return FMath::Lerp(1.0f, ExistingSDF, T);
        }
        
        // Preserve solid structure below terrain
        if (ExistingSDF <= -1.0f)
            return ExistingSDF;
            
        return FMath::Lerp(-1.0f, ExistingSDF, T);
    }
    
    return ExistingSDF;
}

// Helper function for additive SDF calculation
float UVoxelChunk::CalculateAdditiveSDF(
    float Distance,
    float InnerRadius,
    float Radius,
    float OuterRadius,
    float TransitionZone,
    float ExistingSDF,
    bool bIsAboveTerrain)
{
    // Core region
    if (Distance <= InnerRadius)
    {
        return -1.0f;
    }
    
    // Primary transition zone
    if (Distance <= Radius)
    {
        float T = (Distance - InnerRadius) / (Radius - InnerRadius);
        T = FMath::SmoothStep(0.0f, 1.0f, T);
        return FMath::Lerp(-1.0f, ExistingSDF, T);
    }
    
    // Outer transition zone
    if (Distance <= OuterRadius && bIsAboveTerrain)
    {
        float T = (Distance - Radius) / TransitionZone;
        T = FMath::SmoothStep(0.0f, 1.0f, T);
        return FMath::Lerp(ExistingSDF, 1.0f, T);
    }
    
    return ExistingSDF;
}

// Optimized terrain height query
float UVoxelChunk::GetTerrainHeightEfficient(ALandscapeProxy* Landscape, const FVector& WorldPosition)
{
    if (!Landscape)
        return 0.0f;
        
    TOptional<float> HeightResult = Landscape->GetHeightAtLocation(WorldPosition, EHeightfieldSource::Simple);
    
    if (!HeightResult.IsSet())
    {
        HeightResult = Landscape->GetHeightAtLocation(WorldPosition, EHeightfieldSource::Complex);
    }
    
    return HeightResult.IsSet() ? HeightResult.GetValue() : 0.0f;
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
    MinX -= 2;
    MinY -= 2;
    MinZ -= 2;

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
                    // Additive mode: keep original logic
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
	UE_LOG(LogTemp, Warning, TEXT("GetWorldPosition 0 called"));
	
	FVector ChunkWorldPosition = DiggerManager->ChunkToWorldCoordinates(ChunkCoordinates.X, ChunkCoordinates.Y, ChunkCoordinates.Z); // Get chunk world position

	// Calculate the world position by converting the voxel coordinates relative to this chunk
	FVector VoxelWorldPosition = FVector(VoxelCoordinates) * VoxelSize;

	// Add the chunk's world position to the voxel's local position to get the final world position
	return ChunkWorldPosition + VoxelWorldPosition;
}

FVector UVoxelChunk::GetWorldPosition() const
{
	// Convert chunk coordinates to world space
	return FVector(
		ChunkCoordinates.X * ChunkSize * VoxelSize,
		ChunkCoordinates.Y * ChunkSize * VoxelSize,
		ChunkCoordinates.Z * ChunkSize * VoxelSize
	);
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
FVector3d UVoxelChunk::VoxelToWorldCoordinates(const FIntVector& VoxelCoord) const
{
    // Convert local to global voxel coordinates
    FIntVector ChunkOrigin = ChunkCoordinates * ChunkSize;
    FIntVector GlobalVoxel = ChunkOrigin + VoxelCoord;
    
    // Convert to world coordinates
    FVector3d WorldPos(
        GlobalVoxel.X * VoxelSize,
        GlobalVoxel.Y * VoxelSize,
        GlobalVoxel.Z * VoxelSize
    );
    
    return WorldPos;
}


// Converts world coordinates to chunk-local voxel coordinates
FIntVector UVoxelChunk::WorldToVoxelCoordinates(const FVector3d& WorldPosition) const
{
    // Debug log the input
    UE_LOG(LogTemp, Verbose, TEXT("WorldToVoxel: Converting %s"), *WorldPosition.ToString());
    
    // First convert world position to global voxel indices
    int32 GlobalX = FMath::FloorToInt(WorldPosition.X / VoxelSize);
    int32 GlobalY = FMath::FloorToInt(WorldPosition.Y / VoxelSize);
    int32 GlobalZ = FMath::FloorToInt(WorldPosition.Z / VoxelSize);
    
    // IMPORTANT: Calculate chunk origin in voxel units
    FIntVector ChunkOriginVoxels = ChunkCoordinates * ChunkSize;
    
    // Convert to chunk-local voxel coordinates
    int32 LocalX = GlobalX - ChunkOriginVoxels.X;
    int32 LocalY = GlobalY - ChunkOriginVoxels.Y;
    int32 LocalZ = GlobalZ - ChunkOriginVoxels.Z;
    
    FIntVector Result(LocalX, LocalY, LocalZ);
    
    // Debug output
    UE_LOG(LogTemp, Verbose, TEXT("WorldToVoxel: World(%f,%f,%f) -> Global(%d,%d,%d) -> Local(%d,%d,%d)"),
           WorldPosition.X, WorldPosition.Y, WorldPosition.Z,
           GlobalX, GlobalY, GlobalZ, LocalX, LocalY, LocalZ);
    
    return Result;
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
