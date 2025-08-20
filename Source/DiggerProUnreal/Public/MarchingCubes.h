#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
// Error in MarchingCubes.cpp
#include "UDynamicMesh.h" // Not found
#include "MarchingCubes.generated.h"

class ADiggerManager;
class UVoxelChunk;
class USparseVoxelGrid;

//Mesh Ready Delegate
DECLARE_DELEGATE(FOnMeshReady);

UCLASS()
class DIGGERPROUNREAL_API UMarchingCubes : public UObject
{
	GENERATED_BODY()

public:
	static FIntVector GetCornerOffset(int32 Index);
	// Constructors
	UMarchingCubes();
	UMarchingCubes(const FObjectInitializer& ObjectInitializer, const UVoxelChunk* VoxelChunk);
	void ClearSectionAndRebuild(int32 SectionIndex, FIntVector ChunkCoord);
	void Initialize(ADiggerManager* InDiggerManager);

	// Generate the mesh based on the voxel grid
	void GenerateMesh(const UVoxelChunk* ChunkPtr);
	void GenerateMeshSyncronous(const UVoxelChunk* VoxelChunk);

	// class UMarchingCubes:
	float SurfaceInset = 0.25f; // in *voxels*; small negative offset below surface

	// fast cached height fetch using your precomputed HeightValues
	inline float GetCachedHeightFast(const FVector& WS, const FVector& Origin, float VoxelSize, int32 N, const TArray<float>& HeightValues)
	{
		int32 ix = FMath::Clamp(int32(FMath::FloorToFloat((WS.X - Origin.X) / VoxelSize)), 0, N - 1);
		int32 iy = FMath::Clamp(int32(FMath::FloorToFloat((WS.Y - Origin.Y) / VoxelSize)), 0, N - 1);
		return HeightValues[iy * N + ix];
	}

	inline FVector ClampToLandscapeTop(const FVector& V, const FVector& Origin, float VoxelSize, int32 N, const TArray<float>& HeightValues, float InsetVoxels)
	{
		const float H  = GetCachedHeightFast(V, Origin, VoxelSize, N, HeightValues);
		const float Ez = InsetVoxels * VoxelSize; // cm
		const float Zc = FMath::Min(V.Z, H - Ez);
		return FVector(V.X, V.Y, Zc);
	}

	// Add these members to your MarchingCubes class
private:
	// Height caching system
	UPROPERTY()
	TMap<FIntVector, float> HeightCache;
    
	UPROPERTY()
	FVector CachedChunkOrigin;
    
	UPROPERTY()
	bool bHeightCacheInitialized;
    
	UPROPERTY()
	float CachedVoxelSize;
    
	UPROPERTY()
	int32 CachedChunkSize;

public:
	// Height cache methods
	void InitializeHeightCache(const FVector& ChunkOrigin, float VoxelSize);
	float GetCachedHeight(const FVector& WorldPosition) const;
	void ClearHeightCache();
	bool IsHeightCacheValid(const FVector& ChunkOrigin, float VoxelSize) const;

	bool IsDebugging() {return bIsDebugging;}

	
	
	void GenerateMeshFromGrid(
		USparseVoxelGrid* VoxelGrid,
		const FVector& Origin,
		float VoxelSize,
		TArray<FVector>& OutVertices,
		TArray<int32>& OutTriangles,
		TArray<FVector>& OutNormals
		// Add TArray<FVector2D>& OutUVs, TArray<FColor>& OutColors, TArray<FProcMeshTangent>& OutTangents if needed
	);

	void GenerateMeshFromGridSyncronous(
	USparseVoxelGrid* InVoxelGrid,
	const FVector& Origin,
	float VoxelSize,
	TArray<FVector>& OutVertices,
	TArray<int32>& OutTriangles,
	TArray<FVector>& OutNormals
	);
	
	
	void PopulateHeightValues(
	TArray<float>& OutHeights,
	TSharedPtr<TMap<FIntPoint, float>>* HeightCachePtr,
	const FVector& Origin,
	float VoxelSize,
	int32 N
	) const;

	void IdentifyAirVoxelsBelowTerrain(
	USparseVoxelGrid* Grid,
	TSet<FIntVector>& OutSet,
	const TArray<float>& HeightValues,
	const FVector& Origin,
	int32 N,
	float VoxelSize
	) const;

	float EstimateSDFForMissingCorner(
	const USparseVoxelGrid* Grid,
	const FIntVector& CornerCoords,
	const FVector& CornerWorldPos,
	float TerrainHeight,
	float VoxelSize,
	const FVector& Origin
	) const;

	float GetCornerHeight(
	const FVector& CornerWorldPos,
	float CachedTerrainHeight
	) const;
	
	void LogDebug(const FString& Message);

	float GetCachedHeight(
		TSharedPtr<TMap<FIntPoint, float>>* HeightCachePtr,
		const FVector& WorldPos,
		float VoxelSize
	) const;

	void GenerateMeshForIsland(USparseVoxelGrid* IslandGrid, const FVector& Origin, float VoxelSize, int32 IslandId);
	void ClearSectionAndRebuildMesh(int32 SectionIndex, FIntVector ChunkCoord);

	void CreateIslandProceduralMesh(
		const TArray<FVector>& Vertices,
		const TArray<int32>& Triangles,
		const TArray<FVector>& Normals,
		const FVector& Origin,
		int32 IslandId
	);


	

	void AddSkirtMesh(
	const TArray<int32>& RimVertexIndices,
	TArray<FVector>& Vertices,
	TArray<int32>& Triangles,
	TArray<FVector>& Normals) const;

	void FindRimVertices(
	const TArray<FVector>& Vertices,
	const TArray<int32>& Triangles,
	TArray<int32>& OutRimVertexIndices);
	
	static FVector InterpolateVertex(const FVector& P1, const FVector& P2, float SDF1, float SDF2);

	static int32 CalculateMarchingCubesIndex(const TArray<float>& CornerSDFValues);

	void ReconstructMeshSection(int32 SectionIndex, const TArray<FVector>& OutOutVertices, const TArray<int32>& OutTriangles, const TArray<FVector>&
	                            Normals) const;

	// Reference to the associated voxel chunk
	UPROPERTY()
	const UVoxelChunk* MyVoxelChunk;

	UPROPERTY(EditAnywhere, Category="Landscape Transition")
	float TransitionHeight = 20.0f;

	UPROPERTY(EditAnywhere, Category="Landscape Transition")
	float TransitionSharpness = 2.0f;

	// On Mesh Ready Callback
	FOnMeshReady OnMeshReady;
	FCriticalSection OutVerticesMutex;

	//	void GenerateMeshForIsland(USparseVoxelGrid* IslandGrid, TArray<FVector>& Vertices, TArray<int32>& Triangles, TArray<FVector>& Normals, TArray<FVector2D>& UVs, TArray<FColor>& Colors, TArray<FProcMeshTangent>& Tangents);

private:
	// Helper functions for mesh generation
	//FVector InterpolateVertex(float Value1, FVector V1, float Value2, FVector V2);
	int32 GetVertexIndex(const FVector& Vertex, TMap<FVector, int32>& VertexMap, TArray<FVector>& Vertices);

	//DiggerManager Reference
	UPROPERTY()
	ADiggerManager* DiggerManager;

	UPROPERTY()
	USparseVoxelGrid* VoxelGrid;

	bool bIsDebugging = false;

public:
	void SetIsDebugging(bool bWillDebug)
	{
		this->bIsDebugging = bWillDebug;
	}

private:
	float GetSafeSDFValue(const FIntVector& Position) const;
	void ValidateAndResizeBuffers(FIntVector& Size, TArray<FVector>& Vertices, TArray<int32>& Triangles);
	FVector ApplyLandscapeTransition(const FVector& VertexWS) const;

public:
	void SetDiggerManager(ADiggerManager* SetDiggerManager)
	{
		this->DiggerManager = SetDiggerManager;
	}
};
