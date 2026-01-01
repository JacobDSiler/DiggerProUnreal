#include "MarchingCubes.h"
#include "DiggerManager.h"
#include "VoxelChunk.h"
#include "SparseVoxelGrid.h"
#include "EngineUtils.h"
#include "StaticMeshOperations.h"
#include "UDynamicMesh.h"
#include "Async/Async.h"



FIntVector UMarchingCubes::GetCornerOffset(int32 Index)
{
	static const FIntVector Offsets[8] = {
		FIntVector(0, 0, 0),
		FIntVector(1, 0, 0),
		FIntVector(1, 1, 0),
		FIntVector(0, 1, 0),
		FIntVector(0, 0, 1),
		FIntVector(1, 0, 1),
		FIntVector(1, 1, 1),
		FIntVector(0, 1, 1)
	};
	return Offsets[Index];
}

const FVector CornerPositions[8] = {
	FVector(0.0f, 0.0f, 0.0f), // Vertex 0: (0, 0, 0)
	FVector(1.0f, 0.0f, 0.0f), // Vertex 1: (1, 0, 0)
	FVector(1.0f, 1.0f, 0.0f), // Vertex 2: (1, 1, 0)
	FVector(0.0f, 1.0f, 0.0f), // Vertex 3: (0, 1, 0)
	FVector(0.0f, 0.0f, 1.0f), // Vertex 4: (0, 0, 1)
	FVector(1.0f, 0.0f, 1.0f), // Vertex 5: (1, 0, 1)
	FVector(1.0f, 1.0f, 1.0f), // Vertex 6: (1, 1, 1)
	FVector(0.0f, 1.0f, 1.0f)  // Vertex 7: (0, 1, 1)
};



const int EdgeConnection[12][2] = {
	{0, 1}, // Edge 0
	{1, 2}, // Edge 1
	{2, 3}, // Edge 2
	{3, 0}, // Edge 3
	{4, 5}, // Edge 4
	{5, 6}, // Edge 5
	{6, 7}, // Edge 6
	{7, 4}, // Edge 7
	{0, 4}, // Edge 8
	{1, 5}, // Edge 9
	{2, 6}, // Edge 10
	{3, 7}  // Edge 11
};


const float EdgeDirection[12][3] = {
	{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}};

const int CubeEdgeFlags[256] = {
	0x000, 0x109, 0x203, 0x30a, 0x406, 0x50f, 0x605, 0x70c, 0x80c, 0x905, 0xa0f, 0xb06, 0xc0a, 0xd03, 0xe09, 0xf00,
	0x190, 0x099, 0x393, 0x29a, 0x596, 0x49f, 0x795, 0x69c, 0x99c, 0x895, 0xb9f, 0xa96, 0xd9a, 0xc93, 0xf99, 0xe90,
	0x230, 0x339, 0x033, 0x13a, 0x636, 0x73f, 0x435, 0x53c, 0xa3c, 0xb35, 0x83f, 0x936, 0xe3a, 0xf33, 0xc39, 0xd30,
	0x3a0, 0x2a9, 0x1a3, 0x0aa, 0x7a6, 0x6af, 0x5a5, 0x4ac, 0xbac, 0xaa5, 0x9af, 0x8a6, 0xfaa, 0xea3, 0xda9, 0xca0,
	0x460, 0x569, 0x663, 0x76a, 0x066, 0x16f, 0x265, 0x36c, 0xc6c, 0xd65, 0xe6f, 0xf66, 0x86a, 0x963, 0xa69, 0xb60,
	0x5f0, 0x4f9, 0x7f3, 0x6fa, 0x1f6, 0x0ff, 0x3f5, 0x2fc, 0xdfc, 0xcf5, 0xfff, 0xef6, 0x9fa, 0x8f3, 0xbf9, 0xaf0,
	0x650, 0x759, 0x453, 0x55a, 0x256, 0x35f, 0x055, 0x15c, 0xe5c, 0xf55, 0xc5f, 0xd56, 0xa5a, 0xb53, 0x859, 0x950,
	0x7c0, 0x6c9, 0x5c3, 0x4ca, 0x3c6, 0x2cf, 0x1c5, 0x0cc, 0xfcc, 0xec5, 0xdcf, 0xcc6, 0xbca, 0xac3, 0x9c9, 0x8c0,
	0x8c0, 0x9c9, 0xac3, 0xbca, 0xcc6, 0xdcf, 0xec5, 0xfcc, 0x0cc, 0x1c5, 0x2cf, 0x3c6, 0x4ca, 0x5c3, 0x6c9, 0x7c0,
	0x950, 0x859, 0xb53, 0xa5a, 0xd56, 0xc5f, 0xf55, 0xe5c, 0x15c, 0x055, 0x35f, 0x256, 0x55a, 0x453, 0x759, 0x650,
	0xaf0, 0xbf9, 0x8f3, 0x9fa, 0xef6, 0xfff, 0xcf5, 0xdfc, 0x2fc, 0x3f5, 0x0ff, 0x1f6, 0x6fa, 0x7f3, 0x4f9, 0x5f0,
	0xb60, 0xa69, 0x963, 0x86a, 0xf66, 0xe6f, 0xd65, 0xc6c, 0x36c, 0x265, 0x16f, 0x066, 0x76a, 0x663, 0x569, 0x460,
	0xca0, 0xda9, 0xea3, 0xfaa, 0x8a6, 0x9af, 0xaa5, 0xbac, 0x4ac, 0x5a5, 0x6af, 0x7a6, 0x0aa, 0x1a3, 0x2a9, 0x3a0,
	0xd30, 0xc39, 0xf33, 0xe3a, 0x936, 0x83f, 0xb35, 0xa3c, 0x53c, 0x435, 0x73f, 0x636, 0x13a, 0x033, 0x339, 0x230,
	0xe90, 0xf99, 0xc93, 0xd9a, 0xa96, 0xb9f, 0x895, 0x99c, 0x69c, 0x795, 0x49f, 0x596, 0x29a, 0x393, 0x099, 0x190,
	0xf00, 0xe09, 0xd03, 0xc0a, 0xb06, 0xa0f, 0x905, 0x80c, 0x70c, 0x605, 0x50f, 0x406, 0x30a, 0x203, 0x109, 0x000};

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

// Default constructor
UMarchingCubes::UMarchingCubes()
	: MyVoxelChunk(nullptr),
	DiggerManager(nullptr)
{
		bHeightCacheInitialized = false;
		CachedVoxelSize = 0.0f;
		CachedChunkSize = 0;
		CachedChunkOrigin = FVector::ZeroVector;
		// ... your existing constructor code
}

// Constructor with FObjectInitializer and UVoxelChunk
UMarchingCubes::UMarchingCubes(const FObjectInitializer& ObjectInitializer, const UVoxelChunk* VoxelChunk)
	: UObject(ObjectInitializer), MyVoxelChunk(VoxelChunk), DiggerManager(nullptr), VoxelGrid(nullptr)
{
	bHeightCacheInitialized = false;
	CachedVoxelSize = 0.0f;
	CachedChunkSize = 0;
	CachedChunkOrigin = FVector::ZeroVector;
	// ... your existing constructor code
}



void UMarchingCubes::Initialize(ADiggerManager* InDiggerManager)
{
	DiggerManager = InDiggerManager;
}



float UMarchingCubes::GetSafeSDFValue(const FIntVector& Position) const {
	if (!FVoxelConversion::IsValidVoxelIndex(Position)) { 
		return 1.0f; // Default  outside bounds
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

// Blends a vertex Z toward the landscape surface if within the transition band
FVector UMarchingCubes::ApplyLandscapeTransition(const FVector& VertexWS) const
{
    if (!DiggerManager) return VertexWS;

    float LandscapeZ = DiggerManager->GetLandscapeHeightAt(VertexWS);
    float DistanceToSurface = FMath::Abs(VertexWS.Z - LandscapeZ);

    if (DistanceToSurface < TransitionHeight)
    {
        // Alpha: 0 at surface, 1 at bottom of transition band
        float Alpha = DistanceToSurface / TransitionHeight;
        // Sharpen/soften the blend with a power curve
        float BlendAlpha = FMath::Pow(Alpha, TransitionSharpness);
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
// ROUTER
// -------------------------------------------------------------------------

// Router 1: Takes Pointer -> Extracts Map -> Calls Worker
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

	// Forward to the worker using the internal Map
	GenerateMesh_MarchingCubes(
		InVoxelGrid->VoxelData, 
		Origin, VoxelSize, HeightMap, 
		OutVertices, OutTriangles, OutNormals
	);
}

// Router 2: Takes Map -> Calls Worker
void UMarchingCubes::GenerateMeshFromGrid(
	const TMap<FIntVector, FVoxelData>& VoxelData, 
	const FVector& Origin, 
	float VoxelSize,
	const TArray<float>& HeightMap,
	TArray<FVector>& OutVertices, 
	TArray<int32>& OutTriangles, 
	TArray<FVector>& OutNormals)
{
	// Forward to the worker
	GenerateMesh_MarchingCubes(
		VoxelData, 
		Origin, VoxelSize, HeightMap, 
		OutVertices, OutTriangles, OutNormals
	);
}

// RESTORED: Compatibility wrapper for raw grid access (Fixes Linker Error)
void UMarchingCubes::GenerateMeshFromGridSyncronous(
	USparseVoxelGrid* InVoxelGrid,
	const FVector& Origin,
	float VoxelSize,
	TArray<FVector>& OutVertices,
	TArray<int32>& OutTriangles,
	TArray<FVector>& OutNormals)
{
	if (!InVoxelGrid) return;

	// 1. Determine Grid Resolution
	int32 N = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;

	// 2. Capture Heights (We are on Game Thread if calling Sync)
	TArray<float> LocalHeights = CaptureHeightMap(Origin, VoxelSize, N);

	// 3. Forward to the Main Router
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
// THE WORKER (Heavy Logic)
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
    
    // Safety Checks
    int32 HeightMapWidth = N + 1;
    if (HeightValues.Num() != HeightMapWidth * HeightMapWidth)
    {
        UE_LOG(LogTemp, Error, TEXT("MC Crash: HeightMap Mismatch! Got %d, Expected %d"), 
               HeightValues.Num(), HeightMapWidth*HeightMapWidth);
        return;
    }

    if (IsDebugging())
    {
        UE_LOG(LogTemp, Warning, TEXT("MC Start: OriginZ=%.2f"), Origin.Z);
    }

    FVector TotalOffset = FVector(FVoxelConversion::LocalVoxelSize * 0.25F - FVoxelConversion::ChunkWorldSize * 0.5f);
    TMap<FVector, int32> VertexCache;

    // --- VOXEL LOOP ---
    for (int32 x = 0; x < N; ++x)
    {
        for (int32 y = 0; y < N; ++y)
        {
            // Column Height
            float TerrainHeight = HeightValues[y * HeightMapWidth + x];

            for (int32 z = 0; z < N; ++z)
            {
                // --- OPTIMIZATION ---
                bool bHeightValid = (TerrainHeight > (UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT + 1.0f));
                
                if (bHeightValid)
                {
                    FVector CellPos = Origin + FVector(x, y, z) * VoxelSize;
                    // If far above terrain, check if we can skip
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

                // --- CORNERS ---
                FVector CornerWSPositions[8];
                float CornerSDFValues[8];
                bool bAllSolid = true;
                bool bAllAir = true;

                for (int32 i = 0; i < 8; i++) 
                {
                    FIntVector LocalCoord = FIntVector(x, y, z) + GetCornerOffset(i);
                    CornerWSPositions[i] = Origin + FVector(LocalCoord) * VoxelSize;

                    // A. Explicit
                    if (const FVoxelData* Data = VoxelData.Find(LocalCoord))
                    {
                        CornerSDFValues[i] = Data->SDFValue;
                    }
                    // B. Implicit
                    else 
                    {
                        int32 hX = FMath::Clamp(LocalCoord.X, 0, HeightMapWidth - 1);
                        int32 hY = FMath::Clamp(LocalCoord.Y, 0, HeightMapWidth - 1);
                        
                        float CornerH = HeightValues[hY * HeightMapWidth + hX];

                        // Sentinel Check
                        if (CornerH <= (UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT + 1.0f))
                        {
                        	CornerSDFValues[i] = 1.0f; // Default Air
                        } else {
                        	// If Voxel is BELOW terrain -> Solid (-1)
                        	if (CornerWSPositions[i].Z < (CornerH - 0.1f)) {
                        		CornerSDFValues[i] = -1.0f; 
                        	} else {
                        		CornerSDFValues[i] = 1.0f;
                        	}
                        }
                    }

                    if (CornerSDFValues[i] > 0.0f) bAllSolid = false;
                    else bAllAir = false;
                }

                if (bAllSolid || bAllAir) continue;

                // --- TRIANGULATION ---
                int32 CubeIndex = CalculateMarchingCubesIndex(TArray<float>(CornerSDFValues, 8));
                if (CubeIndex == 0 || CubeIndex == 255) continue;

                for (int32 i = 0; TriangleConnectionTable[CubeIndex][i] != -1; i += 3) 
                {
                    FVector Verts[3];
                    for (int32 j = 0; j < 3; ++j) 
                    {
                        int32 EdgeIdx = TriangleConnectionTable[CubeIndex][i + j];
                        Verts[j] = InterpolateVertex(
                            CornerWSPositions[EdgeConnection[EdgeIdx][0]],
                            CornerWSPositions[EdgeConnection[EdgeIdx][1]],
                            CornerSDFValues[EdgeConnection[EdgeIdx][0]],
                            CornerSDFValues[EdgeConnection[EdgeIdx][1]]
                        );
                    }

                    for (int32 j = 0; j < 3; ++j) 
                    {
                        FVector FinalV = Verts[j] + TotalOffset;
                        int32* CacheIdx = VertexCache.Find(FinalV);
                        if (CacheIdx) {
                            OutTriangles.Add(*CacheIdx);
                        } else {
                            int32 NewIdx = OutVertices.Add(FinalV);
                            VertexCache.Add(FinalV, NewIdx);
                            OutTriangles.Add(NewIdx);
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

    for (FVector& NVal : OutNormals) {
        NVal.Normalize();
        NVal = -NVal; // Flip
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
    
	// FIXED: Use the same coordinate system as your mesh generation
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
            
			// Use the unified API. 
			// It handles finding the proxy and checking the cache automatically.
			float Height = DiggerManager->GetLandscapeHeightAt(SamplePos);
            
			// Store using grid coordinates as key
			FIntVector GridKey(x, y, 0);
			HeightCache.Add(GridKey, Height);
		}
	}
    
	// Store cache parameters
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
    
	// Convert world position to grid coordinates - FIXED ALIGNMENT
	// Use the same coordinate system as the original mesh generation
	FVector RelativePos = WorldPosition - CachedChunkOrigin;
    
	// Convert to grid space (matching your original VoxelSize scaling)
	float GridX = RelativePos.X / CachedVoxelSize;
	float GridY = RelativePos.Y / CachedVoxelSize;
    
	// Add the chunk center offset to match your original coordinate system
	GridX += (CachedChunkSize * 0.5f);
	GridY += (CachedChunkSize * 0.5f);
    
	// Bilinear interpolation for smooth height values
	int32 X0 = FMath::FloorToInt(GridX);
	int32 Y0 = FMath::FloorToInt(GridY);
	int32 X1 = X0 + 1;
	int32 Y1 = Y0 + 1;
    
	float FracX = GridX - X0;
	float FracY = GridY - Y0;
    
	// Get the four corner heights with fallback to 0 if not found
	float H00 = HeightCache.FindRef(FIntVector(X0, Y0, 0));
	float H10 = HeightCache.FindRef(FIntVector(X1, Y0, 0));
	float H01 = HeightCache.FindRef(FIntVector(X0, Y1, 0));
	float H11 = HeightCache.FindRef(FIntVector(X1, Y1, 0));
    
	// Bilinear interpolation
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

	// 🔄 Clear mesh section from DiggerManager if it exists
	if (DiggerManager->ProceduralMesh->GetNumSections() > SectionIndex) {
		DiggerManager->ProceduralMesh->ClearMeshSection(SectionIndex);
	}

	// 🎲 Rebuild new mesh for chunk
	// ---
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

	// Create a new ProceduralMeshComponent dynamically
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

	// Fill UVs etc. as needed (can be blank for now)
	IslandMesh->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, VertexColors, Tangents, true);

	// Set collision
	IslandMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	IslandMesh->SetCollisionObjectType(ECC_WorldDynamic);
	IslandMesh->SetCollisionResponseToAllChannels(ECR_Block);
	IslandMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	IslandMesh->bUseComplexAsSimpleCollision = true;

	// Optional: Set material
	if (DiggerManager->GetTerrainMaterial()) {
		IslandMesh->SetMaterial(0, DiggerManager->GetTerrainMaterial());
	}

	if (DiggerDebug::Islands())
	UE_LOG(LogTemp, Log, TEXT("Island mesh %d created at origin %s with %d vertices."), IslandId, *Origin.ToString(), Vertices.Num());

	// Optional: Add to an array for future management
	DiggerManager->IslandMeshes.Add(IslandMesh);
}





void UMarchingCubes::ReconstructMeshSection(int32 SectionIndex, const TArray<FVector>& OutOutVertices, const TArray<int32>& OutTriangles, const TArray<FVector>& Normals) const {
    // Validate pointers
    if (!DiggerManager || !DiggerManager->ProceduralMesh) {
    	if (DiggerDebug::Manager() || DiggerDebug::Mesh())
        UE_LOG(LogTemp, Error, TEXT("DiggerManager or ProceduralMesh is null in ReconstructMeshSection"));
        return;
    }

    // Validate SectionIndex
    if (SectionIndex < 0) {
    	if (DiggerDebug::Manager() || DiggerDebug::Mesh())
        UE_LOG(LogTemp, Error, TEXT("Invalid SectionIndex in ReconstructMeshSection: %d"), SectionIndex);
        return;
    }

    // Validate Mesh Data
    if (OutOutVertices.Num() == 0 || OutTriangles.Num() == 0 || Normals.Num() == 0) {
    	if (DiggerDebug::Manager() || DiggerDebug::Mesh())
        UE_LOG(LogTemp, Error, TEXT("Empty mesh data in ReconstructMeshSection"));
        return;
    }

    TArray<FVector2D> UVs;
    TArray<FColor> VertexColors;
    TArray<FProcMeshTangent> Tangents;

    // Clear the mesh section if it exists
    if (DiggerManager->ProceduralMesh->GetNumSections() > SectionIndex) {
        DiggerManager->ProceduralMesh->ClearMeshSection(SectionIndex);
    }
	if (bIsDebugging)
	{
		UE_LOG(LogTemp, Warning, TEXT("Creating Mesh Section %d"), SectionIndex);
	}
    // Create a new mesh section
    DiggerManager->ProceduralMesh->CreateMeshSection(
        SectionIndex,
        OutOutVertices,
        OutTriangles,
        Normals,
        UVs,
        VertexColors,
        Tangents,
        true  // Enable collision
    );

	UProceduralMeshComponent* Mesh = DiggerManager->ProceduralMesh;

	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetCollisionObjectType(ECC_WorldDynamic);
	Mesh->SetCollisionResponseToAllChannels(ECR_Block);
	Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Mesh->bUseComplexAsSimpleCollision = true;
	//Mesh->RecreatePhysicsState();


    // Ensure the material is set once
	DiggerManager->ProceduralMesh->SetMaterial(SectionIndex, DiggerManager->GetTerrainMaterial());

	// Send the OnMeshReady Callback
	if (OnMeshReady.IsBound())
	{
		OnMeshReady.Execute();
	}
}





// Interpolates a vertex position on the edge between two points based on their SDF values

// Modify InterpolateVertex to work directly in world space
FVector UMarchingCubes::InterpolateVertex(const FVector& P1, const FVector& P2, float SDF1, float SDF2)
{
	if (FMath::Abs(SDF1 - SDF2) < KINDA_SMALL_NUMBER)
	{
		return (P1 + P2) * 0.5f; // Direct world space interpolation
	}

	float T = SDF1 / (SDF1 - SDF2);
	return FMath::Lerp(P1, P2, T); // Direct world space interpolation
}


// NEW CORRECT CODE
TArray<float> UMarchingCubes::CaptureHeightMap(const FVector& Origin, float VoxelSize, int32 GridResolution)
{
	// We need N+1 points to cover the edges of the chunk for interpolation
	int32 SampleSize = GridResolution + 1; 
    
	TArray<float> Heights;
	Heights.SetNumUninitialized(SampleSize * SampleSize); // 129*129 = 16641 (CORRECT)

	if (DiggerManager)
	{
		for (int32 x = 0; x < SampleSize; ++x) 
		{
			for (int32 y = 0; y < SampleSize; ++y) 
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


// Gets the vertex index, ensuring unique OutVertices are not added multiple times
int32 UMarchingCubes::GetVertexIndex(const FVector& Vertex, TMap<FVector, int32>& VertexMap, TArray<FVector>& OutOutVertices)
{
    // Check if the vertex has already been added
    if (int32* Index = VertexMap.Find(Vertex))
    {
        return *Index; // Return existing index
    }

    // Otherwise, add it to the array and map
    int32 NewIndex = OutOutVertices.Add(Vertex);
    VertexMap.Add(Vertex, NewIndex);
    return NewIndex;
}

int32 UMarchingCubes::CalculateMarchingCubesIndex(const TArray<float>& CornerSDFValues)
{
	int32 CubeIndex = 0;
	for (int32 i = 0; i < 8; i++) // There are 8 corners in a voxel cube
	{
		if (CornerSDFValues[i] < 0.0f) // If the corner is inside the surface
		{
			CubeIndex |= (1 << i); // Set the bit corresponding to the corner
		}
	}
	return CubeIndex;
}
