#pragma once

#include "CoreMinimal.h"
// Error in MarchingCubes.cpp
#include "DiggerDebug.h"
#include "SparseVoxelGrid.h"
#include "MarchingCubes.generated.h"

class ADiggerManager;
class UVoxelChunk;

//Mesh Ready Delegate
DECLARE_DELEGATE(FOnMeshReady);

UCLASS()
class DIGGER_API UMarchingCubes : public UObject
{
	GENERATED_BODY()

public:
	static FIntVector GetCornerOffset(int32 Index);
	// Constructors
	UMarchingCubes();
	UMarchingCubes(const FObjectInitializer& ObjectInitializer, const UVoxelChunk* VoxelChunk);
	UFUNCTION()
	void Initialize(ADiggerManager* InDiggerManager);
	void GenerateMesh(const UVoxelChunk* ChunkPtr);

	// Generate the mesh based on the voxel grid
public:
	// --- MAIN API ---
    
	// The Router: Decides between Sync/Async and Algorithms
	// This signature matches what VoxelChunk expects!
	UFUNCTION()
	void GenerateMesh(UVoxelChunk* Chunk);

	// Explicit Sync call (for ForceUpdate)
	UFUNCTION()
	void GenerateMeshSyncronous(UVoxelChunk* Chunk);

	// --- LOW LEVEL API ---

	// Keep this for backward compatibility if other classes call it, 
	// but internally it just calls the function above.
	UFUNCTION()
	void GenerateMeshFromGridSyncronous(
		USparseVoxelGrid* InVoxelGrid,
		const FVector& Origin,
		float VoxelSize,
		TArray<FVector>& OutVertices,
		TArray<int32>& OutTriangles,
		TArray<FVector>& OutNormals
	);
	

	// --- WORKER API ---

	// 1. Pointer Version (Called by DiggerManager/Sync)
	void GenerateMeshFromGrid(
		USparseVoxelGrid* InVoxelGrid,
		const FVector& Origin,
		float VoxelSize,
		const TArray<float>& HeightValues,
		TArray<FVector>& OutVertices,
		TArray<int32>& OutTriangles,
		TArray<FVector>& OutNormals
	);

	// 2. Map Snapshot Version (Called by Async Task) <--- MISSING
	void GenerateMeshFromGrid(
		const TMap<FIntVector, FVoxelData>& VoxelData,
		const FVector& Origin,
		float VoxelSize,
		const TArray<float>& HeightValues,
		TArray<FVector>& OutVertices,
		TArray<int32>& OutTriangles,
		TArray<FVector>& OutNormals
	);

private:
	// 3. The Actual Logic (Worker)
	void GenerateMesh_MarchingCubes(
		const TMap<FIntVector, FVoxelData>& VoxelData,
		const FVector& Origin,
		float VoxelSize,
		const TArray<float>& HeightValues,
		TArray<FVector>& OutVertices,
		TArray<int32>& OutTriangles,
		TArray<FVector>& OutNormals
	);
	

public:

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

	bool IsDebugging() const
	{
		return DiggerDebug::Mesh();
	}
	
	
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

private:
	// Helper to grab heights safely on Game Thread
	TArray<float> CaptureHeightMap(const FVector& Origin, float VoxelSize, int32 GridResolution);
	
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
