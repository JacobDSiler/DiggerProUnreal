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
// Define the FBrushStroke struct
//#include "C:\Users\serpe\Documents\Unreal Projects\DiggerProUnreal\Source\DiggerProUnreal\Public\FBrushStroke.h"
#include "FBrushStroke.h"

#include "AssetToolsModule.h"
#include "FCustomSDFBrush.h"
#include "IAssetTools.h"
#include "StaticMeshAttributes.h"
#include "VoxelConversion.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"


#include "DiggerManager.generated.h"


class AIslandActor;

// Helper struct for an island
struct FIsland
{
    TArray<FIntVector> Voxels;
};

// Structure to save island data for PIE
USTRUCT()
struct FIslandSaveData
{
    GENERATED_BODY()

    UPROPERTY()
    FVector MeshOrigin = FVector::ZeroVector;

    UPROPERTY()
    TArray<FVector> Vertices;

    UPROPERTY()
    TArray<int32> Triangles;

    UPROPERTY()
    TArray<FVector> Normals;

    UPROPERTY()
    bool bEnablePhysics = false;

    // Optional but recommended constructor
    FIslandSaveData()
        : MeshOrigin(FVector::ZeroVector)
        , Vertices()
        , Triangles()
        , Normals()
        , bEnablePhysics(false)
    {}
};


#if WITH_EDITOR
class FDiggerEdModeToolkit;
#endif


USTRUCT(BlueprintType)
struct FIslandData
{
    GENERATED_BODY()

    FIslandData()
        : Location(FVector::ZeroVector)
        , IslandID(-1)
        , VoxelCount(0)
        , Voxels()
        , ReferenceVoxel(FIntVector::ZeroValue)
    {}

    UPROPERTY(BlueprintReadWrite)
    FVector Location;

    UPROPERTY(BlueprintReadWrite)
    int32 IslandID;

    UPROPERTY(BlueprintReadWrite)
    int32 VoxelCount;

    UPROPERTY(BlueprintReadWrite)
    TArray<FIntVector> Voxels;
    
    // Store a reference voxel for this island
    UPROPERTY()
    FIntVector ReferenceVoxel;
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
    void InitializeBrushShapes();
    UVoxelBrushShape* GetActiveBrushShape(EVoxelBrushType BrushType) const;
 

    UFUNCTION(BlueprintCallable, Category="Brush")
    void ApplyBrushToAllChunksPIE(FBrushStroke& BrushStroke);
    void ApplyBrushToAllChunks(FBrushStroke& BrushStroke);
    bool SaveSDFBrushToFile(const FCustomSDFBrush& Brush, const FString& FilePath);
    bool LoadSDFBrushFromFile(const FString& FilePath, FCustomSDFBrush& OutBrush);
    bool GenerateSDFBrushFromStaticMesh(UStaticMesh* Mesh, FTransform MeshTransform, float VoxelSize,
                                        FCustomSDFBrush& OutBrush);
    void DebugBrushPlacement(const FVector& ClickPosition);
    void DebugDrawVoxelAtWorldPosition(const FVector& WorldPosition, FColor BoxColor, float Duration, float Thickness);
    void DrawDebugChunkWithOverflow(FIntVector ChunkCoords);
    void DrawVoxelDebugBounds(const FVector& ChunkOrigin,
                              float DebugDuration);
    void DrawDiagonalDebugVoxels(FIntVector ChunkCoords);
    UStaticMesh* ConvertIslandToStaticMesh(const FIslandData& Island, bool bWorldOrigin, FString AssetName);
    AIslandActor* SpawnIslandActorFromIslandAtPosition(const FVector& IslandCenter, bool bEnablePhysics);

    FIntVector FindNearestSurfaceVoxel(USparseVoxelGrid* VoxelGrid, FIntVector IntVector, int SurfaceSearchRadius);
    FIslandMeshData ExtractAndGenerateIslandMesh(const FVector& IslandCenter);
    void SaveIslandData(AIslandActor* IslandActor, const FIslandMeshData& MeshData);
    void ConvertIslandAtPositionToPhysicsObject(const FVector& Vector);
    void ConvertIslandAtPositionToStaticMesh(const FVector& Vector);
    void ConvertIslandAtPositionToActor(const FVector& IslandCenter, bool bEnablePhysics, FIntVector ReferenceVoxel);
    FIslandMeshData ExtractAndGenerateIslandMeshFromVoxel(UVoxelChunk* Chunk, const FIntVector& StartVoxel);
    void ClearAllIslandActors();
    void DestroyIslandActor(AIslandActor* IslandActor);

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

    UFUNCTION(BlueprintCallable)
    void CreateHoleAt(FVector WorldPosition, FRotator Rotation, FVector Scale, TSubclassOf<AActor> HoleBPClass);

    UFUNCTION(BlueprintCallable)
    bool RemoveHoleNear(FVector WorldPosition, float MaxDistance = 100.0f);


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

    UPROPERTY()
    TArray<AIslandActor*> IslandActors;

    UPROPERTY(EditAnywhere, Category = "Digger|Islands")
    UMaterialInterface* IslandMaterial;

    UPROPERTY(SaveGame)
    TArray<FIslandSaveData> SavedIslands;

    
    bool EnsureWorldReference();

    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Export")
    void BakeToStaticMesh(bool bEnableCollision, bool bEnableNanite, float DetailReduction);

    UPROPERTY(EditAnywhere, Category="Digger Brush")
    UVoxelBrushShape* BrushShape;
        
    // Reference to the terrain hole Blueprint
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Holes")
    TSubclassOf<AActor> HoleBP;
    

    UFUNCTION(BlueprintCallable, Category = "Custom")
    void DuplicateLandscape(ALandscapeProxy* Landscape);

    UPROPERTY()
    TArray<UProceduralMeshComponent*> IslandMeshes;

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

    UFUNCTION(BlueprintCallable, Category = "Voxel Editing")
    void SetVoxelAtWorldPosition(const FVector& WorldPos, float Value);


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
    void RecreateIslandFromSaveData(const FIslandSaveData& SavedIsland);
    virtual void BeginPlay() override;
    void PostInitProperties();
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

    // In ADiggerManager.h
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel System")
    TMap<EVoxelBrushType, UVoxelBrushShape*> BrushShapeMap;
    
    //The world map of the voxel chunks
    UPROPERTY()
    TMap<FIntVector, UVoxelChunk*> ChunkMap;

    // Cache map from proxy pointer to boolean flag (just for example—can be more complex if needed later)
    UPROPERTY()
    TMap<ALandscapeProxy*, bool> CachedLandscapeProxies;

    UPROPERTY()
    ALandscapeProxy* LastUsedLandscape = nullptr;



    // Key is world-space X,Y location snapped to voxel grid
    UPROPERTY()
    TMap<FIntPoint, float> TerrainHeightCache;

    // Top-level cache by Landscape Proxy
    //UPROPERTY()
    TMap<ALandscapeProxy*, TSharedPtr<TMap<FIntPoint, float>>> LandscapeHeightCaches;

    //LoadingCache flag to determine if the height cache on a specific proxy exists. This prevents duplicate async jobs from starting for the same proxy.
    UPROPERTY()
    TSet<ALandscapeProxy*> HeightCacheLoadingSet;



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

public:
    // Single chunk serialization methods
    UFUNCTION(BlueprintCallable, Category = "Voxel Serialization")
    bool SaveChunk(const FIntVector& ChunkCoords);
    
    UFUNCTION(BlueprintCallable, Category = "Voxel Serialization")
    bool LoadChunk(const FIntVector& ChunkCoords);
    
    // Batch serialization methods
    UFUNCTION(BlueprintCallable, Category = "Voxel Serialization")
    bool SaveAllChunks();
    
    UFUNCTION(BlueprintCallable, Category = "Voxel Serialization")
    bool LoadAllChunks();
    
    // Helper methods
    UFUNCTION(BlueprintCallable, Category = "Voxel Serialization")
    FString GetChunkFilePath(const FIntVector& ChunkCoords) const;
    
    UFUNCTION(BlueprintCallable, Category = "Voxel Serialization")
    bool DoesChunkFileExist(const FIntVector& ChunkCoords) const;
    
    
    UFUNCTION(BlueprintCallable, Category = "Voxel Serialization")
    bool DeleteChunkFile(const FIntVector& ChunkCoords);
    
    UFUNCTION(BlueprintCallable, Category = "Voxel Serialization")
    void EnsureVoxelDataDirectoryExists() const;
    void Tick(float DeltaTime);

private:
    // Cache for saved chunk coordinates to avoid constant filesystem scanning
    UPROPERTY()
    TArray<FIntVector> CachedSavedChunkCoordinates;
    
    // Flag to track if cache is valid
    bool bSavedChunkCacheValid = false;
    
    // Timer to refresh cache periodically instead of every frame
    float SavedChunkCacheRefreshTimer = 0.0f;
    static constexpr float CACHE_REFRESH_INTERVAL = 2.0f; // Refresh every 2 seconds

public:
    // Modified method signatures
    UFUNCTION(BlueprintCallable, Category = "Voxel Serialization")
    TArray<FIntVector> GetAllSavedChunkCoordinates(bool bForceRefresh = false) const;
    
    UFUNCTION(BlueprintCallable, Category = "Voxel Serialization")
    void RefreshSavedChunkCache();
    
    UFUNCTION(BlueprintCallable, Category = "Voxel Serialization")
    void InvalidateSavedChunkCache();

private:
    // Constants for file management
    static const FString VOXEL_DATA_DIRECTORY;
    static const FString CHUNK_FILE_EXTENSION;


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


public:
    float GetCachedLandscapeHeightAt(const FVector& WorldPos);
    void PopulateLandscapeHeightCache(ALandscapeProxy* Landscape);
    
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
    TSharedPtr<TMap<FIntPoint, float>> GetOrCreateLandscapeHeightCache(ALandscapeProxy* Landscape);
    void PopulateLandscapeHeightCacheAsync(ALandscapeProxy* Landscape);
    ALandscapeProxy* GetLandscapeProxyAt(const FVector& WorldPos);
    TOptional<float> SampleLandscapeHeight(ALandscapeProxy* Landscape, const FVector& WorldPos);
    float GetSmartLandscapeHeightAt(const FVector& WorldPos);
    float GetSmartLandscapeHeightAt(const FVector& WorldPos, bool bForcePrecise);
    //bool IsNearLandscapeEdge(const FVector& WorldPos, float Threshold);
    bool GetHeightAtLocation(ALandscapeProxy* LandscapeProxy, const FVector& Location, float& OutHeight);
    FVector GetLandscapeNormalAt(const FVector& WorldPosition);
    UWorld* GetSafeWorld() const;

    UFUNCTION(BlueprintCallable, Category="Holes")
    void HandleHoleSpawn(const FBrushStroke& Stroke);

private:

    std::queue<FBrushStroke> BrushStrokeQueue;
    const int32 MaxUndoLength = 10; // Example limit

    FTimerHandle ChunkProcessTimerHandle;
    
};
