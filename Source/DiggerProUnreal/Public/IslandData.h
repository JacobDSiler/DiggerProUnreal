#pragma once
#include "CoreMinimal.h"
#include "VoxelInstance.h"
#include "FVoxelData.h"
#include "IslandData.generated.h"



// Helper struct for an island
struct FIsland
{
	TArray<FIntVector> Voxels;
};

// Structure to save island data for PIE
USTRUCT()
struct FIslandSaveData
{
	GENERATED_BODY()

	UPROPERTY()
	FVector MeshOrigin = FVector::ZeroVector;

	UPROPERTY()
	TArray<FVector> Vertices;

	UPROPERTY()
	TArray<int32> Triangles;

	UPROPERTY()
	TArray<FVector> Normals;

	UPROPERTY()
	bool bEnablePhysics = false;

	// Optional but recommended constructor
	FIslandSaveData()
		: MeshOrigin(FVector::ZeroVector)
		, Vertices()
		, Triangles()
		, Normals()
		, bEnablePhysics(false)
	{}
};


USTRUCT(BlueprintType)
struct FIslandData
{
	GENERATED_BODY()

	FIslandData()
		: Location(FVector::ZeroVector)
		, VoxelCount(0)
		, Voxels()
		, VoxelInstances()
		, ReferenceVoxel(FIntVector::ZeroValue)
	{}

	UPROPERTY(BlueprintReadWrite)
	FName IslandID;
	
	UPROPERTY(BlueprintReadWrite)
	FVector Location;

	UPROPERTY(BlueprintReadWrite)
	int32 VoxelCount;

	UPROPERTY() // Here we store the full voxel data associated with each voxel.
	TMap<FIntVector, FVoxelData> VoxelDataMap;

	// Keep this for backward compatibility with UI/broadcasting (deduplicated global voxels)
	UPROPERTY(BlueprintReadWrite)
	TArray<FIntVector> Voxels;
    
	// NEW: Store all physical voxel instances including overflow slabs
	UPROPERTY(BlueprintReadWrite)
	TArray<FVoxelInstance> VoxelInstances;
    
	// Store a reference voxel for this island
	UPROPERTY()
	FIntVector ReferenceVoxel;

	// Add a UID to make tracking possible across time
	UPROPERTY()
	FGuid PersistentUID;

	//	Island Name
	UPROPERTY()
	FString IslandName;
};

struct FIslandMeshData
{
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	// Add UVs, Colors, Tangents if needed
	FVector MeshOrigin;
	bool bValid = false;
};