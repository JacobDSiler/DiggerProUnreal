#pragma once

#include "CoreMinimal.h"
#include "DiggerManager.h"
#include "SparseVoxelGrid.generated.h"

class ADiggerManager;
// Forward declaration
class UVoxelChunk;


// Enhanced struct to hold SDF value and material data for the triplanar material
USTRUCT()
struct FVoxelData
{
	GENERATED_BODY()

	// SDF value for marching cubes (negative inside, positive outside, zero on the surface)
	float SDFValue;

	// Material weights for triplanar blending (should sum to 1.0 or be normalized)
	UPROPERTY()
	float RockWeight;
    
	UPROPERTY()
	float DirtWeight;
    
	UPROPERTY()
	float GrassWeight;

	// Alternative: Single material ID if you prefer discrete materials
	// int32 MaterialID; // 0=Rock, 1=Dirt, 2=Grass, etc.

	// Optional: Additional properties
	// float Hardness;    // For mining/digging mechanics
	// float Temperature; // For environmental effects
	// int32 BiomeID;     // For biome-specific materials

	FVoxelData() 
		: SDFValue(0.0f)
		, RockWeight(0.0f)
		, DirtWeight(0.0f) 
		, GrassWeight(0.0f)
	{}

	FVoxelData(float InSDFValue) 
		: SDFValue(InSDFValue)
		, RockWeight(0.0f)
		, DirtWeight(1.0f) // Default to dirt
		, GrassWeight(0.0f)
	{}

	FVoxelData(float InSDFValue, float InRock, float InDirt, float InGrass)
		: SDFValue(InSDFValue)
		, RockWeight(InRock)
		, DirtWeight(InDirt)
		, GrassWeight(InGrass)
	{}
	// Helper function to normalize material weights
	void NormalizeMaterialWeights()
	{
		float Total = RockWeight + DirtWeight + GrassWeight;
		if (Total > 0.0f)
		{
			RockWeight /= Total;
			DirtWeight /= Total;
			GrassWeight /= Total;
		}
		else
		{
			// Default to dirt if no materials assigned
			DirtWeight = 1.0f;
		}
	}
	
	// Helper function to set material based on height/biome
	void SetMaterialByHeight(float WorldZ)
	{
		// Example height-based material assignment
		if (WorldZ > 500.0f) // High altitude = rock
		{
			RockWeight = 0.8f;
			DirtWeight = 0.2f;
			GrassWeight = 0.0f;
		}
		else if (WorldZ > 50.0f) // Mid altitude = grass with some dirt
		{
			RockWeight = 0.0f;
			DirtWeight = 0.3f;
			GrassWeight = 0.7f;
		}
		else // Low altitude = mostly dirt
		{
			RockWeight = 0.1f;
			DirtWeight = 0.9f;
			GrassWeight = 0.0f;
		}
	}

	// Helper function to blend materials based on noise or other factors
	void BlendMaterials(const FVoxelData& Other, float BlendFactor)
	{
		BlendFactor = FMath::Clamp(BlendFactor, 0.0f, 1.0f);
        
		RockWeight = FMath::Lerp(RockWeight, Other.RockWeight, BlendFactor);
		DirtWeight = FMath::Lerp(DirtWeight, Other.DirtWeight, BlendFactor);
		GrassWeight = FMath::Lerp(GrassWeight, Other.GrassWeight, BlendFactor);
        
		NormalizeMaterialWeights();
	}
};


DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnIslandDetected, const FIslandData&, Island);

UCLASS()
class DIGGERPROUNREAL_API USparseVoxelGrid : public UObject
{
	GENERATED_BODY()

public:
	USparseVoxelGrid();  // Add this line to declare the constructor

	// The mutex for voxel data operations.
	FCriticalSection VoxelDataMutex;
	
public:

	void Initialize(UVoxelChunk* ParentChunkReference);
	void InitializeDiggerManager();
	bool EnsureDiggerManager();
	UWorld* GetSafeWorld() const;
	bool IsPointAboveLandscape(FVector& Point);
	float GetLandscapeHeightAtPoint(FVector Position);
	bool FindNearestSetVoxel(const FIntVector& Start, FIntVector& OutFound);

	// Add this flag for when a border voxel is set
	bool bBorderIsDirty = false;

	void RemoveVoxels(const TArray<FIntVector>& VoxelsToRemove);
	
	float GetVoxel(FIntVector Vector);
	bool IsVoxelSolid(const FIntVector& VoxelIndex) const;
	const FVoxelData* GetVoxelData(const FIntVector& Voxel) const;
	FVoxelData* GetVoxelData(const FIntVector& Voxel);

	// Delegate to broadcast when a new island is detected
	UPROPERTY(BlueprintAssignable, Category = "Island Detection")
	FOnIslandDetected OnIslandDetected;
	

	//A public getter for VoxelData
	const TMap<FIntVector, FVoxelData>& GetVoxel() const { return VoxelData; }
	
	// Adds a voxel at the given coordinates with the provided SDF value
	void SetVoxel(FIntVector Position, float SDFValue, bool bDig);
	void SetVoxel(int32 X, int32 Y, int32 Z, float NewSDFValue, bool bDig);
	void SetVoxel(int32 X, int32 Y, int32 Z, float NewSDFValue, bool bDig) const;

	bool SerializeToArchive(FArchive& Ar);
	bool SerializeFromArchive(FArchive& Ar);

	// Retrieves the voxel's SDF value; returns true if the voxel exists
	float GetVoxel(int32 X, int32 Y, int32 Z);
	float GetVoxel(int32 X, int32 Y, int32 Z) const;
	bool HasVoxelAt(const FIntVector& Key) const;

	bool HasVoxelAt(int32 X, int32 Y, int32 Z) const;
	// Method to get all voxel data
	TMap<FIntVector, float> GetAllVoxels() const;
	
	// Checks if a voxel exists at the given coordinates
	bool VoxelExists(int32 X, int32 Y, int32 Z) const;
	
	// Logs the current voxels and their SDF values
	void LogVoxelData() const;
	void RenderVoxels();

	bool CollectIslandAtPosition(const FVector& Center, TArray<FIntVector>& OutVoxels);

	bool ExtractIslandAtPosition(
	const FVector& Center,
	USparseVoxelGrid*& OutTempGrid,
	TArray<FIntVector>& OutVoxels);
	FIslandData DetectIsland(float SDFThreshold, const FIntVector& StartPosition);


	void SetParentChunkCoordinates(FIntVector& NewParentChunkPosition)
	{
		this->ParentChunkCoordinates = NewParentChunkPosition;
	}


	UFUNCTION(BlueprintCallable, Category = "Voxel Islands")
	bool ExtractIslandByVoxel(const FIntVector& LocalVoxelCoords, USparseVoxelGrid*& OutIslandGrid, TArray<FIntVector>& OutIslandVoxels);
	
	UFUNCTION(BlueprintCallable, Category = "Voxel")
	TArray<FIslandData> DetectIslands(float SDFThreshold = 0.0f);
	void RemoveSpecifiedVoxels(const TArray<FIntVector>& LocalVoxels);
	bool RemoveVoxel(const FIntVector& LocalVoxel);

	UPROPERTY(EditAnywhere, Category="Debug")
	FVector DebugRenderOffset = FVector(- FVoxelConversion::ChunkWorldSize*0.5f,- FVoxelConversion::ChunkWorldSize*0.5f,- FVoxelConversion::ChunkWorldSize*0.5f);
	
	void SynchronizeBordersWithNeighbors();

	FVector3d GetParentChunkCoordinatesV3D() const
	{
		float X=ParentChunkCoordinates.X;
		float Y=ParentChunkCoordinates.Y;
		float Z=ParentChunkCoordinates.Z;
		return FVector3d(X,Y,Z);
	}
	
	FIntVector GetParentChunkCoordinates() const
	{
		return ParentChunkCoordinates;
	}
	
	// The sparse voxel data map, keyed by 3D coordinates, storing FVoxelData
	TMap<FIntVector, FVoxelData> VoxelData;

	
private:
	//Baked SDF for BaseSDF values for after the undo queue brush strokes fall out the end of the queue and get baked.
	TMap<FIntVector, FVoxelData> BakedSDF;
	
	int32 ChunkSize = 32;  // Number of subdivisions per grid size


private:
	//The VoxelSize which will be set to the VoxelSize within DiggerManager during InitializeDiggerManager;
	int LocalVoxelSize;
	
	//Reference to the DiggerManager
	UPROPERTY()
	ADiggerManager* DiggerManager;
	
	// Declare but don't initialize here
	UPROPERTY()
	UVoxelChunk* ParentChunk;

public:
	[[nodiscard]] UVoxelChunk* GetParentChunk() const
	{
		return ParentChunk;
	}

	void SetVoxelData(const TMap<FIntVector, FVoxelData>& InData)
	{
		VoxelData = InData;
	}

private:
	UPROPERTY()
	UWorld* World;

public:
	[[nodiscard]] ADiggerManager* GetDiggerManager() const
	{
		return DiggerManager;
	}

private:
	FIntVector WorldToChunkSpace(float x, float y, float z) const
	{/* Calculate chunk space coordinates*/ return FIntVector(x, y, z) / (ChunkSize * TerrainGridSize);}

private:
	UPROPERTY()
	FIntVector ParentChunkCoordinates;
	
	int TerrainGridSize;
	int Subdivisions;
};
