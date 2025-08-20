#pragma once

#include "CoreMinimal.h"
#include "LandscapeProxy.h"
#include "UObject/NoExportTypes.h"
#include "FSpawnedHoleData.h"
#include "HoleShapeLibrary.h"
#include "VoxelBrushTypes.h"
#include "Voxel/VoxelEvents.h"
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
class DIGGERPROUNREAL_API UVoxelChunk : public UObject
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
    
    // Initialization
    void InitializeChunk(const FIntVector& InChunkCoordinates, ADiggerManager* InDiggerManager);
    void InitializeMeshComponent(UProceduralMeshComponent* MeshComponent);
    void InitializeDiggerManager(ADiggerManager* InDiggerManager);
    void RestoreAllHoles();
    void OnMarchingMeshComplete() const;

    UPROPERTY(EditAnywhere)
    UHoleShapeLibrary* HoleShapeLibrary;

    // In UVoxelChunk.h
    UFUNCTION()
    void SpawnHoleFromData(const FSpawnedHoleData& HoleData);
    
    // In UVoxelChunk.h
    UFUNCTION()
    void SaveHoleData(const FVector& Location, const FRotator& Rotation, const FVector& Scale);



    UFUNCTION(BlueprintCallable)
    void SpawnHole(TSubclassOf<AActor> HoleBPClass, FVector Location, FRotator Rotation, FVector Scale, EHoleShapeType ShapeType);

    UFUNCTION(BlueprintCallable)
    bool RemoveNearestHole(FVector Location, float MaxDistance = 100.0f);

    
    // Debug functions
    void DebugDrawChunk();
    void DebugPrintVoxelData() const;

    // Brush application
    UFUNCTION(BlueprintCallable, Category = "Voxel")
    void ApplyBrushStroke(const FBrushStroke& Stroke);
    void WriteToOverflows(const FIntVector& LocalVoxelCoords, int32 StorageX, int32 StorageY, int32 StorageZ, float SDF,
                          bool bDig);
    void InitializeBrushShapes();

    // Update functions
    UFUNCTION(BlueprintCallable, Category  =Custom)
    void MarkDirty();
    UFUNCTION(BlueprintCallable, Category  =Custom)
    void UpdateIfDirty();
    UFUNCTION(BlueprintCallable, Category  =Custom)
    void ForceUpdate();
    void RefreshSectionMesh();
    void OnMeshReady(FIntVector Coord, int32 SectionIdx);
    void ClearAndRebuildSection();
    bool SaveChunkData(const FString& FilePath);
    bool LoadChunkData(const FString& FilePath);
    bool LoadChunkData(const FString& FilePath, bool bOverwrite);
    void ClearSpawnedHoles();
    void SpawnHoleMeshes();
    AActor* SpawnTransientActor(UWorld* InWorld, TSubclassOf<AActor> ActorClass, FVector Location, FRotator Rotation,
                                FVector Scale);
    void EnsureDefaultHoleBP();

    // Voxel manipulation
    void SetVoxel(int32 X, int32 Y, int32 Z, const float SDFValue, bool& bDig) const;
    void SetVoxel(const FVector& Position, const float SDFValue, bool& bDig) const;
    
    TSubclassOf<AActor> HoleBP;

    
    UPROPERTY()
    TArray<AActor*> SpawnedHoleInstances;
    TArray<FSpawnedHoleData> HoleDataArray;


    // Mesh generation
    void GenerateMesh() const;
    void GenerateMeshSyncronous() const;
    void GenerateMesh(bool bIsSyncronous) const;

    // Getters
    FIntVector GetChunkCoordinates() const { return ChunkCoordinates; }
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

    float BlendSDF(float SDFValue, float ExistingSDF, bool bDig, float TransitionBand);
    //void ForceRegenerateMesh();

public:
    // Add a hole to the chunk's hole list
    void AddHoleToChunk(ADynamicHole* Hole);

    // Remove a hole from the chunk's hole list
    void RemoveHoleFromChunk(ADynamicHole* Hole);

    // Generate a new unique hole ID for each hole added
    int32 GenerateHoleID();

private:
    FOnVoxelsModified OnVoxelsModified; // event instance lives on each chunk
    
    int32 HoleIDCounter = 0;  // Counter to generate unique Hole IDs
    
    // Array to store all holes in this chunk
    UPROPERTY()
    TArray<ADynamicHole*> SpawnedHoles;

    // // Cached brush shapes for performance
    // UPROPERTY()
    // TMap<EVoxelBrushType, UVoxelBrushShape*> CachedBrushShapes;
    // UVoxelBrushShape* GetBrushShapeForType(EVoxelBrushType BrushType);
    
public:
    UFUNCTION(NetMulticast, Reliable)
    void MulticastApplyBrushStroke(const FBrushStroke& Stroke);

private:
    FCriticalSection BrushStrokeMutex;
    
    FVector CalculateBrushBounds(const FBrushStroke& Stroke) const;
    
    // helpers (static or local)
    inline float SnapDownTo(float Value, float Step)
    {
        return Step > 0.f ? FMath::FloorToFloat(Value / Step) * Step : Value;
    }
    
    void CreateSolidShellAroundAirVoxels(const TArray<FIntVector>& AirVoxels, bool bHiddenSeam = false);

    void ApplySmoothBrush(const FVector& Center, float Radius, bool bDig, int NumIterations);
    float ComputeSDFValue(float NormalizedDist, bool bDig, float TransitionStart, float TransitionEnd);

    void BakeSingleBrushStroke(FBrushStroke StrokeToBake);
    void SetUniqueSectionIndex();
    
    FIntVector ChunkCoordinates;
    int32 ChunkSize;



private:
    int16 TerrainGridSize; // Default size of 1 meter
    int8 Subdivisions;
    int16 VoxelSize;
    int16 SectionIndex;

private:
    bool bIsDirty;
    
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
