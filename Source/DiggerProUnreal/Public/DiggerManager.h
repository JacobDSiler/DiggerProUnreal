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
#include "FCustomSDFBrush.h"
#include "IAssetTools.h"
#include "VoxelConversion.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"


#include "DiggerManager.generated.h"


class AIslandActor;

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

    // The flag for using the advanced cube brush
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Brush")
    bool bUseAdvancedCubeBrush;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Brush")
    float AdvancedCubeHalfExtentX;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Brush")
    float AdvancedCubeHalfExtentY;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Brush")
    float AdvancedCubeHalfExtentZ;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Brush")
    float TorusInnerRadius;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Brush")
    bool bSpiral=false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Brush")
    int32 NumSteps=10;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Brush")
    float StepDepth;

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

struct FIslandMeshData
{
    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    // Add UVs, Colors, Tangents if needed
    FVector MeshOrigin;
    bool bValid = false;
};


UCLASS(Blueprintable)
class DIGGERPROUNREAL_API ADiggerManager : public AActor
{
    GENERATED_BODY()

public:
    ADiggerManager();
    void ApplyBrushToAllChunks(FBrushStroke& BrushStroke);
    bool SaveSDFBrushToFile(const FCustomSDFBrush& Brush, const FString& FilePath);
    bool LoadSDFBrushFromFile(const FString& FilePath, FCustomSDFBrush& OutBrush);
    bool GenerateSDFBrushFromStaticMesh(UStaticMesh* Mesh, FTransform MeshTransform, float VoxelSize,
                                        FCustomSDFBrush& OutBrush);
    void DebugBrushPlacement(const FVector& ClickPosition);
    UStaticMesh* ConvertIslandToStaticMesh(const FIslandData& Island, bool bWorldOrigin, FString AssetName);
    AIslandActor* SpawnIslandActorFromIslandAtPosition(const FVector& IslandCenter, bool bEnablePhysics);

    FIslandMeshData ExtractAndGenerateIslandMesh(const FVector& IslandCenter);
    void ConvertIslandAtPositionToPhysicsObject(const FVector& Vector);
    void ConvertIslandAtPositionToStaticMesh(const FVector& Vector);

    template<typename TIn, typename TOut>
    TArray<TOut> ConvertArray(const TArray<TIn>& InArray)
        {
            TArray<TOut> OutArray;
            OutArray.Reserve(InArray.Num());
            for (const TIn& Item : InArray)
            {
                OutArray.Add(TOut(Item));
            }
            return OutArray;
        }


#if WITH_EDITOR
    
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
    void ApplyBrushInEditor(bool bDig);

    AIslandActor* SpawnIslandActorWithMeshData(
        const FVector& SpawnLocation,
        const FIslandMeshData& MeshData,
        bool bEnablePhysics
    );
    void SaveIslandMeshAsStaticMesh(
        const FString& AssetName,
        const FIslandMeshData& MeshData
    );

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
    void OnConstruction(const FTransform& Transform);
    virtual void PostEditMove(bool bFinished) override;
    virtual void PostEditUndo() override;

    // Expose manual update/rebuild in editor
    UFUNCTION(CallInEditor, Category = "Editor Tools")
    void EditorUpdateChunks();

    UFUNCTION(CallInEditor, Category = "Editor Tools")
    void EditorRebuildAllChunks();
    TArray<FIslandData> GetAllIslands() const;
    //void UpdateIslandsFromChunk(UVoxelChunk* Chunk);


#endif

public:
    
   /* FVector ChunkToWorldCoordinates(int32 XInd, int32 YInd, int32 ZInd) const
    {
        FIntVector ChunkPosition=FIntVector(XInd, YInd, ZInd);
        return FVoxelConversion::ChunkToWorld(ChunkPosition);
    }*/

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Settings")
    int32 ChunkSize = 32;  // Number of grif squares per chunk

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
    UVoxelChunk* GetOrCreateChunkAtWorld(const FVector& Vector);
    
    UFUNCTION(BlueprintCallable, Category = "Debug")
    void DebugLogVoxelChunkGrid() const;


    UVoxelChunk* GetOrCreateChunkAtCoordinates(const float& ProposedChunkX, const float& ProposedChunkY,
                                    const float& ProposedChunkZ);
    UVoxelChunk* GetOrCreateChunkAtChunk(const FIntVector& ChunkCoords);
    

    // Debug voxel system
    UFUNCTION(BlueprintCallable, Category = "Voxel Debug")
    void DebugVoxels();

    // Procedural mesh component for visual representation
    UPROPERTY(VisibleAnywhere)
    UProceduralMeshComponent* ProceduralMesh;

    void InitializeChunks();  // Initialize all chunks
    void InitializeSingleChunk(UVoxelChunk* Chunk);  // Initialize a single chunk
    void UpdateLandscapeProxies();

    //The world map of the voxel chunks
    UPROPERTY()
    TMap<FIntVector, UVoxelChunk*> ChunkMap;

    // Cache map from proxy pointer to boolean flag (just for example—can be more complex if needed later)
    UPROPERTY()
    TMap<ALandscapeProxy*, bool> CachedLandscapeProxies;

    UPROPERTY()
    ALandscapeProxy* LastUsedLandscape = nullptr;


/*
    // Key is world-space X,Y location snapped to voxel grid
    UPROPERTY()
    TMap<FIntPoint, float> TerrainHeightCache;

    // Top-level cache by Landscape Proxy
    UPROPERTY()
    TMap<ALandscapeProxy*, TSharedPtr<TMap<FIntPoint, float>>> LandscapeHeightCaches;

    //LoadingCache flag to determine if the height cache on a specific proxy exists. This prevents duplicate async jobs from starting for the same proxy.
    UPROPERTY()
    TSet<ALandscapeProxy*> HeightCacheLoadingSet;

*/

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


    UStaticMesh* CreateStaticMeshFromRawData(
        UObject* Outer,
        const FString& AssetName,
        const TArray<FVector3f>& Vertices,
        const TArray<int32>& Triangles,
        const TArray<FVector3f>& Normals,
        const TArray<FVector2d>& UVs,
        const TArray<FColor>& Colors,
        const TArray<FProcMeshTangent>& Tangents
    );

    /*float GetCachedLandscapeHeightAt(const FVector& WorldPos);
    void PopulateLandscapeHeightCache(ALandscapeProxy* Landscape);*/

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
    void PopulateLandscapeHeightCacheAsync(ALandscapeProxy* Landscape);
    ALandscapeProxy* GetLandscapeProxyAt(const FVector& WorldPos);
    TOptional<float> SampleLandscapeHeight(ALandscapeProxy* Landscape, const FVector& WorldPos);
    float GetSmartLandscapeHeightAt(const FVector& WorldPos);
    float GetSmartLandscapeHeightAt(const FVector& WorldPos, bool bForcePrecise);
    bool IsNearLandscapeEdge(const FVector& WorldPos, float Threshold);
    bool GetHeightAtLocation(ALandscapeProxy* LandscapeProxy, const FVector& Location, float& OutHeight);
    FVector GetLandscapeNormalAt(const FVector& WorldPosition);

private:

    std::queue<FBrushStroke> BrushStrokeQueue;
    const int32 MaxUndoLength = 10; // Example limit

    FTimerHandle ChunkProcessTimerHandle;
    
};
