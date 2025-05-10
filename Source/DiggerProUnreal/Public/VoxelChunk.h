#pragma once

#include "CoreMinimal.h"
#include "LandscapeProxy.h"
#include "UObject/NoExportTypes.h"
#include "VoxelChunk.generated.h"

struct FBrushStroke;
class ADiggerManager;
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
    

    
    // Debug functions
    void DebugDrawChunk();
    void DebugPrintVoxelData() const;

    // Brush application
    void ApplyBrushStroke(FBrushStroke& Stroke);

    // Update functions
    UFUNCTION(BlueprintCallable, Category  =Custom)
    void MarkDirty();
    UFUNCTION(BlueprintCallable, Category  =Custom)
    void UpdateIfDirty();
    UFUNCTION(BlueprintCallable, Category  =Custom)
    void ForceUpdate();
    bool SaveChunkData(const FString& FilePath);
    bool LoadChunkData(const FString& FilePath);

    // Voxel manipulation
    void SetVoxel(int32 X, int32 Y, int32 Z, const float SDFValue, bool& bDig) const;
    void SetVoxel(const FVector& Position, const float SDFValue, bool& bDig) const;
    float GetVoxel(const FVector& Position) const;
    float GetVoxel(int32 X, int32 Y, int32 Z) const;

    // Coordinate conversion

    bool IsValidChunkLocalCoordinate(FVector Position) const;
    bool IsValidChunkLocalCoordinate(int32 LocalX, int32 LocalY, int32 LocalZ) const;
    FVector GetWorldPosition(const FIntVector& VoxelCoordinates) const;
    FVector GetWorldPosition() const;



    // Mesh generation
    void GenerateMesh() const;

    // Getters
    FIntVector GetChunkPosition() const { return ChunkCoordinates; }
    int32 GetChunkSize() const { return ChunkSize; }
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

private:
    void ApplySphereBrush(FVector3d BrushPosition, float Radius, bool bDig);
    float CalculateDiggingSDF(float Distance, float InnerRadius, float Radius, float OuterRadius, float TransitionZone,
                              float ExistingSDF, bool bIsAboveTerrain, float HeightDifferenceFromTerrain);
    float BlendSDF(float SDFValue, float ExistingSDF, bool bDig, float TransitionBand);
    float CalculateAdditiveSDF(float Distance, float InnerRadius, float Radius, float OuterRadius, float TransitionZone,
                               float ExistingSDF, bool bIsAboveTerrain);
    float GetTerrainHeightEfficient(ALandscapeProxy* Landscape, const FVector& WorldPosition);
    void ApplyCubeBrush(FVector3d BrushPosition, float HalfSize, bool bDig, const FRotator& Rotation);
    void ApplyCylinderBrush(FVector3d BrushPosition, float Radius, float Height, bool bDig, const FRotator& Rotation);
    // UVoxelChunk.h
    void ApplyConeBrush(FVector3d BrushPosition, float Height, float Angle, bool bDig, const FRotator& Rotation);
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
