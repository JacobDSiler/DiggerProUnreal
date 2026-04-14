#pragma once

#include "CoreMinimal.h"
#include "LandscapeProxy.h"
#include "UObject/NoExportTypes.h"
#include "FSpawnedHoleData.h"
#include "HoleShapeLibrary.h"
#include "VoxelBrushTypes.h"
#include "Core/VoxelEvents.h"
#include "FSavedLightData.h"
#include "DiggerHistory.h"
#include "VoxelChunk.generated.h"

class ADynamicHole;
class UVoxelBrushShape;
struct FBrushStroke;
class ADiggerManager;
class VoxelBrushShape;
class USparseVoxelGrid;
class UMarchingCubes;
class UProceduralMeshComponent;


UCLASS()
class DIGGER_API UVoxelChunk : public UObject
{
    GENERATED_BODY()

public:
    UVoxelChunk();
    
    void Tick(float DeltaTime);

    /** Per-chunk event: listeners subscribe to chunk changes here. */
    FOnVoxelsModified& OnVoxelsModifiedEvent() { return OnVoxelsModified; }
    const FOnVoxelsModified& OnVoxelsModifiedEvent() const { return OnVoxelsModified; }

    /** Called by voxel code (CPU/GPU) to broadcast a per-chunk report. */
    void ReportVoxelModification(const FVoxelModificationReport& Report);

    // Called by MarchingCubes when geometry generation is complete.
    // Always uploads the full chunk mesh — partial strokes are handled
    // inside MarchingCubes via the DensityGrid cache.
    void UpdateMeshFromData(const TArray<FVector>& Vertices, const TArray<int32>& Triangles,
                            const TArray<FVector>& Normals);
    
    // Initialization
    void InitializeChunk(const FIntVector& InChunkCoordinates, ADiggerManager* InDiggerManager);
    void InitializeMeshComponent(UProceduralMeshComponent* MeshComponent);
    void InitializeDiggerManager(ADiggerManager* InDiggerManager);
    void RestoreAllHoles();
    void OnMarchingMeshComplete() const;

    TArray<FSavedLightData> SavedLights;

    UPROPERTY(EditAnywhere)
    UHoleShapeLibrary* HoleShapeLibrary;

    UFUNCTION()
    /** Spawn a hole actor from serialised data.
     *  ForceUID: if not INDEX_NONE, assigns this specific UID to the actor
     *  (used by Redo so the re-spawned actor gets its original UID back).
     *  Otherwise, derives the UID from HoleData.HoleUID, or allocates a new one. */
    void SpawnHoleFromData(const FSpawnedHoleData& HoleData, int32 ForceUID = INDEX_NONE);
    
    UFUNCTION()
    void SaveHoleData(const FVector& Location, const FRotator& Rotation, const FVector& Scale);

    UFUNCTION(BlueprintCallable)
    void SpawnHole(TSubclassOf<AActor> HoleBPClass, FVector Location, FRotator Rotation, FVector Scale, EHoleShapeType ShapeType);

    UFUNCTION(BlueprintCallable)
    bool RemoveNearestHole(FVector Location, float MaxDistance = 100.0f);

    // Debug functions
    void DebugDrawChunk();
    void DebugPrintVoxelData() const;

    void HandleSmoothBrush(const FBrushStroke& Stroke, float LocalVoxelSize, FVector ChunkOrigin, bool& bModified,
                           int32 X, int32 Y, int32 Z, FVector VoxelWorldPos, float CurrentSDF);
    
    float GetSDFSafe(const FIntVector& Pos, float LocalVoxelSize, const FVector& ChunkOrigin) const;

    // Brush application
    UFUNCTION(BlueprintCallable, Category = "Voxel")
    void ApplyBrushStroke(const FBrushStroke& Stroke);

    // History recording — called by ApplyBrushStroke internally.
    // Also callable externally when replaying a FDiggerChunkAction onto this
    // chunk (e.g. ZBrush-style history copy from another chunk).
    // bRecordForUndo: false when called from Undo/Redo to avoid re-recording.
    void ApplyChunkAction(const FDiggerChunkAction& Action, bool bApplyNew, bool bRecordForUndo = false);

    void WriteToOverflows(const FIntVector& LocalVoxelCoords, int32 StorageX, int32 StorageY, int32 StorageZ, float SDF,
                          bool bDig);

    // Update functions
    UFUNCTION(BlueprintCallable, Category = Custom)
    void MarkDirty();
    void SetSuppressDirty(bool bSuppress) { bSuppressDirty = bSuppress; }

    // Marks dirty with a world-space AABB hint so the mesher can restrict its
    // traversal to only the region that actually changed.  The bounds are unioned
    // across rapid successive calls so no stroke is ever missed.
    // Pass FBox(ForceInit) (invalid box) to fall back to a full chunk remesh.
    void MarkDirtyWithBounds(const FBox& WorldBounds);

    UFUNCTION(BlueprintCallable, Category = Custom)
    void UpdateIfDirty();
    UFUNCTION(BlueprintCallable, Category = Custom)
    void ForceUpdate();
    void RefreshSectionMesh();
    void OnMeshReady(FIntVector Coord, int32 SectionIdx);
    bool SaveChunkData(const FString& FilePath);
    bool LoadChunkData(const FString& FilePath);

    /** Load ONLY hole data from a chunk file — spawns landscape hole actors
     *  without loading voxel data.  Used by Phase 1 of the streaming loader
     *  so the RVT opacity holes appear immediately. */
    bool LoadChunkHolesOnly(const FString& FilePath);

    // Called by Manager to stash a light into this chunk for saving
    void CaptureLightForSave(AActor* LightActor);

    // Called by Manager after saving to clear memory
    void ClearSavedLights();
    bool LoadChunkData(const FString& FilePath, bool bOverwrite);
    /** When true, LoadChunkData skips hole spawning (holes were pre-loaded in Phase 1). */
    bool bHolesPreLoaded = false;
    void ClearSpawnedHoles();
    void SpawnHoleMeshes();
    AActor* SpawnTransientActor(UWorld* InWorld, TSubclassOf<AActor> ActorClass, FVector Location, FRotator Rotation,
                                FVector Scale);
    FVector SnapToVoxelGrid(const FVector& WorldLocation) const;

    // Voxel manipulation
    void SetVoxel(int32 X, int32 Y, int32 Z, const float SDFValue, bool& bDig) const;
    void SetVoxel(const FVector& Position, const float SDFValue, bool& bDig) const;
    
    TSubclassOf<AActor> HoleBP;
    TArray<FSpawnedHoleData> HoleDataArray;

    // Mesh generation
    //   GenerateMesh()           — async marching cubes (normal gameplay path)
    //   GenerateMeshDC()         — async dual contouring (iterating; swap with GenerateMesh to test)
    //   GenerateMeshSyncronous() — sync marching cubes (ForceUpdate / load path)
    void GenerateMesh();
    void GenerateMeshDC();
    void GenerateMeshSyncronous();

    // Getters
    FIntVector GetChunkCoords() const { return ChunkCoordinates; }
    ADiggerManager* GetDiggerManager() const { return DiggerManager; }
    int32 GetSectionIndex() const { return SectionIndex; }
    UFUNCTION(BluePrintCallable)
    USparseVoxelGrid* GetSparseVoxelGrid() const;
    UMarchingCubes* GetMarchingCubesGenerator() const { return MarchingCubesGenerator; }
    TMap<FIntVector, float> GetActiveVoxels() const;
    bool IsDirty() const { return bIsDirty; }

    // Setters
    void SetMarchingCubesGenerator(UMarchingCubes* InMarchingCubesGenerator) { MarchingCubesGenerator = InMarchingCubesGenerator; }
    void BakeToStaticMesh(bool bEnableCollision, bool bEnableNanite, float DetailReduction, const FString& String);
    void DedupHoles();
    void DeclutterHoles();
    void MergeHoles();
    void SpawnMergedHole(FVector Center, float Radius, EHoleShapeType ShapeType);

    // Spawns actors based on stored HoleDataArray
    void RegenerateHolesFromData();

    // Generate a new unique hole ID for each hole added
    int32 GenerateHoleID();

    // --- Hole Tracking ---------------------------------------------------------

public:

    /** Get all holes stored in this chunk (weak pointers for safety). */
    const TArray<TWeakObjectPtr<ADynamicHole>>& GetSpawnedHoles() const
    {
        return SpawnedHoleInstances;
    }

    int32 GetSpawnedHoleCount() const { return SpawnedHoleInstances.Num(); }

    /** Mutable access for bulk operations (bake/clear). */
    TArray<TWeakObjectPtr<ADynamicHole>>& GetSpawnedHolesArray() { return SpawnedHoleInstances; }

    /** Add a hole to this chunk's registry */
    void AddHoleToChunk(ADynamicHole* Hole);

    /** Remove a hole from this chunk's registry */
    void RemoveHoleFromChunk(ADynamicHole* Hole);

    /** Destroy a specific hole actor by its UID.  Used by undo.
     *  Removes the actor from SpawnedHoleInstances, HolesByUID, and HoleDataArray,
     *  then calls Destroy() on the actor. Returns true if the actor was found. */
    bool DestroyHoleByUID(int32 UID);

    /** Unregister a hole UID without destroying the actor
     *  (used when the actor is moving to another chunk). */
    void UnregisterHoleUID(int32 UID);

private:

    /** All hole actors belonging to this chunk (weak refs = safe) */
    UPROPERTY()
    TArray<TWeakObjectPtr<ADynamicHole>> SpawnedHoleInstances;

    /** Fast UID → actor lookup for O(1) undo destroy. */
    TMap<int32, TWeakObjectPtr<ADynamicHole>> HolesByUID;

private:
    FOnVoxelsModified OnVoxelsModified; // event instance lives on each chunk
    
    int32 HoleIDCounter = 0;
    
    UPROPERTY()
    TArray<ADynamicHole*> SpawnedHoles;
    
public:
    UFUNCTION(NetMulticast, Reliable)
    void MulticastApplyBrushStroke(const FBrushStroke& Stroke);

private:
    FCriticalSection BrushStrokeMutex;
    
    FVector CalculateBrushBounds(const FBrushStroke& Stroke) const;
    
    inline float SnapDownTo(float Value, float Step)
    {
        return Step > 0.f ? FMath::FloorToFloat(Value / Step) * Step : Value;
    }
    
    void GenerateNaturalLandscapeSeamRim(const TArray<FIntVector>& AirVoxels, bool bSeamlessMode = true);
    void ApplySmoothBrush(const FVector& Center, float Radius, bool bDig, int NumIterations);
    float ComputeSDFValue(float NormalizedDist, bool bDig, float TransitionStart, float TransitionEnd);
    void BakeSingleBrushStroke(FBrushStroke StrokeToBake);
    void SetUniqueSectionIndex();
    
    FIntVector ChunkCoordinates;
    int16 SectionIndex;
    int32 ChunkSize;

private:
    int16 TerrainGridSize;
    int8  Subdivisions;
    float VoxelSize;

private:
    bool bIsDirty;
	bool bSuppressDirty;

    // World-space AABB of the region dirtied by the most recent brush stroke(s).
    // Consumed and reset by GenerateMeshSyncronous() / GenerateMesh().
    // FBox(ForceInit) means "no hint — do a full chunk remesh".
    // Multiple rapid strokes are unioned so no work is lost.
    FBox DirtyWorldBounds = FBox(ForceInit);

    // Set by partial mesh updates; cleared after physics catches up.
    bool bNeedsCollisionRebuild = false;

    // Accumulated world-space AABB of all brush strokes since the last
    // collision cook.  Only triangles overlapping this box are submitted
    // for the physics cook, keeping the cook proportional to brush size.
    FBox CollisionDirtyBounds = FBox(ForceInit);
    
public: 
    // Set alongside bNeedsCollisionRebuild.  Cleared only after a FULL
    // section cook (not the AABB-culled partial cook done while brushing).
    // ProcessDirtyChunksLoop promotes to a full cook when idle.
    bool bNeedsFullCollisionRebuild = false;
    
    // Called from ADiggerManager::ProcessDirtyChunksLoop (0.1s timer).
    // Resets the global section ID counter to 0 — call after ClearAllVoxelData
    // so newly created chunks get sections starting from 0 again, matching
    // the freshly cleared PMC which has no sections above 0.
    static void ResetGlobalChunkID();

    void RebuildCollisionIfNeeded(bool bIdleFullRebuild = false);

    // Called by the streaming system for chunks close to the player.
    void RequestImmediateCollisionRebuild()
    {
        bNeedsCollisionRebuild     = true;
        bNeedsFullCollisionRebuild = true;
        // Register in the manager's O(dirty) collision set.
        if (DiggerManager)
            DiggerManager->CollisionDirtyCoords.Add(ChunkCoordinates);
    }

private:
    bool bCollisionPropertiesInitialised = false;  // Set once; avoids re-registering collision props every upload

    UPROPERTY()
    UWorld* World;

    UPROPERTY()
    ADiggerManager* DiggerManager;

    UPROPERTY()
    USparseVoxelGrid* SparseVoxelGrid;

    UPROPERTY()
    UMarchingCubes* MarchingCubesGenerator;

    UPROPERTY()
    UProceduralMeshComponent* ProceduralMeshComponent;
};