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
#include "Components/InstancedStaticMeshComponent.h"
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
#include "DiggerHistory.h"
#include "Materials/DiggerMaterialTypes.h"
#include "Core/VoxelEvents.h"

//4.5 Digger Height Cache
#include "DiggerLandscapeCache.h"
#include "Data/DiggerFXProfile.h"
#include "VT/RuntimeVirtualTextureVolume.h"

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
class ADynamicHole;
// Editor Only Forward Declarations:
#if WITH_EDITOR
class FDiggerEdModeToolkit;
#endif



// Optional aggregate-for-brush event type
DECLARE_MULTICAST_DELEGATE_OneParam(FOnBrushFinished, const FVoxelModificationReport&);

DECLARE_MULTICAST_DELEGATE_OneParam(FOnModifierBlocked, bool bBlocked);

// ── Async Load Phases ────────────────────────────────────────────────────────
UENUM()
enum class EDiggerLoadPhase : uint8
{
    Idle,
    LandscapeHoles,
    VoxelRehydration,
    MeshGeneration,
    Complete,
    Failed
};

/** Rich progress info broadcast each tick during streaming load. */
USTRUCT()
struct FDiggerLoadProgress
{
    GENERATED_BODY()

    EDiggerLoadPhase Phase        = EDiggerLoadPhase::Idle;
    float            Pct          = 0.f;   // 0-1 overall
    int32            ChunksLoaded = 0;
    int32            TotalChunks  = 0;
    int32            ErrorCount   = 0;
    float            ElapsedSeconds = 0.f;
};


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
        , bIsGrounded(true)
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

    // Set by UDiggerIslandRuntimeSubsystem during grounding check.
    // Default true = safe/unprocessed. False = confirmed floating.
    UPROPERTY(BlueprintReadWrite)
    bool bIsGrounded = true;
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
class DIGGER_API ADiggerManager : public AActor
{
    GENERATED_BODY()

public:
    ADiggerManager();
    
    FOnModifierBlocked OnModifierBlocked;

    // Single delegate to track voxels modification stats for ALL chunks
    FOnVoxelsModified OnVoxelsModified;

    // NEW (stub): will be broadcast once after routing a brush across chunks
    FOnBrushFinished OnBrushFinished;
    bool bSmoothLandscapeAware;

    // Simple check to see if we have work unsaved
    bool HasVoxelData() const
    {
        return ChunkMap.Num() > 0 || (SparseVoxelGrid && SparseVoxelGrid->VoxelData.Num() > 0);
    }

    /** True once EditorDeferredInit has completed its first run. */
    bool IsEditorInitComplete() const { return bEditorInitDone; }

    /** True while async chunk loading is in progress (any phase still has work). */
    bool IsLoadingChunks() const
    {
        return PendingHoleLoad.Num() > 0
            || PendingChunksToLoad.Num() > 0
            || PendingCollisionBuild.Num() > 0;
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

    UFUNCTION(CallInEditor, Category = "Digger|Setup")
    void AutoSetupLandscapeForDynamicHoles();

#if WITH_EDITOR
public:
    // Preflight result — used by the EdMode toolkit UI
    struct FDiggerSetupPreflightResult
    {
        bool  bHasLandscape         = false;
        bool  bHasRVT               = false;
        bool  bAllMaterialsMasked   = false;
        bool  bMFSetAttribsDetected = false;
        int32 LandscapeProxyCount   = 0;
        TArray<FString> Warnings;
        TArray<FString> Infos;

        // Setup button is enabled as long as a landscape exists —
        // everything else is fixed automatically.
        bool IsReadyForSetup() const { return bHasLandscape; }
    };

    FDiggerSetupPreflightResult RunSetupPreflight();

private:
    URuntimeVirtualTexture*        EnsureDiggerRVTAsset();
    ARuntimeVirtualTextureVolume*  EnsureRVTVolumeForBounds(
                                       const FBox& LandscapeUnionBox,
                                       URuntimeVirtualTexture* RVT,
                                       UWorld* W);
    void EnsureLandscapeMaterialHasOpacityMask(
             ALandscapeProxy* Landscape, URuntimeVirtualTexture* RVT);
    void InjectOpacityMaskNodes(
             UMaterial* BaseMat, URuntimeVirtualTexture* OpacityRVT);
    void InjectDiggerRVTParameter(
             UMaterial* BaseMat, URuntimeVirtualTexture* OpacityRVT);
    void InjectOpacityMaskNodes_MaterialAttributes(
             UMaterial* BaseMat, URuntimeVirtualTexture* OpacityRVT);
    bool BackupMaterial(UMaterial* BaseMat);


#endif

#if WITH_EDITORONLY_DATA
public:
    UPROPERTY()
    URuntimeVirtualTexture* CachedDiggerRVT = nullptr;
    URuntimeVirtualTexture* ResolveDiggerRVT();
    URuntimeVirtualTexture* ResolveDiggerRVTForPosition(const FVector& WorldPos);
    void InvalidateRVTCache() { CachedDiggerRVT = nullptr; }
#endif

    
    
    // === Global vertex pool for seam-free geometry ===
public:
    // Quantized world position → global vertex index
    TMap<FIntVector, int32> GlobalVertexCache;

    // Global vertex + normal arrays
    TArray<FVector> GlobalVertices;
    TArray<FVector> GlobalNormals;

    // Global triangle list (optional: per-section later)
    TArray<int32> GlobalTriangles;

    // Quantization scale (tweakable)
    float GlobalVertexQuant = 1.0f; // set in BeginMeshBatch()
    
    // Final assembly into ProceduralMeshComponent
    void BuildFinalMesh();
    
    // Tracks which chunks are scheduled for a mesh rebuild in the current batch
    TSet<FIntVector> DirtyChunkCoords;

    // Chunks that need collision rebuilt — separate from mesh-dirty set so
    // the idle collision scan is O(dirty) not O(all chunks).
    UPROPERTY()
    TSet<FIntVector> CollisionDirtyCoords;

    // How many chunk mesh updates are currently pending in this batch
    int32 PendingMeshUpdates = 0;

    // Called by chunks when they become dirty
    void RegisterDirtyChunk(const FIntVector& Coord);

    // Dirty Mesh Batching
    void BeginMeshUpdateBatch();

    // Called by chunks when their mesh has finished rebuilding
    void NotifyChunkMeshComplete(const FIntVector& Coord);
    void InvalidateLandscapeRVTForDirtyBounds();

    // Your existing stitching function
    void StitchNormalsAcrossSections();


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
    float GetWorldSDF(const FVector& WorldPos) const;
    bool ValidateLayeredMaster(UMaterialInterface* Master, int32 ExpectedLayers, FText& OutReport) const;

    UPROPERTY(Transient)
    bool bIsEditorPainting = false;

    
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

    // BrushRadius getter
    float GetEditorBrushRadius() const { return EditorBrushRadius; }



    // Island delegates removed — all island event routing now flows through
    // UDiggerIslandRuntimeSubsystem → UDiggerEditorEventHub.

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

    /** Returns the current hidden-seam setting (used by VoxelChunk rim generation). */
    bool GetEditorBrushHiddenSeamType() const { return EditorBrushHiddenSeam; }

    /** Clears undo/redo stacks and resets pending action state. */
    void ResetHistoryState()
    {
        UndoStack.Empty();
        RedoStack.Empty();
        PendingAction     = FDiggerHistoryAction();
        bHasPendingAction = false;
    }
    
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
    
    // ── Chunk streaming queues ────────────────────────────────────────────────
    // Phase 1: data loaded, mesh not yet built.
    UPROPERTY()
    TArray<FIntVector> PendingChunksToLoad;

    // Total chunks queued at load start — used to compute progress fraction.
    int32 TotalChunksToLoad = 0;

#if WITH_EDITOR
public:
    // Delegate broadcast during chunk streaming so the editor module
    // (which has Slate linked) can drive a progress notification.
    // Carries rich phase / progress / timing info via FDiggerLoadProgress.
    DECLARE_MULTICAST_DELEGATE_OneParam(FOnLoadProgressUpdated, const FDiggerLoadProgress&);
    FOnLoadProgressUpdated OnLoadProgressUpdated;
#endif
protected:

    // Phase 2: data loaded, mesh building queued (async GenerateMesh).
    UPROPERTY()
    TArray<FIntVector> PendingMeshBuild;

    // Phase 3: mesh built, collision cook queued.
    // int32 = distance-priority bucket (0=immediate, 1=deferred).
    UPROPERTY()
    TArray<FIntVector> PendingCollisionBuild;

    // Timer driving all three phases from one callback.
    FTimerHandle LoadQueueTimerHandle;

    // ── Streaming load tuning ────────────────────────────────────────────────
    // Max chunks to process per tick (hard cap even if budget remains).
    UPROPERTY(EditAnywhere, Category = "Digger Performance",
              meta = (DisplayName = "Load Chunks Per Tick", ClampMin="1", ClampMax="32"))
    int32 DiggerLoadChunksPerTick = 4;

    // Millisecond budget per tick — processing stops the moment this is exceeded.
    UPROPERTY(EditAnywhere, Category = "Digger Performance",
              meta = (DisplayName = "Load Budget (ms)", ClampMin="1.0", ClampMax="33.0"))
    float DiggerLoadBudgetMs = 8.0f;

    // Chunks within this radius get double-priority (processed before any chunk
    // outside it, even if the outer chunk is closer in the distance sort).
    UPROPERTY(EditAnywhere, Category = "Digger Performance",
              meta = (DisplayName = "Load Radius Priority (cm)", ClampMin="0"))
    float DiggerLoadRadiusPriority = 5000.f;

    // Radius (in cm) within which collision is cooked immediately on load.
    // Chunks outside this radius defer their collision to the idle cook.
    UPROPERTY(EditAnywhere, Category = "Digger Performance",
              meta = (DisplayName = "Immediate Collision Radius", ClampMin="100"))
    float ImmediateCollisionRadius = 800.f;

    // ── Streaming load state ─────────────────────────────────────────────────
    EDiggerLoadPhase CurrentLoadPhase = EDiggerLoadPhase::Idle;
    double           LoadStartTime    = 0.0;
    int32            LoadErrorCount   = 0;

    // Chunks whose holes have been pre-spawned (Phase 1 output).
    TArray<FIntVector> PendingHoleLoad;

    // True while the streaming load is active — brush input is blocked.
    bool bLoadingBrushDisabled = false;

    // ── Bedrock Floor ─────────────────────────────────────────────────────────
    // A large invisible collision plane placed at a fixed depth below the
    // landscape.  Gives the brush a real blocking hit inside tunnels that
    // haven't been meshed yet, preventing sky-window / disappearing-brush.
    // The plane is ECC_Visibility so SmartTrace finds it naturally.

    /** Enable the invisible bedrock collision floor. */
    UPROPERTY(EditAnywhere, Category = "Digger|Bedrock",
              meta = (DisplayName = "Enable Bedrock Floor"))
    bool bEnableBedrockFloor = true;

    /** Depth below the lowest landscape point where the bedrock plane sits (cm). */
    UPROPERTY(EditAnywhere, Category = "Digger|Bedrock",
              meta = (DisplayName = "Bedrock Depth Below Landscape (cm)",
                      ClampMin = "0", EditCondition = "bEnableBedrockFloor"))
    float BedrockDepthBelowLandscape = 800.f;

    /** Half-extent of the bedrock plane in XY (cm). 500m default covers most landscapes. */
    UPROPERTY(EditAnywhere, Category = "Digger|Bedrock",
              meta = (DisplayName = "Bedrock Half-Extent XY (cm)",
                      ClampMin = "100", EditCondition = "bEnableBedrockFloor"))
    float BedrockHalfExtentXY = 50000.f;

    // Internal — spawned at init, destroyed on clear.
    UPROPERTY(Transient)
    AActor* BedrockFloorActor = nullptr;

    UFUNCTION(BlueprintCallable, Category = "Digger|Bedrock")
    void SpawnOrUpdateBedrockFloor();

    UFUNCTION(BlueprintCallable, Category = "Digger|Bedrock")
    void DestroyBedrockFloor();

protected:

    /** Enable island detection during mesh generation.
     *  Island detection is O(N) over all authored voxels in the chunk — disable
     *  when not needed for a significant interactive performance gain. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Digger Performance",
              meta = (DisplayName = "Enable Island Detection"))
    bool bEnableIslandDetection = false;

    // Store the filename here so the async timer can read it every frame
    UPROPERTY()
    FString AsyncLoadingFileName;

    // Remove the parameter from the function signature!
    // The timer needs a void function.
    UFUNCTION()
    void ProcessChunkLoadQueue();
    
    void RecreateIslandFromSaveData(const FIslandSaveData& SavedIsland);
    virtual void BeginPlay() override;
    virtual void PostInitProperties() override;
    void UpdateVoxelSize();

public:
    // Called from editor toolkit and other external systems.
    void DestroyAllDynamicHoles();
    void ClearHolesFromChunkMap();

protected:

#if WITH_EDITOR
public:
    // Editor support
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
    virtual void PostRegisterAllComponents() override;
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

#if WITH_EDITOR
    // Called by FEditorDelegates::PreSaveWorldWithContext.
    // Ensures all chunk data (voxels + holes) is flushed to disk whenever the
    // user saves the level, so holes survive editor restarts.
    void OnPreSaveWorld(UWorld* InWorld, FObjectPreSaveContext Context);

    // Delegate handle so we can unbind cleanly on actor destruction.
    FDelegateHandle PreSaveWorldHandle;
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
    UVoxelChunk* GetOrCreateChunkAtCoords(const FIntVector& ChunkCoords);
    UVoxelChunk* GetChunkAtCoords(const FIntVector& Coords) const;
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

public:
    // ── Hole Actor Pool ───────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, Category = "Digger Performance",
              meta = (DisplayName = "Hole Pool Size", ClampMin = "8", ClampMax = "512"))
    int32 HolePoolSize = 64;

    ADynamicHole* AcquireHoleFromPool(TSubclassOf<AActor> HoleClass);
    void ReturnHoleToPool(ADynamicHole* Hole);

    UFUNCTION(BlueprintCallable, Category = "Digger|Performance")
    void PrewarmHolePool(int32 Count = 0);

    void FlushHolePool();

    /** Public alias for FlushHolePool — called from editor toolkit reset. */
    void FlushHolePoolPublic() { FlushHolePool(); }

private:
    UPROPERTY(Transient)
    TArray<ADynamicHole*> HolePool;   // idle actors waiting to be reused

    // Class the pool was warmed for — if it changes, flush and re-warm.
    UPROPERTY(Transient)
    TSubclassOf<AActor> PooledHoleClass;

public:
    // ── Baked Holes Mesh ─────────────────────────────────────────────────────
    // Merges all rendered hole geometry into a single ProceduralMeshComponent
    // that writes to the RVT exactly like individual DynamicHole actors.
    // After baking, individual actors are returned to the pool — one mesh
    // replaces hundreds of actors, eliminating draw-call overhead and lag.

    /** Merge all rendered hole actors into a single host actor, return others to pool. */
    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Digger|Performance")
    void BakeStableHoles();

    /** Rebuild individual hole actors from HoleDataArray (reverses a bake). */
    void UnbakeHoles();

private:
    // Per-shape-type ISM components that replace individual hole actors.
    // ISM extends UStaticMeshComponent — full RVT support guaranteed.
    // One ISM per shape type (max 7), each instancing all holes of that shape.
    UPROPERTY(Transient)
    TMap<EHoleShapeType, UInstancedStaticMeshComponent*> BakedHoleISMs;

    // Cached M_OpacityMask material for the baked ISMs.
    UPROPERTY(Transient)
    UMaterialInterface* HoleWriterMaterial = nullptr;

    // Timer that fires BakeStableHoles after a quiet period.
    FTimerHandle HoleBakeTimerHandle;

    // True when holes have been baked — new strokes unbake first.
    bool bHolesBaked = false;

public:
    std::mutex ChunkProcessingMutex;
    
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

    // -----------------------------------------------------------------------
    // Mesh Generation — Bake Controls
    // -----------------------------------------------------------------------

    /**
     * When ON  — every chunk in the brush radius generates a full solid-volume
     *            mesh (the dense earth block you see by default).
     * When OFF — chunks with no authored voxels are skipped entirely; only
     *            chunks that have been sculpted (or have a sculpted neighbour
     *            whose boundary voxels cross into their pad) produce geometry.
     *            This is the fast default for in-editor sculpting.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Generation|Bake",
              meta = (DisplayName = "Bake Full Chunk Volume"))
    bool bBakeFullChunkVolume = false;

    /**
     * Target for the "Bake Target Chunk" operation.
     * Set this in the Details panel, then call BakeTargetChunk() or press
     * Ctrl+Shift+B to force-remesh exactly that one chunk.
     * Ignored when calling BakeFullTerrain().
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Generation|Bake",
              meta = (DisplayName = "Bake Target Chunk Coords"))
    FIntVector BakeTargetCoords = FIntVector(0, 0, 0);
    
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

    // =========================================================================
    // UNDO / REDO — public API
    // =========================================================================

    /** Undo the most recent action. Safe to call every frame — no-ops if stack empty. */
    UFUNCTION(BlueprintCallable, Category = "Digger|History")
    void Undo();

    /**
     * Force-mesh every chunk that has authored voxels or a neighbour with
     * boundary voxels.  Use this to bake the visible terrain after bulk edits,
     * loading saves, or when you need collision to be fully up to date.
     * Normally the early-out in GenerateMesh skips unmodified chunks for speed.
     */
    UFUNCTION(BlueprintCallable, Category = "Digger|Mesh")
    void BakeFullTerrain();

    /**
     * Force-remesh the single chunk at BakeTargetCoords, regardless of
     * whether it has authored voxels.  Use this to preview what the full-
     * volume mesh looks like for one chunk without enabling bBakeFullChunkVolume
     * globally, or to refresh a specific chunk after a save/load.
     */
    UFUNCTION(BlueprintCallable, Category = "Digger|Mesh")
    void BakeTargetChunk();

    /**
     * Force-remesh the chunk at the given world position.
     * Convenience wrapper for Blueprint and editor buttons.
     */
    UFUNCTION(BlueprintCallable, Category = "Digger|Mesh")
    void BakeChunkAtWorldPosition(FVector WorldPos);

    /**
     * Enqueue all currently-loaded chunks (those in ChunkMap) for a
     * full-volume bake.  Chunks are drained one-per-tick (configurable via
     * BakeChunksPerTick) so the editor stays responsive.
     * Only chunks that exist in the lazy-loaded ChunkMap are processed —
     * unvisited terrain is not touched.
     */
    UFUNCTION(BlueprintCallable, Category = "Digger|Mesh")
    void EnqueueBakeAllLoadedChunks();

    /** How many queued chunks to bake per Tick. Default 1 keeps editor smooth. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Generation|Bake",
              meta = (DisplayName = "Bake Chunks Per Tick", ClampMin = "1", ClampMax = "8"))
    int32 BakeChunksPerTick = 1;

    /** Read-only — how many chunks are still waiting in the bake queue. */
    UFUNCTION(BlueprintPure, Category = "Digger|Mesh")
    int32 GetBakeQueueSize() const { return ChunkBakeQueue.Num(); }

    /** Cancel any in-progress queued bake. */
    UFUNCTION(BlueprintCallable, Category = "Digger|Mesh")
    void CancelBakeQueue() { ChunkBakeQueue.Empty(); }

private:
    /** Chunks waiting for a full-volume bake, drained BakeChunksPerTick per Tick. */
    TArray<FIntVector> ChunkBakeQueue;

    /** Drains up to BakeChunksPerTick entries from ChunkBakeQueue. Called in Tick. */
    void DrainBakeQueue();

public:

    /** Redo the last undone action. */
    UFUNCTION(BlueprintCallable, Category = "Digger|History")
    void Redo();

    /** True when there is at least one action to undo. */
    UFUNCTION(BlueprintCallable, Category = "Digger|History")
    bool CanUndo() const { return UndoStack.Num() > 0; }

    /** True when there is at least one action to redo. */
    UFUNCTION(BlueprintCallable, Category = "Digger|History")
    bool CanRedo() const { return RedoStack.Num() > 0; }

    /**
     * Open a pending action with the given label.
     * Called by EdMode on mouse-down (StartTracking).
     * If an action is already open it becomes a no-op — safe to call redundantly.
     */
    UFUNCTION(BlueprintCallable, Category = "Digger|History")
    void BeginStroke(const FString& Label = TEXT("Stroke"));

    /**
     * Seals the in-progress brush stroke as one undoable step.
     * Call this on mouse-up / touch-end so a whole drag is one Ctrl+Z.
     * Safe to call even when nothing is pending.
     */
    UFUNCTION(BlueprintCallable, Category = "Digger|History")
    void CommitPendingAction();

    /**
     * Allocate a globally unique, never-reused actor UID.
     * Called at spawn time by SpawnHoleFromData / SpawnLight / SpawnIslandActor.
     */
    int32 AllocateActorUID();

    /**
     * Register an actor in the UID registry so undo can look it up later.
     * Called once per actor right after spawn.
     */
    void RegisterActorUID(int32 UID, AActor* Actor);

    /**
     * Unregister an actor when it is destroyed (call from BeginDestroy or
     * from undo/destroy logic before calling Actor->Destroy()).
     */
    void UnregisterActorUID(int32 UID);

    /**
     * Look up a live actor by UID.  Returns nullptr if the actor has been
     * destroyed or the UID was never registered.
     */
    AActor* FindActorByUID(int32 UID) const;

    // -------------------------------------------------------------------------
    // Internal recording hooks — called by the sub-systems, not manually.
    // -------------------------------------------------------------------------

    /**
     * Called once per chunk per brush-stroke tick by UVoxelChunk::ApplyBrushStroke.
     * Accumulates chunk deltas into the pending action.
     */
    void RecordChunkAction(FDiggerChunkAction&& ChunkAction);

    /**
     * Called once per chunk by HandleHoleSpawn after a hole is successfully
     * spawned.  Records a FDiggerActorSpawnRecord in the pending action so
     * undo can destroy the actor and redo can recreate it via its UID.
     */
    void NotifyHoleAddedForHistory(const FIntVector& ChunkCoords,
                                   const FSpawnedHoleData& HoleData,
                                   int32 HoleUID);

    /**
     * Record a transform move for any chunk-owned actor.
     * Called from PostEditMove(true) on DynamicHole, DynamicLightActor,
     * IslandActor.  Opens a pending action if none is open, then commits it
     * immediately so each move is a single discrete undo step.
     */
    void RecordActorMove(FDiggerActorMoveRecord&& Record);

    /**
     * Record an explicit actor destruction (e.g. user deletes via editor).
     * Committed immediately as its own undo step.
     */
    void RecordActorDestroy(FDiggerActorDestroyRecord&& Record);

    // -------------------------------------------------------------------------
    // @deprecated: kept for binary-save compatibility.  Not used by new undo code.
    // -------------------------------------------------------------------------
    void SnapshotHoleCount(const FIntVector& ChunkCoords, int32 CountBefore);

    /** Maximum number of undoable actions kept (0 = unlimited). */
    UPROPERTY(EditAnywhere, Category = "Digger|History")
    int32 MaxUndoHistory = 64;

    /**
     * Master switch for the history system.
     */
    UPROPERTY(EditAnywhere, Category = "Digger|History")
    bool bHistoryEnabled = true;

    /**
     * Whether history recording is active during PIE / runtime gameplay.
     */
    UPROPERTY(EditAnywhere, Category = "Digger|History")
    bool bHistoryEnabledInPIE = false;

    /**
     * When true, RecordChunkAction auto-commits after every call (per-tick undo).
     */
    UPROPERTY(EditAnywhere, Category = "Digger|History")
    bool bAutoCommitEveryTick = false;

    // =========================================================================
    // TIMELINE — per-chunk history (ZBrush-style foundations)
    // =========================================================================

    const FDiggerHistory* GetChunkHistory(const FIntVector& ChunkCoords) const
    {
        return ChunkHistories.Find(ChunkCoords);
    }

    UFUNCTION(BlueprintCallable, Category = "Digger|History")
    void ReplayChunkHistoryOnto(FIntVector SourceChunkCoords, FIntVector TargetChunkCoords,
                                bool bRecordForUndo = true);

    void ReplayActionOnChunk(const FDiggerChunkAction& Action,
                             const FIntVector& TargetChunkCoords,
                             const FIntVector& VoxelOffset = FIntVector::ZeroValue,
                             bool bRecordForUndo = true);

private:

    std::queue<FBrushStroke> BrushStrokeQueue;

    // =========================================================================
    // UNDO / REDO — private state
    // =========================================================================

    TArray<FDiggerHistoryAction> UndoStack;
    TArray<FDiggerHistoryAction> RedoStack;

    FDiggerHistoryAction PendingAction;
    bool bHasPendingAction = false;

    int32 NextActionID  = 0;
    int32 NextActorUID  = 1; // starts at 1; INDEX_NONE (0/-1) means "not assigned"

    TMap<FIntVector, FDiggerHistory> ChunkHistories;

    // Global UID → live actor.  Weak-pointer semantics: entry may be stale if
    // the actor was destroyed externally (e.g. GC, level unload).
    TMap<int32, TWeakObjectPtr<AActor>> ActorRegistry;

    // Ensure a pending action is open, creating one with the given label if needed.
    // All recording paths call this before touching PendingAction.
    void EnsurePendingAction(const FString& Label);

    // Apply a FDiggerHistoryAction across all its chunks and actor records.
    // bApplyNew=true → Redo, false → Undo.
    void ApplyHistoryAction(const FDiggerHistoryAction& Action, bool bApplyNew);

    // Remove oldest entries if UndoStack exceeds MaxUndoHistory.
    void TrimUndoStack();

    // Stores bHistoryEnabled as it was set in the editor, so we can restore
    // it after PIE ends without losing the user's preference.
    bool bHistoryEnabledBeforePIE = true;

    // ── Dig FX (Sound + Niagara) ────────────────────────────────────────────
    /** Optional FX profile mapping dig operations to sounds and particles. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Digger|FX")
    UDiggerFXProfile* FXProfile = nullptr;

    /** If true, FX are played as 3D positional audio at the dig location.
     *  If false, 2D sounds are used (editor / first-person). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Digger|FX")
    bool bUse3DSound = false;

    /** Fire sound and Niagara effects for a brush operation. Throttled by FXProfile::MinInterval. */
    void PlayDigFX(EDiggerFXOperation Operation, const FVector& Location, float BrushRadius);

private:
    double LastFXTime = 0.0;
};