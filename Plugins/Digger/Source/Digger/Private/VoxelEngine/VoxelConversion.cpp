#include "VoxelConversion.h"
#include "Diggermanager.h"

// Initialize static members with default values
int32   FVoxelConversion::ChunkSize = 8;
int32   FVoxelConversion::Subdivisions = 4;
float   FVoxelConversion::TerrainGridSize = 100.0f;
float   FVoxelConversion::LocalVoxelSize = FVoxelConversion::TerrainGridSize / FVoxelConversion::Subdivisions;
FVector FVoxelConversion::Origin = FVector::ZeroVector;
float FVoxelConversion::ChunkWorldSize = ChunkSize * TerrainGridSize;

TWeakObjectPtr<ADiggerManager> FVoxelConversion::CachedManager;

float FVoxelConversion::GetTerrainHeight(const FVector& WorldPos)
{
	ADiggerManager* Manager = CachedManager.Get();
	if (!Manager)
		return UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT;

	return Manager->GetLandscapeHeightAt(WorldPos);
}


void FVoxelConversion::RefreshDiggerManager(UWorld* World)
{
	if (!World)
	{
		return;
	}
	ADiggerManager* Manager = ADiggerManager::FindDiggerManager(World);
	CachedManager = Manager;
}


