#pragma once

#include "CoreMinimal.h"
#include "DiggerConfig.h"
#include "DiggerDebug.h"
#include "SparseVoxelGrid.h"
#include "MarchingCubes.generated.h"

class ADiggerManager;
class UVoxelChunk;

// Mesh Ready Delegate
DECLARE_DELEGATE(FOnMeshReady);

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

	//TMap<FEdgeKey, int32> GlobalEdgeVertexCache;
	
	// Helper to interpolate vertices (Public for utility)
	static FORCEINLINE FVector InterpolateVertex(
		const FVector& P1, const FVector& P2,
		float S1, float S2)
	{
		const float EPS = 1e-6f;

		float Den = S1 - S2;
		if (FMath::Abs(Den) < EPS)
		{
			return (P1 + P2) * 0.5f;
		}

		float T = S1 / (S1 - S2);
		T = FMath::Clamp(T, 0.0f, 1.0f);

		return P1 + (P2 - P1) * T;
	}


public:
	// --- LIFECYCLE ---
	UMarchingCubes();
	UMarchingCubes(const FObjectInitializer& ObjectInitializer, const UVoxelChunk* VoxelChunk);
	
	UFUNCTION()
	void Initialize(ADiggerManager* InDiggerManager);

	void SetDiggerManager(ADiggerManager* SetDiggerManager) { this->DiggerManager = SetDiggerManager; }

public:
	// --- MAIN GENERATION API ---
    
	// The Main Router: Called by VoxelChunk to trigger generation. 
	// Decides strategies, but generally forwards to Sync variants in this implementation context.
	UFUNCTION()
	void GenerateMesh(UVoxelChunk* Chunk);

	// Explicit Sync call (for ForceUpdate scenarios)
	UFUNCTION()
	void GenerateMeshSyncronous(UVoxelChunk* Chunk);

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

	// 1. Map Snapshot Version (The Primary Worker)
	// This is called by the Async Task in VoxelChunk.cpp. It takes a TMap copy for thread safety.
	void GenerateMeshFromGrid(
		const TMap<FIntVector, FVoxelData>& VoxelData,
		const FVector& Origin,
		float VoxelSize,
		const TArray<float>& HeightValues,
		TArray<FVector>& OutVertices,
		TArray<int32>& OutTriangles,
		TArray<FVector>& OutNormals
	);

	// 2. Pointer Version (Convenience / Sync)
	// Wraps the pointer to create a map ref and calls the worker.
	void GenerateMeshFromGrid(
		USparseVoxelGrid* InVoxelGrid,
		const FVector& Origin,
		float VoxelSize,
		const TArray<float>& HeightValues,
		TArray<FVector>& OutVertices,
		TArray<int32>& OutTriangles,
		TArray<FVector>& OutNormals
	);

	// 3. Legacy / Compatibility Wrapper
	// Handles Height Capture internally.
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

	// Helper to grab heights safely on Game Thread before launching Async Task
	TArray<float> CaptureHeightMap(const FVector& Origin, float VoxelSize, int32 GridResolution);

	void WeldCloseVertices(
		TArray<FVector>& Vertices,
		TArray<int32>& Triangles);
	
	// Applies the smooth transition blend to landscape
	FVector ApplyLandscapeTransition(const FVector& VertexWS) const;

	// Game Thread callback to apply mesh data to the component
	void ReconstructMeshSection(
		int32 SectionIndex, 
		const TArray<FVector>& OutOutVertices, 
		const TArray<int32>& OutTriangles, 
		const TArray<FVector>& Normals
	) const;

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
	
	// Fast cached height fetch inline helper
	inline float GetCachedHeightFast(const FVector& WS, const FVector& Origin, float VoxelSize, int32 N, const TArray<float>& HeightValues)
	{
		int32 ix = FMath::Clamp(int32(FMath::FloorToFloat((WS.X - Origin.X) / VoxelSize)), 0, N - 1);
		int32 iy = FMath::Clamp(int32(FMath::FloorToFloat((WS.Y - Origin.Y) / VoxelSize)), 0, N - 1);
		return HeightValues[iy * N + ix];
	}

	void ClearSectionAndRebuildMesh(int32 SectionIndex, FIntVector ChunkCoord);

	// --- DEBUGGING ---
	void SetIsDebugging(bool bWillDebug) { this->bIsDebugging = bWillDebug; }
	bool IsDebugging() const { return DiggerDebug::Mesh(); }
	void LogDebug(const FString& Message);

private:
	// --- INTERNAL WORKER ---
	void GenerateMesh_MarchingCubes(
		const TMap<FIntVector, FVoxelData>& VoxelData,
		const FVector& Origin,
		float VoxelSize,
		const TArray<float>& HeightValues,
		TArray<FVector>& OutVertices,
		TArray<int32>& OutTriangles, TArray<FVector>& OutNormals
	);

	// Helper to get vertex index for deduplication
	int32 GetVertexIndex(const FVector& Vertex, TMap<FVector, int32>& VertexMap, TArray<FVector>& Vertices);

	// Safety check
	void ValidateAndResizeBuffers(FIntVector& Size, TArray<FVector>& Vertices, TArray<int32>& Triangles);

public:
	// Settings
	UPROPERTY(EditAnywhere, Category="Landscape Transition")
	float TransitionHeight = 20.0f;

	UPROPERTY(EditAnywhere, Category="Landscape Transition")
	float TransitionSharpness = 2.0f;

	// Delegate
	FOnMeshReady OnMeshReady;

	// Reference to the associated voxel chunk
	UPROPERTY()
	const UVoxelChunk* MyVoxelChunk;

private:
	UPROPERTY()
	ADiggerManager* DiggerManager;

	// Internal height cache storage
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

	bool bIsDebugging = false;
};