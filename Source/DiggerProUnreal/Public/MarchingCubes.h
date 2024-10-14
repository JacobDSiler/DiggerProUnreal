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
	// Constructors
	UMarchingCubes();
	UMarchingCubes(const FObjectInitializer& ObjectInitializer, const UVoxelChunk* VoxelChunk);

	// Generate the mesh based on the voxel grid
	void GenerateMesh(const UVoxelChunk* ChunkPtr);

	void ReconstructMeshSection(int32 SectionIndex, const TArray<FVector>& OutVertices, const TArray<int32>& OutTriangles) const;

	// Reference to the associated voxel chunk
	UPROPERTY()
	const UVoxelChunk* MyVoxelChunk;

private:
	// Helper functions for mesh generation
	FVector InterpolateVertex(float Value1, FVector V1, float Value2, FVector V2);
	int32 GetVertexIndex(const FVector& Vertex, TMap<FVector, int32>& VertexMap, TArray<FVector>& Vertices);

	//DiggerManager Reference
	UPROPERTY()
	ADiggerManager* DiggerManager;
};
