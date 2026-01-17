#include "MarchingCubes.h"
#include "DiggerManager.h"
#include "VoxelChunk.h"
#include "SparseVoxelGrid.h"
#include "EngineUtils.h"
#include "StaticMeshOperations.h"
#include "UDynamicMesh.h"
#include "Async/Async.h"

// Constants for transition (Adjust these to match your header/preferences)
static const float TRANSITION_HEIGHT = 100.0f; 
static const float TRANSITION_SHARPNESS = 2.0f;
static const float LANDSCAPE_BLEND_WEIGHT = 0.85f;

FIntVector UMarchingCubes::GetCornerOffset(int32 Index)
{
	static const FIntVector Offsets[8] = {
		FIntVector(0, 0, 0), FIntVector(1, 0, 0), FIntVector(1, 1, 0), FIntVector(0, 1, 0),
		FIntVector(0, 0, 1), FIntVector(1, 0, 1), FIntVector(1, 1, 1), FIntVector(0, 1, 1)
	};
	return Offsets[Index];
}

const int EdgeConnection[12][2] = {
	{0, 1}, {1, 2}, {2, 3}, {3, 0},
	{4, 5}, {5, 6}, {6, 7}, {7, 4},
	{0, 4}, {1, 5}, {2, 6}, {3, 7}
};

const int TriangleConnectionTable[256][16] = {
	{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{0, 8, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{0, 1, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{1, 8, 3, 9, 8, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{1, 2, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{0, 8, 3, 1, 2, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{9, 2, 10, 0, 2, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{2, 8, 3, 2, 10, 8, 10, 9, 8, -1, -1, -1, -1, -1, -1, -1},
	{3, 11, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{0, 11, 2, 8, 11, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{1, 9, 0, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{1, 11, 2, 1, 9, 11, 9, 8, 11, -1, -1, -1, -1, -1, -1, -1},
	{3, 10, 1, 11, 10, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{0, 10, 1, 0, 8, 10, 8, 11, 10, -1, -1, -1, -1, -1, -1, -1},
	{3, 9, 0, 3, 11, 9, 11, 10, 9, -1, -1, -1, -1, -1, -1, -1},
	{9, 8, 10, 10, 8, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{4, 7, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{4, 3, 0, 7, 3, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{0, 1, 9, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{4, 1, 9, 4, 7, 1, 7, 3, 1, -1, -1, -1, -1, -1, -1, -1},
	{1, 2, 10, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{3, 4, 7, 3, 0, 4, 1, 2, 10, -1, -1, -1, -1, -1, -1, -1},
	{9, 2, 10, 9, 0, 2, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1},
	{2, 10, 9, 2, 9, 7, 2, 7, 3, 7, 9, 4, -1, -1, -1, -1},
	{8, 4, 7, 3, 11, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{11, 4, 7, 11, 2, 4, 2, 0, 4, -1, -1, -1, -1, -1, -1, -1},
	{9, 0, 1, 8, 4, 7, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1},
	{4, 7, 11, 9, 4, 11, 9, 11, 2, 9, 2, 1, -1, -1, -1, -1},
	{3, 10, 1, 3, 11, 10, 7, 8, 4, -1, -1, -1, -1, -1, -1, -1},
	{1, 11, 10, 1, 4, 11, 1, 0, 4, 7, 11, 4, -1, -1, -1, -1},
	{4, 7, 8, 9, 0, 11, 9, 11, 10, 11, 0, 3, -1, -1, -1, -1},
	{4, 7, 11, 4, 11, 9, 9, 11, 10, -1, -1, -1, -1, -1, -1, -1},
	{9, 5, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{9, 5, 4, 0, 8, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{0, 5, 4, 1, 5, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{8, 5, 4, 8, 3, 5, 3, 1, 5, -1, -1, -1, -1, -1, -1, -1},
	{1, 2, 10, 9, 5, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{3, 0, 8, 1, 2, 10, 4, 9, 5, -1, -1, -1, -1, -1, -1, -1},
	{5, 2, 10, 5, 4, 2, 4, 0, 2, -1, -1, -1, -1, -1, -1, -1},
	{2, 10, 5, 3, 2, 5, 3, 5, 4, 3, 4, 8, -1, -1, -1, -1},
	{9, 5, 4, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{0, 11, 2, 0, 8, 11, 4, 9, 5, -1, -1, -1, -1, -1, -1, -1},
	{0, 5, 4, 0, 1, 5, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1},
	{2, 1, 5, 2, 5, 8, 2, 8, 11, 4, 8, 5, -1, -1, -1, -1},
	{10, 3, 11, 10, 1, 3, 9, 5, 4, -1, -1, -1, -1, -1, -1, -1},
	{4, 9, 5, 0, 8, 1, 8, 10, 1, 8, 11, 10, -1, -1, -1, -1},
	{5, 4, 0, 5, 0, 11, 5, 11, 10, 11, 0, 3, -1, -1, -1, -1},
	{5, 4, 8, 5, 8, 10, 10, 8, 11, -1, -1, -1, -1, -1, -1, -1},
	{9, 7, 8, 5, 7, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{9, 3, 0, 9, 5, 3, 5, 7, 3, -1, -1, -1, -1, -1, -1, -1},
	{0, 7, 8, 0, 1, 7, 1, 5, 7, -1, -1, -1, -1, -1, -1, -1},
	{1, 5, 3, 3, 5, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{9, 7, 8, 9, 5, 7, 10, 1, 2, -1, -1, -1, -1, -1, -1, -1},
	{10, 1, 2, 9, 5, 0, 5, 3, 0, 5, 7, 3, -1, -1, -1, -1},
	{8, 0, 2, 8, 2, 5, 8, 5, 7, 10, 5, 2, -1, -1, -1, -1},
	{2, 10, 5, 2, 5, 3, 3, 5, 7, -1, -1, -1, -1, -1, -1, -1},
	{7, 9, 5, 7, 8, 9, 3, 11, 2, -1, -1, -1, -1, -1, -1, -1},
	{9, 5, 7, 9, 7, 2, 9, 2, 0, 2, 7, 11, -1, -1, -1, -1},
	{2, 3, 11, 0, 1, 8, 1, 7, 8, 1, 5, 7, -1, -1, -1, -1},
	{11, 2, 1, 11, 1, 7, 7, 1, 5, -1, -1, -1, -1, -1, -1, -1},
	{9, 5, 8, 8, 5, 7, 10, 1, 3, 10, 3, 11, -1, -1, -1, -1},
	{5, 7, 0, 5, 0, 9, 7, 11, 0, 1, 0, 10, 11, 10, 0, -1},
	{11, 10, 0, 11, 0, 3, 10, 5, 0, 8, 0, 7, 5, 7, 0, -1},
	{11, 10, 5, 7, 11, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{10, 6, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{0, 8, 3, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{9, 0, 1, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{1, 8, 3, 1, 9, 8, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1},
	{1, 6, 5, 2, 6, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{1, 6, 5, 1, 2, 6, 3, 0, 8, -1, -1, -1, -1, -1, -1, -1},
	{9, 6, 5, 9, 0, 6, 0, 2, 6, -1, -1, -1, -1, -1, -1, -1},
	{5, 9, 8, 5, 8, 2, 5, 2, 6, 3, 2, 8, -1, -1, -1, -1},
	{2, 3, 11, 10, 6, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{11, 0, 8, 11, 2, 0, 10, 6, 5, -1, -1, -1, -1, -1, -1, -1},
	{0, 1, 9, 2, 3, 11, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1},
	{5, 10, 6, 1, 9, 2, 9, 11, 2, 9, 8, 11, -1, -1, -1, -1},
	{6, 3, 11, 6, 5, 3, 5, 1, 3, -1, -1, -1, -1, -1, -1, -1},
	{0, 8, 11, 0, 11, 5, 0, 5, 1, 5, 11, 6, -1, -1, -1, -1},
	{3, 11, 6, 0, 3, 6, 0, 6, 5, 0, 5, 9, -1, -1, -1, -1},
	{6, 5, 9, 6, 9, 11, 11, 9, 8, -1, -1, -1, -1, -1, -1, -1},
	{5, 10, 6, 4, 7, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{4, 3, 0, 4, 7, 3, 6, 5, 10, -1, -1, -1, -1, -1, -1, -1},
	{1, 9, 0, 5, 10, 6, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1},
	{10, 6, 5, 1, 9, 7, 1, 7, 3, 7, 9, 4, -1, -1, -1, -1},
	{6, 1, 2, 6, 5, 1, 4, 7, 8, -1, -1, -1, -1, -1, -1, -1},
	{1, 2, 5, 5, 2, 6, 3, 0, 4, 3, 4, 7, -1, -1, -1, -1},
	{8, 4, 7, 9, 0, 5, 0, 6, 5, 0, 2, 6, -1, -1, -1, -1},
	{7, 3, 9, 7, 9, 4, 3, 2, 9, 5, 9, 6, 2, 6, 9, -1},
	{3, 11, 2, 7, 8, 4, 10, 6, 5, -1, -1, -1, -1, -1, -1, -1},
	{5, 10, 6, 4, 7, 2, 4, 2, 0, 2, 7, 11, -1, -1, -1, -1},
	{0, 1, 9, 4, 7, 8, 2, 3, 11, 5, 10, 6, -1, -1, -1, -1},
	{9, 2, 1, 9, 11, 2, 9, 4, 11, 7, 11, 4, 5, 10, 6, -1},
	{8, 4, 7, 3, 11, 5, 3, 5, 1, 5, 11, 6, -1, -1, -1, -1},
	{5, 1, 11, 5, 11, 6, 1, 0, 11, 7, 11, 4, 0, 4, 11, -1},
	{0, 5, 9, 0, 6, 5, 0, 3, 6, 11, 6, 3, 8, 4, 7, -1},
	{6, 5, 9, 6, 9, 11, 4, 7, 9, 7, 11, 9, -1, -1, -1, -1},
	{10, 4, 9, 6, 4, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{4, 10, 6, 4, 9, 10, 0, 8, 3, -1, -1, -1, -1, -1, -1, -1},
	{10, 0, 1, 10, 6, 0, 6, 4, 0, -1, -1, -1, -1, -1, -1, -1},
	{8, 3, 1, 8, 1, 6, 8, 6, 4, 6, 1, 10, -1, -1, -1, -1},
	{1, 4, 9, 1, 2, 4, 2, 6, 4, -1, -1, -1, -1, -1, -1, -1},
	{3, 0, 8, 1, 2, 9, 2, 4, 9, 2, 6, 4, -1, -1, -1, -1},
	{0, 2, 4, 4, 2, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{8, 3, 2, 8, 2, 4, 4, 2, 6, -1, -1, -1, -1, -1, -1, -1},
	{10, 4, 9, 10, 6, 4, 11, 2, 3, -1, -1, -1, -1, -1, -1, -1},
	{0, 8, 2, 2, 8, 11, 4, 9, 10, 4, 10, 6, -1, -1, -1, -1},
	{3, 11, 2, 0, 1, 6, 0, 6, 4, 6, 1, 10, -1, -1, -1, -1},
	{6, 4, 1, 6, 1, 10, 4, 8, 1, 2, 1, 11, 8, 11, 1, -1},
	{9, 6, 4, 9, 3, 6, 9, 1, 3, 11, 6, 3, -1, -1, -1, -1},
	{8, 11, 1, 8, 1, 0, 11, 6, 1, 9, 1, 4, 6, 4, 1, -1},
	{3, 11, 6, 3, 6, 0, 0, 6, 4, -1, -1, -1, -1, -1, -1, -1},
	{6, 4, 8, 11, 6, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{7, 10, 6, 7, 8, 10, 8, 9, 10, -1, -1, -1, -1, -1, -1, -1},
	{0, 7, 3, 0, 10, 7, 0, 9, 10, 6, 7, 10, -1, -1, -1, -1},
	{10, 6, 7, 1, 10, 7, 1, 7, 8, 1, 8, 0, -1, -1, -1, -1},
	{10, 6, 7, 10, 7, 1, 1, 7, 3, -1, -1, -1, -1, -1, -1, -1},
	{1, 2, 6, 1, 6, 8, 1, 8, 9, 8, 6, 7, -1, -1, -1, -1},
	{2, 6, 9, 2, 9, 1, 6, 7, 9, 0, 9, 3, 7, 3, 9, -1},
	{7, 8, 0, 7, 0, 6, 6, 0, 2, -1, -1, -1, -1, -1, -1, -1},
	{7, 3, 2, 6, 7, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{2, 3, 11, 10, 6, 8, 10, 8, 9, 8, 6, 7, -1, -1, -1, -1},
	{2, 0, 7, 2, 7, 11, 0, 9, 7, 6, 7, 10, 9, 10, 7, -1},
	{1, 8, 0, 1, 7, 8, 1, 10, 7, 6, 7, 10, 2, 3, 11, -1},
	{11, 2, 1, 11, 1, 7, 10, 6, 1, 6, 7, 1, -1, -1, -1, -1},
	{8, 9, 6, 8, 6, 7, 9, 1, 6, 11, 6, 3, 1, 3, 6, -1},
	{0, 9, 1, 11, 6, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{7, 8, 0, 7, 0, 6, 3, 11, 0, 11, 6, 0, -1, -1, -1, -1},
	{7, 11, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{7, 6, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{3, 0, 8, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{0, 1, 9, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{8, 1, 9, 8, 3, 1, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1},
	{10, 1, 2, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{1, 2, 10, 3, 0, 8, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1},
	{2, 9, 0, 2, 10, 9, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1},
	{6, 11, 7, 2, 10, 3, 10, 8, 3, 10, 9, 8, -1, -1, -1, -1},
	{7, 2, 3, 6, 2, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{7, 0, 8, 7, 6, 0, 6, 2, 0, -1, -1, -1, -1, -1, -1, -1},
	{2, 7, 6, 2, 3, 7, 0, 1, 9, -1, -1, -1, -1, -1, -1, -1},
	{1, 6, 2, 1, 8, 6, 1, 9, 8, 8, 7, 6, -1, -1, -1, -1},
	{10, 7, 6, 10, 1, 7, 1, 3, 7, -1, -1, -1, -1, -1, -1, -1},
	{10, 7, 6, 1, 7, 10, 1, 8, 7, 1, 0, 8, -1, -1, -1, -1},
	{0, 3, 7, 0, 7, 10, 0, 10, 9, 6, 10, 7, -1, -1, -1, -1},
	{7, 6, 10, 7, 10, 8, 8, 10, 9, -1, -1, -1, -1, -1, -1, -1},
	{6, 8, 4, 11, 8, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{3, 6, 11, 3, 0, 6, 0, 4, 6, -1, -1, -1, -1, -1, -1, -1},
	{8, 6, 11, 8, 4, 6, 9, 0, 1, -1, -1, -1, -1, -1, -1, -1},
	{9, 4, 6, 9, 6, 3, 9, 3, 1, 11, 3, 6, -1, -1, -1, -1},
	{6, 8, 4, 6, 11, 8, 2, 10, 1, -1, -1, -1, -1, -1, -1, -1},
	{1, 2, 10, 3, 0, 11, 0, 6, 11, 0, 4, 6, -1, -1, -1, -1},
	{4, 11, 8, 4, 6, 11, 0, 2, 9, 2, 10, 9, -1, -1, -1, -1},
	{10, 9, 3, 10, 3, 2, 9, 4, 3, 11, 3, 6, 4, 6, 3, -1},
	{8, 2, 3, 8, 4, 2, 4, 6, 2, -1, -1, -1, -1, -1, -1, -1},
	{0, 4, 2, 4, 6, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{1, 9, 0, 2, 3, 4, 2, 4, 6, 4, 3, 8, -1, -1, -1, -1},
	{1, 9, 4, 1, 4, 2, 2, 4, 6, -1, -1, -1, -1, -1, -1, -1},
	{8, 1, 3, 8, 6, 1, 8, 4, 6, 6, 10, 1, -1, -1, -1, -1},
	{10, 1, 0, 10, 0, 6, 6, 0, 4, -1, -1, -1, -1, -1, -1, -1},
	{4, 6, 3, 4, 3, 8, 6, 10, 3, 0, 3, 9, 10, 9, 3, -1},
	{10, 9, 4, 6, 10, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{4, 9, 5, 7, 6, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{0, 8, 3, 4, 9, 5, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1},
	{5, 0, 1, 5, 4, 0, 7, 6, 11, -1, -1, -1, -1, -1, -1, -1},
	{11, 7, 6, 8, 3, 4, 3, 5, 4, 3, 1, 5, -1, -1, -1, -1},
	{9, 5, 4, 10, 1, 2, 7, 6, 11, -1, -1, -1, -1, -1, -1, -1},
	{6, 11, 7, 1, 2, 10, 0, 8, 3, 4, 9, 5, -1, -1, -1, -1},
	{7, 6, 11, 5, 4, 10, 4, 2, 10, 4, 0, 2, -1, -1, -1, -1},
	{3, 4, 8, 3, 5, 4, 3, 2, 5, 10, 5, 2, 11, 7, 6, -1},
	{7, 2, 3, 7, 6, 2, 5, 4, 9, -1, -1, -1, -1, -1, -1, -1},
	{9, 5, 4, 0, 8, 6, 0, 6, 2, 6, 8, 7, -1, -1, -1, -1},
	{3, 6, 2, 3, 7, 6, 1, 5, 0, 5, 4, 0, -1, -1, -1, -1},
	{6, 2, 8, 6, 8, 7, 2, 1, 8, 4, 8, 5, 1, 5, 8, -1},
	{9, 5, 4, 10, 1, 6, 1, 7, 6, 1, 3, 7, -1, -1, -1, -1},
	{1, 6, 10, 1, 7, 6, 1, 0, 7, 8, 7, 0, 9, 5, 4, -1},
	{4, 0, 10, 4, 10, 5, 0, 3, 10, 6, 10, 7, 3, 7, 10, -1},
	{7, 6, 10, 7, 10, 8, 5, 4, 10, 4, 8, 10, -1, -1, -1, -1},
	{6, 9, 5, 6, 11, 9, 11, 8, 9, -1, -1, -1, -1, -1, -1, -1},
	{3, 6, 11, 0, 6, 3, 0, 5, 6, 0, 9, 5, -1, -1, -1, -1},
	{0, 11, 8, 0, 5, 11, 0, 1, 5, 5, 6, 11, -1, -1, -1, -1},
	{6, 11, 3, 6, 3, 5, 5, 3, 1, -1, -1, -1, -1, -1, -1, -1},
	{1, 2, 10, 9, 5, 11, 9, 11, 8, 11, 5, 6, -1, -1, -1, -1},
	{0, 11, 3, 0, 6, 11, 0, 9, 6, 5, 6, 9, 1, 2, 10, -1},
	{11, 8, 5, 11, 5, 6, 8, 0, 5, 10, 5, 2, 0, 2, 5, -1},
	{6, 11, 3, 6, 3, 5, 2, 10, 3, 10, 5, 3, -1, -1, -1, -1},
	{5, 8, 9, 5, 2, 8, 5, 6, 2, 3, 8, 2, -1, -1, -1, -1},
	{9, 5, 6, 9, 6, 0, 0, 6, 2, -1, -1, -1, -1, -1, -1, -1},
	{1, 5, 8, 1, 8, 0, 5, 6, 8, 3, 8, 2, 6, 2, 8, -1},
	{1, 5, 6, 2, 1, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{1, 3, 6, 1, 6, 10, 3, 8, 6, 5, 6, 9, 8, 9, 6, -1},
	{10, 1, 0, 10, 0, 6, 9, 5, 0, 5, 6, 0, -1, -1, -1, -1},
	{0, 3, 8, 5, 6, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{10, 5, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{11, 5, 10, 7, 5, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{11, 5, 10, 11, 7, 5, 8, 3, 0, -1, -1, -1, -1, -1, -1, -1},
	{5, 11, 7, 5, 10, 11, 1, 9, 0, -1, -1, -1, -1, -1, -1, -1},
	{10, 7, 5, 10, 11, 7, 9, 8, 1, 8, 3, 1, -1, -1, -1, -1},
	{11, 1, 2, 11, 7, 1, 7, 5, 1, -1, -1, -1, -1, -1, -1, -1},
	{0, 8, 3, 1, 2, 7, 1, 7, 5, 7, 2, 11, -1, -1, -1, -1},
	{9, 7, 5, 9, 2, 7, 9, 0, 2, 2, 11, 7, -1, -1, -1, -1},
	{7, 5, 2, 7, 2, 11, 5, 9, 2, 3, 2, 8, 9, 8, 2, -1},
	{2, 5, 10, 2, 3, 5, 3, 7, 5, -1, -1, -1, -1, -1, -1, -1},
	{8, 2, 0, 8, 5, 2, 8, 7, 5, 10, 2, 5, -1, -1, -1, -1},
	{9, 0, 1, 5, 10, 3, 5, 3, 7, 3, 10, 2, -1, -1, -1, -1},
	{9, 8, 2, 9, 2, 1, 8, 7, 2, 10, 2, 5, 7, 5, 2, -1},
	{1, 3, 5, 3, 7, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{0, 8, 7, 0, 7, 1, 1, 7, 5, -1, -1, -1, -1, -1, -1, -1},
	{9, 0, 3, 9, 3, 5, 5, 3, 7, -1, -1, -1, -1, -1, -1, -1},
	{9, 8, 7, 5, 9, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{5, 8, 4, 5, 10, 8, 10, 11, 8, -1, -1, -1, -1, -1, -1, -1},
	{5, 0, 4, 5, 11, 0, 5, 10, 11, 11, 3, 0, -1, -1, -1, -1},
	{0, 1, 9, 8, 4, 10, 8, 10, 11, 10, 4, 5, -1, -1, -1, -1},
	{10, 11, 4, 10, 4, 5, 11, 3, 4, 9, 4, 1, 3, 1, 4, -1},
	{2, 5, 1, 2, 8, 5, 2, 11, 8, 4, 5, 8, -1, -1, -1, -1},
	{0, 4, 11, 0, 11, 3, 4, 5, 11, 2, 11, 1, 5, 1, 11, -1},
	{0, 2, 5, 0, 5, 9, 2, 11, 5, 4, 5, 8, 11, 8, 5, -1},
	{9, 4, 5, 2, 11, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{2, 5, 10, 3, 5, 2, 3, 4, 5, 3, 8, 4, -1, -1, -1, -1},
	{5, 10, 2, 5, 2, 4, 4, 2, 0, -1, -1, -1, -1, -1, -1, -1},
	{3, 10, 2, 3, 5, 10, 3, 8, 5, 4, 5, 8, 0, 1, 9, -1},
	{5, 10, 2, 5, 2, 4, 1, 9, 2, 9, 4, 2, -1, -1, -1, -1},
	{8, 4, 5, 8, 5, 3, 3, 5, 1, -1, -1, -1, -1, -1, -1, -1},
	{0, 4, 5, 1, 0, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{8, 4, 5, 8, 5, 3, 9, 0, 5, 0, 3, 5, -1, -1, -1, -1},
	{9, 4, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{4, 11, 7, 4, 9, 11, 9, 10, 11, -1, -1, -1, -1, -1, -1, -1},
	{0, 8, 3, 4, 9, 7, 9, 11, 7, 9, 10, 11, -1, -1, -1, -1},
	{1, 10, 11, 1, 11, 4, 1, 4, 0, 7, 4, 11, -1, -1, -1, -1},
	{3, 1, 4, 3, 4, 8, 1, 10, 4, 7, 4, 11, 10, 11, 4, -1},
	{4, 11, 7, 9, 11, 4, 9, 2, 11, 9, 1, 2, -1, -1, -1, -1},
	{9, 7, 4, 9, 11, 7, 9, 1, 11, 2, 11, 1, 0, 8, 3, -1},
	{11, 7, 4, 11, 4, 2, 2, 4, 0, -1, -1, -1, -1, -1, -1, -1},
	{11, 7, 4, 11, 4, 2, 8, 3, 4, 3, 2, 4, -1, -1, -1, -1},
	{2, 9, 10, 2, 7, 9, 2, 3, 7, 7, 4, 9, -1, -1, -1, -1},
	{9, 10, 7, 9, 7, 4, 10, 2, 7, 8, 7, 0, 2, 0, 7, -1},
	{3, 7, 10, 3, 10, 2, 7, 4, 10, 1, 10, 0, 4, 0, 10, -1},
	{1, 10, 2, 8, 7, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{4, 9, 1, 4, 1, 7, 7, 1, 3, -1, -1, -1, -1, -1, -1, -1},
	{4, 9, 1, 4, 1, 7, 0, 8, 1, 8, 7, 1, -1, -1, -1, -1},
	{4, 0, 3, 7, 4, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{4, 8, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{9, 10, 8, 10, 11, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{3, 0, 9, 3, 9, 11, 11, 9, 10, -1, -1, -1, -1, -1, -1, -1},
	{0, 1, 10, 0, 10, 8, 8, 10, 11, -1, -1, -1, -1, -1, -1, -1},
	{3, 1, 10, 11, 3, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{1, 2, 11, 1, 11, 9, 9, 11, 8, -1, -1, -1, -1, -1, -1, -1},
	{3, 0, 9, 3, 9, 11, 1, 2, 9, 2, 11, 9, -1, -1, -1, -1},
	{0, 2, 11, 8, 0, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{3, 2, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{2, 3, 8, 2, 8, 10, 10, 8, 9, -1, -1, -1, -1, -1, -1, -1},
	{9, 10, 2, 0, 9, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{2, 3, 8, 2, 8, 10, 0, 1, 8, 1, 10, 8, -1, -1, -1, -1},
	{1, 10, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{1, 3, 8, 9, 1, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{0, 9, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{0, 3, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
	{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}};

UMarchingCubes::UMarchingCubes()
	: MyVoxelChunk(nullptr),
	  DiggerManager(nullptr),
	  VoxelGrid(nullptr)
{
	bHeightCacheInitialized = false;
	CachedVoxelSize = 0.0f;
	CachedChunkSize = 0;
	CachedChunkOrigin = FVector::ZeroVector;
}

UMarchingCubes::UMarchingCubes(const FObjectInitializer& ObjectInitializer, const UVoxelChunk* VoxelChunk)
	: UObject(ObjectInitializer), MyVoxelChunk(VoxelChunk), DiggerManager(nullptr), VoxelGrid(nullptr)
{
    bHeightCacheInitialized = false;
    CachedVoxelSize = 0.0f;
    CachedChunkSize = 0;
    CachedChunkOrigin = FVector::ZeroVector;
}

void UMarchingCubes::Initialize(ADiggerManager* InDiggerManager)
{
	DiggerManager = InDiggerManager;
}

float UMarchingCubes::GetSafeSDFValue(const FIntVector& Position) const {
	if (!FVoxelConversion::IsValidVoxelIndex(Position)) { 
		return 1.0f; 
	}
	const float SDFValue = VoxelGrid->GetVoxel(Position.X, Position.Y, Position.Z);
	return SDFValue;
}

void UMarchingCubes::ValidateAndResizeBuffers( FIntVector& Size, TArray<FVector>& Vertices, TArray<int32>& Triangles) {
	if (!Vertices.IsValidIndex(0) || Vertices.Num() < Size.X * Size.Y * Size.Z * 12) {
		Vertices.Empty();
		Vertices.Reserve(Size.X * Size.Y * Size.Z * 12);
	}
	if (!Triangles.IsValidIndex(0) || Triangles.Num() < Size.X * Size.Y * Size.Z * 15) {
		Triangles.Empty();
		Triangles.Reserve(Size.X * Size.Y * Size.Z * 15);
	}
}

// Re-implementing ApplyLandscapeTransition inside the class for sync usage
FVector UMarchingCubes::ApplyLandscapeTransition(const FVector& VertexWS) const
{
    if (!DiggerManager) return VertexWS;

    float LandscapeZ = DiggerManager->GetLandscapeHeightAt(VertexWS);
    float DistanceToSurface = FMath::Abs(VertexWS.Z - LandscapeZ);

    if (DistanceToSurface < TRANSITION_HEIGHT)
    {
        float Alpha = DistanceToSurface / TRANSITION_HEIGHT;
        float BlendAlpha = FMath::Pow(Alpha, TRANSITION_SHARPNESS);
        float NewZ = FMath::Lerp(LandscapeZ, VertexWS.Z, BlendAlpha);
        FVector Result = VertexWS;
        Result.Z = NewZ;
        return Result;
    }
    return VertexWS;
}

// -------------------------------------------------------------------------
// WRAPPERS (Interface)
// -------------------------------------------------------------------------

void UMarchingCubes::GenerateMesh(UVoxelChunk* Chunk)
{
    if (!Chunk || !Chunk->GetSparseVoxelGrid()) return;

    FVector Origin = FVoxelConversion::ChunkToWorld(Chunk->GetChunkCoordinates());
    float VoxelSize = FVoxelConversion::LocalVoxelSize;
    int32 N = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;

    // 1. Capture Heights (Game Thread)
    TArray<float> LocalHeights = CaptureHeightMap(Origin, VoxelSize, N);

    // 2. Snapshot Data (Game Thread)
    TMap<FIntVector, FVoxelData> DataSnapshot = Chunk->GetSparseVoxelGrid()->VoxelData;

    // 3. Launch Async
    TWeakObjectPtr<UVoxelChunk> WeakChunk(Chunk);
    TWeakObjectPtr<UMarchingCubes> WeakThis(this);

    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, 
        [WeakThis, WeakChunk, Origin, VoxelSize, LocalHeights, DataSnapshot = MoveTemp(DataSnapshot)]()
    {
        if (!WeakThis.IsValid()) return;
        
        TArray<FVector> Verts;
        TArray<int32> Tris;
        TArray<FVector> Normals;

        // Run Math (Worker)
        WeakThis->GenerateMeshFromGrid(
            DataSnapshot, 
            Origin,
            VoxelSize,
            LocalHeights,
            Verts, Tris, Normals
        );

        // Return to Game Thread
        AsyncTask(ENamedThreads::GameThread, [WeakThis, WeakChunk, Verts, Tris, Normals]()
        {
            if (UVoxelChunk* FinalChunk = WeakChunk.Get())
            {
                FinalChunk->UpdateMeshFromData(Verts, Tris, Normals);
                
                if (WeakThis.IsValid() && WeakThis->OnMeshReady.IsBound())
                    WeakThis->OnMeshReady.Execute();
            }
        });
    });
}

void UMarchingCubes::GenerateMeshSyncronous(UVoxelChunk* Chunk)
{
    if (!Chunk || !Chunk->GetSparseVoxelGrid()) return;

    FVector Origin = FVoxelConversion::ChunkToWorld(Chunk->GetChunkCoordinates());
    float VoxelSize = FVoxelConversion::LocalVoxelSize;
    int32 N = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;

    // 1. Capture
    TArray<float> LocalHeights = CaptureHeightMap(Origin, VoxelSize, N);
    
    // 2. Containers
    TArray<FVector> Verts;
    TArray<int32> Tris;
    TArray<FVector> Normals;

    // 3. Run Math
    GenerateMeshFromGrid(
        Chunk->GetSparseVoxelGrid()->VoxelData,
        Origin, VoxelSize, LocalHeights,
        Verts, Tris, Normals
    );

    // 4. Apply
    Chunk->UpdateMeshFromData(Verts, Tris, Normals); 
    if (OnMeshReady.IsBound()) OnMeshReady.Execute();
}

void UMarchingCubes::GenerateMeshForIsland(USparseVoxelGrid* IslandGrid, const FVector& Origin, float VoxelSize, int32 IslandId)
{
    if (!IslandGrid) return;

    TArray<FVector> Verts;
    TArray<int32> Tris;
    TArray<FVector> Normals;

    int32 N = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
    TArray<float> HeightMap = CaptureHeightMap(Origin, VoxelSize, N);

    GenerateMeshFromGrid(
        IslandGrid->VoxelData, 
        Origin, VoxelSize, HeightMap, 
        Verts, Tris, Normals
    );

    if (Verts.Num() > 0) {
        AsyncTask(ENamedThreads::GameThread, [=]() {
            CreateIslandProceduralMesh(Verts, Tris, Normals, Origin, IslandId);
        });
    }
}

// -------------------------------------------------------------------------
// ROUTERS
// -------------------------------------------------------------------------

void UMarchingCubes::GenerateMeshFromGrid(
	USparseVoxelGrid* InVoxelGrid,
	const FVector& Origin,
	float VoxelSize,
	const TArray<float>& HeightMap,
	TArray<FVector>& OutVertices,
	TArray<int32>& OutTriangles,
	TArray<FVector>& OutNormals)
{
	if (!InVoxelGrid) return;
	GenerateMesh_MarchingCubes(
		InVoxelGrid->VoxelData, 
		Origin, VoxelSize, HeightMap, 
		OutVertices, OutTriangles, OutNormals
	);
}

void UMarchingCubes::GenerateMeshFromGrid(
	const TMap<FIntVector, FVoxelData>& VoxelData, 
	const FVector& Origin, 
	float VoxelSize,
	const TArray<float>& HeightMap,
	TArray<FVector>& OutVertices, 
	TArray<int32>& OutTriangles, 
	TArray<FVector>& OutNormals)
{
	GenerateMesh_MarchingCubes(
		VoxelData, 
		Origin, VoxelSize, HeightMap, 
		OutVertices, OutTriangles, OutNormals
	);
}

void UMarchingCubes::GenerateMeshFromGridSyncronous(
	USparseVoxelGrid* InVoxelGrid,
	const FVector& Origin,
	float VoxelSize,
	TArray<FVector>& OutVertices,
	TArray<int32>& OutTriangles,
	TArray<FVector>& OutNormals)
{
	if (!InVoxelGrid) return;

	int32 N = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
	TArray<float> LocalHeights = CaptureHeightMap(Origin, VoxelSize, N);

	GenerateMeshFromGrid(
		InVoxelGrid, 
		Origin, 
		VoxelSize, 
		LocalHeights, 
		OutVertices, 
		OutTriangles, 
		OutNormals
	);
}

// -------------------------------------------------------------------------
// THE WORKER (Refactored to match Old Logic)
// -------------------------------------------------------------------------

void UMarchingCubes::GenerateMesh_MarchingCubes(
    const TMap<FIntVector, FVoxelData>& VoxelData,
    const FVector& Origin,
    float VoxelSize,
    const TArray<float>& HeightValues,
    TArray<FVector>& OutVertices,
    TArray<int32>& OutTriangles,
    TArray<FVector>& OutNormals
)
{
    int32 N = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
    int32 HeightMapWidth = N + 1;

    // Helper: Bilinear interpolation from the captured height map
    auto GetHeightBilinear = [&](const FVector& Pos) -> float 
    {
        FVector Local = Pos - Origin;
        float GX = Local.X / VoxelSize;
        float GY = Local.Y / VoxelSize;
        
        int32 X0 = FMath::Clamp(FMath::FloorToInt(GX), 0, HeightMapWidth - 2);
        int32 Y0 = FMath::Clamp(FMath::FloorToInt(GY), 0, HeightMapWidth - 2);
        int32 X1 = X0 + 1;
        int32 Y1 = Y0 + 1;

        float FracX = GX - X0;
        float FracY = GY - Y0;

        float H00 = HeightValues[Y0 * HeightMapWidth + X0];
        float H10 = HeightValues[Y0 * HeightMapWidth + X1];
        float H01 = HeightValues[Y1 * HeightMapWidth + X0];
        float H11 = HeightValues[Y1 * HeightMapWidth + X1];

        // If any sample is invalid, return invalid
        if (H00 <= (UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT + 1.f)) return H00;

        float H0 = FMath::Lerp(H00, H10, FracX);
        float H1 = FMath::Lerp(H01, H11, FracX);
        return FMath::Lerp(H0, H1, FracY);
    };

    // Helper: Transition Logic (Thread-Safe version)
    auto ApplyWorkerTransition = [&](const FVector& V, float H) -> FVector
    {
        if (H <= (UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT + 1.f)) return V;
        
        float Dist = FMath::Abs(V.Z - H);
        if (Dist < TRANSITION_HEIGHT)
        {
            float Alpha = Dist / TRANSITION_HEIGHT;
            float Blend = FMath::Pow(Alpha, TRANSITION_SHARPNESS);
            float NewZ = FMath::Lerp(H, V.Z, Blend);
            return FVector(V.X, V.Y, NewZ);
        }
        return V;
    };

    FVector TotalOffset = FVector(FVoxelConversion::LocalVoxelSize * 0.25F - FVoxelConversion::ChunkWorldSize * 0.5f);
    TMap<FVector, int32> VertexCache;
    
    // Store landscape normal for snapped vertices to blend later
    TMap<int32, FVector> SnappedVertexNormals;

    // --- VOXEL LOOP ---
    for (int32 x = 0; x < N; ++x)
    {
        for (int32 y = 0; y < N; ++y)
        {
            float TerrainHeight = HeightValues[y * HeightMapWidth + x];

            for (int32 z = 0; z < N; ++z)
            {
                // Optimization: Skip if high above terrain and no explicit data
                if (TerrainHeight > (UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT + 1.0f))
                {
                    FVector CellPos = Origin + FVector(x, y, z) * VoxelSize;
                    if (CellPos.Z > (TerrainHeight + VoxelSize * 2.0f))
                    {
                        bool bHasExplicit = false;
                        for(int i=0; i<8; ++i) {
                            if (VoxelData.Contains(FIntVector(x,y,z) + GetCornerOffset(i))) {
                                bHasExplicit = true; break; 
                            }
                        }
                        if (!bHasExplicit) continue; 
                    }
                }

                FVector CornerWSPositions[8];
                float CornerSDFValues[8];
                bool bAllSolid = true;
                bool bAllAir = true;

                for (int32 i = 0; i < 8; i++) 
                {
                    FIntVector LocalCoord = FIntVector(x, y, z) + GetCornerOffset(i);
                    CornerWSPositions[i] = Origin + FVector(LocalCoord) * VoxelSize;

                    if (const FVoxelData* Data = VoxelData.Find(LocalCoord))
                    {
                        CornerSDFValues[i] = Data->SDFValue;
                    }
                    else
                    {
                        // Implicit logic
                        float CornerH = HeightValues[FMath::Clamp(LocalCoord.Y, 0, HeightMapWidth-1) * HeightMapWidth + FMath::Clamp(LocalCoord.X, 0, HeightMapWidth-1)];

                        if (CornerH <= (UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT + 1.0f))
                        {
                            CornerSDFValues[i] = 1.0f; // Air if no landscape
                        }
                        else
                        {
                            float VerticalDelta = CornerWSPositions[i].Z - CornerH;
                            float BaseSDF = FMath::Clamp(VerticalDelta / (VoxelSize * 2.0f), -1.0f, 1.0f);
                            float FinalSDF = BaseSDF;

                            // Cavity Logic
                            if (BaseSDF < 0.0f)
                            {
                                bool bFoundAir = false;
                                float MinAirDist = FLT_MAX;
                                const float MaxInfluence = 2.0f * VoxelSize;

                                for (int32 dx = -2; dx <= 2; ++dx)
                                for (int32 dy = -2; dy <= 2; ++dy)
                                for (int32 dz = -2; dz <= 2; ++dz)
                                {
                                    if (dx==0 && dy==0 && dz==0) continue;
                                    FIntVector NC = LocalCoord + FIntVector(dx, dy, dz);
                                    const FVoxelData* ND = VoxelData.Find(NC);
                                    
                                    // Only blend if neighbor is explicitly AIR (SDF > 0)
                                    // AND neighbor is physically below terrain (a hole)
                                    if (ND && ND->SDFValue > 0.0f) 
                                    {
                                        FVector NPos = Origin + FVector(NC) * VoxelSize;
                                        float NH = HeightValues[FMath::Clamp(NC.Y, 0, HeightMapWidth-1) * HeightMapWidth + FMath::Clamp(NC.X, 0, HeightMapWidth-1)];
                                        
                                        if (NH > (UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT + 1.0f) && NPos.Z < NH)
                                        {
                                            float Dist = FVector(dx, dy, dz).Size() * VoxelSize;
                                            if (Dist < MinAirDist) { MinAirDist = Dist; bFoundAir = true; }
                                        }
                                    }
                                }

                                if (bFoundAir && MinAirDist < MaxInfluence)
                                {
                                    float t = MinAirDist / MaxInfluence;
                                    float AirSDF = FMath::Lerp(-1.0f, 0.0f, 1.0f - t);
                                    FinalSDF = FMath::Max(BaseSDF, AirSDF);
                                }
                            }
                            CornerSDFValues[i] = FinalSDF;
                        }
                    }

                    if (CornerSDFValues[i] > 0.0f) bAllSolid = false;
                    else bAllAir = false;
                }

                if (bAllSolid || bAllAir) continue;

                int32 CubeIndex = CalculateMarchingCubesIndex(TArray<float>(CornerSDFValues, 8));
                if (CubeIndex == 0 || CubeIndex == 255) continue;

                for (int32 i = 0; TriangleConnectionTable[CubeIndex][i] != -1; i += 3) 
                {
                    FVector TriVerts[3];
                    for (int32 j = 0; j < 3; ++j) 
                    {
                        int32 EdgeIdx = TriangleConnectionTable[CubeIndex][i + j];
                        FVector P1 = CornerWSPositions[EdgeConnection[EdgeIdx][0]];
                        FVector P2 = CornerWSPositions[EdgeConnection[EdgeIdx][1]];
                        float S1 = CornerSDFValues[EdgeConnection[EdgeIdx][0]];
                        float S2 = CornerSDFValues[EdgeConnection[EdgeIdx][1]];

                        FVector Interp = InterpolateVertex(P1, P2, S1, S2);
                        
                        // --- SNAP & TRANSITION LOGIC ---
                        float H = GetHeightBilinear(Interp);
                        bool bSnapped = false;
                        FVector SnappedNormal = FVector::ZeroVector;

                        if (H > (UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT + 1.0f))
                        {
                            // 1. Snap Z if very close to surface (underground)
                            float Depth = H - Interp.Z;
                            if (Depth > 0.001f && Depth < VoxelSize)
                            {
                                Interp.Z = H;
                                bSnapped = true;

                                // Calculate landscape normal via finite difference on the grid
                                float HL = GetHeightBilinear(Interp - FVector(VoxelSize,0,0));
                                float HR = GetHeightBilinear(Interp + FVector(VoxelSize,0,0));
                                float HD = GetHeightBilinear(Interp - FVector(0,VoxelSize,0));
                                float HU = GetHeightBilinear(Interp + FVector(0,VoxelSize,0));
                                float dX = (HR - HL) / (2.0f * VoxelSize);
                                float dY = (HU - HD) / (2.0f * VoxelSize);
                                
                                // Store negated normal (as we invert mesh normals later)
                                SnappedNormal = -FVector(-dX, -dY, 1.0f).GetSafeNormal();
                            }

                            // 2. Apply Transition Blend
                            Interp = ApplyWorkerTransition(Interp, H);
                        }
                        
                        TriVerts[j] = Interp + TotalOffset;

                        // Cache & Add
                        int32* CacheIdx = VertexCache.Find(TriVerts[j]);
                        if (CacheIdx) {
                            OutTriangles.Add(*CacheIdx);
                            if (bSnapped) SnappedVertexNormals.Add(*CacheIdx, SnappedNormal);
                        } else {
                            int32 NewIdx = OutVertices.Add(TriVerts[j]);
                            VertexCache.Add(TriVerts[j], NewIdx);
                            OutTriangles.Add(NewIdx);
                            if (bSnapped) SnappedVertexNormals.Add(NewIdx, SnappedNormal);
                        }
                    }
                }
            }
        }
    }

    // --- NORMALS ---
    OutNormals.SetNum(OutVertices.Num());
    for(auto& NVal : OutNormals) NVal = FVector::ZeroVector;

    for (int32 i = 0; i < OutTriangles.Num(); i += 3) {
        FVector v0 = OutVertices[OutTriangles[i]];
        FVector v1 = OutVertices[OutTriangles[i+1]];
        FVector v2 = OutVertices[OutTriangles[i+2]];
        
        FVector FaceNormal = FVector::CrossProduct(v1 - v0, v2 - v0);
        OutNormals[OutTriangles[i]] += FaceNormal;
        OutNormals[OutTriangles[i+1]] += FaceNormal;
        OutNormals[OutTriangles[i+2]] += FaceNormal;
    }

    for (int32 i = 0; i < OutNormals.Num(); ++i) {
        FVector& NVal = OutNormals[i];
        NVal.Normalize();
        NVal = -NVal; // Flip to face correct direction

        // Blend with landscape normal if applicable
        if (FVector* LandNormal = SnappedVertexNormals.Find(i))
        {
            NVal = FMath::Lerp(NVal, *LandNormal, LANDSCAPE_BLEND_WEIGHT).GetSafeNormal();
        }
    }
}


void UMarchingCubes::InitializeHeightCache(const FVector& ChunkOrigin, float VoxelSize)
{
	if (!DiggerManager)
	{
		if (DiggerDebug::Manager())
		UE_LOG(LogTemp, Warning, TEXT("DiggerManager is null, cannot initialize height cache"));
		return;
	}

	// Clear existing cache
	HeightCache.Empty();
    
	int32 N = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
    
	// Add padding around the chunk for smooth interpolation at edges
	int32 Padding = 3;
	int32 TotalSize = N + (Padding * 2);
    
	FVector ChunkMin = ChunkOrigin - FVector(N * VoxelSize * 0.5f);
	FVector SampleStart = ChunkMin - FVector(Padding * VoxelSize);

	if (DiggerDebug::Chunks() || DiggerDebug::Landscape())
	UE_LOG(LogTemp, Log, TEXT("Initializing height cache for chunk at %s with %dx%d samples"), 
		   *ChunkOrigin.ToString(), TotalSize, TotalSize);
    
	// Sample heights across the extended grid
	for (int32 x = 0; x < TotalSize; ++x)
	{
		for (int32 y = 0; y < TotalSize; ++y)
		{
			FVector SamplePos = SampleStart + FVector(x * VoxelSize, y * VoxelSize, 0);
			float Height = DiggerManager->GetLandscapeHeightAt(SamplePos);
			FIntVector GridKey(x, y, 0);
			HeightCache.Add(GridKey, Height);
		}
	}
    
	CachedChunkOrigin = ChunkOrigin;
	CachedVoxelSize = VoxelSize;
	CachedChunkSize = N;
	bHeightCacheInitialized = true;

	if (DiggerDebug::Landscape())
	UE_LOG(LogTemp, Log, TEXT("Height cache initialized with %d entries"), HeightCache.Num());
}

float UMarchingCubes::GetCachedHeight(const FVector& WorldPosition) const
{
	if (!bHeightCacheInitialized)
	{
		if (DiggerDebug::Landscape())
		UE_LOG(LogTemp, Warning, TEXT("Height cache not initialized!"));
		return 0.0f;
	}
    
	FVector RelativePos = WorldPosition - CachedChunkOrigin;
	float GridX = RelativePos.X / CachedVoxelSize;
	float GridY = RelativePos.Y / CachedVoxelSize;
    
	GridX += (CachedChunkSize * 0.5f);
	GridY += (CachedChunkSize * 0.5f);
    
	int32 X0 = FMath::FloorToInt(GridX);
	int32 Y0 = FMath::FloorToInt(GridY);
	int32 X1 = X0 + 1;
	int32 Y1 = Y0 + 1;
    
	float FracX = GridX - X0;
	float FracY = GridY - Y0;
    
	float H00 = HeightCache.FindRef(FIntVector(X0, Y0, 0));
	float H10 = HeightCache.FindRef(FIntVector(X1, Y0, 0));
	float H01 = HeightCache.FindRef(FIntVector(X0, Y1, 0));
	float H11 = HeightCache.FindRef(FIntVector(X1, Y1, 0));
    
	float H0 = FMath::Lerp(H00, H10, FracX);
	float H1 = FMath::Lerp(H01, H11, FracX);
	float FinalHeight = FMath::Lerp(H0, H1, FracY);
    
	return FinalHeight;
}

void UMarchingCubes::ClearHeightCache()
{
    HeightCache.Empty();
    bHeightCacheInitialized = false;
    CachedChunkOrigin = FVector::ZeroVector;
    CachedVoxelSize = 0.0f;
    CachedChunkSize = 0;
}

bool UMarchingCubes::IsHeightCacheValid(const FVector& ChunkOrigin, float VoxelSize) const
{
    return bHeightCacheInitialized && 
           CachedChunkOrigin.Equals(ChunkOrigin, 0.1f) && 
           FMath::IsNearlyEqual(CachedVoxelSize, VoxelSize, 0.001f);
}

void UMarchingCubes::ClearSectionAndRebuildMesh(int32 SectionIndex, FIntVector ChunkCoord)
{
	if (!DiggerManager) return;

	if (DiggerManager->ProceduralMesh->GetNumSections() > SectionIndex) {
		DiggerManager->ProceduralMesh->ClearMeshSection(SectionIndex);
	}
}

void UMarchingCubes::CreateIslandProceduralMesh(
	const TArray<FVector>& Vertices,
	const TArray<int32>& Triangles,
	const TArray<FVector>& Normals,
	const FVector& Origin,
	int32 IslandId
)
{
	if (!DiggerManager) {
		if (DiggerDebug::Manager())
		UE_LOG(LogTemp, Error, TEXT("DiggerManager is null in CreateIslandProceduralMesh"));
		return;
	}

	FString MeshName = FString::Printf(TEXT("IslandMesh_%d"), IslandId);
	UProceduralMeshComponent* IslandMesh = NewObject<UProceduralMeshComponent>(DiggerManager, *MeshName);
	if (!IslandMesh) {
		if (DiggerDebug::Islands())
		UE_LOG(LogTemp, Error, TEXT("Failed to create IslandMeshComponent"));
		return;
	}

	IslandMesh->RegisterComponent();
	IslandMesh->AttachToComponent(DiggerManager->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	IslandMesh->SetRelativeLocation(Origin);

	TArray<FVector2D> UVs;
	TArray<FColor> VertexColors;
	TArray<FProcMeshTangent> Tangents;

	IslandMesh->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, VertexColors, Tangents, true);

	IslandMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	IslandMesh->SetCollisionObjectType(ECC_WorldDynamic);
	IslandMesh->SetCollisionResponseToAllChannels(ECR_Block);
	IslandMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	IslandMesh->bUseComplexAsSimpleCollision = true;

	if (DiggerManager->GetTerrainMaterial()) {
		IslandMesh->SetMaterial(0, DiggerManager->GetTerrainMaterial());
	}

	if (DiggerDebug::Islands())
	UE_LOG(LogTemp, Log, TEXT("Island mesh %d created at origin %s with %d vertices."), IslandId, *Origin.ToString(), Vertices.Num());

	DiggerManager->IslandMeshes.Add(IslandMesh);
}

void UMarchingCubes::ReconstructMeshSection(int32 SectionIndex, const TArray<FVector>& OutOutVertices, const TArray<int32>& OutTriangles, const TArray<FVector>& Normals) const {
    if (!DiggerManager || !DiggerManager->ProceduralMesh) {
    	if (DiggerDebug::Manager() || DiggerDebug::Mesh())
        UE_LOG(LogTemp, Error, TEXT("DiggerManager or ProceduralMesh is null in ReconstructMeshSection"));
        return;
    }

    if (SectionIndex < 0) {
    	if (DiggerDebug::Manager() || DiggerDebug::Mesh())
        UE_LOG(LogTemp, Error, TEXT("Invalid SectionIndex in ReconstructMeshSection: %d"), SectionIndex);
        return;
    }

    if (OutOutVertices.Num() == 0 || OutTriangles.Num() == 0 || Normals.Num() == 0) {
    	if (DiggerDebug::Manager() || DiggerDebug::Mesh())
        UE_LOG(LogTemp, Error, TEXT("Empty mesh data in ReconstructMeshSection"));
        return;
    }

    TArray<FVector2D> UVs;
    TArray<FColor> VertexColors;
    TArray<FProcMeshTangent> Tangents;

    if (DiggerManager->ProceduralMesh->GetNumSections() > SectionIndex) {
        DiggerManager->ProceduralMesh->ClearMeshSection(SectionIndex);
    }
	if (IsDebugging())
	{
		UE_LOG(LogTemp, Warning, TEXT("Creating Mesh Section %d"), SectionIndex);
	}
    DiggerManager->ProceduralMesh->CreateMeshSection(
        SectionIndex,
        OutOutVertices,
        OutTriangles,
        Normals,
        UVs,
        VertexColors,
        Tangents,
        true 
    );

	UProceduralMeshComponent* Mesh = DiggerManager->ProceduralMesh;

	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetCollisionObjectType(ECC_WorldDynamic);
	Mesh->SetCollisionResponseToAllChannels(ECR_Block);
	Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Mesh->bUseComplexAsSimpleCollision = true;

	DiggerManager->ProceduralMesh->SetMaterial(SectionIndex, DiggerManager->GetTerrainMaterial());

	if (OnMeshReady.IsBound())
	{
		OnMeshReady.Execute();
	}
}

// Modify InterpolateVertex to work directly in world space
FVector UMarchingCubes::InterpolateVertex(const FVector& P1, const FVector& P2, float SDF1, float SDF2)
{
	if (FMath::Abs(SDF1 - SDF2) < KINDA_SMALL_NUMBER)
	{
		return (P1 + P2) * 0.5f; 
	}

	float T = SDF1 / (SDF1 - SDF2);
	return FMath::Lerp(P1, P2, T); 
}

TArray<float> UMarchingCubes::CaptureHeightMap(const FVector& Origin, float VoxelSize, int32 GridResolution)
{
	int32 SampleSize = GridResolution + 1; 
    
	TArray<float> Heights;
	Heights.SetNumUninitialized(SampleSize * SampleSize);

	if (DiggerManager)
	{
		for (int32 y = 0; y < SampleSize; ++y) 
		{
			for (int32 x = 0; x < SampleSize; ++x) 
			{
				FVector ColumnPos = Origin + FVector(x * VoxelSize, y * VoxelSize, 0);
				Heights[y * SampleSize + x] = DiggerManager->GetLandscapeHeightAt(ColumnPos);
			}
		}
	}
	else
	{
		for (float& Val : Heights) Val = 0.0f;
	}

	return Heights;
}

int32 UMarchingCubes::GetVertexIndex(const FVector& Vertex, TMap<FVector, int32>& VertexMap, TArray<FVector>& OutOutVertices)
{
    if (int32* Index = VertexMap.Find(Vertex))
    {
        return *Index; 
    }

    int32 NewIndex = OutOutVertices.Add(Vertex);
    VertexMap.Add(Vertex, NewIndex);
    return NewIndex;
}

int32 UMarchingCubes::CalculateMarchingCubesIndex(const TArray<float>& CornerSDFValues)
{
	int32 CubeIndex = 0;
	for (int32 i = 0; i < 8; i++) 
	{
		if (CornerSDFValues[i] < 0.0f) 
		{
			CubeIndex |= (1 << i); 
		}
	}
	return CubeIndex;
}