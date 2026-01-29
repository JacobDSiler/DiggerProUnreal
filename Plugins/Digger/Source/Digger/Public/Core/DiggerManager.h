#pragma once

// 1. CoreMinimal must ALWAYS be first in headers
#include "CoreMinimal.h"

// 2. Standard Libraries (Safe after CoreMinimal)
#include <mutex>
#include <queue>

// 3. Engine Runtime Framework
#include "ProceduralMeshComponent.h"
#include "StaticMeshAttributes.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

// 4. Digger Runtime Internal
#include "FBrushStroke.h"
#include "FCustomSDFBrush.h"
#include "HoleShapeLibrary.h"
#include "MarchingCubes.h"
#include "SparseVoxelGrid.h"
#include "VoxelBrushShape.h"
#include "VoxelBrushTypes.h"
#include "VoxelConversion.h"
#include "Materials/DiggerMaterialTypes.h"
#include "Core/VoxelEvents.h"

//4.5 Digger Height Cache
#include "DiggerLandscapeCache.h"

// 5. Editor-Only Headers
// CRITICAL: These must be wrapped. If you use types from these files 
// in your class variables/functions, those variables must ALSO be wrapped in #if WITH_EDITOR
#if WITH_EDITOR
    #include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "IMeshMergeUtilities.h"
#include "AssetRegistry/AssetRegistryModule.h"
#endif

// 6. The Generated Header MUST be the very last include
#include "DiggerManager.generated.h"

// Forward Declarations
class UVoxelBrushShape;
class AIslandActor;
// Editor Only Forward Declarations:
#if WITH_EDITOR
class FDiggerEdModeToolkit;
#endif



// Optional aggregate-for-brush event type
DECLARE_MULTICAST_DELEGATE_OneParam(FOnBrushFinished, const FVoxelModificationReport&);


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

USTRUCT(BlueprintType)
struct FIslandData
{
    GENERATED_BODY()

    FIslandData()
        : Location(FVector::ZeroVector)
        , VoxelCount(0)
        , Voxels()
        , VoxelInstances()
        , ReferenceVoxel(FIntVector::ZeroValue)
    {}

    UPROPERTY(BlueprintReadWrite)
    FVector Location;

    UPROPERTY(BlueprintReadWrite)
    int32 VoxelCount;

    // Keep this for backward compatibility with UI/broadcasting (deduplicated global voxels)
    UPROPERTY(BlueprintReadWrite)
    TArray<FIntVector> Voxels;
    
    // NEW: Store all physical voxel instances including overflow slabs
    UPROPERTY(BlueprintReadWrite)
    TArray<FVoxelInstance> VoxelInstances;
    
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
class DIGGER_API ADiggerManager : public AActor
{
    GENERATED_BODY()

public:
    ADiggerManager();

    // Single delegate to track voxels modification stats for ALL chunks
    FOnVoxelsModified OnVoxelsModified;

    // NEW (stub): will be broadcast once after routing a brush across chunks
    FOnBrushFinished OnBrushFinished;

    // Simple check to see if we have work unsaved
    bool HasVoxelData() const
    {
        return ChunkMap.Num() > 0 || (SparseVoxelGrid && SparseVoxelGrid->VoxelData.Num() > 0);
    }

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

public:
    [[nodiscard]] UDiggerLandscapeCache* AccessHeightCacheSystem() const
    {
        return HeightCacheSystem;
    }

    ALandscapeProxy* GetLandscapeProxyAt(const FVector& WorldPos) const;

    
    UFUNCTION(BlueprintCallable, Category="Digger|Materials")
    void ApplyMaterialProfileToComponent(UDiggerMaterialProfile* Profile, UPrimitiveComponent* TargetComponent, int32 ElementIndex);

    // (Optionally keep a convenience wrapper for your default component)
    UFUNCTION(BlueprintCallable, Category="Digger|Materials")
    void ApplyMaterialProfile(UDiggerMaterialProfile* Profile);

    /** Optionally expose this so the editor UI can set which component to target by default. */
    UFUNCTION(BlueprintCallable, Category="Digger|Materials")
    void SetTargetRenderComponent(UPrimitiveComponent* InComponent, int32 InMaterialElementIndex = 0);

protected:
    /** Master material to instance at runtime. This must point at your M_SedimentMaster. */
    UPROPERTY(EditAnywhere, Category="Digger|Materials")
    TSoftObjectPtr<UMaterialInterface> MasterMaterial;

    /** Default component to receive the MID (your voxel mesh, landscape proxy, etc.). */
    UPROPERTY(VisibleAnywhere, Category="Digger|Materials")
    TWeakObjectPtr<UPrimitiveComponent> TargetRenderComponent;

    /** Which material slot on the component to use. */
    UPROPERTY(EditAnywhere, Category="Digger|Materials")
    int32 MaterialElementIndex = 0;

    /** Cached MID per (component, element). This avoids recreating every call. */
    UPROPERTY(Transient)
    TMap<TWeakObjectPtr<UPrimitiveComponent>, TObjectPtr<UMaterialInstanceDynamic>> MIDCache;

private:
    UMaterialInstanceDynamic* GetOrCreateMID(UPrimitiveComponent* TargetComponent, int32 ElementIndex);
    void PushProfileParamsToMID(UDiggerMaterialProfile* Profile, UMaterialInstanceDynamic* MID, int32 MaxLayers = 12);
    UMaterialInstanceConstant* BuildMaterialInstanceFromProfile(UDiggerMaterialProfile* Profile, const FString& TargetFolder, const FString& BaseAssetName, UMaterialInterface* Parent);

    UPROPERTY()
    UDiggerLandscapeCache* HeightCacheSystem;
public:
    

    // The simplified API
    UFUNCTION(BlueprintCallable, Category = "Landscape Tools")
    float GetLandscapeHeightAt(const FVector& Location);
    TOptional<float> GetLandscapeHeightAt_Internal(const FVector& WorldPos) const;

    // Generic dig function for Vehicles, AI, or Custom Tools
    UFUNCTION(BlueprintCallable, Category = "Digger Tool")
    bool PerformDig(FVector StartLocation, FVector Direction, float TraceRange, float Radius, float Falloff, EVoxelBrushType Shape, bool bIsDigging);
    
    // Static helper to find the manager in the current world
    // Safe for PIE, Editor, and Runtime
    UFUNCTION(BlueprintCallable, Category = "Digger", meta = (WorldContext = "WorldContextObject"))
    static ADiggerManager* FindDiggerManager(const UObject* WorldContextObject);

    // Returns parameter names like "Layer3_BaseColor"
    static FName LayerParam(int32 Index1Based, const TCHAR* Suffix);
    
    // Validate that a master material exposes all expected LayerN_* params.
    // Returns true if everything required is present. OutReport contains a human-readable summary.
    bool ValidateMasterMaterial(UMaterialInterface* Master, int32 MaxLayers, FText& OutReport) const;
    bool ValidateLayeredMaster(UMaterialInterface* Master, int32 ExpectedLayers, FText& OutReport) const;

    
    FCriticalSection UpdateChunksCriticalSection;

    void DebugBrushPlacement(const FVector& ClickPosition);
    void DebugDrawVoxelAtWorldPositionFast(const FVector& WorldPosition, const FLinearColor& BoxColor, float Duration,
                                           float Thickness);
    void DebugDrawVoxelAtWorldPosition(const FVector& WorldPosition, FColor BoxColor, float Duration, float Thickness);

    UFUNCTION(BlueprintCallable, Category = "Digger Manager")
    void ClearAllVoxelData();
    
    // Return bool so the BP knows if it hit something
    UFUNCTION(BlueprintCallable, Category = "Digger")
    bool PerformPlayerDig();
    
    void DrawDiagonalDebugVoxels(FIntVector ChunkCoords);
    void DrawDiagonalDebugVoxelsFast(FIntVector ChunkCoords);
    UStaticMesh* ConvertIslandToStaticMesh(const FIslandData& Island, bool bWorldOrigin, FString AssetName);

    // Call this button to re-scan the landscape if you sculpted it using Unreal tools
    UFUNCTION(CallInEditor, Category = "Landscape Tools")
    void RefreshLandscapeCache();
    
    // Allows the actor to tick in the editor viewport
    virtual bool ShouldTickIfViewportsOnly() const override;
    
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

    void BuildAndApplyProfileMaterial(UDiggerMaterialProfile* Profile);

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
    TOptional<float> SampleLandscapeHeight(ALandscapeProxy* Landscape, const FVector& WorldPos);
    TOptional<float> SampleLandscapeHeight(ALandscapeProxy* Landscape, const FVector& WorldPos, bool bForcePrecise);
    UFUNCTION(BlueprintCallable)
    void CreateHoleAt(FVector WorldPosition, FRotator Rotation, FVector Scale, TSubclassOf<AActor> InDynamicHoleClass);

    UFUNCTION(BlueprintCallable)
    bool RemoveHoleNear(FVector WorldPosition, float MaxDistance = 100.0f);

public:
    void GetAllHoleActors(TArray<AActor*>& OutHoles) const;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Holes")
    UHoleShapeLibrary* HoleShapeLibrary = nullptr;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Holes")
    TSubclassOf<AActor> DynamicHoleClass = nullptr;

    // New helpers:
    void EnsureHoleShapeLibrary();
    void EnsureDefaultHoleBP();

    // Simple getters:
    FORCEINLINE UHoleShapeLibrary* GetHoleShapeLibrary() const { return HoleShapeLibrary; }
    FORCEINLINE TSubclassOf<AActor> GetDynamicHoleClass() const { return DynamicHoleClass; }



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
    // Called when the game stops, level changes, or actor is destroyed
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    
    // 1. Holds the list of chunks we still need to load. 
    // Must be a member so it persists across frames.
    UPROPERTY()
    TArray<FIntVector> PendingChunksToLoad;

    // 2. Tracks the active timer so we can stop it later.
    FTimerHandle LoadQueueTimerHandle;

    // 3. Configuration for speed vs. lag. 
    // Making it EditAnywhere allows you to tune it in the Editor without compiling!
    UPROPERTY(EditAnywhere, Category = "Digger Performance")
    int32 ChunksPerBatch = 5;

    // Store the filename here so the async timer can read it every frame
    UPROPERTY()
    FString AsyncLoadingFileName;

    // Remove the parameter from the function signature!
    // The timer needs a void function.
    UFUNCTION()
    void ProcessChunkLoadQueue();
    
    void RecreateIslandFromSaveData(const FIslandSaveData& SavedIsland);
    virtual void BeginPlay() override;
    void DestroyAllDynamicHoles();
    void ClearHolesFromChunkMap();
    virtual void PostInitProperties() override;
    void UpdateVoxelSize();

#if WITH_EDITOR
public:
    // Editor support
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
    virtual void OnConstruction(const FTransform& Transform) override;
    void InitHoleShapeLibrary();
    virtual void PostEditMove(bool bFinished) override;
    virtual void PostEditUndo() override;

    FTimerHandle ChunkUpdateTimerHandle;
    void ProcessDirtyChunksLoop();
    

    UFUNCTION(CallInEditor, Category = "Editor Tools")
    void EditorRebuildAllChunks();


    FVector SnapToGrid(const FVector& Position, float CellSize)
    {
        const float Cell = FMath::Max(1.f, CellSize);
        auto Snap = [Cell](float v) { return FMath::GridSnap(v, Cell); };
        return FVector(Snap(Position.X), Snap(Position.Y), Snap(Position.Z));
    }


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
    
    //The world map of the voxel chunks
    UPROPERTY()
    TMap<FIntVector, UVoxelChunk*> ChunkMap;

    
private:
    // Helper function to enforce the zero location
    void EnforceZeroLocation();
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
    UMaterialInterface* TerrainMaterial;

    UPROPERTY()
    TArray<UProceduralMeshComponent*> ProceduralMeshComponents;

    // Update the cache to support multiple save files
    TMap<FString, TArray<FIntVector>> SavedChunkCache;

public:
    // Restore HoleBPs in the chunks after re-entering the editor after a PIE session.
    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Digger Utilities")
    void RestoreHolesInEditor();
    bool IsInsideHole(const FVector& WorldPos) const;

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
    void ProcessDirtyChunks();
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
    void EditorDeferredInit();      // called when the actor is dropped (not while dragging)
public:
    // Modified method signatures
    UFUNCTION(BlueprintCallable, Category = "Voxel Serialization")
    TArray<FIntVector> GetAllSavedChunkCoordinates(bool bForceRefresh = false) const;
    
    UFUNCTION(BlueprintCallable, Category = "Voxel Serialization")
    void RefreshSavedChunkCache();
    

    // In ADiggerManager.h
    TArray<FIntVector> GetAllPhysicalStorageChunks(const FIntVector& GlobalVoxel);

    // In ADiggerManager.h
    TSet<FIntVector> PerformCrossChunkFloodFill(const FIntVector& StartGlobalVoxel);
    
public:

    //UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Generation")
    //EMeshGenerationMethod MeshGenerationMethod = EMeshGenerationMethod::MarchingCubes;

    //EMeshGenerationMethod GetMeshGenerationMethod() const;
    
    //void SetMeshGenerationMethod(const FString& Method);
    
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
    
    [[nodiscard]] UMaterialInterface* GetTerrainMaterial() const
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
    

    
    FVector GetLandscapeNormalAt(const FVector& WorldPosition);
    UWorld* GetSafeWorld() const;

    UFUNCTION(BlueprintCallable, Category="Holes")
    void HandleHoleSpawn(const FBrushStroke& Stroke);

private:

    std::queue<FBrushStroke> BrushStrokeQueue;
    const int32 MaxUndoLength = 10; // Example limit
};
