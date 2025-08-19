#pragma once

#include <mutex>
#include <queue>

#include "CoreMinimal.h"
#if WITH_EDITOR
#include "IMeshMergeUtilities.h"
/** Try to fill light brush fields (LightType/LightColor) from editor UI/toolkit. */
//  bool FillLightBrushFromEditor(struct FBrushStroke& Stroke) const;
#endif
#include "ProceduralMeshComponent.h"
#include "VoxelBrushShape.h"
#include "VoxelBrushTypes.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/Actor.h"
#include "FBrushStroke.h"
#include "HoleShapeLibrary.h"

#include "AssetToolsModule.h"
#include "FCustomSDFBrush.h"
#include "IAssetTools.h"
#include "MarchingCubes.h"
#include "SparseVoxelGrid.h"
#include "StaticMeshAttributes.h"
#include "VoxelConversion.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "Voxel/VoxelEvents.h"
#include "VoxelBrushTypes.h"



#include "DiggerManager.generated.h"

class UVoxelBrushShape;
class IslandData;
class AIslandActor;


// Optional aggregate-for-brush event type
DECLARE_MULTICAST_DELEGATE_OneParam(FOnBrushFinished, const FVoxelModificationReport&);


USTRUCT(BlueprintType)
struct FDebugBrushSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Debug Brush Settings")
    bool bDrawChunkBounds = true;

    UPROPERTY(EditAnywhere, Category = "Debug Brush Settings")
    bool bDrawEdgeVoxels = false;

    UPROPERTY(EditAnywhere, Category = "Debug Brush Settings")
    bool bDrawSelectedVoxel = true;

    UPROPERTY(EditAnywhere, Category = "Debug Brush Settings")
    bool bDrawClickPoint = true;

    UPROPERTY(EditAnywhere, Category = "Debug Brush Settings")
    bool bLogPlacementDetails = false;

    UPROPERTY(EditAnywhere, Category = "Debug Brush Settings")
    float DebugDuration = 30.0f;

    UPROPERTY(EditAnywhere, Category = "Debug Brush Settings")
    float EdgeVoxelSize = 5.0f;

    UPROPERTY(EditAnywhere, Category = "Debug Brush Settings")
    float SelectionMarkerSize = 20.0f;
};


#if WITH_EDITOR
class FDiggerEdModeToolkit;
#endif


// New struct to track voxel storage location
USTRUCT(BlueprintType)
struct FVoxelInstance
{
    GENERATED_BODY()

    FVoxelInstance()
        : GlobalVoxel(FIntVector::ZeroValue)
        , ChunkCoords(FIntVector::ZeroValue)
        , LocalVoxel(FIntVector::ZeroValue)
    {}

    FVoxelInstance(const FIntVector& InGlobal, const FIntVector& InChunk, const FIntVector& InLocal)
        : GlobalVoxel(InGlobal)
        , ChunkCoords(InChunk)
        , LocalVoxel(InLocal)
    {}

    UPROPERTY(BlueprintReadWrite)
    FIntVector GlobalVoxel;
    
    UPROPERTY(BlueprintReadWrite)
    FIntVector ChunkCoords;
    
    UPROPERTY(BlueprintReadWrite)
    FIntVector LocalVoxel;
};



// Brush batching
struct FBrushSample
{
    FVector WorldPos;
    float   Radius;
    float   Strength;
    float   Hardness;
    uint8   Shape;     // 0 sphere, 1 box, 2 capsule...
    uint8   Op;        // 0 add/union, 1 subtract/diff, 2 smin...
};


UCLASS(Blueprintable)
class DIGGERPROUNREAL_API ADiggerManager : public AActor
{
    GENERATED_BODY()

public:
    ADiggerManager();

    // Single delegate to track voxels modification stats for ALL chunks
    FOnVoxelsModified OnVoxelsModified;

    // NEW (stub): will be broadcast once after routing a brush across chunks
    FOnBrushFinished OnBrushFinished;
    
    void SpawnLight(const FBrushStroke& Stroke);
    void InitializeBrushShapes();
    UVoxelBrushShape* GetActiveBrushShape(EVoxelBrushType BrushType) const;
    // Add this helper so callers don’t have to worry about init/fallback:
    UVoxelBrushShape* GetBrushShapeForType(EVoxelBrushType BrushType);

    
    // In ADiggerManager class declaration
    void ApplyLightBrushInEditor(const FBrushStroke& BrushStroke);

    UFUNCTION(BlueprintCallable, Category="Brush")
    void ApplyBrushToAllChunksPIE(FBrushStroke& BrushStroke);
    void ApplyBrushToAllChunks(FBrushStroke& BrushStroke, bool ForceUpdate);
    void ApplyBrushToAllChunks(FBrushStroke& BrushStroke);

    TArray<FIslandData> DetectUnifiedIslands();
    void RemoveUnifiedIslandVoxels(const FIslandData& Island);

    FCriticalSection UpdateChunksCriticalSection;

    void DebugBrushPlacement(const FVector& ClickPosition);
    void DebugDrawVoxelAtWorldPositionFast(const FVector& WorldPosition, const FLinearColor& BoxColor, float Duration,
                                           float Thickness);
    void DebugDrawVoxelAtWorldPosition(const FVector& WorldPosition, FColor BoxColor, float Duration, float Thickness);

    void DrawDiagonalDebugVoxels(FIntVector ChunkCoords);
    void DrawDiagonalDebugVoxelsFast(FIntVector ChunkCoords);
    UStaticMesh* ConvertIslandToStaticMesh(const FIslandData& Island, bool bWorldOrigin, FString AssetName);
    void UpdateAllDirtyChunks();
    AIslandActor* SpawnIslandActorFromIslandAtPosition(const FVector& IslandCenter, bool bEnablePhysics);

    FIntVector FindNearestSurfaceVoxel(USparseVoxelGrid* VoxelGrid, FIntVector IntVector, int SurfaceSearchRadius);
    FIslandMeshData ExtractAndGenerateIslandMesh(const FVector& IslandCenter);
    void SaveIslandData(AIslandActor* IslandActor, const FIslandMeshData& MeshData);
    void OnConvertToPhysicsActorClicked();
    void OnRemoveIslandClicked();
    void ConvertIslandAtPositionToPhysicsObject(const FVector& Vector);
    void ConvertIslandAtPositionToStaticMesh(const FVector& Vector);
    void ConvertIslandAtPositionToActor(const FVector& IslandCenter, bool bEnablePhysics, FIntVector ReferenceVoxel);
    FIslandMeshData ExtractIslandByCenter(const FVector& IslandCenter, bool bRemoveAfter, bool bEnablePhysics);
    FIslandMeshData ExtractAndGenerateIslandMeshFromData(UVoxelChunk* Chunk, const FIslandData& IslandData);
    void RemoveIslandVoxels(const FIslandData& Island);
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

    void ApplyPendingBrushSamples();
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
            OnIslandDetected.Broadcast(Island);
        }
    }
#endif
    
    // Set this true to make the actor never get culled despite distance
    UPROPERTY(EditAnywhere, Category="Digger System")
    bool bNeverCull = true;

    UPROPERTY()
    TArray<AIslandActor*> IslandActors;

    UPROPERTY()
    TArray<AActor*> SpawnedLights;

    UPROPERTY(EditAnywhere, Category = "Digger|Islands")
    UMaterialInterface* IslandMaterial;

    UPROPERTY(SaveGame)
    TArray<FIslandSaveData> SavedIslands;

    
    bool EnsureWorldReference();

    void ClearProceduralMeshes();
    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Export")
    void BakeToStaticMesh(bool bEnableCollision, bool bEnableNanite, float DetailReduction);

    UPROPERTY(EditAnywhere, Category="Digger Brush")
    UVoxelBrushShape* BrushShape;

    UPROPERTY(EditAnywhere)
    UHoleShapeLibrary* HoleShapeLibrary;
        
    // Reference to the terrain hole Blueprint
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Holes")
    TSubclassOf<AActor> HoleBP;
    

    UFUNCTION(BlueprintCallable, Category = "Custom")
    void DuplicateLandscape(ALandscapeProxy* Landscape);

    UPROPERTY()
    TArray<UProceduralMeshComponent*> IslandMeshes;

    UPROPERTY(EditAnywhere, Category="Digger Brush|Settings")
    FVector EditorBrushPosition;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Brush Settings")
    ELightBrushType EditorBrushLightType;

    // Add this with your other editor brush variables
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Brush Settings")
    FLinearColor EditorBrushLightColor = FLinearColor::White;
    
    UPROPERTY(EditAnywhere, Category="Brush")
    EVoxelBrushType EditorBrushType = EVoxelBrushType::Sphere;
    
    UPROPERTY(EditAnywhere, Category="Hole")
    EHoleShapeType EditorBrushHoleShape = EHoleShapeType::Cube;

    UPROPERTY(EditAnywhere, Category="Digger Brush|Settings")
    float EditorBrushRadius = 100.0f;

    UPROPERTY(EditAnywhere, Category="Brush")
    float EditorBrushStrength = 1.f;

    UPROPERTY(EditAnywhere, Category="Digger Brush|Settings")
    bool EditorBrushDig = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor Brush")
    bool EditorBrushHiddenSeam = false;
    
    UPROPERTY(EditAnywhere, Category="Digger Brush|Settings")
    FRotator EditorBrushRotation;
    
    UPROPERTY(EditAnywhere, Category="Digger Brush|Settings")
    bool EditorBrushIsFilled;

    UPROPERTY(EditAnywhere, Category="Digger Brush|Settings")
    float EditorBrushLength;

    UPROPERTY(EditAnywhere, Category="Digger Brush|Settings")
    float EditorBrushAngle;

    UPROPERTY(EditAnywhere, Category="Digger Brush|Settings")
    FVector EditorBrushOffset;

    UFUNCTION(CallInEditor, Category="Digger Brush|Actions")
    void ApplyBrushInEditor(bool bDig);
    void RemoveIslandAtPosition(const FVector& IslandCenter, const FIntVector& ReferenceVoxel);


    UPROPERTY(EditAnywhere, Category="Digger Brush|Settings")
    bool EditorbUseAdvancedCubeBrush;
    
    UPROPERTY(EditAnywhere, Category="Digger Brush|Settings")
    float EditorCubeHalfExtentX;
    
    UPROPERTY(EditAnywhere, Category="Digger Brush|Settings")
    float EditorCubeHalfExtentY;
    
    UPROPERTY(EditAnywhere, Category="Digger Brush|Settings")
    float EditorCubeHalfExtentZ;

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
    void PopulateAllCachedLandscapeHeights();
    virtual void BeginPlay() override;
    void StartHeightCaching();
    void DestroyAllHoleBPs();
    void ClearHolesFromChunkMap();
    virtual void PostInitProperties() override;
    void UpdateVoxelSize();
    void ProcessDirtyChunks();

#if WITH_EDITOR
public:
    // Editor support
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
    virtual void OnConstruction(const FTransform& Transform) override;
    void InitHoleShapeLibrary();
    virtual void PostEditMove(bool bFinished) override;
    virtual void PostEditUndo() override;

    // Convenience Methods.
    void ClearDiggerChanges();

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

    // Debug Brush Settings
    UPROPERTY(EditAnywhere, Category = "Debug Brush")
    FDebugBrushSettings DebugBrushSettings;

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
    bool IsGameWorld() const;

    // Global debug flag
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Debug")
    bool bGlobalDebugDrawingEnabled = true;
    
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
    // IBrush shape cache map.
    UPROPERTY(Transient)
    TMap<EVoxelBrushType, UVoxelBrushShape*> BrushShapeMap;
    
    // One global cache of reusable brush shapes
    UPROPERTY(Transient)
    TMap<EVoxelBrushType, TObjectPtr<UVoxelBrushShape>> CachedBrushShapes;
    std::mutex ChunkProcessingMutex;
    TArray<FBrushSample> PendingStroke;    // cleared each frame after apply
    float BrushResampleSpacing = 0.f;      // set to Radius*0.5f when brush changes
    FVector LastSamplePos = FVector::ZeroVector;
    bool bHasLastSample = false;
    
    // The mutex for Island Removal.
    FCriticalSection IslandRemovalMutex;

    TArray<FIntVector> GetPossibleOwningChunks(const FIntVector& GlobalIndex);
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

    // Update the cache to support multiple save files
    TMap<FString, TArray<FIntVector>> SavedChunkCache;

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
    void QueueBrushPoint(const FVector& HitPointWS, float Radius, float Strength, float Hardness, uint8 Shape,
                         uint8 Op);

    // New methods for multiple save file support
    FString GetSaveFileDirectory(const FString& SaveFileName) const;
    FString GetChunkFilePath(const FIntVector& ChunkCoords, const FString& SaveFileName) const;
    
    bool DoesSaveFileExist(const FString& SaveFileName) const;
    bool DoesChunkFileExist(const FIntVector& ChunkCoords, const FString& SaveFileName) const;
    
    void EnsureSaveFileDirectoryExists(const FString& SaveFileName) const;
    
    bool SaveChunk(const FIntVector& ChunkCoords, const FString& SaveFileName);
    bool LoadChunk(const FIntVector& ChunkCoords, const FString& SaveFileName);
    
    bool SaveAllChunks(const FString& SaveFileName);
    bool LoadAllChunks(const FString& SaveFileName);

    // Default Save
    TArray<FIntVector> GetAllSavedChunkCoordinates(bool bForceRefresh);
    // Named Save
    TArray<FIntVector> GetAllSavedChunkCoordinates(const FString& SaveFileName, bool bForceRefresh = false);
    TArray<FString> GetAllSaveFileNames() const;
    
    bool DeleteSaveFile(const FString& SaveFileName);
    void InvalidateSavedChunkCache(const FString& SaveFileName = TEXT(""));
    void InvalidateSavedChunkCache();

private:
    // Cache for saved chunk coordinates to avoid constant filesystem scanning
    UPROPERTY()
    TArray<FIntVector> CachedSavedChunkCoordinates;
    
    // Flag to track if cache is valid
    bool bSavedChunkCacheValid = false;
    
    // Timer to refresh cache periodically instead of every frame
    float SavedChunkCacheRefreshTimer = 0.0f;
    static constexpr float CACHE_REFRESH_INTERVAL = 2.0f; // Refresh every 2 seconds
    
    bool bEditorInitDone = false;   // prevents repeat init in editor
    bool bRuntimeInitDone = false;  // prevents repeat init at runtime

    bool IsRuntimeLike() const
    {
        UWorld* W = GetWorld();
        if (!W) return false;
        return (W->WorldType == EWorldType::Game) || (W->WorldType == EWorldType::PIE);
    }

    // “tiny” editor preview init vs full init
    void InitEditorLightweight();   // very cheap
    void InitRuntimeHeavy();        // full system init
    void EditorDeferredInit();      // called when the actor is dropped (not while dragging)
public:
    // Hole-shape library bootstrap
    void EnsureHoleShapeLibrary();  // load project default else create transient fallback

    // Modified method signatures
    UFUNCTION(BlueprintCallable, Category = "Voxel Serialization")
    TArray<FIntVector> GetAllSavedChunkCoordinates(bool bForceRefresh = false) const;
    
    UFUNCTION(BlueprintCallable, Category = "Voxel Serialization")
    void RefreshSavedChunkCache();
    

    // In ADiggerManager.h
    TArray<FIntVector> GetAllPhysicalStorageChunks(const FIntVector& GlobalVoxel);

    // In ADiggerManager.h
    TSet<FIntVector> PerformCrossChunkFloodFill(const FIntVector& StartGlobalVoxel);

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
    void ClearAllVoxelData();

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
    TOptional<float> SampleLandscapeHeight(ALandscapeProxy* Landscape, const FVector& WorldPos, bool bForcePrecise);
    TOptional<float> SampleLandscapeHeight(ALandscapeProxy* Landscape, const FVector& WorldPos);
    // Delete this after it works!!!11!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // In DiggerManager.h
    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Debug")
    void QuickDebugTest();
    
    float GetSmartLandscapeHeightAt(const FVector& WorldPos);
    float GetSmartLandscapeHeightAt(const FVector& WorldPos, bool bForcePrecise);
    //bool IsNearLandscapeEdge(const FVector& WorldPos, float Threshold);
    bool GetHeightAtLocation(ALandscapeProxy* LandscapeProxy, const FVector& Location, float& OutHeight);
    FVector GetLandscapeNormalAt(const FVector& WorldPosition);
    UWorld* GetSafeWorld() const;
    void EnsureDefaultHoleBP();

    UFUNCTION(BlueprintCallable, Category="Holes")
    void HandleHoleSpawn(const FBrushStroke& Stroke);

private:

    std::queue<FBrushStroke> BrushStrokeQueue;
    const int32 MaxUndoLength = 10; // Example limit

    FTimerHandle ChunkProcessTimerHandle;
    
};
