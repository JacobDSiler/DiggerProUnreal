#pragma once

#include <mutex>
#include <queue>

#include "CoreMinimal.h"
#include "Landscape.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "VoxelBrushShape.h"
#include "VoxelBrushTypes.h"
#include "DiggerManager.generated.h"

class UTerrainHoleComponent;
// Define the FBrushStroke struct
USTRUCT(BlueprintType)
struct FBrushStroke {
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite)
	FVector BrushPosition;

	UPROPERTY(BlueprintReadWrite)
	float BrushRadius;

	UPROPERTY(BlueprintReadWrite)
	EVoxelBrushType BrushType;

	UPROPERTY(BlueprintReadWrite)
	bool bDig;

	//UPROPERTY(BlueprintReadWrite)
	//TOptional<float> ConeAngle;  // Optional property for cone brush angle

	//UPROPERTY(BlueprintReadWrite)
	//TOptional<bool> FlipOrientation;  // Optional property for flipping the brush rotation 180 degrees
	
	FBrushStroke()
	: BrushPosition(FVector::ZeroVector), // Initialize the enum with a default value
	  BrushRadius(0.f),
	  BrushType(EVoxelBrushType::Sphere),
	  bDig(false)
	{}
};


UCLASS()
class DIGGERPROUNREAL_API ADiggerManager : public AActor
{
	GENERATED_BODY()

public:

	ADiggerManager();
	bool EnsureWorldReference();

	// Reference to the terrain hole Blueprint
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Holes")
	TSubclassOf<AActor> HoleBP;

	UFUNCTION(BlueprintCallable, Category="Holes")
	void SpawnTerrainHole(const FVector& Location);
	
	UFUNCTION(BlueprintCallable, Category = "Custom")
	void DuplicateLandscape(ALandscapeProxy* Landscape);

protected:
	virtual void BeginPlay() override;
	void UpdateVoxelSize();
	void ProcessDirtyChunks();

public:

	FVector ChunkToWorldCoordinates(int32 XInd, int32 YInd, int32 ZInd) const
	{
		//UE_LOG(LogTemp, Warning, TEXT("ChunkToWorldSpace called"));
		/* Calculate world space coordinates*/ return FVector(XInd * ChunkSize * TerrainGridSize, YInd * ChunkSize * TerrainGridSize, ZInd * ChunkSize * TerrainGridSize);
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Settings")
	int32 ChunkSize = 32;  // Number of subdivisions per grid size
	
	// Chunk and grid settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Settings")
	int TerrainGridSize = 100;  // Default size of 1 meter

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Settings")
	int Subdivisions = 4;  // Number of subdivisions per grid size

	int VoxelSize=TerrainGridSize/Subdivisions;

	// The active brush shape
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel System")
	UVoxelBrushShape* ActiveBrush;

	UFUNCTION(BlueprintCallable, Category = "Custom")
	int32 GetHitSectionIndex(const FHitResult& HitResult);
	
	UFUNCTION(BlueprintCallable, Category = "Custom")
	UVoxelChunk* GetChunkBySectionIndex(int32 SectionIndex);

	UFUNCTION(BlueprintCallable, Category = "Custom")
	void UpdateChunkFromSectionIndex(const FHitResult& HitResult);
	void DebugDrawChunkSectionIDs();

	// Apply the active brush to the terrain
	UFUNCTION(BlueprintCallable, Category = "Brush System")
	void ApplyBrush();
	UVoxelChunk* FindOrCreateNearestChunk(const FVector& Position);
	UVoxelChunk* FindNearestChunk(const FVector& Position);
	void MarkNearbyChunksDirty(const FVector& CenterPosition, float Radius);

	// Get the relevant voxel chunk based on position
	UFUNCTION(BlueprintCallable, Category = "Chunk Management")
	UVoxelChunk* GetOrCreateChunkAt(const FIntVector& ProposedChunkPosition);
	
	UFUNCTION(BlueprintCallable, Category = "Debug")
	void DebugLogVoxelChunkGrid() const;

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

	void InitializeChunks();  // Initialize all chunks
	void InitializeSingleChunk(UVoxelChunk* Chunk);  // Initialize a single chunk
	
	//The world map of the voxel chunks
	UPROPERTY()
	TMap<FIntVector, UVoxelChunk*> ChunkMap;


private:
	std::mutex ChunkProcessingMutex;
	// Reference to the sparse voxel grid and marching cubes
	UPROPERTY()
	USparseVoxelGrid* SparseVoxelGrid;

	UPROPERTY()
	UMarchingCubes* MarchingCubes;

	UPROPERTY()
	UVoxelChunk* ZeroChunk;
	
	UPROPERTY()
	UVoxelChunk* OneChunk;

	UPROPERTY()
	UMaterial* TerrainMaterial;

	UPROPERTY()
	TArray<UProceduralMeshComponent*> ProceduralMeshComponents;

public:
	[[nodiscard]] UMaterial* GetTerrainMaterial() const
	{
		return TerrainMaterial;
	}

	void SetTerrainMaterial(UMaterial* SetTerrainMaterial)
	{
		this->TerrainMaterial = SetTerrainMaterial;
	}

	UPROPERTY()
	UWorld* World;

	/*UPROPERTY(VisibleAnywhere)
	bool landscapeHolesDirty;

	UFUNCTION(BlueprintCallable)
	void MarkDirty()
	{
		landscapeHolesDirty = true; // Set the dirty flag
	}

	UFUNCTION(BlueprintCallable)
	void UpdatedHoles()
	{
		landscapeHolesDirty = false; // Set the dirty flag
	}*/
	
	[[nodiscard]] UWorld* GetWorldFromManager()
	{
		if(EnsureWorldReference())
			return World;
		else
			return nullptr;
	}
	UFUNCTION(BlueprintCallable, Category = "Landscape Tools")
	float GetLandscapeHeightAt(FVector WorldPosition);
	bool GetHeightAtLocation(ALandscapeProxy* LandscapeProxy, const FVector& Location, float& OutHeight);

private:

	std::queue<FBrushStroke> BrushStrokeQueue;
	const int32 MaxUndoLength = 10; // Example limit
	
	FTimerHandle ChunkProcessTimerHandle;

	//Space Conversion Helper Methods
	FIntVector WorldToChunkCoordinates(float x, float y, float z) const
	    {/* Calculate chunk space coordinates*/ return FIntVector(x, y, z) / (ChunkSize * TerrainGridSize);}
};
