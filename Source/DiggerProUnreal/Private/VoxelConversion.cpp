#include "VoxelConversion.h"

// Initialize static members with default values
int32   FVoxelConversion::ChunkSize = 8;
int32   FVoxelConversion::Subdivisions = 4;
float   FVoxelConversion::TerrainGridSize = 100.0f;
float   FVoxelConversion::LocalVoxelSize = FVoxelConversion::TerrainGridSize / FVoxelConversion::Subdivisions;
FVector FVoxelConversion::Origin = FVector::ZeroVector;