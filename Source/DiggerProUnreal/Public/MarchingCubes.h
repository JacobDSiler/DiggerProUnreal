#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "MarchingCubes.generated.h"

class ADiggerManager;
class UVoxelChunk;
class USparseVoxelGrid;

UCLASS()
class DIGGERPROUNREAL_API UMarchingCubes : public UObject
{
	GENERATED_BODY()

public:
	FIntVector GetCornerOffset(int32 Index);
	// Constructors
	UMarchingCubes();
	UMarchingCubes(const FObjectInitializer& ObjectInitializer, const UVoxelChunk* VoxelChunk);
	void Initialize(ADiggerManager* InDiggerManager);

	// Generate the mesh based on the voxel grid
	void GenerateMesh(const UVoxelChunk* ChunkPtr);


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

	void GenerateMeshForIsland(USparseVoxelGrid* IslandGrid, const FVector& Origin, float VoxelSize, int32 IslandId);

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
	
	FVector InterpolateVertex(const FVector& P1, const FVector& P2, float SDF1, float SDF2);

	int32 CalculateMarchingCubesIndex(const TArray<float>& CornerSDFValues);

	void ReconstructMeshSection(int32 SectionIndex, const TArray<FVector>& OutOutVertices, const TArray<int32>& OutTriangles, const TArray<FVector>&
	                            Normals) const;

	// Reference to the associated voxel chunk
	UPROPERTY()
	const UVoxelChunk* MyVoxelChunk;

	UPROPERTY(EditAnywhere, Category="Landscape Transition")
	float TransitionHeight = 20.0f;

	UPROPERTY(EditAnywhere, Category="Landscape Transition")
	float TransitionSharpness = 2.0f;

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

	bool bIsDebugging;

public:
	void SetIsDebugging(bool bWillDebug)
	{
		this->bIsDebugging = bWillDebug;
	}

private:
	bool IsValidVoxel(const FIntVector& Position) const;
	float GetSafeSDFValue(const FIntVector& Position) const;
	void ValidateAndResizeBuffers(FIntVector& Size, TArray<FVector>& Vertices, TArray<int32>& Triangles);
	FVector ApplyLandscapeTransition(const FVector& VertexWS) const;

public:
	void SetDiggerManager(ADiggerManager* SetDiggerManager)
	{
		this->DiggerManager = SetDiggerManager;
	}
};
