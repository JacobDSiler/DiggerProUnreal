#pragma once

#include "CoreMinimal.h"
#include "LandscapeProxy.h"
#include "UObject/NoExportTypes.h"
#include "FSpawnedHoleData.h"
#include "VoxelChunk.generated.h"

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

    // Initialization
    void InitializeChunk(const FIntVector& InChunkCoordinates, ADiggerManager* InDiggerManager);
    void InitializeMeshComponent(UProceduralMeshComponent* MeshComponent);
    void InitializeDiggerManager(ADiggerManager* InDiggerManager);
    void RestoreAllHoles();

    // In UVoxelChunk.h
    UFUNCTION()
    void SpawnHoleFromData(const FSpawnedHoleData& HoleData);
    
    // In UVoxelChunk.h
    UFUNCTION()
    void SaveHoleData(const FVector& Location, const FRotator& Rotation, const FVector& Scale);



    UFUNCTION(BlueprintCallable)
    void SpawnHole(TSubclassOf<AActor> HoleBPClass, FVector Location, FRotator Rotation, FVector Scale);

    UFUNCTION(BlueprintCallable)
    bool RemoveNearestHole(FVector Location, float MaxDistance = 100.0f);

    
    // Debug functions
    void DebugDrawChunk();
    void DebugPrintVoxelData() const;

    // Brush application
    void ApplyBrushStroke(const FBrushStroke& Stroke, const UVoxelBrushShape* BrushShape);
    void WriteToOverflows(const FIntVector& LocalVoxelCoords, int32 StorageX, int32 StorageY, int32 StorageZ, float SDF,
                          bool bDig);
    void WriteToNeighborOverflows(const FIntVector& LocalVoxelCoords, int32 StorageX, int32 StorageY, int32 StorageZ,
                                  float SDF, bool bDig);
    UVoxelChunk* GetNeighborChunk(const FIntVector& Offset);

    // Update functions
    UFUNCTION(BlueprintCallable, Category  =Custom)
    void MarkDirty();
    UFUNCTION(BlueprintCallable, Category  =Custom)
    void UpdateIfDirty();
    UFUNCTION(BlueprintCallable, Category  =Custom)
    void ForceUpdate();
    bool SaveChunkData(const FString& FilePath);
    bool LoadChunkData(const FString& FilePath);
    bool LoadChunkData(const FString& FilePath, bool bOverwrite);
    void ClearSpawnedHoles();
    void SpawnHoleMeshes();
    AActor* SpawnTransientActor(UWorld* InWorld, TSubclassOf<AActor> ActorClass, FVector Location, FRotator Rotation,
                                FVector Scale);

    // Voxel manipulation
    void SetVoxel(int32 X, int32 Y, int32 Z, const float SDFValue, bool& bDig) const;
    void SetVoxel(const FVector& Position, const float SDFValue, bool& bDig) const;
    
    TSubclassOf<AActor> HoleBP;

    
    UPROPERTY()
    TArray<AActor*> SpawnedHoleInstances;
    TArray<FSpawnedHoleData> HoleDataArray;


    // Mesh generation
    void GenerateMesh() const;

    // Getters
    FIntVector GetChunkPosition() const { return ChunkCoordinates; }
    ADiggerManager* GetDiggerManager() const { return DiggerManager; }
    int32 GetSectionIndex() const { return SectionIndex; }
    UFUNCTION(BluePrintCallable)
    USparseVoxelGrid* GetSparseVoxelGrid() const;
    UMarchingCubes* GetMarchingCubesGenerator() const { return MarchingCubesGenerator; }
    TMap<FVector, float> GetActiveVoxels() const;
    bool IsDirty() const { return bIsDirty; }
    

    // Setters
    void SetMarchingCubesGenerator(UMarchingCubes* InMarchingCubesGenerator) { MarchingCubesGenerator = InMarchingCubesGenerator; }
    void BakeToStaticMesh(bool bEnableCollision, bool bEnableNanite, float DetailReduction, const FString& String);

    float BlendSDF(float SDFValue, float ExistingSDF, bool bDig, float TransitionBand);
    //void ForceRegenerateMesh();

private:
    void ApplySphereBrush(FVector3d BrushPosition, float Radius, bool bDig);
    void ApplyIcosphereBrush(FVector3d BrushPosition, float Radius, FRotator Rotation, bool bDig);
    void ApplyStairsBrush(FVector3d BrushPosition, float Width, float Height, float Depth, int32 NumSteps,
                          bool bSpiral, bool bDig, const FRotator& Rotation);
    float CalculateDiggingSDF(float Distance, float InnerRadius, float Radius, float OuterRadius, float TransitionZone,
                              float ExistingSDF, bool bIsAboveTerrain, float HeightDifferenceFromTerrain);

    float CalculateAdditiveSDF(float Distance, float InnerRadius, float Radius, float OuterRadius, float TransitionZone,
                               float ExistingSDF, bool bIsAboveTerrain);
    void ModifyVoxel(FIntVector Index,float SDFValue, float TransitionBand, bool bDig);
    void ApplyCapsuleBrush(FVector3d BrushPosition, float Length, float Radius, const FRotator& Rotation, bool bDig);
    void ApplyCubeBrush(FVector3d BrushPosition, float HalfSize, bool bDig, const FRotator& Rotation);
    void ApplyAdvancedCubeBrush(FVector3d BrushPosition, FVector HalfExtents, FRotator Rotation, bool bDig);
    void ApplyTorusBrush(FVector3d BrushPosition, float MajorRadius, float MinorRadius, FRotator Rotation, bool bDig);
    void ApplyPyramidBrush(FVector3d BrushPosition, float Height, float BaseHalfExtent, bool bDig,
                           const FRotator& Rotation);

    void CreateSolidShellAroundAirVoxels(const TArray<FIntVector>& AirVoxels);

    void ApplyCylinderBrush(FVector3d BrushPosition, float Radius, float Height, const FRotator& Rotation, bool bDig, bool bFilled);
    // UVoxelChunk.h
    void ApplyConeBrush(FVector3d BrushPosition, float Height, float Angle, bool bFilled, bool bDig, const FRotator& Rotation);
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
