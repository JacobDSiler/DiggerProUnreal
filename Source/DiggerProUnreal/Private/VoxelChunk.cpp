#include "VoxelChunk.h"
#include "DiggerManager.h"
#include "EngineUtils.h"
#include "HLSLTypeAliases.h"
#include "MarchingCubes.h"
#include "SparseVoxelGrid.h"
#include "VoxelBrushTypes.h"
#include "VoxelConversion.h"
#include "VoxelLogAggregator.h"
#include "Async/Async.h"
#include "Kismet/GameplayStatics.h"


UVoxelChunk::UVoxelChunk()
	: ChunkCoordinates(FIntVector::ZeroValue), 
	  TerrainGridSize(100), 
	  Subdivisions(4),
	  SectionIndex(0),
	  bIsDirty(false),
	  DiggerManager(nullptr),
	  SparseVoxelGrid(CreateDefaultSubobject<USparseVoxelGrid>(TEXT("SparseVoxelGrid")))
{
	// Initialize the SparseVoxelGrid
	SparseVoxelGrid->Initialize( this); 
    
	MarchingCubesGenerator = CreateDefaultSubobject<UMarchingCubes>(TEXT("MarchingCubesGenerator"));


}



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
        if (Stroke.bUseAdvancedCubeBrush)
        {
            // Use the advanced cube brush with custom half extents
            ApplyAdvancedCubeBrush(
                Stroke.BrushPosition,
                FVector(Stroke.AdvancedCubeHalfExtentX, Stroke.AdvancedCubeHalfExtentY, Stroke.AdvancedCubeHalfExtentZ),
                Stroke.BrushRotation,
                Stroke.bDig
            );
        }
        else
        {
            // Use the regular cube brush (uniform half extent)
            ApplyCubeBrush(
                Stroke.BrushPosition,
                Stroke.BrushRadius / 2,
                Stroke.bDig,
                Stroke.BrushRotation
            );
        }
        break;

    case EVoxelBrushType::Sphere:
        ApplySphereBrush(Stroke.BrushPosition, Stroke.BrushRadius, Stroke.bDig);
        break;

    case EVoxelBrushType::Cylinder:
        ApplyCylinderBrush(Stroke.BrushPosition, Stroke.BrushRadius * 0.5f, Stroke.BrushLength, Stroke.BrushRotation, Stroke.bDig, false/*add a Filed Brush Paramter and connect to the UI*/);
        break;
	
	case EVoxelBrushType::Capsule:
		ApplyCapsuleBrush(Stroke.BrushPosition, Stroke.BrushRadius * 0.5f, Stroke.BrushLength, Stroke.BrushRotation, Stroke.bDig);
		break;

    case EVoxelBrushType::Cone:
        ApplyConeBrush(Stroke.BrushPosition, Stroke.BrushLength, Stroke.BrushAngle, true, Stroke.bDig, Stroke.BrushRotation);
        break;

    case EVoxelBrushType::Smooth:
        ApplySmoothBrush(Stroke.BrushPosition, Stroke.BrushRadius, Stroke.bDig, 1);
        break;

    case EVoxelBrushType::Debug:
        UE_LOG(LogTemp, Warning, TEXT("Debug brush not implemented."));
        break;

    case EVoxelBrushType::Custom:
        UE_LOG(LogTemp, Warning, TEXT("Custom brush not implemented."));
        break;

    case EVoxelBrushType::AdvancedCube:
        ApplyAdvancedCubeBrush(Stroke.BrushPosition, FVector(Stroke.BrushRadius / 2), Stroke.BrushRotation, Stroke.bDig);
        break;

    case EVoxelBrushType::Torus:
        ApplyTorusBrush(Stroke.BrushPosition, Stroke.BrushRadius, Stroke.TorusInnerRadius, Stroke.BrushRotation, Stroke.bDig);
        break;

case EVoxelBrushType::Pyramid:
    // For a square-based pyramid, use BrushLength as height and BrushRadius as base half-extent
    ApplyPyramidBrush(
        Stroke.BrushPosition,
        Stroke.BrushLength,           // Height
        Stroke.BrushRadius / 2.0f,    // Base half-extent
        Stroke.bDig,
        Stroke.BrushRotation
    );
    break;

case EVoxelBrushType::Icosphere:
    // Use BrushRadius for the icosphere
    ApplyIcosphereBrush(
	    Stroke.BrushPosition,
	    Stroke.BrushRadius,
	    Stroke.BrushRotation,
	    Stroke.bDig
    );
    break;

case EVoxelBrushType::Stairs:
    // Example: Use BrushRadius for width, BrushLength for height, and BrushAngle for depth.
    // You may want to expose NumSteps and bSpiral in your UI/toolkit.
    ApplyStairsBrush(
	    Stroke.BrushPosition,
	    Stroke.BrushRadius,           // Width
	    Stroke.BrushLength,           // Height
	    Stroke.BrushAngle,            // Depth (or use another parameter)
	    Stroke.NumSteps,                     // Set this from your UI/toolkit
	    Stroke.bSpiral,                      // Set this from your UI/toolkit
	    Stroke.bDig,
	    Stroke.BrushRotation
    );
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



void UVoxelChunk::ApplyCubeBrush(FVector3d BrushPosition, float HalfSize, bool bDig, const FRotator& Rotation)
{
	// Call the advanced cube brush with uniform half extents
	ApplyAdvancedCubeBrush(BrushPosition, FVector(HalfSize, HalfSize, HalfSize), Rotation, bDig);
}


void UVoxelChunk::ApplySphereBrush(FVector3d BrushPosition, float Radius, bool bDig)
{
    FVector ChunkOrigin = FVoxelConversion::ChunkToWorld(ChunkCoordinates);
    int32 VoxelsPerChunk = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
    const float SDFTransitionBand = FVoxelConversion::LocalVoxelSize * 3.0f;
    float PaddedRadius = Radius + SDFTransitionBand;

    FVector MinWorld = BrushPosition - FVector(PaddedRadius);
    FVector MaxWorld = BrushPosition + FVector(PaddedRadius);
    FVector MinLocal = (MinWorld - ChunkOrigin) / FVoxelConversion::LocalVoxelSize;
    FVector MaxLocal = (MaxWorld - ChunkOrigin) / FVoxelConversion::LocalVoxelSize;

    int32 MinX = FMath::Max(0, FMath::FloorToInt(MinLocal.X));
    int32 MaxX = FMath::Min(VoxelsPerChunk - 1, FMath::CeilToInt(MaxLocal.X));
    int32 MinY = FMath::Max(0, FMath::FloorToInt(MinLocal.Y));
    int32 MaxY = FMath::Min(VoxelsPerChunk - 1, FMath::CeilToInt(MaxLocal.Y));
    int32 MinZ = FMath::Max(0, FMath::FloorToInt(MinLocal.Z));
    int32 MaxZ = FMath::Min(VoxelsPerChunk - 1, FMath::CeilToInt(MaxLocal.Z));

    for (int32 X = MinX; X <= MaxX; ++X)
    for (int32 Y = MinY; Y <= MaxY; ++Y)
    for (int32 Z = MinZ; Z <= MaxZ; ++Z)
    {
        FVector WorldPos = ChunkOrigin + FVector(X, Y, Z) * FVoxelConversion::LocalVoxelSize
                         + FVector(FVoxelConversion::LocalVoxelSize * 0.5f);

        float SDFValue = FVector::Dist(WorldPos, BrushPosition) - Radius;

        if (FMath::Abs(SDFValue) < SDFTransitionBand)
        {
            ModifyVoxel(FIntVector(X, Y, Z), SDFValue, SDFTransitionBand, bDig);
        }
    }

    MarkDirty();
    SparseVoxelGrid->SynchronizeBordersIfDirty();
}


void UVoxelChunk::ApplyAdvancedCubeBrush(FVector3d BrushPosition, FVector HalfExtents, FRotator Rotation, bool bDig)
{
    FVector ChunkOrigin = FVoxelConversion::ChunkToWorld(ChunkCoordinates);
    int32 VoxelsPerChunk = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
    const float SDFTransitionBand = FVoxelConversion::LocalVoxelSize * 3.0f;

    // Compute OBB bounds
    TArray<FVector> Corners;
    Corners.SetNum(8);
    Corners[0] = FVector(-HalfExtents.X, -HalfExtents.Y, -HalfExtents.Z);
    Corners[1] = FVector(HalfExtents.X, -HalfExtents.Y, -HalfExtents.Z);
    Corners[2] = FVector(HalfExtents.X, HalfExtents.Y, -HalfExtents.Z);
    Corners[3] = FVector(-HalfExtents.X, HalfExtents.Y, -HalfExtents.Z);
    Corners[4] = FVector(-HalfExtents.X, -HalfExtents.Y, HalfExtents.Z);
    Corners[5] = FVector(HalfExtents.X, -HalfExtents.Y, HalfExtents.Z);
    Corners[6] = FVector(HalfExtents.X, HalfExtents.Y, HalfExtents.Z);
    Corners[7] = FVector(-HalfExtents.X, HalfExtents.Y, HalfExtents.Z);

    FRotationMatrix RotMatrix(Rotation);
    FVector MinWorld = BrushPosition;
    FVector MaxWorld = BrushPosition;
    for (FVector& Corner : Corners)
    {
        Corner = RotMatrix.TransformVector(Corner) + BrushPosition;
        MinWorld.X = FMath::Min(MinWorld.X, Corner.X);
        MinWorld.Y = FMath::Min(MinWorld.Y, Corner.Y);
        MinWorld.Z = FMath::Min(MinWorld.Z, Corner.Z);
        MaxWorld.X = FMath::Max(MaxWorld.X, Corner.X);
        MaxWorld.Y = FMath::Max(MaxWorld.Y, Corner.Y);
        MaxWorld.Z = FMath::Max(MaxWorld.Z, Corner.Z);
    }
    MinWorld -= FVector(SDFTransitionBand);
    MaxWorld += FVector(SDFTransitionBand);

    FVector MinLocal = (MinWorld - ChunkOrigin) / FVoxelConversion::LocalVoxelSize;
    FVector MaxLocal = (MaxWorld - ChunkOrigin) / FVoxelConversion::LocalVoxelSize;
    int32 MinX = FMath::Max(0, FMath::FloorToInt(MinLocal.X));
    int32 MaxX = FMath::Min(VoxelsPerChunk - 1, FMath::CeilToInt(MaxLocal.X));
    int32 MinY = FMath::Max(0, FMath::FloorToInt(MinLocal.Y));
    int32 MaxY = FMath::Min(VoxelsPerChunk - 1, FMath::CeilToInt(MaxLocal.Y));
    int32 MinZ = FMath::Max(0, FMath::FloorToInt(MinLocal.Z));
    int32 MaxZ = FMath::Min(VoxelsPerChunk - 1, FMath::CeilToInt(MaxLocal.Z));

    for (int32 X = MinX; X <= MaxX; ++X)
    for (int32 Y = MinY; Y <= MaxY; ++Y)
    for (int32 Z = MinZ; Z <= MaxZ; ++Z)
    {
        FVector WorldPos = ChunkOrigin + FVector(X, Y, Z) * FVoxelConversion::LocalVoxelSize
                         + FVector(FVoxelConversion::LocalVoxelSize * 0.5f);
        FVector LocalPos = RotMatrix.GetTransposed().TransformVector(WorldPos - BrushPosition);

        float Dx = FMath::Abs(LocalPos.X) - HalfExtents.X;
        float Dy = FMath::Abs(LocalPos.Y) - HalfExtents.Y;
        float Dz = FMath::Abs(LocalPos.Z) - HalfExtents.Z;
        float Outside = FVector(FMath::Max(Dx, 0.0f), FMath::Max(Dy, 0.0f), FMath::Max(Dz, 0.0f)).Size();
        float Inside = FMath::Min(FMath::Max(Dx, FMath::Max(Dy, Dz)), 0.0f);
        float SDFValue = Outside + Inside;

        if (FMath::Abs(SDFValue) < SDFTransitionBand)
        {
            ModifyVoxel(FIntVector(X, Y, Z), SDFValue, SDFTransitionBand, bDig);
        }
    }

    MarkDirty();
    SparseVoxelGrid->SynchronizeBordersIfDirty();
}


void UVoxelChunk::ApplyTorusBrush(FVector3d BrushPosition, float MajorRadius, float MinorRadius, FRotator Rotation, bool bDig)
{
    FVector ChunkOrigin = FVoxelConversion::ChunkToWorld(ChunkCoordinates);
    int32 VoxelsPerChunk = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
    const float SDFTransitionBand = FVoxelConversion::LocalVoxelSize * 3.0f;

    // OBB bounds for torus
    float OuterRadius = MajorRadius + MinorRadius + SDFTransitionBand;
    FRotationMatrix RotMatrix(Rotation);
    TArray<FVector> Points;
    Points.SetNum(8);
    Points[0] = FVector(MajorRadius + MinorRadius, 0, MinorRadius);
    Points[1] = FVector(-MajorRadius - MinorRadius, 0, MinorRadius);
    Points[2] = FVector(0, MajorRadius + MinorRadius, MinorRadius);
    Points[3] = FVector(0, -MajorRadius - MinorRadius, MinorRadius);
    Points[4] = FVector(MajorRadius + MinorRadius, 0, -MinorRadius);
    Points[5] = FVector(-MajorRadius - MinorRadius, 0, -MinorRadius);
    Points[6] = FVector(0, MajorRadius + MinorRadius, -MinorRadius);
    Points[7] = FVector(0, -MajorRadius - MinorRadius, -MinorRadius);

    FVector MinWorld = BrushPosition;
    FVector MaxWorld = BrushPosition;
    for (FVector& Point : Points)
    {
        Point = RotMatrix.TransformVector(Point) + BrushPosition;
        MinWorld.X = FMath::Min(MinWorld.X, Point.X);
        MinWorld.Y = FMath::Min(MinWorld.Y, Point.Y);
        MinWorld.Z = FMath::Min(MinWorld.Z, Point.Z);
        MaxWorld.X = FMath::Max(MaxWorld.X, Point.X);
        MaxWorld.Y = FMath::Max(MaxWorld.Y, Point.Y);
        MaxWorld.Z = FMath::Max(MaxWorld.Z, Point.Z);
    }
    MinWorld -= FVector(SDFTransitionBand);
    MaxWorld += FVector(SDFTransitionBand);

    FVector MinLocal = (MinWorld - ChunkOrigin) / FVoxelConversion::LocalVoxelSize;
    FVector MaxLocal = (MaxWorld - ChunkOrigin) / FVoxelConversion::LocalVoxelSize;
    int32 MinX = FMath::Max(0, FMath::FloorToInt(MinLocal.X));
    int32 MaxX = FMath::Min(VoxelsPerChunk - 1, FMath::CeilToInt(MaxLocal.X));
    int32 MinY = FMath::Max(0, FMath::FloorToInt(MinLocal.Y));
    int32 MaxY = FMath::Min(VoxelsPerChunk - 1, FMath::CeilToInt(MaxLocal.Y));
    int32 MinZ = FMath::Max(0, FMath::FloorToInt(MinLocal.Z));
    int32 MaxZ = FMath::Min(VoxelsPerChunk - 1, FMath::CeilToInt(MaxLocal.Z));

    for (int32 X = MinX; X <= MaxX; ++X)
    for (int32 Y = MinY; Y <= MaxY; ++Y)
    for (int32 Z = MinZ; Z <= MaxZ; ++Z)
    {
        FVector WorldPos = ChunkOrigin + FVector(X, Y, Z) * FVoxelConversion::LocalVoxelSize
                         + FVector(FVoxelConversion::LocalVoxelSize * 0.5f);
        FVector P = RotMatrix.GetTransposed().TransformVector(WorldPos - BrushPosition);

        FVector2D q(FVector(P.X, P.Y, 0).Size() - MajorRadius, P.Z);
        float SDFValue = q.Size() - MinorRadius;

        if (FMath::Abs(SDFValue) < SDFTransitionBand)
        {
            ModifyVoxel(FIntVector(X, Y, Z), SDFValue, SDFTransitionBand, bDig);
        }
    }

    MarkDirty();
    SparseVoxelGrid->SynchronizeBordersIfDirty();
}


void UVoxelChunk::ApplyPyramidBrush(FVector3d BrushPosition, float Height, float BaseHalfExtent, bool bDig, const FRotator& Rotation)
{
    FVector ChunkOrigin = FVoxelConversion::ChunkToWorld(ChunkCoordinates);
    int32 VoxelsPerChunk = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
    const float SDFTransitionBand = FVoxelConversion::LocalVoxelSize * 3.0f;

    TArray<FVector> Corners;
    Corners.SetNum(5);
    Corners[0] = FVector(-BaseHalfExtent, -BaseHalfExtent, 0);
    Corners[1] = FVector(BaseHalfExtent, -BaseHalfExtent, 0);
    Corners[2] = FVector(BaseHalfExtent, BaseHalfExtent, 0);
    Corners[3] = FVector(-BaseHalfExtent, BaseHalfExtent, 0);
    Corners[4] = FVector(0, 0, Height);

    FRotationMatrix RotMatrix(Rotation);
    FVector MinWorld = BrushPosition;
    FVector MaxWorld = BrushPosition;
    for (FVector& Corner : Corners)
    {
        Corner = RotMatrix.TransformVector(Corner) + BrushPosition;
        MinWorld.X = FMath::Min(MinWorld.X, Corner.X);
        MinWorld.Y = FMath::Min(MinWorld.Y, Corner.Y);
        MinWorld.Z = FMath::Min(MinWorld.Z, Corner.Z);
        MaxWorld.X = FMath::Max(MaxWorld.X, Corner.X);
        MaxWorld.Y = FMath::Max(MaxWorld.Y, Corner.Y);
        MaxWorld.Z = FMath::Max(MaxWorld.Z, Corner.Z);
    }
    MinWorld -= FVector(SDFTransitionBand);
    MaxWorld += FVector(SDFTransitionBand);

    FVector MinLocal = (MinWorld - ChunkOrigin) / FVoxelConversion::LocalVoxelSize;
    FVector MaxLocal = (MaxWorld - ChunkOrigin) / FVoxelConversion::LocalVoxelSize;
    int32 MinX = FMath::Max(0, FMath::FloorToInt(MinLocal.X));
    int32 MaxX = FMath::Min(VoxelsPerChunk - 1, FMath::CeilToInt(MaxLocal.X));
    int32 MinY = FMath::Max(0, FMath::FloorToInt(MinLocal.Y));
    int32 MaxY = FMath::Min(VoxelsPerChunk - 1, FMath::CeilToInt(MaxLocal.Y));
    int32 MinZ = FMath::Max(0, FMath::FloorToInt(MinLocal.Z));
    int32 MaxZ = FMath::Min(VoxelsPerChunk - 1, FMath::CeilToInt(MaxLocal.Z));

    for (int32 X = MinX; X <= MaxX; ++X)
    for (int32 Y = MinY; Y <= MaxY; ++Y)
    for (int32 Z = MinZ; Z <= MaxZ; ++Z)
    {
        FVector WorldPos = ChunkOrigin + FVector(X, Y, Z) * FVoxelConversion::LocalVoxelSize
                         + FVector(FVoxelConversion::LocalVoxelSize * 0.5f);
        FVector P = RotMatrix.GetTransposed().TransformVector(WorldPos - BrushPosition);

        float px = FMath::Abs(P.X);
        float py = FMath::Abs(P.Y);
        float h = Height;
        float b = BaseHalfExtent;
        float d = FMath::Max(px, py) * h / b + P.Z - h;
        float SDFValue = d;

        if (FMath::Abs(SDFValue) < SDFTransitionBand)
        {
            if (!bDig && P.Z < FVoxelConversion::LocalVoxelSize && px < b * 0.1f && py < b * 0.1f)
                SDFValue = FMath::Min(SDFValue, -0.1f);
            ModifyVoxel(FIntVector(X, Y, Z), SDFValue, SDFTransitionBand, bDig);
        }
    }

    MarkDirty();
    SparseVoxelGrid->SynchronizeBordersIfDirty();
}


void UVoxelChunk::ApplyIcosphereBrush(FVector3d BrushPosition, float Radius, FRotator Rotation, bool bDig)
{
    FVector ChunkOrigin = FVoxelConversion::ChunkToWorld(ChunkCoordinates);
    int32 VoxelsPerChunk = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
    const float SDFTransitionBand = FVoxelConversion::LocalVoxelSize * 3.0f;

    float PaddedRadius = Radius + SDFTransitionBand;
    FVector MinWorld = BrushPosition - FVector(PaddedRadius);
    FVector MaxWorld = BrushPosition + FVector(PaddedRadius);
    FVector MinLocal = (MinWorld - ChunkOrigin) / FVoxelConversion::LocalVoxelSize;
    FVector MaxLocal = (MaxWorld - ChunkOrigin) / FVoxelConversion::LocalVoxelSize;
    int32 MinX = FMath::Max(0, FMath::FloorToInt(MinLocal.X));
    int32 MaxX = FMath::Min(VoxelsPerChunk - 1, FMath::CeilToInt(MaxLocal.X));
    int32 MinY = FMath::Max(0, FMath::FloorToInt(MinLocal.Y));
    int32 MaxY = FMath::Min(VoxelsPerChunk - 1, FMath::CeilToInt(MaxLocal.Y));
    int32 MinZ = FMath::Max(0, FMath::FloorToInt(MinLocal.Z));
    int32 MaxZ = FMath::Min(VoxelsPerChunk - 1, FMath::CeilToInt(MaxLocal.Z));

    const float t = (1.0 + FMath::Sqrt(5.0)) / 2.0;
    static const FVector Normals[12] = {
        FVector(-1,  t,  0), FVector( 1,  t,  0), FVector(-1, -t,  0), FVector( 1, -t,  0),
        FVector( 0, -1,  t), FVector( 0,  1,  t), FVector( 0, -1, -t), FVector( 0,  1, -t),
        FVector( t,  0, -1), FVector( t,  0,  1), FVector(-t,  0, -1), FVector(-t,  0,  1)
    };
    static bool bNormalized = false;
    static FVector NormalsNormalized[12];
    if (!bNormalized)
    {
        for (int i = 0; i < 12; ++i)
            NormalsNormalized[i] = Normals[i].GetSafeNormal();
        bNormalized = true;
    }
    const FRotationMatrix RotMatrix(Rotation);

    for (int32 X = MinX; X <= MaxX; ++X)
    for (int32 Y = MinY; Y <= MaxY; ++Y)
    for (int32 Z = MinZ; Z <= MaxZ; ++Z)
    {
        FVector WorldPos = ChunkOrigin + FVector(X, Y, Z) * FVoxelConversion::LocalVoxelSize
                         + FVector(FVoxelConversion::LocalVoxelSize * 0.5f);
        FVector P = RotMatrix.GetTransposed().TransformVector(WorldPos - BrushPosition);

        float SphereSDF = P.Size() - Radius;
        float Facet = -FLT_MAX;
        for (int i = 0; i < 12; ++i)
        {
            float d = FVector::DotProduct(P, NormalsNormalized[i]);
            if (d > Facet) Facet = d;
        }
        float IcoSDF = FMath::Max(SphereSDF, Facet - Radius * 0.95f);

        if (FMath::Abs(IcoSDF) < SDFTransitionBand)
        {
            ModifyVoxel(FIntVector(X, Y, Z), IcoSDF, SDFTransitionBand, bDig);
        }
    }

    MarkDirty();
    SparseVoxelGrid->SynchronizeBordersIfDirty();
}




void UVoxelChunk::ApplyStairsBrush(FVector3d BrushPosition, float Width, float Height, float Depth, int32 NumSteps, bool bSpiral, bool bDig, const FRotator& Rotation)
{
    FVector ChunkOrigin = FVoxelConversion::ChunkToWorld(ChunkCoordinates);
    int32 VoxelsPerChunk = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
    const float SDFTransitionBand = FVoxelConversion::LocalVoxelSize * 3.0f;

    float PaddedWidth = Width + SDFTransitionBand;
    float PaddedHeight = Height + SDFTransitionBand;
    float PaddedDepth = Depth + SDFTransitionBand;

    FVector MinWorld = BrushPosition - FVector(PaddedWidth, PaddedDepth, 0);
    FVector MaxWorld = BrushPosition + FVector(PaddedWidth, PaddedDepth, PaddedHeight);

    FVector MinLocal = (MinWorld - ChunkOrigin) / FVoxelConversion::LocalVoxelSize;
    FVector MaxLocal = (MaxWorld - ChunkOrigin) / FVoxelConversion::LocalVoxelSize;

    int32 MinX = FMath::FloorToInt(MinLocal.X) - 1;
    int32 MaxX = FMath::CeilToInt(MaxLocal.X) + 1;
    int32 MinY = FMath::FloorToInt(MinLocal.Y) - 1;
    int32 MaxY = FMath::CeilToInt(MaxLocal.Y) + 1;
    int32 MinZ = FMath::FloorToInt(MinLocal.Z) - 1;
    int32 MaxZ = FMath::CeilToInt(MaxLocal.Z) + 1;

    const FRotationMatrix RotMatrix(Rotation);

    for (int32 X = MinX; X <= MaxX; ++X)
    for (int32 Y = MinY; Y <= MaxY; ++Y)
    for (int32 Z = MinZ; Z <= MaxZ; ++Z)
    {
        if (X < 0 || X >= VoxelsPerChunk || Y < 0 || Y >= VoxelsPerChunk || Z < 0 || Z >= VoxelsPerChunk)
            continue;

        FVector WorldPos = ChunkOrigin + FVector(X, Y, Z) * FVoxelConversion::LocalVoxelSize
                         + FVector(FVoxelConversion::LocalVoxelSize * 0.5f);
        FVector P = RotMatrix.InverseTransformVector(WorldPos - BrushPosition);

        float SDFValue = 0.0f;
    	float StepHeight = Height / NumSteps;
    	float StepDepth = Depth / NumSteps;

        if (!bSpiral)
        {
            // Straight stairs along X
            float px = FMath::Abs(P.X) - Width;
            float py = P.Y;
            float pz = P.Z;

            // SDF for straight stairs: union of boxes for each step
            float minSDF = 1e6f;
            for (int32 i = 0; i < NumSteps; ++i)
            {
                float stepZ = i * StepHeight;
                float stepY = -Depth * 0.5f + i * StepDepth;
                float boxSDF = FMath::Max3(
                    px,
                    FMath::Abs(py - stepY) - StepDepth * 0.5f,
                    FMath::Abs(pz - stepZ - StepHeight * 0.5f) - StepHeight * 0.5f
                );
                minSDF = FMath::Min(minSDF, boxSDF);
            }
            SDFValue = minSDF;
        }
        else
        {
            // Spiral stairs: steps around Z axis
            float radius = Width;
            float anglePerStep = 2 * PI / NumSteps;
            float heightPerStep = Height / NumSteps;

            float r = FVector2D(P.X, P.Y).Size();
            float theta = FMath::Atan2(P.Y, P.X);
            float z = P.Z;

            // Find which step this point is closest to
            float stepIdx = (theta + PI) / anglePerStep;
            float stepZ = FMath::FloorToFloat(stepIdx) * heightPerStep;
            float stepTheta = FMath::FloorToFloat(stepIdx) * anglePerStep - PI;

            // SDF for a box at this step
            FVector stepCenter = FVector(
                radius * FMath::Cos(stepTheta),
                radius * FMath::Sin(stepTheta),
                stepZ + heightPerStep * 0.5f
            );
            FVector local = P - stepCenter;
            float boxSDF = FMath::Max3(
                FMath::Abs(local.X) - StepDepth * 0.5f,
                FMath::Abs(local.Y) - StepDepth * 0.5f,
                FMath::Abs(local.Z) - heightPerStep * 0.5f
            );
            SDFValue = boxSDF;
        }

        if (FMath::Abs(SDFValue) < SDFTransitionBand)
        {
            ModifyVoxel(FIntVector(X, Y, Z), SDFValue, SDFTransitionBand, bDig);
        }
    }

    MarkDirty();
    SparseVoxelGrid->SynchronizeBordersIfDirty();
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
/*
float SmoothMin(float a, float b, float k)
{
	float h = FMath::Clamp(0.5f + 0.5f * (b - a) / k, 0.0f, 1.0f);
	return FMath::Lerp(b, a, h) - k * h * (1.0f - h);
}

float SmoothMax(float a, float b, float k)
{
	// Smooth max is just the negative of smooth min of the negated inputs
	return -SmoothMin(-a, -b, k);
}

float UVoxelChunk::BlendSDF(float SDFValue, float ExistingSDF, bool bDig, float TransitionBand)
{
	// Smooth blend between values to avoid hard steps in terrain
	if (bDig)
	{
		// Digging: smoothly blend towards air
		return SmoothMax(ExistingSDF, SDFValue, TransitionBand);
	}
	else
	{
		// Adding: smoothly blend towards solid
		return SmoothMin(ExistingSDF, SDFValue, TransitionBand);
	}
}
*/


float UVoxelChunk::BlendSDF(float SDFValue, float ExistingSDF, bool bDig, float TransitionBand)
{
    // Standard SDF blending: min for add, max for dig
    if (bDig)
    {
        // Digging: make more air (increase SDF)
        return FMath::Max(ExistingSDF, SDFValue);
    }
    else
    {
        // Adding: make more solid (decrease SDF)
        return FMath::Min(ExistingSDF, SDFValue);
    }
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


void UVoxelChunk::ModifyVoxel(FIntVector Index, float SDFValue, float TransitionBand, bool bDig)
{
    float ExistingSDF = SparseVoxelGrid->GetVoxel(Index.X, Index.Y, Index.Z);
    if (bDig)
    {
        if (SDFValue > ExistingSDF)
            SparseVoxelGrid->SetVoxel(Index.X, Index.Y, Index.Z, SDFValue, bDig);
    }
    else
    {
        if (SDFValue < ExistingSDF)
            SparseVoxelGrid->SetVoxel(Index.X, Index.Y, Index.Z, SDFValue, bDig);
    }
}




// --- Capsule Brush ---
void UVoxelChunk::ApplyCapsuleBrush(FVector3d BrushPosition, float Length, float Radius, const FRotator& Rotation, bool bDig)
{
    FTransform BrushTransform(Rotation, BrushPosition);
    FTransform InvBrushTransform = BrushTransform.Inverse();

    const float HalfLength = Length * 0.5f;
    const float SDFTransitionBand = FVoxelConversion::LocalVoxelSize * 2.0f;

    FVector ChunkOrigin = FVoxelConversion::ChunkToWorld(ChunkCoordinates);
    int32 VoxelsPerChunk = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;

    FVector MinWorld = BrushPosition - FVector(Radius, Radius, HalfLength) - FVector(SDFTransitionBand);
    FVector MaxWorld = BrushPosition + FVector(Radius, Radius, HalfLength) + FVector(SDFTransitionBand);

    FVector MinLocal = (MinWorld - ChunkOrigin) / FVoxelConversion::LocalVoxelSize;
    FVector MaxLocal = (MaxWorld - ChunkOrigin) / FVoxelConversion::LocalVoxelSize;

    int32 MinX = FMath::FloorToInt(MinLocal.X);
    int32 MaxX = FMath::CeilToInt(MaxLocal.X);
    int32 MinY = FMath::FloorToInt(MinLocal.Y);
    int32 MaxY = FMath::CeilToInt(MaxLocal.Y);
    int32 MinZ = FMath::FloorToInt(MinLocal.Z);
    int32 MaxZ = FMath::CeilToInt(MaxLocal.Z);

    for (int32 X = MinX; X <= MaxX; ++X)
    for (int32 Y = MinY; Y <= MaxY; ++Y)
    for (int32 Z = MinZ; Z <= MaxZ; ++Z)
    {
        if (X < 0 || X >= VoxelsPerChunk || Y < 0 || Y >= VoxelsPerChunk || Z < 0 || Z >= VoxelsPerChunk)
            continue;

        FVector WorldPos = ChunkOrigin + FVector(X, Y, Z) * FVoxelConversion::LocalVoxelSize + FVector(FVoxelConversion::LocalVoxelSize * 0.5f);
        FVector3d LocalPos = InvBrushTransform.TransformPosition(WorldPos);

        // SDF for a capsule from (-HalfLength to +HalfLength) along Z axis
        FVector3d SegmentStart(0.0, 0.0, -HalfLength);
        FVector3d SegmentEnd(0.0, 0.0, HalfLength);
        FVector3d Segment = SegmentEnd - SegmentStart;

        FVector3d Point = LocalPos - SegmentStart;
        float t = FMath::Clamp(FVector3d::DotProduct(Point, Segment) / Segment.SizeSquared(), 0.0, 1.0);
        FVector3d Closest = SegmentStart + t * Segment;

        float dist = (LocalPos - Closest).Size() - Radius;

        if (FMath::Abs(dist) < SDFTransitionBand)
        {
            // Log once to prove we're entering
            static bool bLogged = false;
            if (!bLogged) {
                UE_LOG(LogTemp, Warning, TEXT("Capsule voxel set at (%d, %d, %d), dist = %f"), X, Y, Z, dist);
                bLogged = true;
            }

            float ExistingSDF = SparseVoxelGrid->GetVoxel(X, Y, Z);
            float NewSDF = BlendSDF(dist, ExistingSDF, bDig, SDFTransitionBand);
            SparseVoxelGrid->SetVoxel(X, Y, Z, NewSDF, bDig);
        }
    }

    MarkDirty();
    SparseVoxelGrid->SynchronizeBordersIfDirty();
}






void UVoxelChunk::ApplyCylinderBrush(FVector3d BrushPosition, float Radius, float Height, const FRotator& Rotation, bool bDig, bool bFilled)
{
    FTransform BrushTransform(Rotation, BrushPosition);
    FTransform InvBrushTransform = BrushTransform.Inverse();

    const float HalfHeight = Height * 0.5f;
    const float SDFTransitionBand = FVoxelConversion::LocalVoxelSize * 2.0f;

    FVector ChunkOrigin = FVoxelConversion::ChunkToWorld(ChunkCoordinates);
    int32 VoxelsPerChunk = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;

    FVector MinWorld = BrushPosition - FVector(Radius, Radius, HalfHeight) - FVector(SDFTransitionBand);
    FVector MaxWorld = BrushPosition + FVector(Radius, Radius, HalfHeight) + FVector(SDFTransitionBand);

    FVector MinLocal = (MinWorld - ChunkOrigin) / FVoxelConversion::LocalVoxelSize;
    FVector MaxLocal = (MaxWorld - ChunkOrigin) / FVoxelConversion::LocalVoxelSize;

    int32 MinX = FMath::FloorToInt(MinLocal.X);
    int32 MaxX = FMath::CeilToInt(MaxLocal.X);
    int32 MinY = FMath::FloorToInt(MinLocal.Y);
    int32 MaxY = FMath::CeilToInt(MaxLocal.Y);
    int32 MinZ = FMath::FloorToInt(MinLocal.Z);
    int32 MaxZ = FMath::CeilToInt(MaxLocal.Z);

    for (int32 X = MinX; X <= MaxX; ++X)
    for (int32 Y = MinY; Y <= MaxY; ++Y)
    for (int32 Z = MinZ; Z <= MaxZ; ++Z)
    {
        if (X < 0 || X >= VoxelsPerChunk || Y < 0 || Y >= VoxelsPerChunk || Z < 0 || Z >= VoxelsPerChunk)
            continue;

        FVector WorldPos = ChunkOrigin + FVector(X, Y, Z) * FVoxelConversion::LocalVoxelSize + FVector(FVoxelConversion::LocalVoxelSize * 0.5f);
        FVector3d LocalPos = InvBrushTransform.TransformPosition(WorldPos);

        float RadialDist = FVector2D(LocalPos.X, LocalPos.Y).Size() - Radius;
        float HeightDist = FMath::Abs(LocalPos.Z) - HalfHeight;

        // Signed distance: maximum of radial and height for a box-like SDF
        float dist = FMath::Max(RadialDist, HeightDist);

        // Hollow shell: band near the wall
        bool bInShell = FMath::Abs(RadialDist) < SDFTransitionBand && HeightDist <= 0;

        // Solid: anything fully inside
        bool bInSolid = dist < SDFTransitionBand;

        if ((bFilled && bInSolid) || (!bFilled && bInShell))
        {
            float ExistingSDF = SparseVoxelGrid->GetVoxel(X, Y, Z);
            float NewSDF = BlendSDF(dist, ExistingSDF, bDig, SDFTransitionBand);
            SparseVoxelGrid->SetVoxel(X, Y, Z, NewSDF, bDig);
        }
    }

    MarkDirty();
    SparseVoxelGrid->SynchronizeBordersIfDirty();
}



void UVoxelChunk::ApplyConeBrush(FVector3d BrushPosition, float Height, float Angle, bool bFilled, bool bDig, const FRotator& Rotation)
{
    float AngleRad = FMath::DegreesToRadians(Angle);
    float RadiusAtBase = Height * FMath::Tan(AngleRad);
    float HalfHeight = Height * 0.5f;

    FTransform BrushTransform(Rotation, BrushPosition);
    FTransform InvBrushTransform = BrushTransform.Inverse();

    const float SDFTransitionBand = FVoxelConversion::LocalVoxelSize * 2.0f;
    
    FVector ChunkOrigin = FVoxelConversion::ChunkToWorld(ChunkCoordinates);
    int32 VoxelsPerChunk = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;

    // Calculate a bounding sphere that encompasses the entire cone
    // The sphere should be large enough to contain the cone in any rotation
    float BoundingSphereRadius = FMath::Sqrt(FMath::Square(Height) + FMath::Square(RadiusAtBase)) + SDFTransitionBand;
    
    // Calculate bounds in world space
    FVector MinWorld = BrushPosition - FVector(BoundingSphereRadius);
    FVector MaxWorld = BrushPosition + FVector(BoundingSphereRadius);
    
    // Convert to local voxel coordinates
    FVector MinLocal = (MinWorld - ChunkOrigin) / FVoxelConversion::LocalVoxelSize;
    FVector MaxLocal = (MaxWorld - ChunkOrigin) / FVoxelConversion::LocalVoxelSize;
    
    // Calculate voxel index bounds
    int32 MinX = FMath::Max(0, FMath::FloorToInt(MinLocal.X));
    int32 MaxX = FMath::Min(VoxelsPerChunk - 1, FMath::CeilToInt(MaxLocal.X));
    int32 MinY = FMath::Max(0, FMath::FloorToInt(MinLocal.Y));
    int32 MaxY = FMath::Min(VoxelsPerChunk - 1, FMath::CeilToInt(MaxLocal.Y));
    int32 MinZ = FMath::Max(0, FMath::FloorToInt(MinLocal.Z));
    int32 MaxZ = FMath::Min(VoxelsPerChunk - 1, FMath::CeilToInt(MaxLocal.Z));

    // Process only voxels within the bounding sphere
    for (int32 X = MinX; X <= MaxX; ++X)
    for (int32 Y = MinY; Y <= MaxY; ++Y)
    for (int32 Z = MinZ; Z <= MaxZ; ++Z)
    {
        FVector WorldPos = ChunkOrigin + FVector(X, Y, Z) * FVoxelConversion::LocalVoxelSize + FVector(FVoxelConversion::LocalVoxelSize * 0.5f);
        
        // Quick sphere check before doing the more expensive cone SDF calculation
        if (FVector::DistSquared(WorldPos, BrushPosition) > BoundingSphereRadius * BoundingSphereRadius)
            continue;
            
        FVector3d LocalPos = InvBrushTransform.TransformPosition(WorldPos);

        // Calculate SDF for cone
    	// Raw distance to cone surface (linear radius along Z)
    	float radiusAtZ = (LocalPos.Z / Height) * RadiusAtBase;
    	float d = FVector2D(LocalPos.X, LocalPos.Y).Size() - radiusAtZ;

    	// Flat capped cone SDF
    	float dist;
    	if (bFilled)
    	{
    		// Hard caps: bottom at Z=0, top at Z=Height
    		float cappedZ = FMath::Clamp(LocalPos.Z, 0.0f, Height);
    		radiusAtZ = (cappedZ / Height) * RadiusAtBase;
    		d = FVector2D(LocalPos.X, LocalPos.Y).Size() - radiusAtZ;

    		// Only inside the cylinder-ish cone body
    		float dz = 0.0f;
    		if (LocalPos.Z < 0.0f) dz = LocalPos.Z;
    		else if (LocalPos.Z > Height) dz = LocalPos.Z - Height;

    		dist = FMath::Max(d, dz); // flat bottom/top cap
    	}
    	else
    	{
    		// Keep the soft SDF for shell-style cones
    		dist = FMath::Max(d, -LocalPos.Z);
    		dist = FMath::Min(dist, FMath::Max(d, LocalPos.Z - Height));
    	}


        if (!bFilled && dist < -SDFTransitionBand)
        {
            continue; // Skip interior if not filled
        }

        // Only process voxels that are affected by the brush
        if (dist < SDFTransitionBand)
        {
            // Fix for tip issue: Add a small bias to ensure the tip is properly handled
            if (!bDig && LocalPos.Z < FVoxelConversion::LocalVoxelSize)
            {
                // Near the tip, make sure the SDF is negative enough
                dist = FMath::Min(dist, -0.1f);
            }
            
            ModifyVoxel(FIntVector(X, Y, Z), dist, SDFTransitionBand, bDig);
        }
    }

    MarkDirty();
    SparseVoxelGrid->SynchronizeBordersIfDirty();
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
		CurrentSDF.Add(Voxel, GetVoxel(Voxel.X, Voxel.Y, Voxel.Z));
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
						float V = CurrentSDF.Contains(N) ? CurrentSDF[N] : GetVoxel(N.X, N.Y, N.Z);
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
	// and LocalVoxelSize is the size of each voxel in world units.
	UE_LOG(LogTemp, Warning, TEXT("GetWorldPosition 0 called"));
	
	FVector ChunkWorldPosition =  FVoxelConversion::ChunkToWorld(ChunkCoordinates);// Get chunk world position

	// Calculate the world position by converting the voxel coordinates relative to this chunk
	FVector VoxelWorldPosition = FVector(VoxelCoordinates) * FVoxelConversion::LocalVoxelSize;

	// Add the chunk's world position to the voxel's local position to get the final world position
	return ChunkWorldPosition + VoxelWorldPosition;
}

FVector UVoxelChunk::GetWorldPosition() const
{
	// Convert chunk coordinates to world space
	return FVector(
		ChunkCoordinates.X * ChunkSize * FVoxelConversion::LocalVoxelSize,
		ChunkCoordinates.Y * ChunkSize * FVoxelConversion::LocalVoxelSize,
		ChunkCoordinates.Z * ChunkSize * FVoxelConversion::LocalVoxelSize
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
		UE_LOG(LogTemp, Error, TEXT("Attempt to access out-of-bounds voxel at X=%d, Y=%d, Z=%d"), X, Y, Z);
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
