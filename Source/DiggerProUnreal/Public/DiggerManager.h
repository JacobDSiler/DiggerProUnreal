#pragma once

#include <mutex>
#include <queue>

#include "CoreMinimal.h"
#if WITH_EDITOR
#include "IMeshMergeUtilities.h"
#endif
#include "ProceduralMeshComponent.h"
#include "VoxelBrushShape.h"
#include "VoxelBrushTypes.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/Actor.h"

#include "Engine/StaticMesh.h"
#include "StaticMeshAttributes.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

#include "DiggerManager.generated.h"


// Helper struct for an island
struct FIsland
{
    TArray<FIntVector> Voxels;
};

class UTerrainHoleComponent;
#if WITH_EDITOR
class FDiggerEdModeToolkit;
#endif

// Define the FBrushStroke struct
USTRUCT(BlueprintType)
struct FBrushStroke
{
    GENERATED_BODY()

    // Location of the stroke in world space
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Brush")
    FVector BrushPosition = FVector::ZeroVector;

    // Rotation of the brush stroke
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Brush")
    FRotator BrushRotation = FRotator::ZeroRotator;

    // Size of the brush stroke
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Brush")
    float BrushRadius = 100.f;

    // Type of the brush (e.g., Sphere, Cube, Custom)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Brush")
    EVoxelBrushType BrushType = EVoxelBrushType::Sphere;

    // Whether this stroke digs (true) or adds (false)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Brush")
    bool bDig = true;

    // Length of the brush stroke in world space
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Brush")
    float BrushLength;
    
    // Angle of the brush stroke in degrees
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Brush")
    float BrushAngle;

    // Offset for the brush stroke as a vector
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Brush")
    FVector BrushOffset;

    // Optional: allow pressure sensitivity in the future?
    // float Intensity = 1.0f;

    FBrushStroke() {}

    FBrushStroke(FVector InPosition, FRotator InRotation, float InRadius, EVoxelBrushType InType, bool bInDig)
        : BrushPosition(InPosition)
          , BrushRotation(InRotation)
          , BrushRadius(InRadius)
          , BrushType(InType)
          , bDig(bInDig)
          , BrushLength(0), BrushAngle(0)
    {
    }
};

USTRUCT(BlueprintType)
struct FIslandData
{
    GENERATED_BODY()

    FIslandData()
        : Location(FVector::ZeroVector)
        , IslandID(-1)
        , VoxelCount(0)
        , Voxels()
    {}

    UPROPERTY(BlueprintReadWrite)
    FVector Location;

    UPROPERTY(BlueprintReadWrite)
    int32 IslandID;

    UPROPERTY(BlueprintReadWrite)
    int32 VoxelCount;

    UPROPERTY(BlueprintReadWrite)
    TArray<FIntVector> Voxels;
};





UCLASS(Blueprintable)
class DIGGERPROUNREAL_API ADiggerManager : public AActor
{
    GENERATED_BODY()

public:
    ADiggerManager();
    UStaticMesh* ConvertIslandToStaticMesh(const FIslandData& Island, bool bWorldOrigin, FString AssetName);

#if WITH_EDITOR
    // Add this member
   /* FDiggerEdModeToolkit* EditorToolkit = nullptr;

    // Add a setter
    void SetEditorToolkit(FDiggerEdModeToolkit* InToolkit)
    {
        EditorToolkit = InToolkit;
    }*/
    
    // Native C++ delegates (not UObject/Blueprint)
    DECLARE_MULTICAST_DELEGATE(FIslandsDetectionStartedEvent);
    FIslandsDetectionStartedEvent OnIslandsDetectionStarted;

    DECLARE_MULTICAST_DELEGATE_OneParam(FIslandDetectedEvent, const FIslandData&);
    FIslandDetectedEvent OnIslandDetected;




    // Method to broadcast the event
    UFUNCTION(BlueprintCallable, Category = "Island Detection")
    void BroadcastIslandDetected(const FIslandData& Island)
    {
        if (OnIslandDetected.IsBound())
        {
            UE_LOG(LogTemp, Warning, TEXT("Broadcasting OnIslandDetected: %d"), Island.IslandID);
            OnIslandDetected.Broadcast(Island);
        }
    }
#endif
    
    // Set this true to make the actor never get culled despite distance
    UPROPERTY(EditAnywhere, Category="Digger System")
    bool bNeverCull = true;

    
    bool EnsureWorldReference();

    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Export")
    void BakeToStaticMesh(bool bEnableCollision, bool bEnableNanite, float DetailReduction);

        
    // Reference to the terrain hole Blueprint
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Holes")
    TSubclassOf<AActor> HoleBP;

    UFUNCTION(BlueprintCallable, Category="Holes")
    void SpawnTerrainHole(const FVector& Location);

    UFUNCTION(BlueprintCallable, Category = "Custom")
    void DuplicateLandscape(ALandscapeProxy* Landscape);

    UPROPERTY(EditAnywhere, Category="Digger Brush|Settings")
    FVector EditorBrushPosition;

    UPROPERTY(EditAnywhere, Category="Brush")
    EVoxelBrushType EditorBrushType = EVoxelBrushType::Sphere;

    UPROPERTY(EditAnywhere, Category="Digger Brush|Settings")
    float EditorBrushRadius = 100.0f;

    UPROPERTY(EditAnywhere, Category="Digger Brush|Settings")
    bool EditorBrushDig = true;
    
    UPROPERTY(EditAnywhere, Category="Digger Brush|Settings")
    FRotator EditorBrushRotation;

    UPROPERTY(EditAnywhere, Category="Digger Brush|Settings")
    float EditorBrushLength;

    UPROPERTY(EditAnywhere, Category="Digger Brush|Settings")
    float EditorBrushAngle;

    UPROPERTY(EditAnywhere, Category="Digger Brush|Settings")
    FVector EditorBrushOffset;

    UFUNCTION(CallInEditor, Category="Digger Brush|Actions")
    void ApplyBrushInEditor();

    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Islands")
    AStaticMeshActor* SpawnPhysicsIsland(UStaticMesh* StaticMesh, FVector Location = FVector::ZeroVector)
    {
        if (!StaticMesh || !GetWorld())
        {
            UE_LOG(LogTemp, Error, TEXT("Invalid mesh or world!"));
            return nullptr;
        }

        FActorSpawnParameters SpawnParams;
        AStaticMeshActor* NewActor = GetWorld()->SpawnActor<AStaticMeshActor>(Location, FRotator::ZeroRotator, SpawnParams);
        if (NewActor)
        {
            NewActor->GetStaticMeshComponent()->SetStaticMesh(StaticMesh);
            NewActor->GetStaticMeshComponent()->SetSimulatePhysics(true);
            NewActor->SetActorLabel(TEXT("PhysicsIsland"));
            UE_LOG(LogTemp, Warning, TEXT("Spawned physics island at %s"), *Location.ToString());
        }
        return NewActor;
    }

    
protected:
    virtual void BeginPlay() override;
    void UpdateVoxelSize();
    void ProcessDirtyChunks();

#if WITH_EDITOR
public:
    // Editor support
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
    virtual void PostEditMove(bool bFinished) override;
    virtual void PostEditUndo() override;

    // Expose manual update/rebuild in editor
    UFUNCTION(CallInEditor, Category = "Editor Tools")
    void EditorUpdateChunks();

    UFUNCTION(CallInEditor, Category = "Editor Tools")
    void EditorRebuildAllChunks();
    TArray<FIslandData> GetAllIslands() const;
    //void UpdateIslandsFromChunk(UVoxelChunk* Chunk);

    FVector WorldToChunkCoordinates(FVector Position) const
    {
        return FVector(WorldToChunkCoordinates(Position.X, Position.Y, Position.Z));
    }

#endif

public:

    FVector ChunkToWorldCoordinates(int32 XInd, int32 YInd, int32 ZInd) const
    {
        return FVector(XInd * ChunkSize * TerrainGridSize, YInd * ChunkSize * TerrainGridSize, ZInd * ChunkSize * TerrainGridSize);
    }

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Settings")
    int32 ChunkSize = 32;  // Number of subdivisions per grid size

    // Chunk and grid settings
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Settings")
    int TerrainGridSize = 100;  // Default size of 1 meter

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Settings")
    int Subdivisions = 4;  // Number of subdivisions per grid size

    int VoxelSize=TerrainGridSize/Subdivisions;

    // The active brush shape
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel System")
    UVoxelBrushShape* ActiveBrush;

    UFUNCTION(BlueprintCallable, Category = "Custom")
    int32 GetHitSectionIndex(const FHitResult& HitResult);

    UFUNCTION(BlueprintCallable, Category = "Custom")
    UVoxelChunk* GetChunkBySectionIndex(int32 SectionIndex);

    UFUNCTION(BlueprintCallable, Category = "Custom")
    void UpdateChunkFromSectionIndex(const FHitResult& HitResult);
    void DebugDrawChunkSectionIDs();

    // Apply the active brush to the terrain
    UFUNCTION(BlueprintCallable, Category = "Brush System")
    void ApplyBrush();
    UVoxelChunk* FindOrCreateNearestChunk(const FVector& Position);
    UVoxelChunk* FindNearestChunk(const FVector& Position);
    void MarkNearbyChunksDirty(const FVector& CenterPosition, float Radius);

    // Get the relevant voxel chunk based on position
    UFUNCTION(BlueprintCallable, Category = "Chunk Management")
    UVoxelChunk* GetOrCreateChunkAt(const FIntVector& ProposedChunkPosition);

    UFUNCTION(BlueprintCallable, Category = "Debug")
    void DebugLogVoxelChunkGrid() const;

    //Helper functions
    //Calculate a chunk position according to the chunk grid.
    FIntVector CalculateChunkPosition(const FIntVector3& WorldPosition) const;
    FIntVector CalculateChunkPosition(const FVector3d& WorldPosition) const;
    UVoxelChunk* GetOrCreateChunkAt(const FVector3d& ProposedChunkPosition);
    UVoxelChunk* GetOrCreateChunkAt(const float& ProposedChunkX, const float& ProposedChunkY,
                                    const float& ProposedChunkZ);

    // Debug voxel system
    UFUNCTION(BlueprintCallable, Category = "Voxel Debug")
    void DebugVoxels();

    // Procedural mesh component for visual representation
    UPROPERTY(VisibleAnywhere)
    UProceduralMeshComponent* ProceduralMesh;

    void InitializeChunks();  // Initialize all chunks
    void InitializeSingleChunk(UVoxelChunk* Chunk);  // Initialize a single chunk

    //The world map of the voxel chunks
    UPROPERTY()
    TMap<FIntVector, UVoxelChunk*> ChunkMap;

private:
    std::mutex ChunkProcessingMutex;
    // Reference to the sparse voxel grid and marching cubes
    UPROPERTY()
    USparseVoxelGrid* SparseVoxelGrid;

    UPROPERTY()
    UMarchingCubes* MarchingCubes;

    UPROPERTY()
    UVoxelChunk* ZeroChunk;

    UPROPERTY()
    UVoxelChunk* OneChunk;

    UPROPERTY()
    UMaterial* TerrainMaterial;

    UPROPERTY()
    TArray<UProceduralMeshComponent*> ProceduralMeshComponents;


/*
UStaticMesh* CreateStaticMeshFromRawData(
    UObject* Outer,
    const FString& AssetName,
    const TArray<FVector>& Vertices,
    const TArray<int32>& Triangles,
    const TArray<FVector>& Normals,
    const TArray<FVector2D>& UVs,
    const TArray<FColor>& Colors,
    const TArray<FProcMeshTangent>& Tangents
)
{
    // 1. Create a new package for the asset
    FString PackageName = FString::Printf(TEXT("/Game/Islands/%s"), *AssetName);
    UPackage* MeshPackage = CreatePackage(*PackageName);

    // 2. Create the static mesh object
    UStaticMesh* StaticMesh = NewObject<UStaticMesh>(MeshPackage, *AssetName, RF_Public | RF_Standalone);
    if (!StaticMesh)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create UStaticMesh object!"));
        return nullptr;
    }

    // 3. Create a MeshDescription
    FMeshDescription MeshDescription;
    FStaticMeshAttributes Attributes(MeshDescription);
    Attributes.Register();

    // 4. Create a polygon group (single material slot)
    FPolygonGroupID PolyGroupID = MeshDescription.CreatePolygonGroup();

    // 5. Build the mesh
    TMap<int32, FVertexID> IndexToVertexID;
    for (int32 i = 0; i < Vertices.Num(); ++i)
    {
        FVertexID VertexID = MeshDescription.CreateVertex();
        Attributes.GetVertexPositions()[VertexID] = Vertices[i];
        IndexToVertexID.Add(i, VertexID);
    }

    // 6. Create triangles and set attributes
    for (int32 i = 0; i < Triangles.Num(); i += 3)
    {
        TArray<FVertexInstanceID> VertexInstanceIDs;
        for (int32 j = 0; j < 3; ++j)
        {
            FVertexInstanceID InstanceID = MeshDescription.CreateVertexInstance(IndexToVertexID[Triangles[i + j]]);
            // Set normals
            if (Normals.IsValidIndex(Triangles[i + j]))
                Attributes.GetVertexInstanceNormals()[InstanceID] = Normals[Triangles[i + j]];
            // Set UVs (only first channel for simplicity)
            if (UVs.IsValidIndex(Triangles[i + j]))
                Attributes.GetVertexInstanceUVs()[InstanceID][0] = UVs[Triangles[i + j]];
            // Set colors
            if (Colors.IsValidIndex(Triangles[i + j]))
                Attributes.GetVertexInstanceColors()[InstanceID] = FVector4f(Colors[Triangles[i + j]]);
            // Set tangents
            if (Tangents.IsValidIndex(Triangles[i + j]))
            {
                Attributes.GetVertexInstanceTangents()[InstanceID] = (FVector3f)Tangents[Triangles[i + j]].TangentX;
                Attributes.GetVertexInstanceBinormalSigns()[InstanceID] = Tangents[Triangles[i + j]].bFlipTangentY ? -1.0f : 1.0f;
            }
            VertexInstanceIDs.Add(InstanceID);
        }
        // Create the polygon (triangle)
        MeshDescription.CreatePolygon(PolyGroupID, VertexInstanceIDs);
    }

    // 7. Set the mesh description
    StaticMesh->SetMeshDescription(0, MeshDescription);
    StaticMesh->CommitMeshDescription(0);

    // 8. Build the static mesh
    StaticMesh->Build(false);

    // 9. Register and save the asset
    FAssetRegistryModule::AssetCreated(StaticMesh);
    StaticMesh->MarkPackageDirty();

    UE_LOG(LogTemp, Warning, TEXT("Static mesh asset created: %s"), *PackageName);

    return StaticMesh;
}
*/


public:
    [[nodiscard]] UMaterial* GetTerrainMaterial() const
    {
        return TerrainMaterial;
    }

    void SetTerrainMaterial(UMaterial* SetTerrainMaterial)
    {
        this->TerrainMaterial = SetTerrainMaterial;
    }

    UPROPERTY()
    UWorld* World;

    [[nodiscard]] UWorld* GetWorldFromManager()
    {
        if(EnsureWorldReference())
            return World;
        else
            return nullptr;
    }
    UFUNCTION(BlueprintCallable, Category = "Landscape Tools")
    float GetLandscapeHeightAt(FVector WorldPosition);
    bool GetHeightAtLocation(ALandscapeProxy* LandscapeProxy, const FVector& Location, float& OutHeight);

private:

    std::queue<FBrushStroke> BrushStrokeQueue;
    const int32 MaxUndoLength = 10; // Example limit

    FTimerHandle ChunkProcessTimerHandle;

    //Space Conversion Helper Methods
    FIntVector WorldToChunkCoordinates(float x, float y, float z) const
    {
        const float ChunkWorldSize = ChunkSize * TerrainGridSize;
        return FIntVector(
            FMath::FloorToInt(x / ChunkWorldSize),
            FMath::FloorToInt(y / ChunkWorldSize),
            FMath::FloorToInt(z / ChunkWorldSize)
        );
    }
};
