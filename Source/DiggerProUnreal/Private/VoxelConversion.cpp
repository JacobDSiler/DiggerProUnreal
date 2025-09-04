#include "VoxelConversion.h"

// single, authoritative definitions
int32  FVoxelConversion::ChunkSize       = 8;
int32  FVoxelConversion::Subdivisions    = 4;
float  FVoxelConversion::TerrainGridSize = 100.f;
float  FVoxelConversion::LocalVoxelSize  = 25.f; // overwritten in InitFromConfig
FVector FVoxelConversion::Origin         = FVector::ZeroVector;
