#include "DiggerManager.h"
#include "CoreMinimal.h"

// Voxel & Mesh Systems
#include "FCustomSDFBrush.h"
#include "MarchingCubes.h"
#include "SparseVoxelGrid.h"
#include "VoxelChunk.h"
#include "VoxelConversion.h"

// Landscape & Island
#include "IslandActor.h"
#include "LandscapeEdit.h"
#include "LandscapeInfo.h"
#include "LandscapeProxy.h"

// Procedural & Static Meshes
#include "ProceduralMeshComponent.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"
#include "Engine/StaticMesh.h"

// Engine Systems
#include "Engine/World.h"
#include "TimerManager.h"

// Async & Performance
#include "Async/Async.h"
#include "Async/ParallelFor.h"

// Asset Management
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"

// File & Path Management
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

// Kismet & Helpers
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"

// Rendering
#include "Rendering/PositionVertexBuffer.h"
#include "Rendering/StaticMeshVertexBuffer.h"

// UObject Utilities
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/Package.h"
#include "Voxel/BrushShapes/CubeBrushShape.h"
#include "Voxel/BrushShapes/SphereBrushShape.h"

// Editor Tool Specific Includes
#if WITH_EDITOR
#include "DiggerEdModeToolkit.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "Engine/Selection.h"


class Editor;
class FDiggerEdModeToolkit;
class FDiggerEdMode;
class MeshDescriptors;
class StaticMeshAttributes;




void ADiggerManager::ApplyBrushInEditor(bool bDig)
{
    FBrushStroke BrushStroke;
    BrushStroke.BrushPosition = EditorBrushPosition + EditorBrushOffset;
    BrushStroke.BrushRadius = EditorBrushRadius;
    BrushStroke.BrushRotation = EditorBrushRotation;
    BrushStroke.BrushType = EditorBrushType;
    BrushStroke.bDig = bDig; // Use the parameter, not EditorBrushDig
    BrushStroke.BrushLength = EditorBrushLength;
    BrushStroke.BrushAngle = EditorBrushAngle;

    UE_LOG(LogTemp, Warning, TEXT("[ApplyBrushInEditor] EditorBrushPosition (World): %s"), *EditorBrushPosition.ToString());

    ApplyBrushToAllChunks(BrushStroke);
    ProcessDirtyChunks();

    if (GEditor)
    {
        GEditor->RedrawAllViewports();
    }

    // Update islands for the UI/toolkit
    TArray<FIslandData> DetectedIslands = GetAllIslands();
}





#endif
void ADiggerManager::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    FVoxelConversion::InitFromConfig(ChunkSize,Subdivisions, TerrainGridSize, GetActorLocation());
}


ADiggerManager::ADiggerManager()
{
#if WITH_EDITOR  
    this->SetFlags(RF_Transactional); // Enable undo/redo support in editor
#endif

    // Initialize the ProceduralMeshComponent
    ProceduralMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("GeneratedMesh"));
    RootComponent = ProceduralMesh;

    // Initialize the voxel grid and marching cubes
    SparseVoxelGrid = CreateDefaultSubobject<USparseVoxelGrid>(TEXT("SparseVoxelGrid"));
    MarchingCubes = CreateDefaultSubobject<UMarchingCubes>(TEXT("MarchingCubes"));

    // Initialize the brush component
    ActiveBrush = CreateDefaultSubobject<UVoxelBrushShape>(TEXT("ActiveBrush"));
    

    // Load the material in the constructor
    static ConstructorHelpers::FObjectFinder<UMaterial> Material(TEXT("/Game/Materials/M_VoxelMat.M_VoxelMat"));
    if (Material.Succeeded())
    {
        TerrainMaterial = Material.Object;
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Material M_ProcGrid required in /Content/Materials/ folder. Please ensure it is there."));
    }
}



void ADiggerManager::PostInitProperties()
{
    Super::PostInitProperties();
    if (!HasAnyFlags(RF_ClassDefaultObject))
    {
        InitializeBrushShapes();
    }
}

// In ADiggerManager.cpp
void ADiggerManager::InitializeBrushShapes()
{
    BrushShapeMap.Empty();
    BrushShapeMap.Add(EVoxelBrushType::Sphere, NewObject<USphereBrushShape>(this));
    BrushShapeMap.Add(EVoxelBrushType::Cube, NewObject<UCubeBrushShape>(this));
    // ...add other shapes
}

UVoxelBrushShape* ADiggerManager::GetActiveBrushShape(EVoxelBrushType BrushType) const
{
    if (UVoxelBrushShape* const* Found = BrushShapeMap.Find(BrushType))
    {
        return *Found;
    }
    return nullptr;
}



void ADiggerManager::ApplyBrushToAllChunks(FBrushStroke& BrushStroke)
{
    const UVoxelBrushShape* ActiveBrushShape = GetActiveBrushShape(BrushStroke.BrushType);
    if (!ActiveBrushShape)
    {
        UE_LOG(LogTemp, Error, TEXT("ActiveBrushShape is null for brush type %d"), (int32)BrushStroke.BrushType);
        return;
    }
    
    // Make sure our static conversion class is initialized
    if (FVoxelConversion::LocalVoxelSize <= 0.0f)
    {
        UE_LOG(LogTemp, Error, TEXT("VoxelConversion not initialized! Using default values."));
        FVoxelConversion::InitFromConfig(8, 4, 100.0f, FVector::ZeroVector);
    }

    // ROBUST CHUNK SELECTION: Use a more conservative approach to ensure ALL affected chunks are included
    // The brush effect radius is the maximum distance where voxels can be modified
    float BrushEffectRadius = BrushStroke.BrushRadius + BrushStroke.BrushFalloff;
    
    // Calculate chunk size in world units
    float ChunkWorldSize = FVoxelConversion::ChunkSize * FVoxelConversion::LocalVoxelSize;
    
    // CRITICAL: Add enough padding to guarantee we don't miss any chunks
    // We need to account for the diagonal distance across a chunk plus some safety margin
    float ChunkDiagonal = ChunkWorldSize * 1.732f; // sqrt(3) for 3D diagonal
    float SafetyPadding = BrushEffectRadius + ChunkDiagonal;
    
    // Create expanded bounding box
    FVector Min = BrushStroke.BrushPosition - FVector(SafetyPadding);
    FVector Max = BrushStroke.BrushPosition + FVector(SafetyPadding);

    // Convert to chunk coordinates
    FIntVector MinChunk = FVoxelConversion::WorldToChunk(Min);
    FIntVector MaxChunk = FVoxelConversion::WorldToChunk(Max);
    
    // ALTERNATIVE APPROACH: Calculate chunk range directly from brush radius
    // This ensures we definitely get all chunks that could be touched
    int32 ChunkRadiusX = FMath::CeilToInt(SafetyPadding / ChunkWorldSize) + 1;
    int32 ChunkRadiusY = FMath::CeilToInt(SafetyPadding / ChunkWorldSize) + 1;
    int32 ChunkRadiusZ = FMath::CeilToInt(SafetyPadding / ChunkWorldSize) + 1;
    
    FIntVector BrushCenterChunk = FVoxelConversion::WorldToChunk(BrushStroke.BrushPosition);
    
    // Use the more conservative of the two approaches
    FIntVector DirectMinChunk = BrushCenterChunk - FIntVector(ChunkRadiusX, ChunkRadiusY, ChunkRadiusZ);
    FIntVector DirectMaxChunk = BrushCenterChunk + FIntVector(ChunkRadiusX, ChunkRadiusY, ChunkRadiusZ);
    
    MinChunk.X = FMath::Min(MinChunk.X, DirectMinChunk.X);
    MinChunk.Y = FMath::Min(MinChunk.Y, DirectMinChunk.Y);
    MinChunk.Z = FMath::Min(MinChunk.Z, DirectMinChunk.Z);
    
    MaxChunk.X = FMath::Max(MaxChunk.X, DirectMaxChunk.X);
    MaxChunk.Y = FMath::Max(MaxChunk.Y, DirectMaxChunk.Y);
    MaxChunk.Z = FMath::Max(MaxChunk.Z, DirectMaxChunk.Z);

    // Debug output - extra verbose for diagnosing issues
    UE_LOG(LogTemp, Warning, TEXT("--------------------------------------------"));
    UE_LOG(LogTemp, Warning, TEXT("ApplyBrushToAllChunks: BrushPosition=%s, Radius=%f, Falloff=%f"),
           *BrushStroke.BrushPosition.ToString(), BrushStroke.BrushRadius, BrushStroke.BrushFalloff);
    UE_LOG(LogTemp, Warning, TEXT("Brush Effect Radius: %f, Safety Padding: %f, Chunk World Size: %f"), 
           BrushEffectRadius, SafetyPadding, ChunkWorldSize);
    UE_LOG(LogTemp, Warning, TEXT("Brush Center Chunk: %s"), *BrushCenterChunk.ToString());
    UE_LOG(LogTemp, Warning, TEXT("Chunk Radius: [%d,%d,%d]"), ChunkRadiusX, ChunkRadiusY, ChunkRadiusZ);
    UE_LOG(LogTemp, Warning, TEXT("Min World Pos: %s, Max World Pos: %s"),
           *Min.ToString(), *Max.ToString());
    UE_LOG(LogTemp, Warning, TEXT("Final Affected Chunks: X=[%d,%d], Y=[%d,%d], Z=[%d,%d]"),
           MinChunk.X, MaxChunk.X, MinChunk.Y, MaxChunk.Y, MinChunk.Z, MaxChunk.Z);
    UE_LOG(LogTemp, Warning, TEXT("Total chunks to process: %d"), 
           (MaxChunk.X - MinChunk.X + 1) * (MaxChunk.Y - MinChunk.Y + 1) * (MaxChunk.Z - MinChunk.Z + 1));
    UE_LOG(LogTemp, Warning, TEXT("VoxelConversion Config: ChunkSize=%d, Subs=%d, GridSize=%f, VoxelSize=%f"),
           FVoxelConversion::ChunkSize, FVoxelConversion::Subdivisions,
           FVoxelConversion::TerrainGridSize, FVoxelConversion::LocalVoxelSize);

    // Show brush center location in voxel space
    FIntVector BrushChunk = FVoxelConversion::WorldToChunk(BrushStroke.BrushPosition);
    FIntVector BrushLocalVoxel = FVoxelConversion::WorldToLocalVoxel(BrushStroke.BrushPosition);
    UE_LOG(LogTemp, Warning, TEXT("Brush center is in chunk %s at local voxel %s"),
           *BrushChunk.ToString(), *BrushLocalVoxel.ToString());

    int32 TotalChunksProcessed = 0;
    int32 ChunksWithModifications = 0;

    // Loop over all affected chunks
    for (int32 X = MinChunk.X; X <= MaxChunk.X; ++X)
    {
        for (int32 Y = MinChunk.Y; Y <= MaxChunk.Y; ++Y)
        {
            for (int32 Z = MinChunk.Z; Z <= MaxChunk.Z; ++Z)
            {
                FIntVector ChunkCoords(X, Y, Z);
                
                // SIMPLIFIED: Remove the pre-filtering for now to ensure ALL chunks in range are processed
                // The distance check might be incorrectly excluding chunks that should be affected
                TotalChunksProcessed++;
                
                if (UVoxelChunk* Chunk = GetOrCreateChunkAtChunk(ChunkCoords))
                {
                    // Calculate distance for logging purposes
                    FVector ChunkCenter = FVoxelConversion::ChunkToWorld(ChunkCoords);
                    float DistanceToChunk = FVector::Dist(BrushStroke.BrushPosition, ChunkCenter);
                    
                    UE_LOG(LogTemp, Verbose, TEXT("Processing chunk %s (distance to center: %f)"), 
                           *ChunkCoords.ToString(), DistanceToChunk);
                    
                    // Always apply the brush - let the individual chunk method handle the filtering
                    Chunk->ApplyBrushStroke(BrushStroke, ActiveBrushShape);
                    Chunk->MarkDirty(); // Always mark dirty since we can't easily track changes here
                    ChunksWithModifications++; // Count all processed chunks for now
                }
                else
                {
                    UE_LOG(LogTemp, Warning, TEXT("Failed to get or create chunk at %s"), *ChunkCoords.ToString());
                }
            }
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("Brush application complete: %d chunks processed, %d chunks modified"), 
           TotalChunksProcessed, ChunksWithModifications);
    UE_LOG(LogTemp, Warning, TEXT("--------------------------------------------"));
}



void ADiggerManager::SetVoxelAtWorldPosition(const FVector& WorldPos, float Value)
{
    // Safety checks
    if (TerrainGridSize <= 0.0f || Subdivisions <= 0)
    {
        UE_LOG(LogTemp, Error, TEXT("SetVoxelAtWorldPosition: Invalid TerrainGridSize or Subdivisions!"));
        return;
    }

    // Calculate chunk and voxel index
    FIntVector ChunkCoords = FVoxelConversion::WorldToChunk(WorldPos);
    FIntVector LocalVoxelIndex = FVoxelConversion::WorldToMinCornerVoxel(WorldPos);

    // Try to get or create the chunk
    if (UVoxelChunk* Chunk = GetOrCreateChunkAtChunk(ChunkCoords))
    {
        Chunk->GetSparseVoxelGrid()->SetVoxel(LocalVoxelIndex, Value, true);

        UE_LOG(LogTemp, Display, TEXT("[SetVoxelAtWorldPosition] WorldPos: %s → Chunk: %s, Voxel: %s, Value: %.2f"),
            *WorldPos.ToString(), *ChunkCoords.ToString(), *LocalVoxelIndex.ToString(), Value);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("SetVoxelAtWorldPosition: Failed to get or create chunk at %s"), *ChunkCoords.ToString());
    }
}


void ADiggerManager::DebugBrushPlacement(const FVector& ClickPosition)
{
    if (!IsInGameThread())
    {
        UE_LOG(LogTemp, Error, TEXT("DebugBrushPlacement: Not called from game thread!"));
        return;
    }

    UWorld* CurrentWorld = GetSafeWorld();
    if (!CurrentWorld)
    {
        UE_LOG(LogTemp, Error, TEXT("DebugBrushPlacement: GetSafeWorld() returned null!"));
        return;
    }

    // Ensure parameters are valid
    if (TerrainGridSize <= 0.0f || Subdivisions <= 0)
    {
        UE_LOG(LogTemp, Error, TEXT("DebugBrushPlacement: Invalid TerrainGridSize or Subdivisions!"));
        return;
    }

    VoxelSize = TerrainGridSize / Subdivisions;
    float ChunkWorldSize = ChunkSize * TerrainGridSize;

    UE_LOG(LogTemp, Display, TEXT("ChunkSize=%d, Subdivisions=%d, VoxelSize=%.2f, ChunkWorldSize=%.2f"),
        ChunkSize, Subdivisions, VoxelSize, ChunkWorldSize);

    // Compute chunk coordinates from world position
    FIntVector ChunkCoords = FVoxelConversion::WorldToChunk(ClickPosition);


    UE_LOG(LogTemp, Display, TEXT("ClickPos: %s → ChunkCoords: %s"),
        *ClickPosition.ToString(), *ChunkCoords.ToString());

    // Fetch or create the relevant chunk
    if (UVoxelChunk* Chunk = GetOrCreateChunkAtChunk(ChunkCoords))
    {
        Chunk->GetSparseVoxelGrid()->SetVoxel(FVoxelConversion::WorldToLocalVoxel(ClickPosition), -1.0f, true);
        SetVoxelAtWorldPosition(ClickPosition, -1.0f);
        Chunk->GetSparseVoxelGrid()->RenderVoxels();
        Chunk->GetSparseVoxelGrid()->LogVoxelData();
        Chunk->DebugDrawChunk();
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to get or create chunk at %s"), *ChunkCoords.ToString());
        return;
    }

    // Compute chunk origin and extent
    FVector ChunkCenter = FVector(ChunkCoords) * ChunkWorldSize;
    FVector ChunkExtent = FVector(ChunkWorldSize * 0.5f);
    FVector ChunkOrigin = ChunkCenter - ChunkExtent;


    FVector LocalInChunk = ClickPosition - ChunkOrigin;

    // Convert to local voxel coordinates using VoxelSize
    FIntVector LocalVoxel = FVoxelConversion::WorldToLocalVoxel(ClickPosition);
    FVector LocalVoxelToWorld = FVoxelConversion::LocalVoxelToWorld(LocalVoxel);

    UE_LOG(LogTemp, Error, TEXT("LocalInChunk: %s, LocalVoxel: %s, WorldPosition: %s"),
        *LocalInChunk.ToString(), *LocalVoxel.ToString(), *LocalVoxelToWorld.ToString());

    // World-space center of the voxel
    FVector VoxelCenter = ChunkOrigin +
                          FVector(LocalVoxel) * VoxelSize +
                          FVector(VoxelSize * 0.5f);

    UE_LOG(LogTemp, Display, TEXT("VoxelCenter: %s"), *VoxelCenter.ToString());
    
    
    try
    {
        //const float ChunkWorldSize = ChunkSize * TerrainGridSize;
        //const FVector ChunkExtent = FVector(ChunkWorldSize * 0.5f);
        //const FVector ChunkCenter = FVector(ChunkCoords) * ChunkWorldSize;
        //const FVector ChunkOrigin = ChunkCenter - ChunkExtent;

        // Draw the chunk bounding box
        DrawDebugBox(CurrentWorld, ChunkCenter, ChunkExtent, FColor::Red, false, 30.0f, 0, 2.0f);

        // Label: Chunk coordinates
        DrawDebugString(CurrentWorld, ChunkCenter + FVector(0, 0, ChunkExtent.Z + 50.0f),
            FString::Printf(TEXT("Chunk: %s"), *ChunkCoords.ToString()),
            nullptr, FColor::Yellow, 30.0f);

        // Draw chunk center
        DrawDebugSphere(CurrentWorld, ChunkCenter, 30.0f, 16, FColor::Yellow, false, 30.0f);
        DrawDebugString(CurrentWorld, ChunkCenter + FVector(0, 0, 35.0f), TEXT("Chunk Center"), nullptr, FColor::Yellow, 30.0f);

        // Draw chunk origin (minimum corner)
        DrawDebugSphere(CurrentWorld, ChunkOrigin, 20.0f, 16, FColor::Orange, false, 30.0f);
        DrawDebugString(CurrentWorld, ChunkOrigin + FVector(0, 0, 25.0f), TEXT("Chunk Origin"), nullptr, FColor::Orange, 30.0f);

        const int32 ChunkVoxels = ChunkSize * Subdivisions;
        FVector SelectedVoxelCenter = FVector::ZeroVector;

        // Draw voxel grid (only outer shell + selected voxel)
        for (int32 X = 0; X < ChunkVoxels; X++)
        {
            for (int32 Y = 0; Y < ChunkVoxels; Y++)
            {
                for (int32 Z = 0; Z < ChunkVoxels; Z++)
                {
                    FIntVector VoxelCoord(X, Y, Z);
                   // FVector VoxelCenter = ChunkOrigin + FVector(VoxelCoord) * VoxelSize + FVector(VoxelSize * 0.5f);

                    // Highlight edge voxels
                    if (X == 0 || X == ChunkVoxels - 1 ||
                        Y == 0 || Y == ChunkVoxels - 1 ||
                        Z == 0 || Z == ChunkVoxels - 1)
                    {
                        DrawDebugPoint(CurrentWorld, VoxelCenter, 5.0f, FColor::Cyan, false, 30.0f);
                    }

                    // Highlight the selected voxel
                    if (VoxelCoord == LocalVoxel)
                    {
                        SelectedVoxelCenter = VoxelCenter;

                        DrawDebugBox(CurrentWorld, SelectedVoxelCenter, FVector(VoxelSize * 0.5f),
                            FColor::Magenta, false, 30.0f, 0, 2.0f);

                        DrawDebugString(CurrentWorld, SelectedVoxelCenter + FVector(0, 0, 25.0f),
                            TEXT("Selected Voxel"), nullptr, FColor::Magenta, 30.0f);
                    }
                }
            }
        }

        // Draw interaction line and key points
        DrawDebugLine(CurrentWorld, ClickPosition, SelectedVoxelCenter, FColor::Green, false, 30.0f, 0, 3.0f);
        DrawDebugSphere(CurrentWorld, ClickPosition, 20.0f, 16, FColor::Blue, false, 30.0f);
        DrawDebugString(CurrentWorld, ClickPosition + FVector(0, 0, 25.0f), TEXT("Click"), nullptr, FColor::White, 30.0f);

        DrawDebugSphere(CurrentWorld, SelectedVoxelCenter, 20.0f, 16, FColor::Magenta, false, 30.0f);
        DrawDebugString(CurrentWorld, SelectedVoxelCenter + FVector(0, 0, 25.0f), TEXT("Voxel"), nullptr, FColor::White, 30.0f);

        UE_LOG(LogTemp, Warning, TEXT("DebugBrushPlacement: Debug visuals drawn successfully"));
    }
    catch (...)
    {
        UE_LOG(LogTemp, Error, TEXT("DebugBrushPlacement: Exception drawing debug visuals"));
    }

    // Diagonal marker boxes
    DrawDiagonalDebugVoxels(ChunkCoords);
    //Individual voxels right nearest the click position.
    DebugDrawVoxelAtWorldPosition(ClickPosition, FColor::White, 25/*= 5.0f*/, 2.f);
    
    UE_LOG(LogTemp, Warning, TEXT("DebugBrushPlacement: completed"));
}

void ADiggerManager::DebugDrawVoxelAtWorldPosition(const FVector& WorldPosition, FColor BoxColor, float Duration /*= 5.0f*/, float Thickness /*= 2.0f*/)
{
    FIntVector ChunkCoords;
    FIntVector VoxelIndex;
    FVoxelConversion::WorldToChunkAndVoxel(WorldPosition, ChunkCoords, VoxelIndex);

    if (!FVoxelConversion::IsValidVoxelIndex(VoxelIndex))
    {
        UE_LOG(LogTemp, Warning, TEXT("Invalid voxel index at world position %s: Chunk %s, VoxelIndex %s"),
            *WorldPosition.ToString(), *ChunkCoords.ToString(), *VoxelIndex.ToString());
        return;
    }

    // Get the voxel center in world space
    FVector VoxelCenter = FVoxelConversion::MinCornerVoxelToWorld(ChunkCoords, VoxelIndex);

    // Draw a debug box to represent the voxel
    DrawDebugBox(
        World,
        VoxelCenter,
        FVector(FVoxelConversion::LocalVoxelSize * 0.5f), // Half extents
        FQuat::Identity,
        BoxColor,
        false,
        Duration,
        0,
        Thickness
    );

    UE_LOG(LogTemp, Log, TEXT("Drew voxel at world position %s: Chunk %s, VoxelIndex %s, Center %s"),
        *WorldPosition.ToString(), *ChunkCoords.ToString(), *VoxelIndex.ToString(), *VoxelCenter.ToString());
}

void ADiggerManager::DrawDiagonalDebugVoxels(FIntVector ChunkCoords)
{
    // Calculate chunk dimensions
    const int32 VoxelsPerChunk = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
    const float DebugDuration = 30.0f;
    
    // Get the chunk center using the conversion method
    FVector ChunkCenter = FVoxelConversion::ChunkToWorld(ChunkCoords);
    FVector ChunkExtent = FVector(FVoxelConversion::ChunkSize * FVoxelConversion::TerrainGridSize / 2.0f);
    
    // Calculate the minimum corner based on the center and extent
    FVector ChunkMinCorner = ChunkCenter - ChunkExtent;
    
    UE_LOG(LogTemp, Warning, TEXT("DrawDiagonalDebugVoxels - ChunkCoords: %s"), *ChunkCoords.ToString());
    UE_LOG(LogTemp, Warning, TEXT("ChunkCenter: %s, ChunkMinCorner: %s"), 
           *ChunkCenter.ToString(), *ChunkMinCorner.ToString());
    UE_LOG(LogTemp, Warning, TEXT("VoxelsPerChunk: %d, LocalVoxelSize: %f"),
           VoxelsPerChunk, FVoxelConversion::LocalVoxelSize);
    
    // Draw diagonal voxels from minimum corner to maximum corner
    for (int32 i = 0; i < VoxelsPerChunk; i++)
    {
        // Calculate world position for the voxel at position (i, i, i) from the minimum corner
        FVector VoxelCenter = ChunkMinCorner + 
                             FVector(i + 0.5f, i + 0.5f, i + 0.5f) * FVoxelConversion::LocalVoxelSize;
        
        // Draw debug box at the voxel center
        DrawDebugBox(
            World, 
            VoxelCenter, 
            FVector(FVoxelConversion::LocalVoxelSize * 0.45f), // Slightly smaller for visibility
            FColor::Green, 
            false, 
            DebugDuration, 
            0, 
            1.5f
        );
    }
    
    // Draw overflow voxels in the negative direction (1 voxel outside the chunk boundary)
    FVector overflowPositions[] = {
        FVector(-1, -1, -1),  // Corner overflow
        FVector(-1, 0, 0),    // X-axis overflow
        FVector(0, -1, 0),    // Y-axis overflow
        FVector(0, 0, -1)     // Z-axis overflow
    };
    
    for (const FVector& offset : overflowPositions)
    {
        // Calculate world position for overflow voxel - these are just outside the minimum corner
        FVector VoxelCenter = ChunkMinCorner + 
                             (offset + FVector(0.5f)) * FVoxelConversion::LocalVoxelSize;
        
        // Draw debug box at the overflow voxel center in purple
        DrawDebugBox(
            World, 
            VoxelCenter, 
            FVector(FVoxelConversion::LocalVoxelSize * 0.45f),
            FColor::Purple, // Different color for overflow voxels
            false, 
            DebugDuration, 
            0, 
            2.0f  // Thicker lines for overflow voxels
        );
        
        UE_LOG(LogTemp, Warning, TEXT("Overflow voxel at local position %s, world position %s"),
               *offset.ToString(), *VoxelCenter.ToString());
    }
    
    // Draw red box for chunk bounds
    DrawDebugBox(
        World, 
        ChunkCenter, 
        ChunkExtent, 
        FColor::Red, 
        false, 
        DebugDuration, 
        0, 
        2.5f
    );
    
    // Draw yellow box at the chunk minimum corner for reference
    DrawDebugBox(
        World,
        ChunkMinCorner,
        FVector(FVoxelConversion::LocalVoxelSize * 0.75f),
        FColor::Yellow,
        false,
        DebugDuration,
        0,
        3.0f
    );
    
    // Draw blue box at the chunk maximum corner for reference
    FVector ChunkMaxCorner = ChunkCenter + ChunkExtent;
    DrawDebugBox(
        World,
        ChunkMaxCorner,
        FVector(FVoxelConversion::LocalVoxelSize * 0.75f),
        FColor::Blue,
        false,
        DebugDuration,
        0,
        3.0f
    );
    
    // Draw a magenta box at the world origin for reference
    DrawDebugBox(
        World,
        FVector::ZeroVector,
        FVector(FVoxelConversion::LocalVoxelSize * 1.5f),
        FColor::Magenta,
        false,
        DebugDuration,
        0,
        3.0f
    );
    
    // Draw overflow regions on the minimum sides
    
    // 1. X-axis overflow region (full face)
    {
        FVector OverflowCenter = ChunkMinCorner + FVector(- FVoxelConversion::LocalVoxelSize * 0.5f, FVoxelConversion::ChunkWorldSize  * 0.5f,FVoxelConversion::ChunkWorldSize  * 0.5f);
        FVector OverflowExtent = FVector(
            FVoxelConversion::LocalVoxelSize * 0.5f,
            ChunkExtent.Y,
            ChunkExtent.Z
        );
        
        DrawDebugBox(
            World,
            OverflowCenter,
            OverflowExtent,
            FColor(255, 128, 255, 64), // Light purple with transparency
            false,
            DebugDuration,
            0,
            1.0f
        );
    }
    
    // 2. Y-axis overflow region (full face)
    {
        FVector OverflowCenter = ChunkMinCorner + FVector(FVoxelConversion::ChunkWorldSize  * 0.5f, - FVoxelConversion::LocalVoxelSize * 0.5f, FVoxelConversion::ChunkWorldSize  * 0.5f      );
        FVector OverflowExtent = FVector(
            ChunkExtent.X,
            FVoxelConversion::LocalVoxelSize * 0.5f,
            ChunkExtent.Z
        );
        
        DrawDebugBox(
            World,
            OverflowCenter,
            OverflowExtent,
            FColor(255, 128, 255, 64), // Light purple with transparency
            false,
            DebugDuration,
            0,
            1.0f
        );
    }
    
    // 3. Z-axis overflow region (full face)
    {
        FVector OverflowCenter = ChunkMinCorner + FVector( FVoxelConversion::ChunkWorldSize  * 0.5f, FVoxelConversion::ChunkWorldSize  * 0.5f,- FVoxelConversion::LocalVoxelSize  * 0.5f);
        FVector OverflowExtent = FVector(
            ChunkExtent.X,
            ChunkExtent.Y,
            FVoxelConversion::LocalVoxelSize * 0.5f
        );
        
        DrawDebugBox(
            World,
            OverflowCenter,
            OverflowExtent,
            FColor(255, 128, 255, 64), // Light purple with transparency
            false,
            DebugDuration,
            0,
            1.0f
        );
    }
}


UStaticMesh* ADiggerManager::ConvertIslandToStaticMesh(const FIslandData& Island, bool bWorldOrigin, FString AssetName)
{
    UE_LOG(LogTemp, Warning, TEXT("Converting island %d to static mesh..."), Island.IslandID);

    // 1. Generate mesh data for the island using Marching Cubes
    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FColor> Colors;
    TArray<FProcMeshTangent> Tangents;

/* Single Island Mesh Generation in DiggerManager
            // You may need to create a temporary SparseVoxelGrid for just this island
            USparseVoxelGrid* TempGrid = NewObject<USparseVoxelGrid>();
            
            //TempGrid->AddVoxels(IslandVoxels);

            // Generate mesh for just this island
            MarchingCubes->GenerateMeshForIsland(TempGrid, Vertices, Triangles, Normals, UVs, Colors, Tangents);

            // 4. Remove island voxels from the main grid
            //TempGrid->RemoveVoxels(IslandVoxels);
    */

    // For demonstration, we'll log and return nullptr if not implemented
    if (Vertices.Num() == 0 || Triangles.Num() == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("No mesh data generated for island!"));
        return nullptr;
    }

    // 2. Create a temporary ProceduralMeshComponent
    UProceduralMeshComponent* TempProcMesh = NewObject<UProceduralMeshComponent>(this);
    TempProcMesh->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, true);

    // 3. Convert ProceduralMeshComponent to StaticMesh
    FString PackageName = FString::Printf(TEXT("/Game/Islands/%s"), *AssetName);
    UPackage* MeshPackage = CreatePackage(*PackageName);

    UStaticMesh* NewStaticMesh = NewObject<UStaticMesh>(MeshPackage, *AssetName, RF_Public | RF_Standalone);
    if (!NewStaticMesh)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create UStaticMesh object!"));
        return nullptr;
    }

    // Use the built-in conversion utility (UE 4.23+)
    bool bSuccess = false;//TempProcMesh->CreateStaticMesh(NewStaticMesh, MeshPackage, AssetName, true);
    if (!bSuccess)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to convert procedural mesh to static mesh!"));
        return nullptr;
    }

    // 4. Register and save the asset
    FAssetRegistryModule::AssetCreated(NewStaticMesh);
    NewStaticMesh->MarkPackageDirty();

#if WITH_EDITOR
    // Notify the content browser
    TArray<UObject*> ObjectsToSync;
    ObjectsToSync.Add(NewStaticMesh);
    GEditor->SyncBrowserToObjects(ObjectsToSync);
#endif

    UE_LOG(LogTemp, Warning, TEXT("Static mesh asset created: %s"), *PackageName);

    return NewStaticMesh;
}



FIslandMeshData ADiggerManager::ExtractAndGenerateIslandMesh(const FVector& IslandCenter)
{
    UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Starting ExtractAndGenerateIslandMesh at IslandCenter: %s"), *IslandCenter.ToString());

    FIslandMeshData Result;

    // Step 1: Find or create the voxel chunk at the given center
    UVoxelChunk* Chunk = FindOrCreateNearestChunk(IslandCenter);
    if (!Chunk)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] No chunk found or created near IslandCenter."));
        return Result;
    }

    FVector ChunkOrigin = FVoxelConversion::ChunkToWorld(Chunk->GetChunkPosition());
    FVector LocalPosition = IslandCenter - ChunkOrigin;

    FIntVector VoxelCoords = FIntVector(
        FMath::FloorToInt(LocalPosition.X / VoxelSize),
        FMath::FloorToInt(LocalPosition.Y / VoxelSize),
        FMath::FloorToInt(LocalPosition.Z / VoxelSize)
    );

    FVector VoxelWorldCenter = ChunkOrigin + FVector(VoxelCoords) * VoxelSize + FVector(VoxelSize * 0.5f);

    UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Chunk Origin: %s | LocalPosition: %s | VoxelCoords: %s | VoxelWorldCenter: %s"),
        *ChunkOrigin.ToString(),
        *LocalPosition.ToString(),
        *VoxelCoords.ToString(),
        *VoxelWorldCenter.ToString()
    );

    // Visual debugging
    if (true)
    {
        // Draw debug string above search origin
        DrawDebugString(
            GetSafeWorld(),
            VoxelWorldCenter + FVector(0, 0, VoxelSize),
            TEXT("Search Origin"),
            nullptr,
            FColor::White,
            60.0f,
            false
        );

        // RED: Voxel being searched
        DrawDebugBox(
            GetSafeWorld(),
            VoxelWorldCenter,
            FVector(VoxelSize * 0.5f),
            FColor::Red,
            false,
            60.0f,
            0,
            3.0f
        );

        // GREEN: 3x3x3 area around it
        for (int32 dx = -1; dx <= 1; dx++)
        {
            for (int32 dy = -1; dy <= 1; dy++)
            {
                for (int32 dz = -1; dz <= 1; dz++)
                {
                    FIntVector Offset = VoxelCoords + FIntVector(dx, dy, dz);
                    FVector OffsetWorldCenter = ChunkOrigin + FVector(Offset) * VoxelSize + FVector(VoxelSize * 0.5f);

                    DrawDebugBox(
                        GetSafeWorld(),
                        OffsetWorldCenter,
                        FVector(VoxelSize * 0.5f),
                        FColor::Green,
                        false,
                        60.0f,
                        0,
                        1.5f
                    );
                }
            }
        }
    }

    // Step 2: Get voxel grid
    USparseVoxelGrid* VoxelGrid = Chunk->GetSparseVoxelGrid();
    if (!VoxelGrid)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] No SparseVoxelGrid found on chunk."));
        return Result;
    }
    
    if (true)
    {
        for (const auto& Pair : VoxelGrid->VoxelData)
        {
            FVector WorldPos = FVoxelConversion::LocalVoxelToWorld(Pair.Key);
            DrawDebugBox(GetSafeWorld(), WorldPos, FVector(VoxelSize * 0.5f), FColor::Blue, false, 60.0f, 0, 1.5f);
        }
    }

    // Step 3: Find the nearest set voxel and extract the island
    USparseVoxelGrid* ExtractedGrid = nullptr;
    TArray<FIntVector> IslandVoxels;

    FIntVector StartVoxel;
    if (!VoxelGrid->FindNearestSetVoxel(VoxelCoords, StartVoxel))
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] No set voxel found near the provided position."));
        return Result;
    }

    // Make a backup of the original grid before extraction
    USparseVoxelGrid* OriginalGridBackup = nullptr;
   /* if (bMakeBackupBeforeExtraction)
    {
        OriginalGridBackup = DuplicateObject<USparseVoxelGrid>(VoxelGrid, this);
    }*/

    // Run extraction to get surface voxels
    bool bExtractionSuccess = VoxelGrid->ExtractIslandAtPosition(
        FVoxelConversion::LocalVoxelToWorld(StartVoxel), 
        ExtractedGrid, 
        IslandVoxels
    );

    if (!bExtractionSuccess || !ExtractedGrid || IslandVoxels.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Failed to extract island or ExtractedGrid is null."));
        
        // Restore from backup if needed
        /*if (bMakeBackupBeforeExtraction && OriginalGridBackup)
        {
            UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Restoring grid from backup."));
            Chunk->SetSparseVoxelGrid(OriginalGridBackup);
        }*/
        
        return Result;
    }

    // Ensure the extracted grid has the necessary references
    //ExtractedGrid->ParentChunk = nullptr; // Don't want to affect the original chunk
    //ExtractedGrid->DiggerManager = this;

    // Step 4: Generate mesh
    UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Starting mesh generation using Marching Cubes."));

    // Use the chunk origin for mesh generation
    MarchingCubes->GenerateMeshFromGrid(
        ExtractedGrid, ChunkOrigin, VoxelSize,
        Result.Vertices, Result.Triangles, Result.Normals
    );
    
    
    // Step 5: Finalize result
    Result.MeshOrigin = ChunkOrigin;
    Result.bValid = (Result.Vertices.Num() > 0 && Result.Triangles.Num() > 0);

    if (Result.Vertices.Num() > 0 && Result.Triangles.Num() > 0 && Result.Normals.Num() > 0) {
        // Call mesh creation on game thread
        AsyncTask(ENamedThreads::GameThread, [=]()
        {
            int IslandId=0;
            MarchingCubes->CreateIslandProceduralMesh(Result.Vertices, Result.Triangles, Result.Normals, ChunkOrigin, IslandId);
        });
    } else {
        UE_LOG(LogTemp, Warning, TEXT("Island mesh generation returned empty data"));
    }

    // Debug visualization of the extracted island
    //if (IsDebugging && Result.bValid)
    //{
        for (const FIntVector& Voxel : IslandVoxels)
        {
            FVector WorldPos = ChunkOrigin + FVector(Voxel) * VoxelSize + FVector(VoxelSize * 0.5f);
            DrawDebugBox(GetSafeWorld(), WorldPos, FVector(VoxelSize * 0.4f), FColor::Yellow, false, 60.0f, 0, 2.0f);
        }
   //}

    UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Mesh generation complete. Valid: %s, Vertices: %d, Triangles: %d"),
        Result.bValid ? TEXT("true") : TEXT("false"),
        Result.Vertices.Num(),
        Result.Triangles.Num() / 3
    );

    // Mark the chunk as dirty to trigger a rebuild
    Chunk->MarkDirty();

    return Result;
}


void ADiggerManager::SaveIslandData(AIslandActor* IslandActor, const FIslandMeshData& MeshData)
{
    if (!IslandActor)
        return;
        
    FIslandSaveData SaveData;
    SaveData.MeshOrigin = MeshData.MeshOrigin;
    SaveData.Vertices = MeshData.Vertices;
    SaveData.Triangles = MeshData.Triangles;
    SaveData.Normals = MeshData.Normals;
    SaveData.bEnablePhysics = IslandActor->ProcMesh->IsSimulatingPhysics();
    
    SavedIslands.Add(SaveData);
}


AIslandActor* ADiggerManager::SpawnIslandActorWithMeshData(
    const FVector& SpawnLocation,
    const FIslandMeshData& MeshData,
    bool bEnablePhysics
)
{
    if (!MeshData.bValid) return nullptr;
    

    AIslandActor* IslandActor = World->SpawnActor<AIslandActor>(AIslandActor::StaticClass(), SpawnLocation, FRotator::ZeroRotator);
    if (IslandActor && IslandActor->ProcMesh)
    {
        IslandActor->ProcMesh->CreateMeshSection_LinearColor(
            0, MeshData.Vertices, MeshData.Triangles, MeshData.Normals, {}, {}, {}, true
        );
        if (bEnablePhysics)
            IslandActor->ApplyPhysics();
        else
            IslandActor->RemovePhysics();
    }
    return IslandActor;
}

void ADiggerManager::ConvertIslandAtPositionToPhysicsObject(const FVector& IslandCenter)
{
    UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] ConvertIslandAtPositionToPhysicsObject called at %s"), *IslandCenter.ToString());
    
    FIslandMeshData MeshData = ExtractAndGenerateIslandMesh(IslandCenter);
    if (MeshData.bValid)
    {
        SpawnIslandActorWithMeshData(IslandCenter, MeshData, /*bEnablePhysics=*/true);
    }
}


void ADiggerManager::ConvertIslandAtPositionToStaticMesh(const FVector& IslandCenter)
{
    UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] ConvertIslandAtPositionToStaticMesh called at %s"), *IslandCenter.ToString());
    FIslandMeshData MeshData = ExtractAndGenerateIslandMesh(IslandCenter);
    if (MeshData.bValid)
    {
        FString AssetName = FString::Printf(TEXT("Island_%s_StaticMesh"), *IslandCenter.ToString());
        SaveIslandMeshAsStaticMesh(AssetName, MeshData);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] ConvertIslandAtPositionToStaticMesh called with invalid static mesh data at %s"), *IslandCenter.ToString());
    }
}

void ADiggerManager::ConvertIslandAtPositionToActor(const FVector& IslandCenter, bool bEnablePhysics, FIntVector ReferenceVoxel)
{
    UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] ConvertIslandAtPositionToActor called at %s"), *IslandCenter.ToString());
    
    // Debug: Visualize the search position
    DrawDebugSphere(GetSafeWorld(), IslandCenter, 50.0f, 16, FColor::Red, false, 10.0f, 0, 2.0f);
    DrawDebugString(GetSafeWorld(), IslandCenter + FVector(0, 0, 60.0f), TEXT("Island Search Point"), nullptr, FColor::White, 10.0f);
    
    // Find the chunk at this position
    UVoxelChunk* Chunk = FindOrCreateNearestChunk(IslandCenter);
    if (!Chunk)
    {
        UE_LOG(LogTemp, Error, TEXT("[DiggerPro] No chunk found at position %s"), *IslandCenter.ToString());
        return;
    }
    
    // Debug: Visualize all voxels in the chunk
    USparseVoxelGrid* Grid = Chunk->GetSparseVoxelGrid();
    if (!Grid)
    {
        UE_LOG(LogTemp, Error, TEXT("[DiggerPro] Chunk has no voxel grid"));
        return;
    }
    
    UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Chunk has %d voxels"), Grid->VoxelData.Num());
    
    // If we have a reference voxel, use it directly
    if (ReferenceVoxel != FIntVector::ZeroValue)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Using reference voxel: %s"), *ReferenceVoxel.ToString());
        
        // Extract the island mesh data using the reference voxel
        FIslandMeshData MeshData = ExtractAndGenerateIslandMeshFromVoxel(Chunk, ReferenceVoxel);
        
        if (MeshData.bValid)
        {
            // Spawn the island actor
            AIslandActor* IslandActor = SpawnIslandActorWithMeshData(MeshData.MeshOrigin, MeshData, bEnablePhysics);
            
            if (IslandActor)
            {
                UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Successfully created island actor at %s"), 
                    *MeshData.MeshOrigin.ToString());
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("[DiggerPro] Failed to create island actor at %s"), 
                    *MeshData.MeshOrigin.ToString());
            }
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Failed to generate valid mesh data from reference voxel"));
        }
    }
    else
    {
        // Fall back to the old method
        FIslandMeshData MeshData = ExtractAndGenerateIslandMesh(IslandCenter);
        
        if (MeshData.bValid)
        {
            // Spawn the island actor
            AIslandActor* IslandActor = SpawnIslandActorWithMeshData(MeshData.MeshOrigin, MeshData, bEnablePhysics);
            
            if (IslandActor)
            {
                UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Successfully created island actor at %s"), 
                    *MeshData.MeshOrigin.ToString());
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("[DiggerPro] Failed to create island actor at %s"), 
                    *MeshData.MeshOrigin.ToString());
            }
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] ConvertIslandAtPositionToActor called with invalid mesh data at %s"), 
                *IslandCenter.ToString());
        }
    }
}

// New function to extract island mesh from a specific voxel
FIslandMeshData ADiggerManager::ExtractAndGenerateIslandMeshFromVoxel(UVoxelChunk* Chunk, const FIntVector& StartVoxel)
{
    FIslandMeshData Result;
    
    if (!Chunk)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] No chunk provided for extraction."));
        return Result;
    }
    
    USparseVoxelGrid* VoxelGrid = Chunk->GetSparseVoxelGrid();
    if (!VoxelGrid)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] No SparseVoxelGrid found on chunk."));
        return Result;
    }
    
    // Check if the start voxel exists
    if (!VoxelGrid->VoxelData.Contains(StartVoxel))
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Start voxel %s not found in grid."), *StartVoxel.ToString());
        return Result;
    }
    
    FVector ChunkOrigin = FVoxelConversion::ChunkToWorld(Chunk->GetChunkPosition());
    FVector StartVoxelWorldPos = FVoxelConversion::LocalVoxelToWorld(StartVoxel);
    
    // Make a backup of the original grid before extraction
    USparseVoxelGrid* OriginalGridBackup = nullptr;
   /* if (bMakeBackupBeforeExtraction)
    {
        OriginalGridBackup = DuplicateObject<USparseVoxelGrid>(VoxelGrid, this);
    }*/
    
    // Extract the island
    USparseVoxelGrid* ExtractedGrid = nullptr;
    TArray<FIntVector> IslandVoxels;
    
    bool bExtractionSuccess = VoxelGrid->ExtractIslandAtPosition(
        StartVoxelWorldPos, 
        ExtractedGrid, 
        IslandVoxels
    );
    
    if (!bExtractionSuccess || !ExtractedGrid || IslandVoxels.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Failed to extract island or ExtractedGrid is null."));
        
        // Restore from backup if needed
/*        if (bMakeBackupBeforeExtraction && OriginalGridBackup)
        {
            UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Restoring grid from backup."));
            Chunk->SetSparseVoxelGrid(OriginalGridBackup);
        }*/
        
        return Result;
    }
    
    // Ensure the extracted grid has the necessary references
    /*ExtractedGrid->ParentChunk = nullptr; // Don't want to affect the original chunk
    ExtractedGrid->DiggerManager = this;*/
    ExtractedGrid->Initialize(nullptr);
    
    // Generate mesh
    UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Starting mesh generation using Marching Cubes."));
    
    MarchingCubes->GenerateMeshFromGrid(
        ExtractedGrid, ChunkOrigin, VoxelSize,
        Result.Vertices, Result.Triangles, Result.Normals
    );
    
    // Finalize result
    Result.MeshOrigin = ChunkOrigin;
    Result.bValid = (Result.Vertices.Num() > 0 && Result.Triangles.Num() > 0);
    
    UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Mesh generation complete. Valid: %s, Vertices: %d, Triangles: %d"),
        Result.bValid ? TEXT("true") : TEXT("false"),
        Result.Vertices.Num(),
        Result.Triangles.Num() / 3
    );
    
    // Mark the chunk as dirty to trigger a rebuild
    Chunk->MarkDirty();
    
    return Result;
}



void ADiggerManager::ClearAllIslandActors()
{
    for (AIslandActor* Actor : IslandActors)
    {
        if (Actor && IsValid(Actor))
        {
            Actor->Destroy();
        }
    }
    IslandActors.Empty();
    SavedIslands.Empty();
}

void ADiggerManager::DestroyIslandActor(AIslandActor* IslandActor)
{
    if (IslandActor && IsValid(IslandActor))
    {
        IslandActors.Remove(IslandActor);
        
        // Find and remove from saved islands
        for (int32 i = SavedIslands.Num() - 1; i >= 0; --i)
        {
            // This is a simple check - you might need a more sophisticated way to match
            if (FVector::DistSquared(SavedIslands[i].MeshOrigin, IslandActor->GetActorLocation()) < 1.0f)
            {
                SavedIslands.RemoveAt(i);
                break;
            }
        }
        
        IslandActor->Destroy();
    }
}



void ADiggerManager::SaveIslandMeshAsStaticMesh(
    const FString& AssetName,
    const FIslandMeshData& MeshData
)
{
    if (!MeshData.bValid) return;

    TArray<FVector3f> FloatVerts = ConvertArray<FVector, FVector3f>(MeshData.Vertices);
    TArray<FVector3f> FloatNormals = ConvertArray<FVector, FVector3f>(MeshData.Normals);
    
    CreateStaticMeshFromRawData(
        GetTransientPackage(),
        AssetName,
        FloatVerts,
        MeshData.Triangles,
        FloatNormals,
        {}, {}, {}
    );
}


bool ADiggerManager::EnsureWorldReference()
{
    if (World)
    {
        return true;
    }

    World = GetSafeWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("World not found in DiggerManager."));
        return false; // Exit if World is not found
    }
    return true;
}

void ADiggerManager::RecreateIslandFromSaveData(const FIslandSaveData& SaveData)
{
    // Create a temporary FIslandMeshData
    FIslandMeshData MeshData;
    MeshData.MeshOrigin = SaveData.MeshOrigin;
    MeshData.Vertices = SaveData.Vertices;
    MeshData.Triangles = SaveData.Triangles;
    MeshData.Normals = SaveData.Normals;
    MeshData.bValid = (MeshData.Vertices.Num() > 0 && MeshData.Triangles.Num() > 0);
    
    // Spawn the island actor
    SpawnIslandActorWithMeshData(SaveData.MeshOrigin, MeshData, SaveData.bEnablePhysics);
}



void ADiggerManager::BeginPlay()
{
    Super::BeginPlay();

    if (!EnsureWorldReference())
    {
        UE_LOG(LogTemp, Error, TEXT("World is null in DiggerManager BeginPlay()! Continuing with default behavior."));
    }

    UpdateVoxelSize();
    ActiveBrush->InitializeBrush(ActiveBrush->GetBrushType(),ActiveBrush->GetBrushSize(),ActiveBrush->GetBrushLocation(),this);
    InitializeBrushShapes();

    // Start the timer to process dirty chunks
    if (World)
    {
        World->GetTimerManager().SetTimer(ChunkProcessTimerHandle, this, &ADiggerManager::ProcessDirtyChunks, 1.0f, true);
    }

    // Set the ProceduralMesh material if it's valid
    if (TerrainMaterial)
    {
        if (ProceduralMesh && ProceduralMesh->GetMaterial(0) != TerrainMaterial)
        {
            ProceduralMesh->SetMaterial(0, TerrainMaterial);
            UE_LOG(LogTemp, Warning, TEXT("Set Material M_ProcGrid at index 0"));
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Material M_ProcGrid is null. Please ensure it is loaded properly."));
    }

    // Recreate saved islands when entering PIE
    for (const FIslandSaveData& SavedIsland : SavedIslands)
    {
        RecreateIslandFromSaveData(SavedIsland);
    }
}


UStaticMesh* ADiggerManager::CreateStaticMeshFromRawData(
    UObject* Outer,
    const FString& AssetName,
    const TArray<FVector3f>& Vertices,
    const TArray<int32>& Triangles,
    const TArray<FVector3f>& Normals,
    const TArray<FVector2d>& UVs,
    const TArray<FColor>& Colors,
    const TArray<FProcMeshTangent>& Tangents
)
{
    UE_LOG(LogTemp, Warning, TEXT("Starting mesh creation for asset: %s"), *AssetName);
    UE_LOG(LogTemp, Warning, TEXT("Vertices: %d, Triangles: %d"), Vertices.Num(), Triangles.Num());

    FString PackageName = FString::Printf(TEXT("/Game/Islands/%s"), *AssetName);
    UPackage* MeshPackage = CreatePackage(*PackageName);
    UE_LOG(LogTemp, Warning, TEXT("Created package: %s"), *PackageName);

    UStaticMesh* StaticMesh = NewObject<UStaticMesh>(MeshPackage, *AssetName, RF_Public | RF_Standalone);
    if (!StaticMesh)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create UStaticMesh object!"));
        return nullptr;
    }

    FMeshDescription MeshDescription;
    FStaticMeshAttributes Attributes(MeshDescription);
    Attributes.Register();
    UE_LOG(LogTemp, Warning, TEXT("Registered mesh attributes"));

    FPolygonGroupID PolyGroupID = MeshDescription.CreatePolygonGroup();
    UE_LOG(LogTemp, Warning, TEXT("Created polygon group"));

    TMap<int32, FVertexID> IndexToVertexID;
    for (int32 i = 0; i < Vertices.Num(); ++i)
    {
        FVertexID VertexID = MeshDescription.CreateVertex();
        Attributes.GetVertexPositions()[VertexID] = Vertices[i];
        IndexToVertexID.Add(i, VertexID);
    }
    UE_LOG(LogTemp, Warning, TEXT("Created %d vertex positions"), Vertices.Num());

    for (int32 i = 0; i < Triangles.Num(); i += 3)
    {
        TArray<FVertexInstanceID> VertexInstanceIDs;
        for (int32 j = 0; j < 3; ++j)
        {
            int32 VertexIndex = Triangles[i + j];
            if (!IndexToVertexID.Contains(VertexIndex))
            {
                UE_LOG(LogTemp, Error, TEXT("Vertex index %d not found in vertex map!"), VertexIndex);
                continue;
            }

            FVertexInstanceID InstanceID = MeshDescription.CreateVertexInstance(IndexToVertexID[VertexIndex]);

            if (Normals.IsValidIndex(VertexIndex))
                Attributes.GetVertexInstanceNormals()[InstanceID] = Normals[VertexIndex];

            if (UVs.IsValidIndex(VertexIndex))
                Attributes.GetVertexInstanceUVs().Set(InstanceID, 0, FVector2f(UVs[VertexIndex]));

            if (Colors.IsValidIndex(VertexIndex))
                Attributes.GetVertexInstanceColors()[InstanceID] = FVector4f(Colors[VertexIndex]);

            if (Tangents.IsValidIndex(VertexIndex))
            {
                Attributes.GetVertexInstanceTangents()[InstanceID] = (FVector3f)Tangents[VertexIndex].TangentX;
                Attributes.GetVertexInstanceBinormalSigns()[InstanceID] = Tangents[VertexIndex].bFlipTangentY ? -1.0f : 1.0f;
            }

            VertexInstanceIDs.Add(InstanceID);
        }

        if (VertexInstanceIDs.Num() == 3)
        {
            MeshDescription.CreatePolygon(PolyGroupID, VertexInstanceIDs);
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to create triangle at index %d (missing vertex instances)"), i);
        }
    }

#if WITH_EDITORONLY_DATA
    StaticMesh->BuildFromMeshDescriptions({ &MeshDescription });
    UE_LOG(LogTemp, Warning, TEXT("Built mesh description"));
#endif

    StaticMesh->CommitMeshDescription(0);
    UE_LOG(LogTemp, Warning, TEXT("Committed mesh description"));

    StaticMesh->Build(false);
    UE_LOG(LogTemp, Warning, TEXT("Built static mesh"));

    FAssetRegistryModule::AssetCreated(StaticMesh);
    StaticMesh->MarkPackageDirty();

    UE_LOG(LogTemp, Warning, TEXT("Static mesh asset created successfully: %s"), *PackageName);
    return StaticMesh;
}



void ADiggerManager::BakeToStaticMesh(bool bEnableCollision, bool bEnableNanite, float DetailReduction)
{
    UE_LOG(LogTemp, Warning, TEXT("BakeToStaticMesh called with:\n  Collision: %s\n  Nanite: %s\n  DetailReduction: %.2f"),
        bEnableCollision ? TEXT("True") : TEXT("False"),
        bEnableNanite ? TEXT("True") : TEXT("False"),
        DetailReduction);

/*#if WITH_EDITOR
    FString AssetName = FString::Printf(TEXT("SM_Baked_%s_%s_%.2f"),
        bEnableCollision ? TEXT("Col") : TEXT("NoCol"),
        bEnableNanite ? TEXT("Nanite") : TEXT("NoNanite"),
        DetailReduction);

    UStaticMesh* NewMesh = CreateStaticMeshFromProceduralMesh(
        GetTransientPackage(), // Or another suitable Outer
        AssetName,
        MyProceduralMeshComponent // <-- Replace with your actual component
    );

    if (NewMesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("Static mesh created: %s"), *AssetName);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create static mesh!"));
    }
#endif*/
}




void ADiggerManager::UpdateVoxelSize()
{
    if (Subdivisions != 0)
    {
        VoxelSize = TerrainGridSize / Subdivisions;
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("Subdivisions value is zero. VoxelSize will not be updated to avoid division by zero."));
        VoxelSize = 1.0f; // Default fallback value
    }
}

void ADiggerManager::InitializeChunks()
{
    for (const auto& ChunkPair : ChunkMap)
    {
        const FIntVector& Coordinates = ChunkPair.Key;
        UVoxelChunk* NewChunk = NewObject<UVoxelChunk>(this);
        if (!NewChunk)
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to create a new chunk for coordinates: %s"), *Coordinates.ToString());
            continue;
        }

        UProceduralMeshComponent* NewMeshComponent = NewObject<UProceduralMeshComponent>(this);
        if (NewMeshComponent)
        {
            NewMeshComponent->RegisterComponent();
            NewMeshComponent->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
            NewChunk->InitializeMeshComponent(NewMeshComponent);
            ChunkMap.Add(Coordinates, NewChunk);
            ProceduralMeshComponents.Add(NewMeshComponent);
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to create a new ProceduralMeshComponent for coordinates: %s"), *Coordinates.ToString());
        }
    }
}

void ADiggerManager::InitializeSingleChunk(UVoxelChunk* Chunk)
{
    if (!Chunk)
    {
        UE_LOG(LogTemp, Error, TEXT("InitializeSingleChunk called with a null chunk."));
        return;
    }

    FIntVector Coordinates = Chunk->GetChunkPosition();
    UProceduralMeshComponent* NewMeshComponent = NewObject<UProceduralMeshComponent>(this);
    if (NewMeshComponent)
    {
        NewMeshComponent->RegisterComponent();
        NewMeshComponent->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
        Chunk->InitializeMeshComponent(NewMeshComponent);
        ChunkMap.Add(Coordinates, Chunk);
        ProceduralMeshComponents.Add(NewMeshComponent);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create a ProceduralMeshComponent for chunk at coordinates: %s"), *Coordinates.ToString());
    }
}

void ADiggerManager::UpdateLandscapeProxies()
{
    CachedLandscapeProxies.Empty();
    TArray<AActor*> FoundLandscapes;
    UGameplayStatics::GetAllActorsOfClass(GetSafeWorld(), ALandscapeProxy::StaticClass(), FoundLandscapes);

    for (AActor* Actor : FoundLandscapes)
    {
        if (ALandscapeProxy* Proxy = Cast<ALandscapeProxy>(Actor))
        {
            CachedLandscapeProxies.Add(Proxy);
        }
    }
}

TSharedPtr<TMap<FIntPoint, float>> ADiggerManager::GetOrCreateLandscapeHeightCache(ALandscapeProxy* Landscape)
{
    if (!IsValid(Landscape))
    {
        UE_LOG(LogTemp, Warning, TEXT("Invalid LandscapeProxy passed to GetOrCreateLandscapeHeightCache."));
        return nullptr;
    }

    if (!LandscapeHeightCaches.Contains(Landscape))
    {
        LandscapeHeightCaches.Add(Landscape, MakeShared<TMap<FIntPoint, float>>());
    }

    return LandscapeHeightCaches[Landscape];
}


float ADiggerManager::GetLandscapeHeightAt(FVector WorldPosition)
{
    ALandscapeProxy* LandscapeProxy = nullptr;

    // Use the last used proxy if it's still valid and encompasses the position
    if (LastUsedLandscape && LastUsedLandscape->GetRootComponent() &&
        LastUsedLandscape->GetComponentsBoundingBox().IsInsideXY(WorldPosition))
    {
        LandscapeProxy = LastUsedLandscape;
    }
    else
    {
        // Look through cached proxies to find one that fits the position
        for (auto& Elem : CachedLandscapeProxies)
        {
            ALandscapeProxy* Candidate = Elem.Key;
            if (Candidate && Candidate->GetRootComponent() &&
                Candidate->GetComponentsBoundingBox().IsInsideXY(WorldPosition))
            {
                LandscapeProxy = Candidate;
                break;
            }
        }

        // If none found, do an expensive search ONCE and cache
        if (!LandscapeProxy)
        {
            TArray<AActor*> FoundLandscapes;
            UGameplayStatics::GetAllActorsOfClass(GetSafeWorld(), ALandscapeProxy::StaticClass(), FoundLandscapes);

            for (AActor* Actor : FoundLandscapes)
            {
                ALandscapeProxy* Candidate = Cast<ALandscapeProxy>(Actor);
                if (Candidate && Candidate->GetRootComponent() &&
                    Candidate->GetComponentsBoundingBox().IsInsideXY(WorldPosition))
                {
                    CachedLandscapeProxies.Add(Candidate, true);
                    LandscapeProxy = Candidate;
                    break;
                }
            }
        }

        LastUsedLandscape = LandscapeProxy;
    }

    if (!LandscapeProxy)
    {
        UE_LOG(LogTemp, Warning, TEXT("No landscape found at location %s"), *WorldPosition.ToString());
        return -100000.0f;
    }

    // Try getting height using different sources in order of complexity
    TOptional<float> HeightResult;

    HeightResult = LandscapeProxy->GetHeightAtLocation(WorldPosition, EHeightfieldSource::Simple);

    if (!HeightResult.IsSet())
    {
        HeightResult = LandscapeProxy->GetHeightAtLocation(WorldPosition, EHeightfieldSource::Complex);
    }

    if (!HeightResult.IsSet() && GIsEditor)
    {
        HeightResult = LandscapeProxy->GetHeightAtLocation(WorldPosition, EHeightfieldSource::Editor);
    }

    if (!HeightResult.IsSet())
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to get height at location: %s"), *WorldPosition.ToString());
        return -100000.0f;
    }

    return HeightResult.GetValue();
}


void ADiggerManager::PopulateLandscapeHeightCacheAsync(ALandscapeProxy* Landscape)
{
    if (!Landscape) return;

    const float LocalVoxelSize = VoxelSize;
    FBox Bounds = Landscape->GetComponentsBoundingBox();

    // Prepare data for async task
    FVector Min = Bounds.Min;
    FVector Max = Bounds.Max;
    TWeakObjectPtr<ALandscapeProxy> WeakLandscape = Landscape;
    TWeakObjectPtr<ADiggerManager> WeakSelf = this;

    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [WeakSelf, WeakLandscape, Min, Max, LocalVoxelSize]()
    {
        if (!WeakSelf.IsValid() || !WeakLandscape.IsValid()) return;

        TMap<FIntPoint, float> LocalMap;

        for (float X = Min.X; X < Max.X; X += LocalVoxelSize)
        {
            for (float Y = Min.Y; Y < Max.Y; Y += LocalVoxelSize)
            {
                FVector SamplePos(X, Y, 0);
                TOptional<float> Sampled = WeakSelf->SampleLandscapeHeight(WeakLandscape.Get(), SamplePos);
                if (Sampled.IsSet())
                {
                    int32 GridX = FMath::FloorToInt(X / LocalVoxelSize);
                    int32 GridY = FMath::FloorToInt(Y / LocalVoxelSize);
                    FIntPoint Key(GridX, GridY);
                    LocalMap.Add(Key, Sampled.GetValue());
                }
            }
        }

        // Copy back on game thread
        AsyncTask(ENamedThreads::GameThread, [WeakSelf, WeakLandscape, LocalMap = MoveTemp(LocalMap)]()
        {
            if (!WeakSelf.IsValid() || !WeakLandscape.IsValid()) return;

            WeakSelf->LandscapeHeightCaches.FindOrAdd(WeakLandscape.Get()) = MakeShared<TMap<FIntPoint, float>>(LocalMap);
            WeakSelf->HeightCacheLoadingSet.Remove(WeakLandscape.Get());

            UE_LOG(LogTemp, Warning, TEXT("Async height cache complete for landscape: %s (%d entries)"), *WeakLandscape->GetName(), LocalMap.Num());
        });
    });
}



float ADiggerManager::GetCachedLandscapeHeightAt(const FVector& WorldPos)
{
    ALandscapeProxy* Landscape = GetLandscapeProxyAt(WorldPos);
    if (!IsValid(Landscape)) return 0.0f;

    const int32 GridX = FMath::FloorToInt(WorldPos.X / VoxelSize);
    const int32 GridY = FMath::FloorToInt(WorldPos.Y / VoxelSize);
    const FIntPoint Key(GridX, GridY);

    // Get or create the per-landscape height cache
    TSharedPtr<TMap<FIntPoint, float>> HeightCache = GetOrCreateLandscapeHeightCache(Landscape);
    if (!HeightCache.IsValid()) return 0.0f;

    // If the cache is not already being loaded, trigger async population
    if (!HeightCacheLoadingSet.Contains(Landscape))
    {
        HeightCacheLoadingSet.Add(Landscape);
        PopulateLandscapeHeightCacheAsync(Landscape);
    }

    // Try returning cached value
    if (float* Cached = HeightCache->Find(Key))
    {
        return *Cached;
    }

    // Fallback: sample landscape height directly
    TOptional<float> SampledHeight = SampleLandscapeHeight(Landscape, WorldPos);
    if (SampledHeight.IsSet())
    {
        const float Height = SampledHeight.GetValue();
        HeightCache->Add(Key, Height);
        return Height;
    }

    return 0.0f;
}



void ADiggerManager::PopulateLandscapeHeightCache(ALandscapeProxy* Landscape)
{
    if (!Landscape || LandscapeHeightCaches.Contains(Landscape)) return;

    UE_LOG(LogTemp, Warning, TEXT("Populating height cache for landscape proxy: %s"), *Landscape->GetName());

    TMap<FIntPoint, float>& HeightMap = *LandscapeHeightCaches.FindOrAdd(Landscape);

    FBox Bounds = Landscape->GetComponentsBoundingBox();
    FVector Min = Bounds.Min;
    FVector Max = Bounds.Max;

    for (float X = Min.X; X < Max.X; X += VoxelSize)
    {
        for (float Y = Min.Y; Y < Max.Y; Y += VoxelSize)
        {
            FVector SamplePos(X, Y, 0);
            TOptional<float> Sampled = SampleLandscapeHeight(Landscape, SamplePos);
            if (Sampled.IsSet())
            {
                int32 GridX = FMath::FloorToInt(X / VoxelSize);
                int32 GridY = FMath::FloorToInt(Y / VoxelSize);
                FIntPoint Key(GridX, GridY);
                HeightMap.Add(Key, Sampled.GetValue());
            }
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("Completed height cache for landscape proxy: %s. Cached %d entries."), *Landscape->GetName(), HeightMap.Num());
}



ALandscapeProxy* ADiggerManager::GetLandscapeProxyAt(const FVector& WorldPos)
{
    for (TActorIterator<ALandscapeProxy> It(GetSafeWorld()); It; ++It)
    {
        ALandscapeProxy* Proxy = *It;
        if (Proxy && Proxy->GetComponentsBoundingBox().IsInsideXY(WorldPos))
        {
            return Proxy;
        }
    }

    return nullptr;
}

TOptional<float> ADiggerManager::SampleLandscapeHeight(ALandscapeProxy* Landscape, const FVector& WorldPos)
{
    FVector LandscapeLocal = Landscape->GetTransform().InverseTransformPosition(WorldPos);
    return Landscape->GetHeightAtLocation(LandscapeLocal);
}

float ADiggerManager::GetSmartLandscapeHeightAt(const FVector& WorldPos)
{
    return GetSmartLandscapeHeightAt(WorldPos, false);
}

float ADiggerManager::GetSmartLandscapeHeightAt(const FVector& WorldPos, bool bForcePrecise)
{
    //const float HeightTolerance = VoxelSize * 0.5f; // Allowable vertical margin for refinement

   // float CachedHeight = GetCachedLandscapeHeightAt(WorldPos);

   // if (bForcePrecise)
    //{
        return GetLandscapeHeightAt(WorldPos);
   /* }

    // Compare Z difference (height) only, not full vector
    float VerticalDifference = FMath::Abs(WorldPos.Z - CachedHeight);

    if (VerticalDifference <= HeightTolerance)
    {
        // We're close enough — use precise sample
        return GetLandscapeHeightAt(WorldPos);
    }

    return CachedHeight;*/
}



bool ADiggerManager::GetHeightAtLocation(ALandscapeProxy* LandscapeProxy, const FVector& Location, float& OutHeight)
{
    if (!LandscapeProxy || !LandscapeProxy->GetLandscapeInfo())
    {
        return false;
    }

    // Convert world location to landscape local space
    FVector LocalPosition = LandscapeProxy->GetTransform().InverseTransformPosition(Location);

    // Try getting height using different sources
    TOptional<float> HeightResult = LandscapeProxy->GetHeightAtLocation(LocalPosition, EHeightfieldSource::Simple);

    if (!HeightResult.IsSet())
    {
        HeightResult = LandscapeProxy->GetHeightAtLocation(LocalPosition, EHeightfieldSource::Complex);
    }

    if (HeightResult.IsSet())
    {
        // Convert the height to world space
        OutHeight = static_cast<float>(HeightResult.GetValue() * LandscapeProxy->GetActorScale3D().Z +
            LandscapeProxy->GetActorLocation().Z);
        return true;
    }

    return false;
}

FVector ADiggerManager::GetLandscapeNormalAt(const FVector& WorldPosition)
{
    // Get all landscape actors in the level
    TArray<AActor*> FoundLandscapes;
    UGameplayStatics::GetAllActorsOfClass(GetSafeWorld(), ALandscapeProxy::StaticClass(), FoundLandscapes);

    if (FoundLandscapes.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("No landscape found in level"));
        return FVector::UpVector;
    }

    ALandscapeProxy* LandscapeProxy = Cast<ALandscapeProxy>(FoundLandscapes[0]);
    if (!LandscapeProxy)
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to cast landscape actor"));
        return FVector::UpVector;
    }

    // Use a small offset for sampling (in cm)
    const float Delta = 5.0f;

    FVector PosX = WorldPosition + FVector(Delta, 0, 0);
    FVector NegX = WorldPosition - FVector(Delta, 0, 0);
    FVector PosY = WorldPosition + FVector(0, Delta, 0);
    FVector NegY = WorldPosition - FVector(0, Delta, 0);

    float HeightX1 = GetLandscapeHeightAt(PosX);
    float HeightX0 = GetLandscapeHeightAt(NegX);
    float HeightY1 = GetLandscapeHeightAt(PosY);
    float HeightY0 = GetLandscapeHeightAt(NegY);

    // Calculate gradient
    FVector Gradient;
    Gradient.X = (HeightX1 - HeightX0) / (2 * Delta);
    Gradient.Y = (HeightY1 - HeightY0) / (2 * Delta);
    Gradient.Z = 1.0f;

    // The normal is the inverse of the gradient in X and Y, with Z up
    FVector Normal = FVector(-Gradient.X, -Gradient.Y, 1.0f).GetSafeNormal();
    return Normal;
}

UWorld* ADiggerManager::GetSafeWorld() const
{
#if WITH_EDITOR
    if (GIsEditor && GEditor)
    {
        return GEditor->GetEditorWorldContext().World();
    }
#endif
    return GetWorld();
}

void ADiggerManager::HandleHoleSpawn(const FBrushStroke& Stroke)
{
    if (!HoleBP)
    {
        UE_LOG(LogTemp, Error, TEXT("HoleBP is not set in ADiggerManager!"));
        return;
    }

    if (!BrushShape)
    {
        UE_LOG(LogTemp, Error, TEXT("BrushShape is null in HandleHoleSpawn!"));
        return;
    }

    // Use the safer raycast method that performs complex trace when hitting existing holes
    FHitResult Hit = BrushShape->GetCameraHitLocation();

    FVector SpawnLocation = Stroke.BrushPosition; // Default to stroke position
    FRotator SpawnRotation = Stroke.BrushRotation;
    FVector SpawnScale = FVector(Stroke.BrushRadius/2);

    // Only use hit location if we got a valid blocking hit
    if (Hit.bBlockingHit && Hit.Location != FVector::ZeroVector)
    {
        SpawnLocation = Hit.Location;
        
        // If we hit something, use the surface normal to orient the hole properly
        if (Hit.Normal != FVector::ZeroVector)
        {
            SpawnRotation = FRotationMatrix::MakeFromZ(Hit.Normal).Rotator();
        }
        
        UE_LOG(LogTemp, Log, TEXT("Using hit location for hole spawn: %s with normal: %s"), 
               *SpawnLocation.ToString(), *Hit.Normal.ToString());
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("Using stroke position for hole spawn: %s"), *SpawnLocation.ToString());
    }

    // Debug the spawn parameters
    UE_LOG(LogTemp, Log, TEXT("Spawning HoleBP: %s"), HoleBP ? *HoleBP->GetName() : TEXT("NULL"));
    UE_LOG(LogTemp, Log, TEXT("Spawn Location: %s"), *SpawnLocation.ToString());
    UE_LOG(LogTemp, Log, TEXT("Spawn Rotation: %s"), *SpawnRotation.ToString());
    UE_LOG(LogTemp, Log, TEXT("Spawn Scale: %s"), *SpawnScale.ToString());

    // Use your existing GetSafeWorld method that handles both editor and PIE
    if(!World)
        World = GetSafeWorld();
    
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("GetSafeWorld() returned null in HandleHoleSpawn!"));
        return;
    }
    
    UE_LOG(LogTemp, Log, TEXT("Using world: %s"), *World->GetName());

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    
    AActor* SpawnedHole = nullptr;
    
#if WITH_EDITOR
    if (GIsEditor && !GIsPlayInEditorWorld)
    {
        // In editor mode, use transactional spawning
        const FScopedTransaction Transaction(NSLOCTEXT("DiggerManager", "SpawnHole", "Spawn Hole"));
        World->Modify();
        
        SpawnedHole = World->SpawnActor<AActor>(HoleBP, SpawnLocation, SpawnRotation, SpawnParams);
        
        if (SpawnedHole)
        {
            // Mark the actor as transactional for undo/redo
            SpawnedHole->Modify();
            SpawnedHole->SetFlags(RF_Transactional);
            
            // Register with editor for proper cleanup
            GEditor->SelectNone(false, true);
            GEditor->SelectActor(SpawnedHole, true, false);
        }
    }
    else
#endif
    {
        // In PIE or packaged game, use normal spawning
        SpawnedHole = World->SpawnActor<AActor>(HoleBP, SpawnLocation, SpawnRotation, SpawnParams);
    }

    if (SpawnedHole)
    {
        // Set scale based on brush radius
        SpawnedHole->SetActorScale3D(SpawnScale);
        UE_LOG(LogTemp, Log, TEXT("Successfully spawned hole: %s at: %s with scale: %s"), 
               *SpawnedHole->GetName(), *SpawnLocation.ToString(), *SpawnScale.ToString());
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to spawn hole at location: %s"), *SpawnLocation.ToString());
        UE_LOG(LogTemp, Error, TEXT("Check: 1) HoleBP is valid 2) World is valid 3) Location is not in solid geometry"));
    }
}



void ADiggerManager::DuplicateLandscape(ALandscapeProxy* Landscape)
{
    if (!Landscape) return;

    ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
    if (!LandscapeInfo) return;

    // Get landscape extent
    int32 LandscapeMinX, LandscapeMinY, LandscapeMaxX, LandscapeMaxY;
    LandscapeInfo->GetLandscapeExtent(LandscapeMinX, LandscapeMinY, LandscapeMaxX, LandscapeMaxY);

    // Create data access interface
    FLandscapeEditDataInterface LandscapeData(LandscapeInfo);

    int32 ChunkVoxelCount = ChunkSize / VoxelSize;

    // Get landscape transform for proper world position calculation
    FTransform LandscapeTransform = Landscape->GetActorTransform();
    float LandscapeScale = LandscapeTransform.GetScale3D().Z;

    // Iterate over the landscape extent in chunks
    for (int32 Y = LandscapeMinY; Y <= LandscapeMaxY; Y += ChunkVoxelCount)
    {
        for (int32 X = LandscapeMinX; X <= LandscapeMaxX; X += ChunkVoxelCount)
        {
            FIntVector ChunkPosition(X, Y, 0);
            UVoxelChunk* Chunk = GetOrCreateChunkAtChunk(ChunkPosition);

            if (Chunk)
            {
                // Calculate chunk boundaries in landscape space
                int32 ChunkEndX = FMath::Min(X + ChunkVoxelCount, LandscapeMaxX);
                int32 ChunkEndY = FMath::Min(Y + ChunkVoxelCount, LandscapeMaxY);

                // Get height data for the entire chunk area
                TMap<FIntPoint, uint16> HeightData;
                int32 StartX = X;
                int32 StartY = Y;
                int32 EndX = ChunkEndX;
                int32 EndY = ChunkEndY;
                LandscapeData.GetHeightData(StartX, StartY, EndX, EndY, HeightData);

                for (int32 VoxelY = 0; VoxelY < ChunkVoxelCount; ++VoxelY)
                {
                    for (int32 VoxelX = 0; VoxelX < ChunkVoxelCount; ++VoxelX)
                    {
                        int32 LandscapeX = X + VoxelX;
                        int32 LandscapeY = Y + VoxelY;

                        // Skip if we're outside landscape bounds
                        if (LandscapeX > LandscapeMaxX || LandscapeY > LandscapeMaxY)
                            continue;

                        // Get height value from the height data map
                        FIntPoint Key(LandscapeX, LandscapeY);
                        float Height = 0.0f;

                        if (const uint16* HeightValue = HeightData.Find(Key))
                        {
                            // Convert from uint16 to world space height
                            // LandscapeDataAccess.h: Landscape uses USHRT_MAX as "32768" = 0.0f world space
                            const float ScaleFactor = LandscapeScale / 128.0f; // Landscape units to world units
                            Height = ((float)*HeightValue - 32768.0f) * ScaleFactor;
                            Height += LandscapeTransform.GetLocation().Z; // Add landscape base height
                        }

                        for (int32 VoxelZ = 0; VoxelZ < ChunkVoxelCount; ++VoxelZ)
                        {
                            // Convert voxel coordinates to world space
                            FVector WorldPosition = FVoxelConversion::LocalVoxelToWorld(FIntVector(VoxelX, VoxelY, VoxelZ));

                            // Calculate SDF value (distance from surface)
                            // Positive values are above the surface, negative values are below
                            float SDFValue = WorldPosition.Z - Height;

                            // Optional: Normalize SDF value by voxel size
                            SDFValue /= VoxelSize;

                            // Set the voxel value
                            bool bDig = false;
                            Chunk->SetVoxel(VoxelX, VoxelY, VoxelZ, SDFValue, bDig);
                        }
                    }
                }

                // Mark the chunk for update
                Chunk->MarkDirty();
            }
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

void ADiggerManager::DebugVoxels()
{
    if (!ChunkMap.IsEmpty())
    {
        DebugDrawChunkSectionIDs();
        DebugLogVoxelChunkGrid();
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("No chunks available for rendering."));
    }
}

void ADiggerManager::ProcessDirtyChunks()
{
    for (auto& Elem : ChunkMap)
    {
        UVoxelChunk* Chunk = Elem.Value;
        if (Chunk)
        {
            Chunk->UpdateIfDirty();
        }
    }

    if (World)
    {
        AsyncTask(ENamedThreads::GameThread, [this]()
        {
            if (World)
            {
                World->GetTimerManager().ClearTimer(ChunkProcessTimerHandle);
                World->GetTimerManager().SetTimer(ChunkProcessTimerHandle, this, &ADiggerManager::ProcessDirtyChunks, 2.0f, true);
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("World is null when resetting the ProcessDirtyChunks timer."));
            }
        });
    }
}



int32 ADiggerManager::GetHitSectionIndex(const FHitResult& HitResult)
{
    if (!HitResult.GetComponent()) return -1;

    UProceduralMeshComponent* HitProceduralMesh = Cast<UProceduralMeshComponent>(HitResult.GetComponent());
    if (!HitProceduralMesh) return -1;

    const FVector HitLocation = HitResult.ImpactPoint;

    for (int32 SectionIndex = 0; SectionIndex < HitProceduralMesh->GetNumSections(); ++SectionIndex)
    {
        const FProcMeshSection* Section = HitProceduralMesh->GetProcMeshSection(SectionIndex);
        if (!Section || Section->ProcVertexBuffer.Num() == 0) continue;

        FBox SectionBox(Section->ProcVertexBuffer[0].Position, Section->ProcVertexBuffer[0].Position);
        for (const FProcMeshVertex& Vertex : Section->ProcVertexBuffer)
        {
            SectionBox += Vertex.Position;
        }

        if (SectionBox.IsInside(HitLocation))
        {
            return SectionIndex;
        }
    }

    return -1; // Section not found
}

UVoxelChunk* ADiggerManager::GetChunkBySectionIndex(int32 SectionIndex)
{
    for (auto& Entry : ChunkMap)
    {
        if (Entry.Value->GetSectionIndex() == SectionIndex)
        {
            return Entry.Value;
        }
    }

    return nullptr; // Chunk not found
}

void ADiggerManager::UpdateChunkFromSectionIndex(const FHitResult& HitResult)
{
    int32 SectionIndex = GetHitSectionIndex(HitResult);
    if (SectionIndex == -1) return;

    UVoxelChunk* HitChunk = GetChunkBySectionIndex(SectionIndex);
    if (HitChunk)
    {
        HitChunk->MarkDirty();
    }
}

void ADiggerManager::DebugDrawChunkSectionIDs()
{
    for (auto& Entry : ChunkMap)
    {
        UVoxelChunk* Chunk = Entry.Value;
        if (Chunk)
        {
            Chunk->GetSparseVoxelGrid()->RenderVoxels();
            const FVector ChunkPosition = FVoxelConversion::ChunkToWorld(Chunk->GetChunkPosition());
            const FString SectionIDText = FString::Printf(TEXT("ID: %d"), Chunk->GetSectionIndex());
            DrawDebugString(GetSafeWorld(), ChunkPosition, SectionIDText, nullptr, FColor::Green, 5.0f, true);
        }
    }
}

void ADiggerManager::ApplyBrush()
{
    if (!ActiveBrush)
    {
        UE_LOG(LogTemp, Error, TEXT("ActiveBrush is null. Cannot apply brush."));
        return;
    }

    FVector BrushPosition = ActiveBrush->GetBrushLocation();
    float BrushRadius = ActiveBrush->GetBrushSize();
    FBrushStroke BrushStroke;
    BrushStroke.BrushPosition = BrushPosition;
    BrushStroke.BrushRadius = BrushRadius;
    BrushStroke.bDig = ActiveBrush->GetDig();
    BrushStroke.BrushType = ActiveBrush->GetBrushType();

    ApplyBrushToAllChunks(BrushStroke);


    if (BrushStroke.bDig)
    {
        MarkNearbyChunksDirty(BrushPosition, BrushRadius);
    }
}

UVoxelChunk* ADiggerManager::FindOrCreateNearestChunk(const FVector& Position)
{
    FIntVector ChunkCoords = FVoxelConversion::WorldToChunk(Position);
    UVoxelChunk** ExistingChunk = ChunkMap.Find(ChunkCoords);
    if (ExistingChunk && *ExistingChunk)
    {
        UE_LOG(LogTemp, Warning, TEXT("Chunk found at %s"), *ChunkCoords.ToString());
        return *ExistingChunk;
    }

    
    // Find nearest chunk if no exact match found
    UVoxelChunk* NearestChunk = nullptr;
    float MinDistance = FLT_MAX;
    for (auto& Entry : ChunkMap)
    {
        FVector WorldPos = FVoxelConversion::ChunkToWorld(NearestChunk->GetChunkPosition());
        float Distance = FVector::Dist(Position, WorldPos);
        if (Distance < MinDistance)
        {
            MinDistance = Distance;
            NearestChunk = Entry.Value;
        }
    }

    // If no nearby chunk is found, create a new one
    if (!NearestChunk)
    {
        UE_LOG(LogTemp, Warning, TEXT("[FindOrCreateNearestChunk] Input Position (World): %s"), *Position.ToString());
        ChunkCoords = FVoxelConversion::WorldToChunk(Position);
        UE_LOG(LogTemp, Warning, TEXT("[FindOrCreateNearestChunk] ChunkCoords: %s"), *ChunkCoords.ToString());

        
        UVoxelChunk* NewChunk = GetOrCreateChunkAtCoordinates(ChunkCoords.X, ChunkCoords.Y, ChunkCoords.Z);
        if (NewChunk)
        {
            NewChunk->InitializeChunk(ChunkCoords, this);
            ChunkMap.Add(ChunkCoords, NewChunk);
            return NewChunk;
        }
    }

    return NearestChunk;
}

UVoxelChunk* ADiggerManager::FindNearestChunk(const FVector& Position)
{
    FIntVector ChunkCoords = FVoxelConversion::WorldToChunk(Position);
    UVoxelChunk** ExistingChunk = ChunkMap.Find(ChunkCoords);
    if (ExistingChunk && *ExistingChunk)
    {
        return *ExistingChunk;
    }

    // Find nearest chunk if no exact match found
    UVoxelChunk* NearestChunk = nullptr;
    float MinDistance = FLT_MAX;
    for (auto& Entry : ChunkMap)
    {
        FVector WorldPos = FVoxelConversion::ChunkToWorld(Entry.Value->GetChunkPosition());
        float Distance = FVector::Dist(Position, WorldPos);
        if (Distance < MinDistance)
        {
            MinDistance = Distance;
            NearestChunk = Entry.Value;
        }
    }

    return NearestChunk;
}


void ADiggerManager::MarkNearbyChunksDirty(const FVector& CenterPosition, float Radius)
{
    int32 Reach = FMath::CeilToInt(Radius / (ChunkSize * TerrainGridSize));

    UE_LOG(LogTemp, Warning, TEXT("MarkNearbyChunksDirty: CenterPosition=%s, Radius=%f"), *CenterPosition.ToString(), Radius);
    UE_LOG(LogTemp, Warning, TEXT("FVoxelConversion: ChunkSize=%d, TerrainGridSize=%f, Origin=%s"), FVoxelConversion::ChunkSize, FVoxelConversion::TerrainGridSize, *FVoxelConversion::Origin.ToString());
    FIntVector CenterChunkCoords = FVoxelConversion::WorldToChunk(CenterPosition);
    UE_LOG(LogTemp, Warning, TEXT("WorldToChunk: CenterPosition=%s -> CenterChunkCoords=%s"), *CenterPosition.ToString(), *CenterChunkCoords.ToString());


    for (int32 X = -Reach; X <= Reach; ++X)
    {
        for (int32 Y = -Reach; Y <= Reach; ++Y)
        {
            for (int32 Z = -Reach; Z <= Reach; ++Z)
            {
                FIntVector NearbyChunkCoords = CenterChunkCoords + FIntVector(X, Y, Z);
                UVoxelChunk** NearbyChunk = ChunkMap.Find(NearbyChunkCoords);
                if (NearbyChunk && *NearbyChunk)
                {
                    (*NearbyChunk)->MarkDirty();
                    UE_LOG(LogTemp, Log, TEXT("Marked nearby chunk dirty at position: %s"), *NearbyChunkCoords.ToString());
                }
            }
        }
    }
}


// Main function: always works in chunk coordinates
UVoxelChunk* ADiggerManager::GetOrCreateChunkAtChunk(const FIntVector& ChunkCoords)
{
    //Run Init on the static FVoxelCoversion struct so all of our conversions work properly even if the settings have changed!
    FVoxelConversion::InitFromConfig(ChunkSize,Subdivisions,TerrainGridSize, GetActorLocation());

    UE_LOG(LogTemp, Warning, TEXT("➡️ Attempting chunk at coords: %s"), *ChunkCoords.ToString());
    
    if (UVoxelChunk** ExistingChunk = ChunkMap.Find(ChunkCoords))
    {
        UE_LOG(LogTemp, Log, TEXT("Found existing chunk at position: %s"), *ChunkCoords.ToString());
        return *ExistingChunk;
    }

    UVoxelChunk* NewChunk = NewObject<UVoxelChunk>(this);
    if (NewChunk)
    {
        NewChunk->InitializeChunk(ChunkCoords, this);
        ChunkMap.Add(ChunkCoords, NewChunk);
        UE_LOG(LogTemp, Log, TEXT("Created a new chunk at position: %s"), *ChunkCoords.ToString());
        return NewChunk;
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create a new chunk at position: %s"), *ChunkCoords.ToString());
        return nullptr;
    }
}

UVoxelChunk* ADiggerManager::GetOrCreateChunkAtCoordinates(const float& ProposedChunkX, const float& ProposedChunkY, const float& ProposedChunkZ)
{
    return GetOrCreateChunkAtWorld(FVector(ProposedChunkX, ProposedChunkY, ProposedChunkZ));
}

// Thin wrapper: converts world position to chunk coordinates
UVoxelChunk* ADiggerManager::GetOrCreateChunkAtWorld(const FVector& WorldPosition)
{
    FIntVector ChunkCoords = FVoxelConversion::WorldToChunk(WorldPosition);
    return GetOrCreateChunkAtChunk(ChunkCoords);
}


// In ADiggerManager.cpp
TArray<FIslandData> ADiggerManager::GetAllIslands() const
{
    // Create a local array to collect islands before broadcasting
    TArray<FIslandData> AllIslands;
    TMap<FIntVector, FIslandData> UniqueIslands;
    
    // First collect all islands without broadcasting
    for (const auto& ChunkPair : ChunkMap)
    {
        UVoxelChunk* Chunk = ChunkPair.Value;
        if (!Chunk || !Chunk->IsValidLowLevel())
            continue;
            
        USparseVoxelGrid* Grid = Chunk->GetSparseVoxelGrid();
        if (!Grid || !Grid->IsValidLowLevel())
            continue;
            
        TArray<FIslandData> ChunkIslands = Grid->DetectIslands(0.0f);
        for (const FIslandData& Island : ChunkIslands)
        {
            FIntVector Center = FIntVector(
                FMath::RoundToInt(Island.Location.X),
                FMath::RoundToInt(Island.Location.Y),
                FMath::RoundToInt(Island.Location.Z)
            );
            if (!UniqueIslands.Contains(Center))
            {
                UniqueIslands.Add(Center, Island);
            }
        }
    }
    
    // Generate the final array
    UniqueIslands.GenerateValueArray(AllIslands);
    
    // Now it's safe to broadcast
#if WITH_EDITOR
    // First notify listeners to clear existing islands
    OnIslandsDetectionStarted.Broadcast();
    
    // Then broadcast each island
    for (const FIslandData& Island : AllIslands)
    {
        OnIslandDetected.Broadcast(Island);
    }
#endif

    return AllIslands;
}



#if WITH_EDITOR
void ADiggerManager::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    // Sync updated settings to FVoxelConversion using the actor's position as origin
    FVoxelConversion::InitFromConfig(ChunkSize,Subdivisions, TerrainGridSize, GetActorLocation());

    // Respond to property changes (e.g., ChunkSize, TerrainGridSize, etc.)
    EditorUpdateChunks();
}

void ADiggerManager::PostEditMove(bool bFinished)
{
    Super::PostEditMove(bFinished);

    // Update FVoxelConversion origin if the actor is moved in the editor
    FVoxelConversion::InitFromConfig(ChunkSize,Subdivisions, TerrainGridSize, GetActorLocation());

    if (bFinished)
    {
        EditorUpdateChunks();
    }
}

void ADiggerManager::PostEditUndo()
{
    Super::PostEditUndo();

    // Update FVoxelConversion in case undo/redo changed position or settings
    FVoxelConversion::InitFromConfig(ChunkSize, Subdivisions, TerrainGridSize, GetActorLocation());

    EditorUpdateChunks();
}

void ADiggerManager::EditorUpdateChunks()
{
    // Call your chunk update logic here
    ProcessDirtyChunks();
}

void ADiggerManager::EditorRebuildAllChunks()
{
    // Optionally clear and rebuild all chunks
    InitializeChunks();
}


#endif // WITH_EDITOR
