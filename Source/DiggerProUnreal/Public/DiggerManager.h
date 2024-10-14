#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "DiggerManager.generated.h"

class UVoxelChunk;
class UVoxelBrushShape;
class USparseVoxelGrid;
class UMarchingCubes;

UCLASS()
class DIGGERPROUNREAL_API ADiggerManager : public AActor
{
	GENERATED_BODY()

public:
	ADiggerManager();

protected:
	virtual void BeginPlay() override;

public:

	FVector ChunkToWorldCoordinates(int32 XInd, int32 YInd, int32 ZInd) const
	{
		UE_LOG(LogTemp, Warning, TEXT("ChunkToWorldSpace: XInd=1 YInd=1 ZInd=0"));
		/* Calculate world space coordinates*/ return FVector(XInd * ChunkSize * TerrainGridSize, YInd * ChunkSize * TerrainGridSize, ZInd * ChunkSize * TerrainGridSize);
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Settings")
	int32 ChunkSize = 8;  // Number of subdivisions per grid size
	
	// Chunk and grid settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Settings")
	int TerrainGridSize = 100;  // Default size of 1 meter

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Settings")
	int Subdivisions = 4;  // Number of subdivisions per grid size

	int VoxelSize=TerrainGridSize/Subdivisions;

	// The active brush shape
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel System")
	UVoxelBrushShape* ActiveBrush;


	// Apply the active brush to the terrain
	UFUNCTION(BlueprintCallable, Category = "Brush System")
	void ApplyBrush(FVector BrushPosition, float BrushRadius);

	// Get the relevant voxel chunk based on position
	UFUNCTION(BlueprintCallable, Category = "Chunk Management")
	UVoxelChunk* GetOrCreateChunkAt(const FIntVector& ProposedChunkPosition);
	void UpdateChunks();
	
	UFUNCTION(BlueprintCallable, Category = "Debug")
	void DebugLogVoxelChunkGrid() const;
	
	void CreateSingleHole(FVector3d HoleCenter, int HoleSize);

	//Helper functions
	//Calculate a chunk position according to the chunk grid.
	FIntVector CalculateChunkPosition(const FIntVector3& WorldPosition) const;
	FIntVector CalculateChunkPosition(const FVector3d& WorldPosition) const;
	UVoxelChunk* GetOrCreateChunkAt(const FVector3d& ProposedChunkPosition);
	UVoxelChunk* GetOrCreateChunkAt(const float& ProposedChunkX, const float& ProposedChunkY,
	                                const float& ProposedChunkZ);

	// Debug voxel system
	UFUNCTION(BlueprintCallable, Category = "Voxel Debug")
	void DebugVoxels();

	// Procedural mesh component for visual representation
	UPROPERTY(VisibleAnywhere)
	UProceduralMeshComponent* ProceduralMesh;
	
	//The world map of the voxel chunks
	TMap<FIntVector, UVoxelChunk*> ChunkMap;

private:
	// Reference to the sparse voxel grid and marching cubes
	UPROPERTY()
	USparseVoxelGrid* SparseVoxelGrid;

	UPROPERTY()
	UMarchingCubes* MarchingCubes;

	UPROPERTY()
	UVoxelChunk* ZeroChunk;
	
	UPROPERTY()
	UVoxelChunk* OneChunk;

	void CreateSphereVoxelGrid(UVoxelChunk* Chunk, const FVector& Position, float Radius) const;
	void PlaceVoxelForVoxelLine(UVoxelChunk* Chunk, int32 X, int32 Y, int32 Z) const;
	void GenerateAxesAlignedVoxelsInChunk(UVoxelChunk* Chunk) const;
	// Handle the generation of mesh using marching cubes
	void GenerateVoxelsTest();

	//Space Conversion Helper Methods
	FIntVector WorldToChunkCoordinates(float x, float y, float z) const
	    {/* Calculate chunk space coordinates*/ return FIntVector(x, y, z) / (ChunkSize * TerrainGridSize);}
};
