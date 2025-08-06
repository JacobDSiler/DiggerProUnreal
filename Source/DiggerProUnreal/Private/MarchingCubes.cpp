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

void UMarchingCubes::GenerateMesh(const UVoxelChunk* ChunkPtr)
{ 

    if (!DiggerManager) {
        UE_LOG(LogTemp, Error, TEXT("DiggerManager is null in UMarchingCubes::GenerateMesh()!"));
        DiggerManager = ChunkPtr->GetDiggerManager();
        if (!DiggerManager) return;
    }

    int32 SectionIndex = ChunkPtr->GetSectionIndex();
	if (IsDebugging())
	{
		UE_LOG(LogTemp, Error, TEXT("Generating Mesh for Section with Section ID: %i"), SectionIndex);
	}

    USparseVoxelGrid* InVoxelGrid = ChunkPtr->GetSparseVoxelGrid();
    if (!InVoxelGrid) {
    	if (DiggerDebug::Voxels)
        UE_LOG(LogTemp, Warning, TEXT("No InVoxelGrid and/or data to generate mesh! InVoxelGrid: %s, VoxelData.Num(): %d"),
            InVoxelGrid ? TEXT("Valid") : TEXT("Invalid"),
            InVoxelGrid ? InVoxelGrid->VoxelData.Num() : 0)
        return;
    }
	if(InVoxelGrid->VoxelData.IsEmpty())
	{
		if (DiggerDebug::Voxels)
		UE_LOG(LogTemp, Warning, TEXT("InVoxelGrid MarchingCubes.cpp, LN 373 : VoxelData is empty!"))
		return;
	}
	
    // Use FVoxelConversion to get the chunk's world position
    FIntVector ChunkCoords = ChunkPtr->GetChunkCoordinates();
    FVector ChunkOrigin = FVoxelConversion::ChunkToWorld(ChunkCoords);
    
	float VoxelSize = FVoxelConversion::LocalVoxelSize; // Use the consistent voxel size

    TArray<FVector> OutVertices;
    TArray<int32> OutTriangles;
    TArray<FVector> OutNormals;

    // Call the modular function, passing the offset
    GenerateMeshFromGrid(InVoxelGrid, ChunkOrigin , VoxelSize, OutVertices, OutTriangles, OutNormals);

    if (OutVertices.Num() > 0 && OutTriangles.Num() > 0 && OutNormals.Num() > 0) {
        AsyncTask(ENamedThreads::GameThread, [this, SectionIndex, OutVertices, OutTriangles, OutNormals]()
        {
            ReconstructMeshSection(SectionIndex, OutVertices, OutTriangles, OutNormals);
        });
    } else {
        UE_LOG(LogTemp, Warning, TEXT("Empty mesh data in GenerateMesh"));
    }
}

void UMarchingCubes::GenerateMeshSyncronous(const UVoxelChunk* ChunkPtr)
{
	if (!DiggerManager) {
		UE_LOG(LogTemp, Error, TEXT("DiggerManager is null in UMarchingCubes::GenerateMesh()!"));
		DiggerManager = ChunkPtr->GetDiggerManager();
		if (!DiggerManager) return;
	}

	int32 SectionIndex = ChunkPtr->GetSectionIndex();
	if (IsDebugging())
	{
		UE_LOG(LogTemp, Error, TEXT("Generating Mesh for Section with Section ID: %i"), SectionIndex);
	}

	USparseVoxelGrid* InVoxelGrid = ChunkPtr->GetSparseVoxelGrid();
	if (!InVoxelGrid) {
		if (DiggerDebug::Voxels)
			UE_LOG(LogTemp, Warning, TEXT("No InVoxelGrid and/or data to generate mesh! InVoxelGrid: %s, VoxelData.Num(): %d"),
				InVoxelGrid ? TEXT("Valid") : TEXT("Invalid"),
				InVoxelGrid ? InVoxelGrid->VoxelData.Num() : 0)
		return;
	}
	if(InVoxelGrid->VoxelData.IsEmpty())
	{
		if (DiggerDebug::Voxels)
			UE_LOG(LogTemp, Warning, TEXT("InVoxelGrid MarchingCubes.cpp, LN 373 : VoxelData is empty!"))
		return;
	}
	
	// Use FVoxelConversion to get the chunk's world position
	FIntVector ChunkCoords = ChunkPtr->GetChunkCoordinates();
	FVector ChunkOrigin = FVoxelConversion::ChunkToWorld(ChunkCoords);
    
	float VoxelSize = FVoxelConversion::LocalVoxelSize; // Use the consistent voxel size

	TArray<FVector> OutVertices;
	TArray<int32> OutTriangles;
	TArray<FVector> OutNormals;

	// Call the modular function, passing the offset
	GenerateMeshFromGridSyncronous(InVoxelGrid, ChunkOrigin , VoxelSize, OutVertices, OutTriangles, OutNormals);

	if (OutVertices.Num() > 0 && OutTriangles.Num() > 0 && OutNormals.Num() > 0) {
		AsyncTask(ENamedThreads::GameThread, [this, SectionIndex, OutVertices, OutTriangles, OutNormals]()
		{
			ReconstructMeshSection(SectionIndex, OutVertices, OutTriangles, OutNormals);
		});
	} else {
		UE_LOG(LogTemp, Warning, TEXT("Empty mesh data in GenerateMesh"));
	}
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

void UMarchingCubes::AddSkirtMesh(
    const TArray<int32>& RimVertexIndices,
    TArray<FVector>& Vertices,
    TArray<int32>& Triangles,
    TArray<FVector>& Normals) const
{
    int32 NumRim = RimVertexIndices.Num();
    if (NumRim < 2) return;

    TArray<int32> SkirtVertexIndices;
    SkirtVertexIndices.SetNum(NumRim);

    const float SkirtOffset = 1.0f; // 1cm above the landscape

    // Add skirt vertices (projected to landscape, offset along normal)
    for (int32 i = 0; i < NumRim; ++i)
    {
        const FVector& RimV = Vertices[RimVertexIndices[i]];
        FVector SkirtV = RimV;
        SkirtV.Z = DiggerManager->GetLandscapeHeightAt(RimV);

        // Offset along the landscape normal to avoid z-fighting and ensure visibility
        FVector LandscapeNormal = DiggerManager->GetLandscapeNormalAt(RimV);
        SkirtV += LandscapeNormal * SkirtOffset;

        SkirtVertexIndices[i] = Vertices.Add(SkirtV);
    }

    // Add triangles for the skirt
    for (int32 i = 0; i < NumRim; ++i)
    {
        int32 Next = (i + 1) % NumRim;
        int32 RimA = RimVertexIndices[i];
        int32 RimB = RimVertexIndices[Next];
        int32 SkirtA = SkirtVertexIndices[i];
        int32 SkirtB = SkirtVertexIndices[Next];

        // Triangle 1
        Triangles.Add(RimA);
        Triangles.Add(RimB);
        Triangles.Add(SkirtB);

        // Triangle 2
        Triangles.Add(RimA);
        Triangles.Add(SkirtB);
        Triangles.Add(SkirtA);
    }
	
	// Fix: Set normals for skirt vertices at correct index
	Normals.SetNum(Vertices.Num()); // Ensure size match
	for (int32 i = 0; i < NumRim; ++i)
	{
		FVector Normal = DiggerManager->GetLandscapeNormalAt(Vertices[SkirtVertexIndices[i]]);
		Normals.Insert(Normal, SkirtVertexIndices[i]); // Set at the right vertex index
	}

}



void UMarchingCubes::FindRimVertices(
    const TArray<FVector>& Vertices,
    const TArray<int32>& Triangles,
    TArray<int32>& OutRimVertexIndices)
{
    // Map to count how many times each edge appears
    TMap<TPair<int32, int32>, int32> EdgeCount;

    // Count edges
    for (int32 i = 0; i < Triangles.Num(); i += 3)
    {
        int32 Indices[3] = {Triangles[i], Triangles[i+1], Triangles[i+2]};
        for (int32 e = 0; e < 3; ++e)
        {
            int32 A = Indices[e];
            int32 B = Indices[(e+1)%3];
            // Always store edge with smaller index first for consistency
            TPair<int32, int32> Edge = (A < B) ? TPair<int32, int32>(A, B) : TPair<int32, int32>(B, A);
            EdgeCount.FindOrAdd(Edge)++;
        }
    }

    // Collect all vertices that are part of a boundary edge (used only once)
    TSet<int32> RimSet;
    for (const auto& Pair : EdgeCount)
    {
        if (Pair.Value == 1) // Boundary edge
        {
            RimSet.Add(Pair.Key.Key);
            RimSet.Add(Pair.Key.Value);
        }
    }
    OutRimVertexIndices = RimSet.Array();
}



void UMarchingCubes::GenerateMeshFromGrid(
    USparseVoxelGrid* InVoxelGrid,
    const FVector& Origin,
    float VoxelSize,
    TArray<FVector>& OutVertices,
    TArray<int32>& OutTriangles,
    TArray<FVector>& OutNormals
)
{
    if (!InVoxelGrid) {
        UE_LOG(LogTemp, Error, TEXT("Invalid VoxelGrid in GenerateMeshFromGrid!"));
        return;
    }

    // INITIALIZE HEIGHT CACHE FIRST - This runs on the game thread before any parallel processing
    if (!IsHeightCacheValid(Origin, VoxelSize))
    {
        InitializeHeightCache(Origin, VoxelSize);
    }

    // WorldSpaceOffset for proper alignment for center aligned chunk schema.
    FVector TotalOffset = FVector(FVoxelConversion::LocalVoxelSize * 0.25F - FVoxelConversion::ChunkWorldSize * 0.5f);
    
    int32 N = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;

    if (IsDebugging()) {
        DrawDebugBox(
            DiggerManager->GetSafeWorld(),
            Origin + FVector(N * VoxelSize * 0.5f),
            FVector(N * VoxelSize * 0.5f),
            FQuat::Identity,
            FColor::Blue,
            false,
            10.0f,
            0,
            2.0f
        );
    }

    TMap<FVector, int32> VertexCache;

    // Pre-compute height values for the entire chunk using our robust cache
    TArray<float> HeightValues;
    HeightValues.SetNumZeroed(N * N);
    
    for (int32 x = 0; x < N; ++x) {
        for (int32 y = 0; y < N; ++y) {
            FVector WorldPos = Origin + FVector(x * VoxelSize, y * VoxelSize, 0);
            HeightValues[y * N + x] = GetCachedHeight(WorldPos);
        }
    }

    // Track cells with explicit voxels for debugging
    TArray<FIntVector> CellsWithExplicitVoxels;
    TArray<FIntVector> BelowTerrainCellsWithAirVoxels;

    // First pass: Identify cells with air voxels below terrain
    TSet<FIntVector> CellsWithAirVoxelsBelowTerrain;
    
    for (int32 x = 0; x < N; ++x)
    for (int32 y = 0; y < N; ++y)
    {
        float TerrainHeight = HeightValues[y * N + x];
        
        for (int32 z = 0; z < N; ++z)
        {
            float MinZ = Origin.Z + z * VoxelSize;
            float MaxZ = MinZ + VoxelSize;
            bool bBelowTerrain = MaxZ < TerrainHeight;
            
            if (!bBelowTerrain) continue;
            
            // Check if this cell has any explicit air voxels below terrain
            for (int32 i = 0; i < 8; i++) {
                FIntVector CornerCoords = FIntVector(x, y, z) + GetCornerOffset(i);
                if (InVoxelGrid->VoxelData.Contains(CornerCoords)) {
                    float SDFValue = InVoxelGrid->GetVoxel(CornerCoords.X, CornerCoords.Y, CornerCoords.Z);
                    if (SDFValue > 0) { // Air voxel
                        CellsWithAirVoxelsBelowTerrain.Add(FIntVector(x, y, z));
                        break;
                    }
                }
            }
        }
    }

    // Second pass: Process all cells and generate mesh
    for (int32 x = 0; x < N; ++x)
    for (int32 y = 0; y < N; ++y)
    {
        // Get terrain height for this column using our cached value
        float TerrainHeight = HeightValues[y * N + x];
        
        for (int32 z = 0; z < N; ++z)
        {
            // Quick check if cell is entirely above terrain with no explicit voxels
            float MinZ = Origin.Z + z * VoxelSize;
            float MaxZ = MinZ + VoxelSize;
            bool bBelowTerrain = MaxZ < TerrainHeight;
            
            // Check if this cell has any explicit voxels
            bool bHasExplicitVoxels = false;
            bool bHasAirVoxelsBelowTerrain = false;
            
            for (int32 i = 0; i < 8 && !bHasExplicitVoxels; i++) {
                FIntVector CornerCoords = FIntVector(x, y, z) + GetCornerOffset(i);
                if (InVoxelGrid->VoxelData.Contains(CornerCoords)) {
                    bHasExplicitVoxels = true;
                    
                    // Check if this is an air voxel below terrain
                    FVector CornerWorldPos = Origin + FVector(CornerCoords) * VoxelSize;
                    // Use cached height for this check too
                    float CornerTerrainHeight = GetCachedHeight(CornerWorldPos);
                    if (CornerWorldPos.Z < CornerTerrainHeight) {
                        float SDFValue = InVoxelGrid->GetVoxel(CornerCoords.X, CornerCoords.Y, CornerCoords.Z);
                        if (SDFValue > 0) { // Air voxel
                            bHasAirVoxelsBelowTerrain = true;
                        }
                    }
                }
            }
            
            // For debugging
            if (bHasExplicitVoxels) {
                CellsWithExplicitVoxels.Add(FIntVector(x, y, z));
            }
            
            if (bBelowTerrain && bHasAirVoxelsBelowTerrain) {
                BelowTerrainCellsWithAirVoxels.Add(FIntVector(x, y, z));
            }
            
            // Check if we need to process this cell
            bool bShouldProcess = false;
            
            // Process if it has explicit voxels
            if (bHasExplicitVoxels) {
                bShouldProcess = true;
            }
            // Process if it's below terrain and adjacent to a cell with air voxels
            else if (bBelowTerrain) {
                // Check if any adjacent cell has air voxels below terrain
                for (int32 dx = -1; dx <= 1 && !bShouldProcess; dx++)
                for (int32 dy = -1; dy <= 1 && !bShouldProcess; dy++)
                for (int32 dz = -1; dz <= 1 && !bShouldProcess; dz++) {
                    if (dx == 0 && dy == 0 && dz == 0) continue;
                    
                    FIntVector AdjacentCell(x + dx, y + dy, z + dz);
                    if (AdjacentCell.X < 0 || AdjacentCell.Y < 0 || AdjacentCell.Z < 0 ||
                        AdjacentCell.X >= N || AdjacentCell.Y >= N || AdjacentCell.Z >= N)
                        continue;
                        
                    if (CellsWithAirVoxelsBelowTerrain.Contains(AdjacentCell)) {
                        bShouldProcess = true;
                    }
                }
            }
            // Process if it's above terrain but not too far above
            else if (MinZ <= TerrainHeight + VoxelSize) {
                bShouldProcess = true;
            }
            
            // Skip if we don't need to process this cell
            if (!bShouldProcess) {
                continue;
            }

            FVector CellOrigin = Origin + FVector(x * VoxelSize, y * VoxelSize, z * VoxelSize);

            FVector CornerWSPositions[8];
            float CornerSDFValues[8];

            for (int32 i = 0; i < 8; i++) {
                FIntVector CornerCoords = FIntVector(x, y, z) + GetCornerOffset(i);
                CornerWSPositions[i] = Origin + FVector(
                    CornerCoords.X * VoxelSize,
                    CornerCoords.Y * VoxelSize,
                    CornerCoords.Z * VoxelSize
                );
                
                // Handle voxel values
                if (InVoxelGrid->VoxelData.Contains(CornerCoords))
                {
                    // Use the explicit voxel value
                    CornerSDFValues[i] = InVoxelGrid->GetVoxel(CornerCoords.X, CornerCoords.Y, CornerCoords.Z);
                }
                else
                {
                    // For uninitialized voxels, check if they're below terrain using cached height
                    FVector WorldPos = CornerWSPositions[i];
                    float CornerTerrainHeight = GetCachedHeight(WorldPos);
                    bool bCornerBelowTerrain = WorldPos.Z < CornerTerrainHeight;
                    
                    if (bCornerBelowTerrain)
                    {
                        // Default to solid for unset voxels below terrain
                        CornerSDFValues[i] = -1.0f;
                        
                        // Check for nearby explicit air voxels that should create a surface
                        bool bFoundNearbyAir = false;
                        float MinDistanceToAir = FLT_MAX;
                        
                        // Search in a small radius around this corner
                        const int32 SearchRadius = 2;
                        const float MaxInfluenceDistance = SearchRadius * VoxelSize;
                        
                        for (int32 dx = -SearchRadius; dx <= SearchRadius; dx++)
                        for (int32 dy = -SearchRadius; dy <= SearchRadius; dy++)
                        for (int32 dz = -SearchRadius; dz <= SearchRadius; dz++)
                        {
                            if (dx == 0 && dy == 0 && dz == 0) continue;
                            
                            FIntVector SearchCoords = CornerCoords + FIntVector(dx, dy, dz);
                            
                            if (InVoxelGrid->VoxelData.Contains(SearchCoords))
                            {
                                float NearbySDFValue = InVoxelGrid->GetVoxel(SearchCoords.X, SearchCoords.Y, SearchCoords.Z);
                                
                                // If we find an explicit air voxel
                                if (NearbySDFValue > 0)
                                {
                                    FVector NearbyWorldPos = Origin + FVector(SearchCoords) * VoxelSize;
                                    float NearbyTerrainHeight = GetCachedHeight(NearbyWorldPos);
                                    
                                    // Check if this air voxel is also below terrain (creating a cavity)
                                    if (NearbyWorldPos.Z < NearbyTerrainHeight)
                                    {
                                        float Distance = FVector(dx, dy, dz).Size() * VoxelSize;
                                        if (Distance < MinDistanceToAir)
                                        {
                                            MinDistanceToAir = Distance;
                                            bFoundNearbyAir = true;
                                        }
                                    }
                                }
                            }
                        }
                        
                        // If we found nearby air voxels, create a gradient towards them
                        if (bFoundNearbyAir && MinDistanceToAir < MaxInfluenceDistance)
                        {
                            // Alternative approach: Use a more aggressive transition
                            CornerSDFValues[i] = (MinDistanceToAir < VoxelSize) ? 0.1f : -1.0f;
                        }
                    }
                    else
                    {
                        // Above terrain - default to air
                        CornerSDFValues[i] = 1.0f;
                    }
                }
            }

            if (IsDebugging()) {
                // Only visualize cells with explicit voxels or below-terrain cells with air voxels
                if (bHasExplicitVoxels || (bBelowTerrain && bHasAirVoxelsBelowTerrain)) {
                    for (int32 i = 0; i < 8; ++i) {
                        FColor PointColor = CornerSDFValues[i] > 0 ? FColor::Blue : FColor::Red;
                        DrawDebugPoint(
                            DiggerManager->GetWorld(),
                            CornerWSPositions[i],
                            5.0f,
                            PointColor,
                            false,
                            10.0f
                        );
                    }
                    
                    // Draw cell bounds
                    DrawDebugBox(
                        DiggerManager->GetWorld(),
                        CellOrigin + FVector(VoxelSize * 0.5f),
                        FVector(VoxelSize * 0.5f),
                        FQuat::Identity,
                        FColor::Green,
                        false,
                        10.0f,
                        0,
                        1.0f
                    );
                }
            }

            // Calculate marching cubes index
            TArray<float> SDFValues;
            SDFValues.Append(CornerSDFValues, 8);
            int32 CubeIndex = CalculateMarchingCubesIndex(SDFValues);

            if (CubeIndex == 0 || CubeIndex == 255) {
                continue;
            }

            // Generate triangles for this cube
            for (int32 i = 0; TriangleConnectionTable[CubeIndex][i] != -1; i += 3) {
                FVector TriangleVertices[3];
                
                for (int32 j = 0; j < 3; ++j) {
                    int32 EdgeIndex = TriangleConnectionTable[CubeIndex][i + j];
                    
                    FVector InterpolatedVertex = InterpolateVertex(
                        CornerWSPositions[EdgeConnection[EdgeIndex][0]],
                        CornerWSPositions[EdgeConnection[EdgeIndex][1]],
                        CornerSDFValues[EdgeConnection[EdgeIndex][0]],
                        CornerSDFValues[EdgeConnection[EdgeIndex][1]]
                    );
                    
                    TriangleVertices[j] = ApplyLandscapeTransition(InterpolatedVertex);
                }

                // Add vertices to mesh with proper offset
                for (int32 j = 0; j < 3; ++j) {
                    FVector FinalVertex = TriangleVertices[j] + TotalOffset;
                    
                    int32* CachedIndex = VertexCache.Find(FinalVertex);
                    if (CachedIndex) {
                        OutTriangles.Add(*CachedIndex);
                    } else {
                        int32 NewVertexIndex = OutVertices.Add(FinalVertex);
                        VertexCache.Add(FinalVertex, NewVertexIndex);
                        OutTriangles.Add(NewVertexIndex);
                    }
                }
            }
        }
    }

    // CORRECTED SMOOTH NORMAL CALCULATION - FLIPPED FOR PROPER LIGHTING
    OutNormals.SetNum(OutVertices.Num());
    for (FVector& Normal : OutNormals) {
        Normal = FVector::ZeroVector;
    }

    // Accumulate face normals to vertices (area-weighted)
    for (int32 i = 0; i < OutTriangles.Num(); i += 3) {
        int32 I0 = OutTriangles[i];
        int32 I1 = OutTriangles[i + 1];
        int32 I2 = OutTriangles[i + 2];
        
        const FVector& V0 = OutVertices[I0];
        const FVector& V1 = OutVertices[I1];
        const FVector& V2 = OutVertices[I2];

        // Calculate face normal with proper winding order
        FVector Edge1 = V1 - V0;
        FVector Edge2 = V2 - V0;
        FVector FaceNormal = FVector::CrossProduct(Edge1, Edge2);
        
        // The magnitude represents twice the triangle area
        float DoubleArea = FaceNormal.Size();
        if (DoubleArea > SMALL_NUMBER)
        {
            // Normalize to get unit normal, then weight by area
            FVector UnitNormal = FaceNormal / DoubleArea;
            FVector WeightedNormal = UnitNormal * DoubleArea;
            
            // Accumulate to all three vertices
            OutNormals[I0] += WeightedNormal;
            OutNormals[I1] += WeightedNormal;
            OutNormals[I2] += WeightedNormal;
        }
    }

    // Normalize all vertex normals and FLIP them for proper lighting
    for (FVector& Normal : OutNormals) {
        Normal = Normal.GetSafeNormal();
        
        // FLIP normals to point outward for proper lighting
        Normal = -Normal;
    }

    // Debug output
    if (IsDebugging()) {
        UE_LOG(LogTemp, Log, TEXT("Generated mesh: %d vertices, %d triangles, %d cells with explicit voxels, %d Fbelow-terrain cells with air"),
               OutVertices.Num(), OutTriangles.Num() / 3, CellsWithExplicitVoxels.Num(), BelowTerrainCellsWithAirVoxels.Num());
    }
}


void UMarchingCubes::GenerateMeshFromGridSyncronous(
    USparseVoxelGrid* InVoxelGrid,
    const FVector& Origin,
    float VoxelSize,
    TArray<FVector>& OutVertices,
    TArray<int32>& OutTriangles,
    TArray<FVector>& OutNormals
)
{
    if (!InVoxelGrid) {
        UE_LOG(LogTemp, Error, TEXT("Invalid VoxelGrid in GenerateMeshFromGrid!"));
        return;
    }

	//Make the vertex colors data
	//Parameter version: TArray<FColor>& OutVertexColors
	TArray<FColor> OutVertexColors; // NEW: Vertex colors for material data
	
    // WorldSpaceOffset for proper alignment for center aligned chunk schema.
    FVector TotalOffset = FVector(FVoxelConversion::LocalVoxelSize * 0.25F - FVoxelConversion::ChunkWorldSize * 0.25f);
    
    int32 N = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;

    if (IsDebugging()) {
        DrawDebugBox(
            DiggerManager->GetSafeWorld(),
            Origin + FVector(N * VoxelSize * 0.5f),
            FVector(N * VoxelSize * 0.5f),
            FQuat::Identity,
            FColor::Blue,
            false,
            10.0f,
            0,
            2.0f
        );
    }

    TMap<FVector, int32> VertexCache;

    // Get the height cache pointer
    TSharedPtr<TMap<FIntPoint, float>>* HeightCachePtr = nullptr;

    if (DiggerManager)
    {
        ALandscapeProxy* Landscape = DiggerManager->GetLandscapeProxyAt(Origin);
        if (Landscape)
        {
            HeightCachePtr = DiggerManager->LandscapeHeightCaches.Find(Landscape);
        }
    }
    
    // Create a lambda for height lookup that properly handles the pointer to shared pointer
    auto GetCachedHeight = [HeightCachePtr, VoxelSize](const FVector& WorldPos) -> float
    {
        // Check if the pointer to shared pointer is valid and the shared pointer itself is valid
        if (!HeightCachePtr || !(*HeightCachePtr).IsValid())
            return 0.0f;

        // Calculate grid coordinates
        int32 GridX = FMath::FloorToInt(WorldPos.X / VoxelSize);
        int32 GridY = FMath::FloorToInt(WorldPos.Y / VoxelSize);
        FIntPoint Key(GridX, GridY);

        // Access the map through the shared pointer
        const TMap<FIntPoint, float>& HeightMap = *(*HeightCachePtr).Get();
        const float* FoundHeight = HeightMap.Find(Key);
        
        return FoundHeight ? *FoundHeight : 0.0f;
    };

    // Pre-compute height values for the entire chunk for better performance
    TArray<float> HeightValues;
    HeightValues.SetNumZeroed(N * N);
    
    if (HeightCachePtr && (*HeightCachePtr).IsValid()) {
        for (int32 x = 0; x < N; ++x) {
            for (int32 y = 0; y < N; ++y) {
                FVector WorldPos = Origin + FVector(x * VoxelSize, y * VoxelSize, 0);
                HeightValues[y * N + x] = GetCachedHeight(WorldPos);
            }
        }
    }

    // Track cells with explicit voxels for debugging
    TArray<FIntVector> CellsWithExplicitVoxels;
    TArray<FIntVector> BelowTerrainCellsWithAirVoxels;

    // First pass: Identify cells with air voxels below terrain
    TSet<FIntVector> CellsWithAirVoxelsBelowTerrain;
    
    for (int32 x = 0; x < N; ++x)
    for (int32 y = 0; y < N; ++y)
    {
        float TerrainHeight = HeightValues[y * N + x];
        
        for (int32 z = 0; z < N; ++z)
        {
            float MinZ = Origin.Z + z * VoxelSize;
            float MaxZ = MinZ + VoxelSize;
            bool bBelowTerrain = MaxZ < TerrainHeight;
            
            if (!bBelowTerrain) continue;
            
            // Check if this cell has any explicit air voxels below terrain
            for (int32 i = 0; i < 8; i++) {
                FIntVector CornerCoords = FIntVector(x, y, z) + GetCornerOffset(i);
                if (InVoxelGrid->VoxelData.Contains(CornerCoords)) {
                    float SDFValue = InVoxelGrid->GetVoxel(CornerCoords.X, CornerCoords.Y, CornerCoords.Z);
                    if (SDFValue > 0) { // Air voxel
                        CellsWithAirVoxelsBelowTerrain.Add(FIntVector(x, y, z));
                        break;
                    }
                }
            }
        }
    }

    // Second pass: Process all cells and generate mesh
    for (int32 x = 0; x < N; ++x)
    for (int32 y = 0; y < N; ++y)
    {
        // Get terrain height for this column
        float TerrainHeight = HeightValues[y * N + x];
        
        for (int32 z = 0; z < N; ++z)
        {
            // Quick check if cell is entirely above terrain with no explicit voxels
            float MinZ = Origin.Z + z * VoxelSize;
            float MaxZ = MinZ + VoxelSize;
            bool bBelowTerrain = MaxZ < TerrainHeight;
            
            // Check if this cell has any explicit voxels
            bool bHasExplicitVoxels = false;
            bool bHasAirVoxelsBelowTerrain = false;
            
            for (int32 i = 0; i < 8 && !bHasExplicitVoxels; i++) {
                FIntVector CornerCoords = FIntVector(x, y, z) + GetCornerOffset(i);
                if (InVoxelGrid->VoxelData.Contains(CornerCoords)) {
                    bHasExplicitVoxels = true;
                    
                    // Check if this is an air voxel below terrain
                    FVector CornerWorldPos = Origin + FVector(CornerCoords) * VoxelSize;
                    if (CornerWorldPos.Z < TerrainHeight) {
                        float SDFValue = InVoxelGrid->GetVoxel(CornerCoords.X, CornerCoords.Y, CornerCoords.Z);
                        if (SDFValue > 0) { // Air voxel
                            bHasAirVoxelsBelowTerrain = true;
                        }
                    }
                }
            }
            
            // For debugging
            if (bHasExplicitVoxels) {
                CellsWithExplicitVoxels.Add(FIntVector(x, y, z));
            }
            
            if (bBelowTerrain && bHasAirVoxelsBelowTerrain) {
                BelowTerrainCellsWithAirVoxels.Add(FIntVector(x, y, z));
            }
            
            // Check if we need to process this cell
            bool bShouldProcess = false;
            
            // Process if it has explicit voxels
            if (bHasExplicitVoxels) {
                bShouldProcess = true;
            }
            // Process if it's below terrain and adjacent to a cell with air voxels
            else if (bBelowTerrain) {
                // Check if any adjacent cell has air voxels below terrain
                for (int32 dx = -1; dx <= 1 && !bShouldProcess; dx++)
                for (int32 dy = -1; dy <= 1 && !bShouldProcess; dy++)
                for (int32 dz = -1; dz <= 1 && !bShouldProcess; dz++) {
                    if (dx == 0 && dy == 0 && dz == 0) continue;
                    
                    FIntVector AdjacentCell(x + dx, y + dy, z + dz);
                    if (AdjacentCell.X < 0 || AdjacentCell.Y < 0 || AdjacentCell.Z < 0 ||
                        AdjacentCell.X >= N || AdjacentCell.Y >= N || AdjacentCell.Z >= N)
                        continue;
                        
                    if (CellsWithAirVoxelsBelowTerrain.Contains(AdjacentCell)) {
                        bShouldProcess = true;
                    }
                }
            }
            // Process if it's above terrain but not too far above
            else if (MinZ <= TerrainHeight + VoxelSize) {
                bShouldProcess = true;
            }
            
            // Skip if we don't need to process this cell
            if (!bShouldProcess) {
                continue;
            }

            FVector CellOrigin = Origin + FVector(x * VoxelSize, y * VoxelSize, z * VoxelSize);

            FVector CornerWSPositions[8];
            float CornerSDFValues[8];

            for (int32 i = 0; i < 8; i++) {
                FIntVector CornerCoords = FIntVector(x, y, z) + GetCornerOffset(i);
                CornerWSPositions[i] = Origin + FVector(
                    CornerCoords.X * VoxelSize,
                    CornerCoords.Y * VoxelSize,
                    CornerCoords.Z * VoxelSize
                );
                
                // Handle voxel values
                if (InVoxelGrid->VoxelData.Contains(CornerCoords))
                {
                    // Use the explicit voxel value
                    CornerSDFValues[i] = InVoxelGrid->GetVoxel(CornerCoords.X, CornerCoords.Y, CornerCoords.Z);
                }
                else
{
    // For uninitialized voxels, check if they're below terrain
    FVector WorldPos = CornerWSPositions[i];
    bool bCornerBelowTerrain = WorldPos.Z < TerrainHeight;
    
    if (bCornerBelowTerrain)
    {
        // Default to solid for unset voxels below terrain
        CornerSDFValues[i] = -1.0f;
        
        // Check for nearby explicit air voxels that should create a surface
        bool bFoundNearbyAir = false;
        float MinDistanceToAir = FLT_MAX;
        
        // Search in a small radius around this corner
        const int32 SearchRadius = 2;
        const float MaxInfluenceDistance = SearchRadius * VoxelSize;
        
        for (int32 dx = -SearchRadius; dx <= SearchRadius; dx++)
        for (int32 dy = -SearchRadius; dy <= SearchRadius; dy++)
        for (int32 dz = -SearchRadius; dz <= SearchRadius; dz++)
        {
            if (dx == 0 && dy == 0 && dz == 0) continue;
            
            FIntVector SearchCoords = CornerCoords + FIntVector(dx, dy, dz);
            
            if (InVoxelGrid->VoxelData.Contains(SearchCoords))
            {
                float NearbySDFValue = InVoxelGrid->GetVoxel(SearchCoords.X, SearchCoords.Y, SearchCoords.Z);
                
                // If we find an explicit air voxel
                if (NearbySDFValue > 0)
                {
                    FVector NearbyWorldPos = Origin + FVector(SearchCoords) * VoxelSize;
                    
                    // Check if this air voxel is also below terrain (creating a cavity)
                    if (NearbyWorldPos.Z < TerrainHeight)
                    {
                        float Distance = FVector(dx, dy, dz).Size() * VoxelSize;
                        if (Distance < MinDistanceToAir)
                        {
                            MinDistanceToAir = Distance;
                            bFoundNearbyAir = true;
                        }
                    }
                }
            }
        }
        
        // If we found nearby air voxels, create a gradient towards them
        if (bFoundNearbyAir && MinDistanceToAir < MaxInfluenceDistance)
        {
            // Create a smooth transition from solid to air based on distance
            float InfluenceFactor = 1.0f - (MinDistanceToAir / MaxInfluenceDistance);
            
            // Interpolate from solid (-1) towards air based on influence
            // This creates the sign change needed for surface generation
            CornerSDFValues[i] = FMath::Lerp(-1.0f, 0.5f, InfluenceFactor);
            
            // Alternative approach: Use a more aggressive transition
            // CornerSDFValues[i] = (MinDistanceToAir < VoxelSize) ? 0.1f : -1.0f;
        }
    }
    else
    {
        // Above terrain - default to air
        CornerSDFValues[i] = 1.0f;
    }
}
            }

            if (IsDebugging()) {
                // Only visualize cells with explicit voxels or below-terrain cells with air voxels
                if (bHasExplicitVoxels || (bBelowTerrain && bHasAirVoxelsBelowTerrain)) {
                    for (int32 i = 0; i < 8; ++i) {
                        FColor PointColor = CornerSDFValues[i] > 0 ? FColor::Blue : FColor::Red;
                        DrawDebugPoint(
                            DiggerManager->GetWorld(),
                            CornerWSPositions[i],
                            5.0f,
                            PointColor,
                            false,
                            10.0f
                        );
                    }
                    
                    // Draw cell bounds
                    DrawDebugBox(
                        DiggerManager->GetWorld(),
                        CellOrigin + FVector(VoxelSize * 0.5f),
                        FVector(VoxelSize * 0.5f),
                        FQuat::Identity,
                        FColor::Yellow,
                        false,
                        10.0f,
                        0,
                        1.0f
                    );
                }
            }

            TArray<float> SDFValuesArray;
            SDFValuesArray.Append(CornerSDFValues, 8);

            int32 CubeIndex = CalculateMarchingCubesIndex(SDFValuesArray);
            if (CubeIndex == 0 || CubeIndex == 255) {
                continue;
            }

            for (int32 i = 0; TriangleConnectionTable[CubeIndex][i] != -1; i += 3) {
                FVector Vertices[3];

                for (int32 j = 0; j < 3; ++j) {
                    int32 EdgeIndex = TriangleConnectionTable[CubeIndex][i + j];
                    FVector Vertex = InterpolateVertex(
                        CornerWSPositions[EdgeConnection[EdgeIndex][0]],
                        CornerWSPositions[EdgeConnection[EdgeIndex][1]],
                        CornerSDFValues[EdgeConnection[EdgeIndex][0]],
                        CornerSDFValues[EdgeConnection[EdgeIndex][1]]
                    );
                    FVector AdjustedVertex = ApplyLandscapeTransition(Vertex);
                    Vertices[j] = AdjustedVertex;
                }

                for (int32 j = 0; j < 3; ++j) {
                    // Apply offset to vertex before caching/lookup
                    FVector OffsetVertex = Vertices[j] + TotalOffset;
                    
                    int32* CachedIndex = VertexCache.Find(OffsetVertex);
                    if (CachedIndex) {
                        OutTriangles.Add(*CachedIndex);
                    } else {
                        int32 NewIndex = OutVertices.Add(OffsetVertex);
                        VertexCache.Add(OffsetVertex, NewIndex);
                        OutTriangles.Add(NewIndex);
                    }
                }
            }
        }
    }

    // Calculate smooth normals
    OutNormals.SetNum(OutVertices.Num(), false);
    for (int32 i = 0; i < OutNormals.Num(); ++i) {
        OutNormals[i] = FVector::ZeroVector;
    }

    for (int32 i = 0; i < OutTriangles.Num(); i += 3) {
        const FVector& A = OutVertices[OutTriangles[i]];
        const FVector& B = OutVertices[OutTriangles[i + 1]];
        const FVector& C = OutVertices[OutTriangles[i + 2]];
        FVector Normal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
        Normal = -Normal; // Flip as in original
        OutNormals[OutTriangles[i]]     += Normal;
        OutNormals[OutTriangles[i + 1]] += Normal;
        OutNormals[OutTriangles[i + 2]] += Normal;
    }

    for (int32 i = 0; i < OutNormals.Num(); ++i) {
        OutNormals[i].Normalize();
    }

    // Apply world space offset to all vertices after mesh generation is complete
    for (int32 i = 0; i < OutVertices.Num(); ++i) {
        OutVertices[i] += TotalOffset;
    }

	/*TArray<int32> RimVertices;
	FindRimVertices(OutVertices, OutTriangles, RimVertices);
	AddSkirtMesh(RimVertices, OutVertices, OutTriangles, OutNormals);*/

	
	// NEW: Generate vertex colors based on voxel material data
	// ToDo: GenerateVertexColors(OutVertices, OutVertexColors);

	// Optional: Generate UVs (even though we're using triplanar, some shaders might need them)
	// ToDo: GenerateUVs(OutVertices, OutUVs);

	// Apply world space offset to all vertices after mesh generation is complete
	for (int32 i = 0; i < OutVertices.Num(); ++i) {
		OutVertices[i] += TotalOffset;
	}

    if (IsDebugging()) {
        UE_LOG(LogTemp, Log, TEXT("Mesh generated from grid: %d vertices, %d triangles."), OutVertices.Num(), OutTriangles.Num());
        UE_LOG(LogTemp, Log, TEXT("Cells with explicit voxels: %d"), CellsWithExplicitVoxels.Num());
        UE_LOG(LogTemp, Log, TEXT("Below-terrain cells with air voxels: %d"), BelowTerrainCellsWithAirVoxels.Num());
    	UE_LOG(LogTemp, Log, TEXT("Mesh generated: %d vertices, %d triangles, %d vertex colors"), 
			   OutVertices.Num(), OutTriangles.Num(), OutVertexColors.Num());
    	
        // Visualize below-terrain cells with air voxels
        for (const FIntVector& Cell : BelowTerrainCellsWithAirVoxels) {
            FVector CellOrigin = Origin + FVector(Cell) * VoxelSize;
            DrawDebugBox(
                DiggerManager->GetWorld(),
                CellOrigin + FVector(VoxelSize * 0.5f),
                FVector(VoxelSize * 0.5f),
                FQuat::Identity,
                FColor::Green,
                false,
                10.0f,
                0,
                2.0f
            );
        }
    }
}

// NEW: Function to generate vertex colors from voxel data
/*void UMarchingCubes::GenerateVertexColors(
	const TArray<FVector>& Vertices, 
	TArray<FColor>& OutVertexColors)
{
	OutVertexColors.SetNum(Vertices.Num());

	for (int32 i = 0; i < Vertices.Num(); ++i)
	{
		const FVector& WorldPos = Vertices[i];
        
		// Sample voxel data at this vertex position
		FVoxelData VoxelData = VoxelGrid->SampleVoxelDataAtPosition(WorldPos);
        
		// Calculate blend factor from SDF (0 = landscape, 1 = pure voxel)
		float BlendFactor = CalculateBlendFactor(VoxelData.SDFValue, WorldPos);
        
		// Pack material weights and blend factor into vertex color
		OutVertexColors[i] = PackMaterialData(VoxelData, BlendFactor);
	}
}

FVoxelData UMarchingCubes::SampleVoxelDataAtPosition(const FVector& WorldPos)
{
    // Convert world position to voxel grid coordinates
    FVector LocalPos = WorldPos - FVoxelConversion::Origin;
    FIntVector VoxelCoord = FIntVector(
        FMath::FloorToInt(LocalPos.X / FVoxelConversion::LocalVoxelSize),
        FMath::FloorToInt(LocalPos.Y / FVoxelConversion::LocalVoxelSize),
        FMath::FloorToInt(LocalPos.Z / FVoxelConversion::LocalVoxelSize)
    );

    // Sample the voxel data (adjust based on your voxel storage system)
    FVoxelData VoxelData;
    
   if (true)// if (IsValid(VoxelCoord))
    {
        // Get voxel data from your storage system
        VoxelData = *VoxelGrid->GetVoxelData(VoxelCoord);
        
        // If material weights aren't set, calculate them based on world position
        if (VoxelData.RockWeight + VoxelData.DirtWeight + VoxelData.GrassWeight <= 0.0f)
        {
            VoxelData.SetMaterialByHeight(WorldPos.Z);
        }
    }
    else
    {
        // Default voxel data for out-of-bounds positions
        VoxelData = FVoxelData(1.0f); // Positive SDF (outside)
        VoxelData.SetMaterialByHeight(WorldPos.Z); 
    }

    return VoxelData;
}
*/
// Calculate blend factor between landscape and voxel materials
float UMarchingCubes::CalculateBlendFactor(float SDFValue, const FVector& WorldPos)
{
    // Option 1: Simple SDF-based blending
    float BlendFactor = FMath::Clamp((SDFValue + 1.0f) / 2.0f, 0.0f, 1.0f);
    
    // Option 2: Distance from landscape surface
    // float LandscapeHeight = GetLandscapeHeightAtPosition(WorldPos);
    // float DistanceFromLandscape = FMath::Abs(WorldPos.Z - LandscapeHeight);
    // BlendFactor = FMath::Clamp(DistanceFromLandscape / BlendDistance, 0.0f, 1.0f);
    
    // Option 3: Combination of both
    // BlendFactor = FMath::Max(BlendFactor, DistanceBlendFactor);
    
    return BlendFactor;
}

// Pack material data into vertex color (RGBA channels)
/* ToDo: FColor UMarchingCubes::PackMaterialData(const FVoxelData& VoxelData, float BlendFactor)
{
    // Ensure material weights are normalized
    FVoxelData NormalizedData = VoxelData;
    NormalizedData.NormalizeMaterialWeights();
    
    // Pack into vertex color:
    // R = Rock weight (0-255)
    // G = Dirt weight (0-255)  
    // B = Grass weight (0-255)
    // A = Blend factor (0-255)
    
    uint8 RockValue = (uint8)(NormalizedData.RockWeight * 255.0f);
    uint8 DirtValue = (uint8)(NormalizedData.DirtWeight * 255.0f);
    uint8 GrassValue = (uint8)(NormalizedData.GrassWeight * 255.0f);
    uint8 BlendValue = (uint8)(FMath::Clamp(BlendFactor, 0.0f, 1.0f) * 255.0f);
    
    return FColor(RockValue, DirtValue, GrassValue, BlendValue);
}*/

// Optional: Generate UVs for vertices (simple planar projection)
/* ToDo: void UMarchingCubes::GenerateUVs(const TArray<FVector>& Vertices, TArray<FVector2D>& OutUVs)
{
    OutUVs.SetNum(Vertices.Num());
    
    const float UVScale = 0.01f; // Adjust based on your world scale
    
    for (int32 i = 0; i < Vertices.Num(); ++i)
    {
        const FVector& Vertex = Vertices[i];
        
        // Simple XY projection for UVs (triplanar handles the rest)
        OutUVs[i] = FVector2D(
            Vertex.X * UVScale,
            Vertex.Y * UVScale
        );
    }
}
*/
// Update your mesh component creation to include vertex colors
void UMarchingCubes::UpdateMeshComponent(UProceduralMeshComponent* MeshComponent)
{
    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FColor> VertexColors;    // NEW
    TArray<FVector2D> UVs;          // NEW
    
    // Generate mesh with material data
    // ToDo: GenerateMeshWithMaterials(Vertices, Triangles, Normals, VertexColors, UVs);
    
    // Create mesh section with vertex colors
    TArray<FProcMeshTangent> Tangents; // Can be empty for triplanar materials
    
    MeshComponent->CreateMeshSection_LinearColor(
        0,                                    // Section index
        Vertices,                            // Vertices
        Triangles,                           // Triangles
        Normals,                             // Normals
        UVs,                                 // UVs
        TArray<FLinearColor>(),              // Vertex colors (convert FColor to FLinearColor if needed)
        Tangents,                            // Tangents
        true                                 // Create collision
    );
    
    // Alternative: If you need to convert FColor to FLinearColor
    TArray<FLinearColor> LinearVertexColors;
    LinearVertexColors.Reserve(VertexColors.Num());
    for (const FColor& Color : VertexColors)
    {
        LinearVertexColors.Add(FLinearColor(Color));
    }
    
    // Then use LinearVertexColors in CreateMeshSection_LinearColor
}

// Example: Enhanced voxel modification for digging/building
void UMarchingCubes::ModifyVoxelWithMaterial(
    const FIntVector& VoxelCoord, 
    float NewSDFValue, 
    float RockWeight = 0.0f,
    float DirtWeight = 1.0f, 
    float GrassWeight = 0.0f)
{
    FVoxelData NewVoxelData(NewSDFValue, RockWeight, DirtWeight, GrassWeight);
    NewVoxelData.NormalizeMaterialWeights();
    /* ToDo:
    SetVoxel(VoxelCoord, NewVoxelData);
    
    // Mark chunk for remeshing
    MarkChunkForUpdate(VoxelCoord);
    */
}


void UMarchingCubes::InitializeHeightCache(const FVector& ChunkOrigin, float VoxelSize)
{
	if (!DiggerManager)
	{
		if (DiggerDebug::Manager)
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

	if (DiggerDebug::Chunks || DiggerDebug::Landscape)
	UE_LOG(LogTemp, Log, TEXT("Initializing height cache for chunk at %s with %dx%d samples"), 
		   *ChunkOrigin.ToString(), TotalSize, TotalSize);
    
	// Sample heights across the extended grid
	for (int32 x = 0; x < TotalSize; ++x)
	{
		for (int32 y = 0; y < TotalSize; ++y)
		{
			FVector SamplePos = SampleStart + FVector(x * VoxelSize, y * VoxelSize, 0);
            
			// Use your precise terrain sampling method
			float Height = 0.0f;
			if (ALandscapeProxy* LandscapeProxy = DiggerManager->GetLandscapeProxyAt(SamplePos))
			{
				TOptional<float> SampledHeight = DiggerManager->GetLandscapeHeightAt(SamplePos);
				Height = SampledHeight.IsSet() ? SampledHeight.GetValue() : 0.0f;
			}
            
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

	if (DiggerDebug::Landscape)
	UE_LOG(LogTemp, Log, TEXT("Height cache initialized with %d entries"), HeightCache.Num());
}

float UMarchingCubes::GetCachedHeight(const FVector& WorldPosition) const
{
	if (!bHeightCacheInitialized)
	{
		if (DiggerDebug::Landscape)
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

void UMarchingCubes::GenerateMeshForIsland(
	USparseVoxelGrid* IslandGrid,
	const FVector& Origin,
	float VoxelSize,
	int32 IslandId
)
{
	TArray<FVector> OutVertices;
	TArray<int32> OutTriangles;
	TArray<FVector> OutNormals;

	GenerateMeshFromGrid(IslandGrid, Origin, VoxelSize, OutVertices, OutTriangles, OutNormals);

	if (OutVertices.Num() > 0 && OutTriangles.Num() > 0 && OutNormals.Num() > 0) {
		// Call mesh creation on game thread
		AsyncTask(ENamedThreads::GameThread, [=]()
		{
			CreateIslandProceduralMesh(OutVertices, OutTriangles, OutNormals, Origin, IslandId);
		});
	} else {
		if (DiggerDebug::Mesh || DiggerDebug::Islands)
		UE_LOG(LogTemp, Warning, TEXT("Island mesh generation returned empty data"));
	}
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
		if (DiggerDebug::Manager)
		UE_LOG(LogTemp, Error, TEXT("DiggerManager is null in CreateIslandProceduralMesh"));
		return;
	}

	// Create a new ProceduralMeshComponent dynamically
	FString MeshName = FString::Printf(TEXT("IslandMesh_%d"), IslandId);
	UProceduralMeshComponent* IslandMesh = NewObject<UProceduralMeshComponent>(DiggerManager, *MeshName);
	if (!IslandMesh) {
		if (DiggerDebug::Islands)
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

	if (DiggerDebug::Islands)
	UE_LOG(LogTemp, Log, TEXT("Island mesh %d created at origin %s with %d vertices."), IslandId, *Origin.ToString(), Vertices.Num());

	// Optional: Add to an array for future management
	DiggerManager->IslandMeshes.Add(IslandMesh);
}





void UMarchingCubes::ReconstructMeshSection(int32 SectionIndex, const TArray<FVector>& OutOutVertices, const TArray<int32>& OutTriangles, const TArray<FVector>& Normals) const {
    // Validate pointers
    if (!DiggerManager || !DiggerManager->ProceduralMesh) {
    	if (DiggerDebug::Manager || DiggerDebug::Mesh)
        UE_LOG(LogTemp, Error, TEXT("DiggerManager or ProceduralMesh is null in ReconstructMeshSection"));
        return;
    }

    // Validate SectionIndex
    if (SectionIndex < 0) {
    	if (DiggerDebug::Manager || DiggerDebug::Mesh)
        UE_LOG(LogTemp, Error, TEXT("Invalid SectionIndex in ReconstructMeshSection: %d"), SectionIndex);
        return;
    }

    // Validate Mesh Data
    if (OutOutVertices.Num() == 0 || OutTriangles.Num() == 0 || Normals.Num() == 0) {
    	if (DiggerDebug::Manager || DiggerDebug::Mesh)
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
