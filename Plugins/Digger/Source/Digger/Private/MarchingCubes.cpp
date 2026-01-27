#include "MarchingCubes.h"
#include "DiggerManager.h"
#include "VoxelChunk.h"
#include "SparseVoxelGrid.h"
#include "VoxelConversion.h"
#include "DiggerLandscapeCache.h"
#include "ProceduralMeshComponent.h"
#include "Async/Async.h"

// ----------------------------------------------------------------------------------
// CONSTANTS & TABLES
// ----------------------------------------------------------------------------------

// 8 Corners of a cube
const FVector CornerPositions[8] = {
	FVector(0, 0, 0), FVector(1, 0, 0), FVector(1, 1, 0), FVector(0, 1, 0),
	FVector(0, 0, 1), FVector(1, 0, 1), FVector(1, 1, 1), FVector(0, 1, 1)
};

// 12 Edges (Pairs of indices into CornerPositions)
const int EdgeConnection[12][2] = {
	{0,1}, {1,2}, {2,3}, {3,0},
	{4,5}, {5,6}, {6,7}, {7,4},
	{0,4}, {1,5}, {2,6}, {3,7}
};

// Constants for transition (Adjust these to match your header/preferences)
static const float TRANSITION_HEIGHT = 100.0f; 
static const float TRANSITION_SHARPNESS = 2.0f;
static const float LANDSCAPE_BLEND_WEIGHT = 0.85f;


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

// Local implementation of offset getter
FIntVector UMarchingCubes::GetCornerOffset(int32 Index)
{
	static const FIntVector Offsets[8] = {
		FIntVector(0, 0, 0), FIntVector(1, 0, 0), FIntVector(1, 1, 0), FIntVector(0, 1, 0),
		FIntVector(0, 0, 1), FIntVector(1, 0, 1), FIntVector(1, 1, 1), FIntVector(0, 1, 1)
	};
	if (Index >= 0 && Index < 8) return Offsets[Index];
	return FIntVector::ZeroValue;
}


// ----------------------------------------------------------------------------------
// LIFECYCLE
// ----------------------------------------------------------------------------------

UMarchingCubes::UMarchingCubes()
	: bHeightCacheInitialized(false), CachedVoxelSize(0), CachedChunkSize(0), MyVoxelChunk(nullptr)
	  , DiggerManager(nullptr)
{
}

UMarchingCubes::UMarchingCubes(const FObjectInitializer& ObjectInitializer, const UVoxelChunk* VoxelChunk)
	: UObject(ObjectInitializer)
	  , bHeightCacheInitialized(false), CachedVoxelSize(0), CachedChunkSize(0), MyVoxelChunk(VoxelChunk)
	  , DiggerManager(nullptr)
{
}

void UMarchingCubes::Initialize(ADiggerManager* InDiggerManager)
{
	DiggerManager = InDiggerManager;
}

// ----------------------------------------------------------------------------------
// MAIN API (ROUTERS)
// ----------------------------------------------------------------------------------

void UMarchingCubes::GenerateMesh(UVoxelChunk* Chunk)
{
	// This is the Entry Point called by the Chunk.
	// In your architecture, the Chunk handles the Async Dispatch (VoxelChunk.cpp).
	// So if this is called, it usually implies a Direct/Sync request or a re-route.
	
	if (!Chunk) return;
	GenerateMeshSyncronous(Chunk);
}

void UMarchingCubes::GenerateMeshSyncronous(UVoxelChunk* Chunk)
{
	if (!Chunk) return;
	if (!DiggerManager) DiggerManager = Chunk->GetDiggerManager();

	USparseVoxelGrid* Grid = Chunk->GetSparseVoxelGrid();
	if (!Grid) return;

	// 1. Setup Data
	FVector Origin = FVoxelConversion::ChunkToWorld(Chunk->GetChunkCoordinates());
	float VoxelSize = FVoxelConversion::LocalVoxelSize;
	
	// 2. Containers
	TArray<FVector> Verts;
	TArray<int32> Tris;
	TArray<FVector> Normals;

	// 3. Run Logic (Sync) via the Pointer Overload
	GenerateMeshFromGridSyncronous(Grid, Origin, VoxelSize, Verts, Tris, Normals);

	// 4. Apply
	if (Verts.Num() > 0)
	{
		Chunk->UpdateMeshFromData(Verts, Tris, Normals);
	}
	else
	{
		// Clear mesh if empty
		Chunk->UpdateMeshFromData({}, {}, {});
	}
}

// ----------------------------------------------------------------------------------
// LOW LEVEL API (HANDLERS)
// ----------------------------------------------------------------------------------

void UMarchingCubes::GenerateMeshFromGridSyncronous(
	USparseVoxelGrid* InVoxelGrid,
	const FVector& Origin,
	float VoxelSize,
	TArray<FVector>& OutVertices,
	TArray<int32>& OutTriangles,
	TArray<FVector>& OutNormals
)
{
	if (!InVoxelGrid) return;

	// 1. Capture Heights (Main Thread Safe)
	int32 N = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
	TArray<float> Heights = CaptureHeightMap(Origin, VoxelSize, N);

	// 2. Delegate to the Map-based Worker (This ensures consistent logic with Async)
	GenerateMeshFromGrid(
		InVoxelGrid->VoxelData, // Use the grid's internal map
		Origin, 
		VoxelSize, 
		Heights, 
		OutVertices, 
		OutTriangles, 
		OutNormals
	);
}

void UMarchingCubes::GenerateMeshFromGrid(
	USparseVoxelGrid* InVoxelGrid,
	const FVector& Origin,
	float VoxelSize,
	const TArray<float>& HeightValues,
	TArray<FVector>& OutVertices,
	TArray<int32>& OutTriangles,
	TArray<FVector>& OutNormals
)
{
	if (!InVoxelGrid) return;
	// Forward to the Map version
	GenerateMeshFromGrid(InVoxelGrid->VoxelData, Origin, VoxelSize, HeightValues, OutVertices, OutTriangles, OutNormals);
}

void UMarchingCubes::GenerateMeshFromGrid(
	const TMap<FIntVector, FVoxelData>& VoxelData,
	const FVector& Origin,
	float VoxelSize,
	const TArray<float>& HeightValues,
	TArray<FVector>& OutVertices,
	TArray<int32>& OutTriangles,
	TArray<FVector>& OutNormals
)
{
	// This is the wrapper that calls the heavy logic
	GenerateMesh_MarchingCubes(VoxelData, Origin, VoxelSize, HeightValues, OutVertices, OutTriangles, OutNormals);
}

// ----------------------------------------------------------------------------------
// THE CORE WORKER (LOGIC ENGINE)
// ----------------------------------------------------------------------------------

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
    // 1. Setup Dimensions
    int32 N = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
    int32 HeightMapWidth = N + 1;

    FVector TotalOffset = FVector::ZeroVector; 

    // 2. Vertex Caching
    TMap<FVector, int32> VertexCache;
    VertexCache.Reserve(N * N * N / 4);

    // 3. Lambda: Safe Height Lookup
    auto GetHeightAt = [&](const FVector& Pos) -> float
    {
        FVector Local = Pos - Origin;
        float GX = Local.X / VoxelSize;
        float GY = Local.Y / VoxelSize;

        int32 X0 = FMath::Clamp(FMath::FloorToInt(GX), 0, N);
        int32 Y0 = FMath::Clamp(FMath::FloorToInt(GY), 0, N);
        int32 X1 = FMath::Min(X0 + 1, N);
        int32 Y1 = FMath::Min(Y0 + 1, N);

        if (Y0 * HeightMapWidth + X0 >= HeightValues.Num())
            return UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT;

        float H00 = HeightValues[Y0 * HeightMapWidth + X0];
        float H10 = HeightValues[Y0 * HeightMapWidth + X1];
        float H01 = HeightValues[Y1 * HeightMapWidth + X0];
        float H11 = HeightValues[Y1 * HeightMapWidth + X1];

        if (H00 < (UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT + 1.0f))
            return UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT;
        
        float LerpX1 = FMath::Lerp(H00, H10, GX - (float)X0);
        float LerpX2 = FMath::Lerp(H01, H11, GX - (float)X0);
        return FMath::Lerp(LerpX1, LerpX2, GY - (float)Y0);
    };

    // 4. Main Loop
    for (int32 x = 0; x < N; ++x)
    {
        for (int32 y = 0; y < N; ++y)
        {
            for (int32 z = 0; z < N; ++z)
            {
                FVector CornerWSPositions[8];
                float CornerSDFValues[8];
                bool bAllSolid = true;
                bool bAllAir = true;

                // --- OPTIMIZATION FLAG ---
                bool bHasExplicitInteraction = false;

                // Identify if we are on the edge of the chunk
                bool bIsBoundary =
                    (x == 0 || x == N - 1 ||
                     y == 0 || y == N - 1 ||
                     z == 0 || z == N - 1);

                // Process 8 corners
                for (int32 i = 0; i < 8; i++)
                {
                    FIntVector LocalCoord = FIntVector(x, y, z) + GetCornerOffset(i);
                    CornerWSPositions[i] = Origin + FVector(LocalCoord) * VoxelSize;

                    // --- LOGIC: HYBRID TERRAIN ---
                    if (const FVoxelData* Data = VoxelData.Find(LocalCoord))
                    {
                        // Explicit user modification (Hole/Mound)
                        CornerSDFValues[i] = Data->SDFValue;
                        bHasExplicitInteraction = true;
                    }
                    else
                    {
                        // Implicit Landscape (The Seal)
                        float H = GetHeightAt(CornerWSPositions[i]);

                        if (H <= (UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT + 1.0f))
                        {
                            CornerSDFValues[i] = -1.0f; // Infinite bedrock
                        }
                        else
                        {
                            float Dist = CornerWSPositions[i].Z - H;

                            // Tighter clamp for ghost landscape
                            CornerSDFValues[i] = FMath::Clamp(Dist / VoxelSize, -1.0f, 1.0f);
                        }
                    }

                    if (CornerSDFValues[i] > 0.0f)
                        bAllSolid = false;
                    else
                        bAllAir = false;
                }

                // --- FIXED OPTIMIZATION ---
                // Only skip if there is no user data AND we are not on a boundary.
                if (!bHasExplicitInteraction && !bIsBoundary)
                    continue;

                if (bAllSolid || bAllAir)
                    continue;

                int32 CubeIndex = CalculateMarchingCubesIndex(TArray<float>(CornerSDFValues, 8));
                if (CubeIndex == 0 || CubeIndex == 255)
                    continue;

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

                        TriVerts[j] = Interp + TotalOffset;

                        int32* CacheIdx = VertexCache.Find(TriVerts[j]);
                        if (CacheIdx)
                        {
                            OutTriangles.Add(*CacheIdx);
                        }
                        else
                        {
                            int32 NewIdx = OutVertices.Add(TriVerts[j]);
                            VertexCache.Add(TriVerts[j], NewIdx);
                            OutTriangles.Add(NewIdx);
                        }
                    }
                }
            }
        }
    }

    // 5. Generate Normals
    OutNormals.SetNumZeroed(OutVertices.Num());
    for (int32 i = 0; i < OutTriangles.Num(); i += 3)
    {
        int32 i0 = OutTriangles[i];
        int32 i1 = OutTriangles[i + 1];
        int32 i2 = OutTriangles[i + 2];

        FVector Edge1 = OutVertices[i1] - OutVertices[i0];
        FVector Edge2 = OutVertices[i2] - OutVertices[i0];
        FVector FaceNormal = FVector::CrossProduct(Edge2, Edge1);
        
        OutNormals[i0] += FaceNormal;
        OutNormals[i1] += FaceNormal;
        OutNormals[i2] += FaceNormal;
    }

    for (FVector& Normal : OutNormals)
    {
        Normal.Normalize();
    }
}


// ----------------------------------------------------------------------------------
// HELPERS
// ----------------------------------------------------------------------------------

TArray<float> UMarchingCubes::CaptureHeightMap(const FVector& Origin, float VoxelSize, int32 GridResolution)
{
	int32 SampleSize = GridResolution + 1;
	TArray<float> Heights;
	Heights.SetNumUninitialized(SampleSize * SampleSize);

	if (DiggerManager)
	{
		// We use ParallelFor here if capturing is heavy, but GameThread requirement usually limits us.
		// Keeping it simple loop for safety on Game Thread.
		for (int32 y = 0; y < SampleSize; ++y)
		{
			for (int32 x = 0; x < SampleSize; ++x)
			{
				FVector ColumnPos = Origin + FVector(x * VoxelSize, y * VoxelSize, 0);
				// Call the fast/cached getter from Manager
				Heights[y * SampleSize + x] = DiggerManager->GetLandscapeHeightAt(ColumnPos);
			}
		}
	}
	else
	{
		// Fallback if no manager
		for (float& Val : Heights) Val = UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT;
	}

	return Heights;
}

FVector UMarchingCubes::InterpolateVertex(const FVector& P1, const FVector& P2, float SDF1, float SDF2)
{
	if (FMath::Abs(SDF1 - SDF2) < KINDA_SMALL_NUMBER)
	{
		return (P1 + P2) * 0.5f;
	}
	float t = (0.0f - SDF1) / (SDF2 - SDF1);
	return FMath::Lerp(P1, P2, t);
}

int32 UMarchingCubes::CalculateMarchingCubesIndex(const TArray<float>& CornerSDFValues)
{
	int32 CubeIndex = 0;
	for (int32 i = 0; i < 8; i++)
	{
		if (CornerSDFValues[i] < 0.0f) CubeIndex |= (1 << i);
	}
	return CubeIndex;
}

FVector UMarchingCubes::ApplyLandscapeTransition(const FVector& VertexWS) const
{
	if (!DiggerManager) return VertexWS;

	// Use GetHeight here again (note: this is less efficient inside the loop than checking pre-calc, 
	// but strictly robust). For optimization, you might pass the height map into this func.
	// For now, robustness first.
	float LandscapeZ = DiggerManager->GetLandscapeHeightAt(VertexWS);
	
	if (LandscapeZ <= (UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT + 1.0f)) return VertexWS;

	float DistanceToSurface = FMath::Abs(VertexWS.Z - LandscapeZ);

	if (DistanceToSurface < TransitionHeight)
	{
		float Alpha = DistanceToSurface / TransitionHeight;
		float BlendAlpha = FMath::Pow(Alpha, TransitionSharpness);
		float NewZ = FMath::Lerp(LandscapeZ, VertexWS.Z, BlendAlpha);
		FVector Result = VertexWS;
		Result.Z = NewZ;
		return Result;
	}
	return VertexWS;
}

// ----------------------------------------------------------------------------------
// MESH RECONSTRUCTION (GAME THREAD)
// ----------------------------------------------------------------------------------

void UMarchingCubes::ReconstructMeshSection(int32 SectionIndex, const TArray<FVector>& OutVertices, const TArray<int32>& OutTriangles, const TArray<FVector>& Normals) const 
{
	if (!DiggerManager || !DiggerManager->ProceduralMesh) return;
	if (SectionIndex < 0) return;
	if (OutVertices.Num() == 0) return;

	// Prepare buffers
	TArray<FVector2D> UVs; // Can implement triplanar projection here later
	TArray<FColor> Colors;
	TArray<FProcMeshTangent> Tangents;

	// Update PMC
	DiggerManager->ProceduralMesh->CreateMeshSection(
		SectionIndex,
		OutVertices,
		OutTriangles,
		Normals,
		UVs,
		Colors,
		Tangents,
		true // Enable Collision
	);

	// Ensure Collision & Material
	DiggerManager->ProceduralMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	if (DiggerManager->GetTerrainMaterial())
	{
		DiggerManager->ProceduralMesh->SetMaterial(SectionIndex, DiggerManager->GetTerrainMaterial());
	}

	// Callback
	if (OnMeshReady.IsBound())
	{
		OnMeshReady.Execute();
	}
}

// ----------------------------------------------------------------------------------
// ISLAND GENERATION
// ----------------------------------------------------------------------------------

void UMarchingCubes::GenerateMeshForIsland(USparseVoxelGrid* IslandGrid, const FVector& Origin, float VoxelSize, int32 IslandId)
{
	if (!IslandGrid) return;

	TArray<FVector> Verts;
	TArray<int32> Tris;
	TArray<FVector> Normals;

	// 1. Capture Heights
	int32 N = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
	TArray<float> Heights = CaptureHeightMap(Origin, VoxelSize, N);

	// 2. Generate
	GenerateMeshFromGrid(IslandGrid->VoxelData, Origin, VoxelSize, Heights, Verts, Tris, Normals);

	// 3. Create Actor (Game Thread)
	if (Verts.Num() > 0)
	{
		AsyncTask(ENamedThreads::GameThread, [=]()
		{
			CreateIslandProceduralMesh(Verts, Tris, Normals, Origin, IslandId);
		});
	}
}

void UMarchingCubes::CreateIslandProceduralMesh(const TArray<FVector>& Vertices, const TArray<int32>& Triangles, const TArray<FVector>& Normals, const FVector& Origin, int32 IslandId)
{
	if (!DiggerManager) return;

	FString MeshName = FString::Printf(TEXT("IslandMesh_%d"), IslandId);
	UProceduralMeshComponent* IslandMesh = NewObject<UProceduralMeshComponent>(DiggerManager, *MeshName);
	if (!IslandMesh) return;

	IslandMesh->RegisterComponent();
	IslandMesh->AttachToComponent(DiggerManager->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	IslandMesh->SetRelativeLocation(Origin);

	IslandMesh->CreateMeshSection(0, Vertices, Triangles, Normals, {}, {}, {}, true);
	IslandMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

	if (DiggerManager->GetTerrainMaterial())
	{
		IslandMesh->SetMaterial(0, DiggerManager->GetTerrainMaterial());
	}

	DiggerManager->IslandMeshes.Add(IslandMesh);
}

// ----------------------------------------------------------------------------------
// HEIGHT CACHE UTILS
// ----------------------------------------------------------------------------------

void UMarchingCubes::InitializeHeightCache(const FVector& ChunkOrigin, float VoxelSize)
{
	// Legacy / Helper if needed for other ops, though GenerateMesh now handles its own capture.
	// Implementation matches previous versions.
	bHeightCacheInitialized = true;
	CachedChunkOrigin = ChunkOrigin;
	CachedVoxelSize = VoxelSize;
	// Populate Map logic if specifically requested by other systems
}

float UMarchingCubes::GetCachedHeight(const FVector& WorldPosition) const
{
	// Legacy accessor - redirects to Manager for precision if cache not manually built
	if (DiggerManager) return DiggerManager->GetLandscapeHeightAt(WorldPosition);
	return UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT;
}

void UMarchingCubes::ClearHeightCache()
{
	HeightCache.Empty();
	bHeightCacheInitialized = false;
}

bool UMarchingCubes::IsHeightCacheValid(const FVector& ChunkOrigin, float VoxelSize) const
{
	// Simple check, mostly used to trigger refresh
	return bHeightCacheInitialized && CachedChunkOrigin.Equals(ChunkOrigin, 1.0f);
}

void UMarchingCubes::ClearSectionAndRebuildMesh(int32 SectionIndex, FIntVector ChunkCoord)
{
	if (DiggerManager && DiggerManager->ProceduralMesh)
	{
		DiggerManager->ProceduralMesh->ClearMeshSection(SectionIndex);
	}
}