#pragma once

#include "CoreMinimal.h"
#include "DiggerConfig.h"
#include "DiggerDebug.h"
#include "SparseVoxelGrid.h"
#include "MarchingCubes.generated.h"

class ADiggerManager;
class UVoxelChunk;

UCLASS()
class DIGGER_API UMarchingCubes : public UObject
{
	GENERATED_BODY()

public:
	// --- CONSTANTS & HELPERS ---
	static FIntVector GetCornerOffset(int32 Index);
	
	// Helper to calculate MC index (Public so it can be tested/debugged if needed)
	static int32 CalculateMarchingCubesIndex(const TArray<float>& CornerSDFValues);
	int32 CalculateMarchingCubesIndex(const float CornerSDF[8]);

	// Helper to interpolate vertices (Public for utility)
	static FORCEINLINE FVector InterpolateVertex(
		const FVector& P1, const FVector& P2,
		float S1, float S2)
	{
		const float EPS = 1e-6f;
		float Den = S1 - S2;
		if (FMath::Abs(Den) < EPS)
			return (P1 + P2) * 0.5f;
		float T = FMath::Clamp(S1 / (S1 - S2), 0.0f, 1.0f);
		return P1 + (P2 - P1) * T;
	}

public:
	// --- LIFECYCLE ---
	UMarchingCubes();
	UMarchingCubes(const FObjectInitializer& ObjectInitializer, const UVoxelChunk* VoxelChunk);
	
	UFUNCTION()
	void Initialize(ADiggerManager* InDiggerManager);

	void SetDiggerManager(ADiggerManager* InDiggerManager) { DiggerManager = InDiggerManager; }

public:
	// --- MAIN GENERATION API ---
    
	// The Main Router: Called by VoxelChunk to trigger generation.
	UFUNCTION()
	void GenerateMesh(UVoxelChunk* Chunk);

	// Explicit Sync call (for ForceUpdate / load scenarios).
	// When BoundsHint is valid the marching cubes traversal is restricted
	// to voxels inside that world-space box.  DensityGrid is still built
	// for the full padded chunk so smoothing and normals are always correct.
	// Pass FBox(ForceInit) (the default) for a full chunk remesh.
	UFUNCTION()
	void GenerateMeshSyncronous(UVoxelChunk* Chunk);
	void GenerateMeshSyncronous(UVoxelChunk* Chunk, const FBox& BoundsHint);

	// Public config-forwarding overload (used by GenerateMeshFromGrid paths)
	void GenerateMesh_MarchingCubes(
		const TMap<FIntVector, FVoxelData>& VoxelData,
		const FVector& Origin,
		float VoxelSize,
		const TArray<float>& HeightValues,
		const FDiggerMeshConfig& Config,
		TArray<FVector>& OutVertices,
		TArray<int32>& OutTriangles,
		TArray<FVector>& OutNormals
	);

	// --- WORKER API (Called by VoxelChunk Async Tasks) ---

	// 1. Map Snapshot Version (The Primary Worker) — full chunk
	void GenerateMeshFromGrid(
		const TMap<FIntVector, FVoxelData>& VoxelData,
		const FVector& Origin,
		float VoxelSize,
		const TArray<float>& HeightValues,
		TArray<FVector>& OutVertices,
		TArray<int32>& OutTriangles,
		TArray<FVector>& OutNormals
	);

	// 1b. AABB-aware version — MC traversal restricted to BoundsHint world box.
	//     DensityGrid, smoothing, and normals are still built for the full chunk.
	//     Pass FBox(ForceInit) for a full remesh (identical to overload 1 above).
	void GenerateMeshFromGrid(
		const TMap<FIntVector, FVoxelData>& VoxelData,
		const FVector& Origin,
		float VoxelSize,
		const TArray<float>& HeightValues,
		const FBox& BoundsHint,
		TArray<FVector>& OutVertices,
		TArray<int32>& OutTriangles,
		TArray<FVector>& OutNormals
	);

	// 2. Pointer Version (Convenience / Sync)
	void GenerateMeshFromGrid(
		USparseVoxelGrid* InVoxelGrid,
		const FVector& Origin,
		float VoxelSize,
		const TArray<float>& HeightValues,
		TArray<FVector>& OutVertices,
		TArray<int32>& OutTriangles,
		TArray<FVector>& OutNormals
	);

	// 3. Legacy / Compatibility Wrapper — captures height map internally.
	UFUNCTION()
	void GenerateMeshFromGridSyncronous(
		USparseVoxelGrid* InVoxelGrid,
		const FVector& Origin,
		float VoxelSize,
		TArray<FVector>& OutVertices,
		TArray<int32>& OutTriangles,
		TArray<FVector>& OutNormals
	);

	// --- UTILITIES (Must be Public for Async Tasks) ---

	// Captures the height map and stores it in CachedHeightValues for reuse.
	// Call once after chunk init; landscape doesn't move at runtime.
	void CacheHeightMap(const FVector& Origin, float VoxelSize, int32 GridResolution);

	// Returns the cached height array.  Empty if CacheHeightMap hasn't been called.
	const TArray<float>& GetCachedHeightValues() const { return CachedHeightValues; }

	// Original on-demand capture (still used by island / sync paths that don't cache)
	// Pad > 0 extends the sample grid by Pad cells in all XY directions so
	// Step 1 pad cells can be looked up directly instead of extrapolated,
	// giving both chunks the exact same landscape value at the boundary.
	// Meshing callers pass Pad=2; legacy/island callers use default Pad=0.
	TArray<float> CaptureHeightMap(const FVector& Origin, float VoxelSize, int32 GridResolution, int32 Pad = 0);

	void WeldCloseVertices(TArray<FVector>& Vertices, TArray<int32>& Triangles);
	
	// Applies the smooth transition blend to landscape
	FVector ApplyLandscapeTransition(const FVector& VertexWS) const;

	// --- ISLAND GENERATION ---
	
	void GenerateMeshForIsland(USparseVoxelGrid* IslandGrid, const FVector& Origin, float VoxelSize, int32 IslandId);
	
	void CreateIslandProceduralMesh(
		const TArray<FVector>& Vertices,
		const TArray<int32>& Triangles,
		const TArray<FVector>& Normals,
		const FVector& Origin,
		int32 IslandId
	);

	// --- HEIGHT CACHE (Legacy/Compat) ---
	
	void InitializeHeightCache(const FVector& ChunkOrigin, float VoxelSize);
	float GetCachedHeight(const FVector& WorldPosition) const;
	void ClearHeightCache();
	bool IsHeightCacheValid(const FVector& ChunkOrigin, float VoxelSize) const;

	// Invalidate the DensityGrid cache so the next remesh performs a full
	// rebuild rather than a warm-start partial update.  Call after any
	// direct voxel writes that bypass the normal brush stroke path (e.g. Undo/Redo).
	void InvalidateDensityCache() { bDensityCacheValid = false; }
	
	inline float GetCachedHeightFast(const FVector& WS, const FVector& Origin, float VoxelSize, int32 N, const TArray<float>& HeightValues)
	{
		int32 ix = FMath::Clamp(int32(FMath::FloorToFloat((WS.X - Origin.X) / VoxelSize)), 0, N - 1);
		int32 iy = FMath::Clamp(int32(FMath::FloorToFloat((WS.Y - Origin.Y) / VoxelSize)), 0, N - 1);
		return HeightValues[iy * N + ix];
	}

	void ClearSectionAndRebuildMesh(int32 SectionIndex, FIntVector ChunkCoord);

	// --- DEBUGGING ---
	void SetIsDebugging(bool bWillDebug) { bIsDebugging = bWillDebug; }
	bool IsDebugging() const { return DiggerDebug::Mesh(); }
	void LogDebug(const FString& Message);

private:
	// --- INTERNAL WORKER ---
	// Auto-reads settings/config then calls the config overload below.
	void GenerateMesh_MarchingCubes(
		const TMap<FIntVector, FVoxelData>& VoxelData,
		const FVector& Origin,
		float VoxelSize,
		const TArray<float>& HeightValues,
		TArray<FVector>& OutVertices,
		TArray<int32>& OutTriangles,
		TArray<FVector>& OutNormals
	);

	// Core implementation — accepts an optional AABB (in local voxel integer space)
	// to restrict the MC traversal loop.  Pass (INT32_MIN sentinels) for full chunk.
	void GenerateMesh_MarchingCubes(
		const TMap<FIntVector, FVoxelData>& VoxelData,
		const FVector& Origin,
		float VoxelSize,
		const TArray<float>& HeightValues,
		const FDiggerMeshConfig& Config,
		const FIntVector& AABBMin,
		const FIntVector& AABBMax,
		TArray<FVector>& OutVertices,
		TArray<int32>& OutTriangles,
		TArray<FVector>& OutNormals
	);

	// Partial update worker — only re-runs Steps 1b+1c over the AABB region
	// of CachedDensityGrid, then runs the FULL MC loop.
	// Requires bDensityCacheValid = true (falls back to full build otherwise).
	void GenerateMesh_PartialUpdate(
		const TMap<FIntVector, FVoxelData>& VoxelData,
		const FVector& Origin,
		float VoxelSize,
		const FDiggerMeshConfig& Config,
		const FIntVector& AABBMin,
		const FIntVector& AABBMax,
		TArray<FVector>& OutVertices,
		TArray<int32>& OutTriangles,
		TArray<FVector>& OutNormals
	);

	int32 GetVertexIndex(const FVector& Vertex, TMap<FVector, int32>& VertexMap, TArray<FVector>& Vertices);
	void ValidateAndResizeBuffers(FIntVector& Size, TArray<FVector>& Vertices, TArray<int32>& Triangles);

	struct FHeightMapCacheKey
	{
		FVector Origin;
		float VoxelSize;
		int32 GridResolution;
		int32 Pad;

		bool operator==(const FHeightMapCacheKey& O) const
		{
			return Origin.Equals(O.Origin, 0.1f)
				&& FMath::IsNearlyEqual(VoxelSize, O.VoxelSize, 0.01f)
				&& GridResolution == O.GridResolution
				&& Pad == O.Pad;
		}
	};

	friend uint32 GetTypeHash(const FHeightMapCacheKey& K)
	{
		return HashCombine(
			HashCombine(GetTypeHash(K.Origin.X), GetTypeHash(K.Origin.Y)),
			HashCombine(GetTypeHash(K.GridResolution), GetTypeHash(K.Pad)));
	}

	TMap<FHeightMapCacheKey, TArray<float>> HeightMapCache;

public:
	void SetOwningChunk(UVoxelChunk* InChunk) { MyVoxelChunk = InChunk; }
	
	UPROPERTY()
	TMap<FIntPoint, float> HeightCache;  // replaced by HeightMapCache
	
	UPROPERTY(EditAnywhere, Category = "Landscape Transition")
	float TransitionHeight = 20.0f;

	UPROPERTY(EditAnywhere, Category = "Landscape Transition")
	float TransitionSharpness = 2.0f;

	UPROPERTY()
	UVoxelChunk* MyVoxelChunk;

private:
	UPROPERTY()
	ADiggerManager* DiggerManager;
    
	UPROPERTY()
	FVector CachedChunkOrigin;
    
	UPROPERTY()
	bool bHeightCacheInitialized;
    
	UPROPERTY()
	float CachedVoxelSize;
    
	UPROPERTY()
	int32 CachedChunkSize;

	// Flat height array cached from CacheHeightMap — reused every remesh
	// instead of re-querying the landscape.  Size = (N+1)².
	TArray<float> CachedHeightValues;

	// Cached DensityGrid and NormalGrid from the last full build.
	// On partial strokes, only the AABB region is recomputed (Steps 1b+1c),
	// then the full MC loop runs from the updated grid — no triangle splicing,
	// no seam artifacts.
	TArray<float>   CachedDensityGrid;
	TArray<FVector> CachedNormalGrid;
	bool            bDensityCacheValid = false;
	FVector         CachedDensityOrigin;
	float           CachedDensityVoxelSize = 0.f;

	bool bIsDebugging = false;
};