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

bool UVoxelChunk::SaveChunkData(const FString& FilePath)
{
	if (SparseVoxelGrid)
	{
		return SparseVoxelGrid->SaveVoxelDataToFile(FilePath);
	}
	return false;
}

bool UVoxelChunk::LoadChunkData(const FString& FilePath)
{
	if (SparseVoxelGrid)
	{
		return SparseVoxelGrid->LoadVoxelDataFromFile(FilePath);
	}
	return false;
}


void UVoxelChunk::ApplyBrushStroke(FBrushStroke& Stroke)
{
	
	if (!DiggerManager)
	{
		UE_LOG(LogTemp, Error, TEXT("DiggerManager is null in UVoxelChunk::ApplyBrushStroke"));
		return;
	}
	
	UE_LOG(LogTemp, Warning, TEXT("Applying Brush Stroke. Chunk ID: %d, Dirty: %d"), SectionIndex, bIsDirty);
	UE_LOG(LogTemp, Warning, TEXT("[ApplyBrushStroke] BrushPosition (World): %s, Chunk SectionIndex: %d"), *Stroke.BrushPosition.ToString(), SectionIndex);

	

	switch (Stroke.BrushType)
	{
	case EVoxelBrushType::Cube:
		ApplyCubeBrush( Stroke.BrushPosition, Stroke.BrushRadius / 2, Stroke.bDig, Stroke.BrushRotation);
		break;
	case EVoxelBrushType::Sphere:
		ApplySphereBrush( Stroke.BrushPosition, Stroke.BrushRadius, Stroke.bDig);
		break;
	case EVoxelBrushType::Cylinder:
		ApplyCylinderBrush( Stroke.BrushPosition, Stroke.BrushLength, Stroke.BrushRadius * .5f, Stroke.bDig, Stroke.BrushRotation);
		break;
	case EVoxelBrushType::Cone:
		ApplyConeBrush( Stroke.BrushPosition, Stroke.BrushLength, Stroke.BrushAngle, Stroke.bDig, Stroke.BrushRotation);
		break;
	case EVoxelBrushType::Smooth:
		ApplySmoothBrush( Stroke.BrushPosition, Stroke.BrushRadius, Stroke.bDig, 1);
		break;
	default:
		UE_LOG(LogTemp, Warning, TEXT("Invalid BrushType: %d"), (int32)Stroke.BrushType);
		break;
	}

	MarkDirty();

	UE_LOG(LogTemp, Warning, TEXT("BrushStroke applied successfully. Chunk ID: %d"), SectionIndex);
}

void UVoxelChunk::BakeToStaticMesh(bool bEnableCollision, bool bEnableNanite, float DetailReduction,
	const FString& String)
{
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



void UVoxelChunk::ApplyCubeBrush(FVector3d BrushPosition, float HalfSize, bool bDig, const FRotator& Rotation)
{
    FTransform BrushTransform(Rotation, BrushPosition);
    FTransform InvBrushTransform = BrushTransform.Inverse();

    // Compute bounds in world space (expand for transition)
    FIntVector VoxelCenter = WorldToVoxelCoordinates(BrushPosition);
    int32 MinX = VoxelCenter.X - HalfSize - 2;
    int32 MaxX = VoxelCenter.X + HalfSize + 2;
    int32 MinY = VoxelCenter.Y - HalfSize - 2;
    int32 MaxY = VoxelCenter.Y + HalfSize + 2;
    int32 MinZ = VoxelCenter.Z - HalfSize - 2;
    int32 MaxZ = VoxelCenter.Z + HalfSize + 2;

    for (int32 X = MinX; X <= MaxX; ++X)
    for (int32 Y = MinY; Y <= MaxY; ++Y)
    for (int32 Z = MinZ; Z <= MaxZ; ++Z)
    {
        FVector3d WorldPosition = VoxelToWorldCoordinates(FIntVector(X, Y, Z));
        FVector3d LocalPos = InvBrushTransform.TransformPosition(WorldPosition);

        float MaxDistance = FMath::Max3(FMath::Abs(LocalPos.X), FMath::Abs(LocalPos.Y), FMath::Abs(LocalPos.Z));
        float NormalizedDist = MaxDistance / HalfSize;

        float SDFValue = ComputeSDFValue(NormalizedDist, bDig, 1.0f, 1.5f);
        SetVoxel(X, Y, Z, SDFValue, bDig);
    }
}


void UVoxelChunk::ApplyCylinderBrush(
    FVector3d BrushPosition,
    float Height,
    float Radius,
    bool bDig,
    const FRotator& Rotation)
{
    FTransform BrushTransform(Rotation, BrushPosition);
    FTransform InvBrushTransform = BrushTransform.Inverse();

    float TransitionEnd = 1.7f;
    float HalfHeight = Height * 0.5f;

    // Compute bounds in world space (expand for transition)
    FIntVector VoxelCenter = WorldToVoxelCoordinates(BrushPosition);
    int32 MinX = FMath::FloorToInt(VoxelCenter.X - Radius * TransitionEnd - 2);
    int32 MaxX = FMath::CeilToInt(VoxelCenter.X + Radius * TransitionEnd + 2);
    int32 MinY = FMath::FloorToInt(VoxelCenter.Y - Radius * TransitionEnd - 2);
    int32 MaxY = FMath::CeilToInt(VoxelCenter.Y + Radius * TransitionEnd + 2);
    int32 MinZ = FMath::FloorToInt(VoxelCenter.Z - HalfHeight * TransitionEnd - 2);
    int32 MaxZ = FMath::CeilToInt(VoxelCenter.Z + HalfHeight * TransitionEnd + 2);

    for (int32 X = MinX; X <= MaxX; ++X)
    for (int32 Y = MinY; Y <= MaxY; ++Y)
    for (int32 Z = MinZ; Z <= MaxZ; ++Z)
    {
        FVector3d VoxelPosition = VoxelToWorldCoordinates(FIntVector(X, Y, Z));
        FVector3d LocalPos = InvBrushTransform.TransformPosition(VoxelPosition);

        // Robust SDF for finite cylinder
        float dXY = FVector2D(LocalPos.X, LocalPos.Y).Size() - Radius;
        float dZ = FMath::Abs(LocalPos.Z) - HalfHeight;
        float dist = FMath::Min(FMath::Max(dXY, dZ), 0.0f) + FVector2D(FMath::Max(dXY, 0.0f), FMath::Max(dZ, 0.0f)).Size();

        // Always process voxels in the full transition zone
        if (dist <= Radius * (TransitionEnd - 1.0f))
        {
            float SDFValue = ComputeSDFValue(dist / (Radius * (TransitionEnd - 1.0f)), bDig, 1.0f, TransitionEnd);
            SetVoxel(X, Y, Z, SDFValue, bDig);
        }
    }

    // Optional: Light smoothing pass for watertightness
    ApplySmoothBrush(BrushPosition, FMath::Max(Radius, Height) * 1.1f, false, 3);
}

void UVoxelChunk::ApplyConeBrush(
    FVector3d BrushPosition,
    float Height,
    float Angle, // in degrees
    bool bDig,
    const FRotator& Rotation)
{
    float AngleRad = FMath::DegreesToRadians(Angle);
    float RadiusAtBase = Height * FMath::Tan(AngleRad);

    FTransform BrushTransform(Rotation, BrushPosition);
    FTransform InvBrushTransform = BrushTransform.Inverse();

    float TransitionEnd = 1.7f;

    FIntVector VoxelCenter = WorldToVoxelCoordinates(BrushPosition);
    int32 MinX = FMath::FloorToInt(VoxelCenter.X - RadiusAtBase * TransitionEnd - 2);
    int32 MaxX = FMath::CeilToInt(VoxelCenter.X + RadiusAtBase * TransitionEnd + 2);
    int32 MinY = FMath::FloorToInt(VoxelCenter.Y - RadiusAtBase * TransitionEnd - 2);
    int32 MaxY = FMath::CeilToInt(VoxelCenter.Y + RadiusAtBase * TransitionEnd + 2);
    int32 MinZ = VoxelCenter.Z - 2;
    int32 MaxZ = FMath::CeilToInt(VoxelCenter.Z + Height * TransitionEnd + 2);

    constexpr float Epsilon = 0.5f; // Helps fill edge voxels

    for (int32 X = MinX; X <= MaxX; ++X)
    for (int32 Y = MinY; Y <= MaxY; ++Y)
    for (int32 Z = MinZ; Z <= MaxZ; ++Z)
    {
        FVector3d VoxelPosition = VoxelToWorldCoordinates(FIntVector(X, Y, Z));
        FVector3d LocalPos = InvBrushTransform.TransformPosition(VoxelPosition);

        // Only process voxels within the cone's height
        if (LocalPos.Z >= 0 && LocalPos.Z <= Height)
        {
            float r = (Height - LocalPos.Z) * FMath::Tan(AngleRad);
            r = FMath::Max(0.0f, r); // Clamp to zero at the tip

            float dXY = FVector2D(LocalPos.X, LocalPos.Y).Size();

            // Robust inclusion test: add epsilon and use <=
            if (dXY <= r + Epsilon)
            {
                // SDF for smooth transition (optional, or just set value directly)
                float dist = dXY - r;
                if (dist <= RadiusAtBase * (TransitionEnd - 1.0f))
                {
                    float SDFValue = ComputeSDFValue(dist / (RadiusAtBase * (TransitionEnd - 1.0f)), bDig, 1.0f, TransitionEnd);
                    SetVoxel(X, Y, Z, SDFValue, bDig);
                }
            }
        }
    }

    // Optional: Light smoothing pass for watertightness
    ApplySmoothBrush(BrushPosition, FMath::Max(RadiusAtBase, Height) * 1.1f, false, 3);
}




void UVoxelChunk::ApplySmoothBrush(const FVector& Center, float Radius, bool bDig, int NumIterations)
{
    // Collect affected voxels
    TArray<FIntVector> VoxelsToSmooth;
    FIntVector CenterVoxel = WorldToVoxelCoordinates(Center);
    int32 VoxelRadius = FMath::CeilToInt(Radius / GetVoxelSize());

    for (int32 X = CenterVoxel.X - VoxelRadius; X <= CenterVoxel.X + VoxelRadius; ++X)
    for (int32 Y = CenterVoxel.Y - VoxelRadius; Y <= CenterVoxel.Y + VoxelRadius; ++Y)
    for (int32 Z = CenterVoxel.Z - VoxelRadius; Z <= CenterVoxel.Z + VoxelRadius; ++Z)
    {
        FVector VoxelWorldPos = VoxelToWorldCoordinates(FIntVector(X, Y, Z));
        if (FVector::Dist(VoxelWorldPos, Center) <= Radius)
        {
            VoxelsToSmooth.Add(FIntVector(X, Y, Z));
        }
    }

    // We'll use a local copy of SDF values for iterative smoothing
    TMap<FIntVector, float> CurrentSDFValues;
    for (const FIntVector& Voxel : VoxelsToSmooth)
    {
        CurrentSDFValues.Add(Voxel, GetVoxel(Voxel.X, Voxel.Y, Voxel.Z));
    }

    for (int32 Iter = 0; Iter < NumIterations; ++Iter)
    {
        TMap<FIntVector, float> NewSDFValues;

        for (const FIntVector& Voxel : VoxelsToSmooth)
        {
            float Sum = 0.f;
            float CenterSDF = CurrentSDFValues[Voxel];
            int32 Count = 0;

            // 3x3x3 neighborhood
            for (int32 dX = -1; dX <= 1; ++dX)
            for (int32 dY = -1; dY <= 1; ++dY)
            for (int32 dZ = -1; dZ <= 1; ++dZ)
            {
                FIntVector Neighbor = Voxel + FIntVector(dX, dY, dZ);
                float NeighborSDF;
                if (CurrentSDFValues.Contains(Neighbor))
                {
                    NeighborSDF = CurrentSDFValues[Neighbor];
                }
                else
                {
                    NeighborSDF = GetVoxel(Neighbor.X, Neighbor.Y, Neighbor.Z);
                }
                Sum += NeighborSDF;
                ++Count;
            }

            float Avg = Sum / Count;

            if (!bDig)
            {
                // Smooth: blend towards average
                float Smoothed = FMath::Lerp(CenterSDF, Avg, 0.5f); // 0.5f = smoothing strength
                NewSDFValues.Add(Voxel, Smoothed);
            }
            else
            {
                // Sharpen: exaggerate difference from average
                float Sharpened = CenterSDF + (CenterSDF - Avg) * 0.5f; // 0.5f = sharpening strength
                NewSDFValues.Add(Voxel, Sharpened);
            }
        }

        // Prepare for next iteration
        CurrentSDFValues = NewSDFValues;
    }

    // Apply new SDF values
    for (const auto& Pair : CurrentSDFValues)
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

	
	// --- Island Detection ---
	TArray<FIslandData> Islands = SparseVoxelGrid->DetectIslands(0.0f);
	if (Islands.Num() > 0)
	{
	    UE_LOG(LogTemp, Warning, TEXT("Island detection: %d islands found!"), Islands.Num());
	    for (int32 i = 0; i < Islands.Num(); ++i)
	    {
	        UE_LOG(LogTemp, Warning, TEXT("  Island %d: %d voxels"), i, Islands[i].VoxelCount);
	        // Or, to double-check:
	        // UE_LOG(LogTemp, Warning, TEXT("  Island %d: %d voxels (Voxels.Num: %d)"), i, Islands[i].VoxelCount, Islands[i].Voxels.Num());
	    }
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
