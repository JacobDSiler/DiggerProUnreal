#include "VoxelConversion.h"

// These defaults can be overridden at startup, or set from your manager
FVector FVoxelConversion::Origin = FVector(0.f,0.f,0.f);
int32  FVoxelConversion::ChunkSize = 64;    // e.g. 64 voxels per chunk
int FVoxelConversion::Subdivisions = 1;
float FVoxelConversion::LocalVoxelSize=25;
float FVoxelConversion::TerrainGridSize = 100.0f;
