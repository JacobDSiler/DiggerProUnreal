#include "MarchingCubes.h"
#include "EigenShim.h"
#include <C:\Users\serpe\Documents\Unreal Projects\DiggerProUnreal\Plugins\Eigen\Dense> // This is the only header you need
#include "DiggerManager.h"
#include "EngineUtils.h"
#include "SparseVoxelGrid.h"
#include "StaticMeshOperations.h"
#include "UDynamicMesh.h"
#include "VoxelChunk.h"
#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "Components/InstancedStaticMeshComponent.h"


using namespace Eigen;

FVector SolveQEF(const TArray<FVector>& Positions, const TArray<FVector>& Normals)
{
	const int32 Count = Positions.Num();
	if (Count == 0 || Count != Normals.Num())
	{
		return FVector::ZeroVector;
	}

	MatrixXf A(Count, 3); // Normals
	VectorXf b(Count);    // Dot products

	for (int32 i = 0; i < Count; ++i)
	{
		const FVector& N = Normals[i];
		const FVector& P = Positions[i];
		A.row(i) << N.X, N.Y, N.Z;
		b(i) = FVector::DotProduct(N, P);
	}

	// Solve A * x = b using least squares
	Vector3f x = A.colPivHouseholderQr().solve(b);

	return FVector(x(0), x(1), x(2));
}


// Add this near your existing SolveQEF (requires Eigen/Dense).
FVector SolveQEFRegularized(const TArray<FVector>& Positions, const TArray<FVector>& Normals, const FVector& Pin = FVector::ZeroVector, float Lambda = 1e-3f)
{
	const int Count = Positions.Num();
	if (Count == 0 || Count != Normals.Num())
		return FVector::ZeroVector;

	// Build AtA and Atb manually into fixed-size containers (3x3, 3x1)
	Eigen::Matrix3f AtA = Eigen::Matrix3f::Zero();
	Eigen::Vector3f Atb = Eigen::Vector3f::Zero();

	for (int i = 0; i < Count; ++i)
	{
		const Eigen::Vector3f n(Normals[i].X, Normals[i].Y, Normals[i].Z);
		const Eigen::Vector3f p(Positions[i].X, Positions[i].Y, Positions[i].Z);
		AtA += n * n.transpose();        // outer product
		Atb += n * n.dot(p);             // n * (n·p)
	}

	// Regularization: minimize ||A x - b||^2 + lambda * ||x - pin||^2
	// => (AtA + lambda I) x = Atb + lambda * pin
	if (Lambda > 0.f)
	{
		AtA += Lambda * Eigen::Matrix3f::Identity();
		Atb += Lambda * Eigen::Vector3f(Pin.X, Pin.Y, Pin.Z);
	}

	// Solve 3x3 system with Eigen fast fixed-size solver
	Eigen::Vector3f x = AtA.ldlt().solve(Atb);

	return FVector(x(0), x(1), x(2));
}


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
    	if (DiggerDebug::Voxels())
        UE_LOG(LogTemp, Warning, TEXT("No InVoxelGrid and/or data to generate mesh! InVoxelGrid: %s, VoxelData.Num(): %d"),
            InVoxelGrid ? TEXT("Valid") : TEXT("Invalid"),
            InVoxelGrid ? InVoxelGrid->VoxelData.Num() : 0)
        return;
    }
	if(InVoxelGrid->VoxelData.IsEmpty())
	{
		if (DiggerDebug::Voxels())
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
		if (DiggerDebug::Voxels())
			UE_LOG(LogTemp, Warning, TEXT("No InVoxelGrid and/or data to generate mesh! InVoxelGrid: %s, VoxelData.Num(): %d"),
				InVoxelGrid ? TEXT("Valid") : TEXT("Invalid"),
				InVoxelGrid ? InVoxelGrid->VoxelData.Num() : 0)
		return;
	}
	if(InVoxelGrid->VoxelData.IsEmpty())
	{
		if (DiggerDebug::Voxels())
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
	GenerateMCMeshFromGridSyncronous(InVoxelGrid, ChunkOrigin , VoxelSize, OutVertices, OutTriangles, OutNormals);

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

	TOptional<float> Height = DiggerManager->GetLandscapeHeightAt(VertexWS);
	if (!Height.IsSet())
	{
		// No Landscape Proxy Found
		if (DiggerDebug::Landscape())
			UE_LOG(LogTemp, Warning, TEXT("No Landscape Found at current x / y coordinate"));
		// Skip voxel write or mesh generation
		return VertexWS;
	}
	float LandscapeZ = Height.GetValue();
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

    	TOptional<float> Height = DiggerManager->GetLandscapeHeightAt(RimV);
    	if (!Height.IsSet())
    	{
    		// No Landscape Proxy Found
    		if (DiggerDebug::Landscape())
    			UE_LOG(LogTemp, Warning, TEXT("No Landscape Found at current x / y coordinate"));
    		// Skip voxel write or mesh generation
    		return;
    	}
    	
        SkirtV.Z = Height.GetValue();

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

void UMarchingCubes::GenerateMCMeshFromGridSyncronous(
	USparseVoxelGrid* InVoxelGrid,
	const FVector& Origin,
	float VoxelSize,
	TArray<FVector>& OutVertices,
	TArray<int32>& OutTriangles,
	TArray<FVector>& OutNormals
)
{
	// This synchronous wrapper simply forwards to the main mesh generator.
	// Because GenerateMeshFromGrid runs entirely on the calling thread
	// (no task scheduling or parallel processing), calling it here will
	// compute the mesh immediately and fill OutVertices/OutTriangles/OutNormals.
	GenerateMCMeshFromGrid(InVoxelGrid, Origin, VoxelSize, OutVertices, OutTriangles, OutNormals);
}


void UMarchingCubes::GenerateCubicMesh(
	USparseVoxelGrid* InVoxelGrid,
	const FVector& Origin,
	float VoxelSize,
	TArray<FVector>& OutVertices,
	TArray<int32>& OutTriangles,
	TArray<FVector>& OutNormals)
{
	if (!InVoxelGrid) return;
	UE_LOG(LogTemp, Warning, TEXT("Trying to generate a Cubic Voxel Visualization!"));
	/*// Create or reuse an InstancedStaticMeshComponent
	if (!CubicMeshComponent)
	{
		CubicMeshComponent = NewObject<UInstancedStaticMeshComponent>(this);
		CubicMeshComponent->RegisterComponent();
		CubicMeshComponent->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);

		// Assign cube mesh and triplanar material
		static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube"));
		if (CubeMesh.Succeeded())
		{
			CubicMeshComponent->SetStaticMesh(CubeMesh.Object);
		}

		if (TriplanarSedimentMaterial)
		{
			CubicMeshComponent->SetMaterial(0, TriplanarSedimentMaterial);
		}
	}
	else
	{
		CubicMeshComponent->ClearInstances();
	}

	// Iterate over voxel grid
	const FIntVector GridSize = InVoxelGrid->GetGridSize();
	for (int32 X = 0; X < GridSize.X; ++X)
	{
		for (int32 Y = 0; Y < GridSize.Y; ++Y)
		{
			for (int32 Z = 0; Z < GridSize.Z; ++Z)
			{
				if (InVoxelGrid->IsSolid(X, Y, Z))
				{
					FVector VoxelWorldPos = Origin + FVector(X, Y, Z) * VoxelSize;
					FTransform InstanceTransform(FQuat::Identity, VoxelWorldPos, FVector(VoxelSize / 100.0f)); // scale to cm
					CubicMeshComponent->AddInstance(InstanceTransform);
				}
			}
		}
	}*/

	// Clear traditional mesh outputs
	OutVertices.Empty();
	OutTriangles.Empty();
	OutNormals.Empty();
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
	if (!DiggerManager)
	{
		if (DiggerDebug::Manager())
			UE_LOG(LogTemp, Warning, TEXT("DiggerManager is null, cannot initialize height cache"));
		return;
	}
	
	switch (DiggerManager->GetMeshGenerationMethod())
	{
	case EMeshGenerationMethod::Cubic:
		GenerateCubicMesh(InVoxelGrid,
					   Origin,
					   VoxelSize,
					   OutVertices,
					   OutTriangles,
					   OutNormals);
		break;
	case EMeshGenerationMethod::MarchingCubes:
		GenerateMCMeshFromGrid(InVoxelGrid,
					   Origin,
					   VoxelSize,
					   OutVertices,
					   OutTriangles,
					   OutNormals);
		break;
	case EMeshGenerationMethod::DualContouring:
		GenerateDCMeshFromGrid(InVoxelGrid,
					   Origin,
					   VoxelSize,
					   OutVertices,
					   OutTriangles,
					   OutNormals);;
		break;
	}


}

// Make sure <Eigen/Dense> is included above in this translation unit.
// Also ensure the Eigen MSVC compatibility shim we discussed earlier is present before including Eigen if you target MSVC.

void UMarchingCubes::GenerateDCMeshFromGrid(
    USparseVoxelGrid* InVoxelGrid,
    const FVector& Origin,
    float VoxelSize,
    TArray<FVector>& OutVertices,
    TArray<int32>& OutTriangles,
    TArray<FVector>& OutNormals)
{
    if (!InVoxelGrid)
    {
        UE_LOG(LogTemp, Error, TEXT("Invalid VoxelGrid in GenerateDCMeshFromGrid!"));
        return;
    }

    if (!IsHeightCacheValid(Origin, VoxelSize))
    {
        InitializeHeightCache(Origin, VoxelSize);
    }

    const int32 N = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
    const FVector TotalOffset = FVector(FVoxelConversion::LocalVoxelSize * 0.25F - FVoxelConversion::ChunkWorldSize * 0.5f);

    // --- Bounding box filter for sparse grids (massive perf gain)
    FIntVector MinV(INT_MAX, INT_MAX, INT_MAX);
    FIntVector MaxV(INT_MIN, INT_MIN, INT_MIN);

    for (const TPair<FIntVector, FVoxelData>& KV : InVoxelGrid->VoxelData)
    {
        const FIntVector& k = KV.Key;
        MinV.X = FMath::Min(MinV.X, k.X);
        MinV.Y = FMath::Min(MinV.Y, k.Y);
        MinV.Z = FMath::Min(MinV.Z, k.Z);
        MaxV.X = FMath::Max(MaxV.X, k.X);
        MaxV.Y = FMath::Max(MaxV.Y, k.Y);
        MaxV.Z = FMath::Max(MaxV.Z, k.Z);
    }

    if (MinV.X == INT_MAX)
    {
        UE_LOG(LogTemp, Warning, TEXT("Voxel grid empty"));
        return;
    }

    MinV -= FIntVector(1, 1, 1);
    MaxV += FIntVector(1, 1, 1);

    static const FIntVector CornerOffsets[8] = {
        FIntVector(0,0,0), FIntVector(1,0,0), FIntVector(1,1,0), FIntVector(0,1,0),
        FIntVector(0,0,1), FIntVector(1,0,1), FIntVector(1,1,1), FIntVector(0,1,1)
    };
    static const int EdgeEnds[12][2] = {
        {0,1},{1,2},{2,3},{3,0},
        {4,5},{5,6},{6,7},{7,4},
        {0,4},{1,5},{2,6},{3,7}
    };

    auto ComputeCornerSDFsForCell = [&](const FIntVector& Cell, TArray<float>& OutSDFs, TArray<FVector>* OutCorners = nullptr)
    {
        OutSDFs.SetNumZeroed(8);
        if (OutCorners) OutCorners->SetNumZeroed(8);
        for (int i = 0; i < 8; ++i)
        {
            FIntVector Corner = Cell + CornerOffsets[i];
            FVector P = Origin + FVector(Corner) * VoxelSize;

            if (InVoxelGrid->VoxelData.Contains(Corner))
                OutSDFs[i] = InVoxelGrid->GetVoxel(Corner.X, Corner.Y, Corner.Z);
            else
            {
                float H = GetCachedHeight(P);
                float Delta = P.Z - H;
                const float MaxDepth = VoxelSize * 2.f;
                OutSDFs[i] = FMath::Clamp(Delta / MaxDepth, -1.f, 1.f);
            }

            if (OutCorners)
                (*OutCorners)[i] = P;
        }
    };

    // Local QEF solver (Eigen regularized)
    auto SolveQEFRegularized = [&](const TArray<FVector>& Positions, const TArray<FVector>& Normals, const FVector& Pin, float Lambda = 0.001f) -> FVector
    {
        const int Count = Positions.Num();
        if (Count == 0 || Count != Normals.Num())
            return FVector::ZeroVector;

        Eigen::Matrix3f AtA = Eigen::Matrix3f::Zero();
        Eigen::Vector3f Atb = Eigen::Vector3f::Zero();

        for (int i = 0; i < Count; ++i)
        {
            const Eigen::Vector3f n(Normals[i].X, Normals[i].Y, Normals[i].Z);
            const Eigen::Vector3f p(Positions[i].X, Positions[i].Y, Positions[i].Z);
            AtA += n * n.transpose();
            Atb += n * n.dot(p);
        }

        AtA += Lambda * Eigen::Matrix3f::Identity();
        Atb += Lambda * Eigen::Vector3f(Pin.X, Pin.Y, Pin.Z);

        Eigen::Vector3f x = AtA.ldlt().solve(Atb);
        return FVector(x(0), x(1), x(2));
    };

    // --- Cell vertex build
    TMap<FIntVector, int32> CellVertexIndex;
    TMap<FVector, int32> VertexCache;
    TArray<FVector> CandidateVerts;
    CandidateVerts.Reserve((MaxV.X - MinV.X + 1) * (MaxV.Y - MinV.Y + 1));

    for (int32 x = MinV.X; x <= MaxV.X; ++x)
    {
        for (int32 y = MinV.Y; y <= MaxV.Y; ++y)
        {
            for (int32 z = MinV.Z; z <= MaxV.Z; ++z)
            {
                FIntVector Cell(x, y, z);
                TArray<float> SDFs; TArray<FVector> Pos;
                ComputeCornerSDFsForCell(Cell, SDFs, &Pos);

                TArray<FVector> EdgePoints;
                TArray<FVector> EdgeNormals;

                for (int e = 0; e < 12; ++e)
                {
                    int a = EdgeEnds[e][0], b = EdgeEnds[e][1];
                    float Sa = SDFs[a], Sb = SDFs[b];
                    if ((Sa <= 0.f && Sb > 0.f) || (Sa > 0.f && Sb <= 0.f))
                    {
                        float t = Sa / (Sa - Sb);
                        FVector P = FMath::Lerp(Pos[a], Pos[b], t);
                        FVector SafeN = (Pos[b] - Pos[a]).GetSafeNormal();
                        EdgePoints.Add(P);
                        EdgeNormals.Add(SafeN);
                    }
                }

                if (EdgePoints.Num() == 0)
                    continue;

                FVector Pin = Origin + FVector(Cell) * VoxelSize + FVector(VoxelSize * 0.5f);
                FVector qef = SolveQEFRegularized(EdgePoints, EdgeNormals, Pin);
                qef = ApplyLandscapeTransition(qef);
                FVector Final = qef + TotalOffset;

                int32 Index = OutVertices.Add(Final);
                CellVertexIndex.Add(Cell, Index);
                VertexCache.Add(Final, Index);
            }
        }
    }

    // --- Robust face filler (never leaves holes)
    auto AddFaceOrFill = [&](const FIntVector& c00, const FIntVector& c10, const FIntVector& c11, const FIntVector& c01)
    {
        int32 i00 = CellVertexIndex.Contains(c00) ? CellVertexIndex[c00] : -1;
        int32 i10 = CellVertexIndex.Contains(c10) ? CellVertexIndex[c10] : -1;
        int32 i11 = CellVertexIndex.Contains(c11) ? CellVertexIndex[c11] : -1;
        int32 i01 = CellVertexIndex.Contains(c01) ? CellVertexIndex[c01] : -1;

        if (i00 >= 0 && i10 >= 0 && i11 >= 0 && i01 >= 0)
        {
            OutTriangles.Append({ i00, i10, i11, i00, i11, i01 });
            return;
        }

        // collect intersection points if missing vertices
        TArray<FVector> pts;
        FIntVector cells[4] = { c00, c10, c11, c01 };
        for (FIntVector C : cells)
        {
            TArray<float> sdf; TArray<FVector> pos;
            ComputeCornerSDFsForCell(C, sdf, &pos);
            for (int e = 0; e < 12; ++e)
            {
                int a = EdgeEnds[e][0], b = EdgeEnds[e][1];
                float Sa = sdf[a], Sb = sdf[b];
                if ((Sa <= 0.f && Sb > 0.f) || (Sa > 0.f && Sb <= 0.f))
                {
                    float t = Sa / (Sa - Sb);
                    pts.Add(FMath::Lerp(pos[a], pos[b], t));
                }
            }
        }

        if (pts.Num() < 3)
            return;

        FVector centroid(0, 0, 0);
        for (auto& p : pts) centroid += p;
        centroid /= pts.Num();

        FVector normal(0, 0, 0);
        for (int i = 0; i < pts.Num(); ++i)
        {
            const FVector& a = pts[i];
            const FVector& b = pts[(i + 1) % pts.Num()];
            normal += FVector::CrossProduct(a - centroid, b - centroid);
        }
        normal.Normalize();

        FVector u = (FMath::Abs(normal.Z) > 0.7f) ? FVector(1, 0, 0) : FVector::CrossProduct(FVector(0, 0, 1), normal).GetSafeNormal();
        FVector v = FVector::CrossProduct(normal, u);

        TArray<TPair<float, int>> order;
        for (int i = 0; i < pts.Num(); ++i)
        {
            FVector d = pts[i] - centroid;
            float ang = FMath::Atan2(FVector::DotProduct(d, v), FVector::DotProduct(d, u));
            order.Add({ ang, i });
        }
        order.Sort([](auto& A, auto& B) { return A.Key < B.Key; });

        TArray<int32> idx;
        for (auto& o : order)
        {
            FVector P = ApplyLandscapeTransition(pts[o.Value]) + TotalOffset;
            int32* found = VertexCache.Find(P);
            int id = found ? *found : OutVertices.Add(P);
            if (!found) VertexCache.Add(P, id);
            idx.Add(id);
        }

        for (int i = 1; i + 1 < idx.Num(); ++i)
            OutTriangles.Append({ idx[0], idx[i], idx[i + 1] });
    };

    // --- Faces
    for (int32 x = MinV.X; x < MaxV.X; ++x)
        for (int32 y = MinV.Y; y < MaxV.Y; ++y)
            for (int32 z = MinV.Z; z < MaxV.Z; ++z)
            {
                FIntVector c00(x, y, z);
                FIntVector c10(x + 1, y, z);
                FIntVector c11(x + 1, y + 1, z);
                FIntVector c01(x, y + 1, z);
                AddFaceOrFill(c00, c10, c11, c01);
            }

    // --- Hole filling pass
    auto PackEdge = [](int32 a, int32 b)
    {
        if (a > b) Swap(a, b);
        return (uint64)a | ((uint64)b << 32);
    };

    TMap<uint64, int32> EdgeCount;
    for (int i = 0; i + 2 < OutTriangles.Num(); i += 3)
    {
        int32 a = OutTriangles[i], b = OutTriangles[i + 1], c = OutTriangles[i + 2];
        for (auto e : { PackEdge(a, b), PackEdge(b, c), PackEdge(c, a) })
            EdgeCount.FindOrAdd(e)++;
    }

    TArray<TPair<int32, int32>> BoundaryEdges;
    for (auto& P : EdgeCount)
        if (P.Value == 1)
        {
            int32 a = (int32)(P.Key & 0xFFFFFFFF);
            int32 b = (int32)(P.Key >> 32);
            BoundaryEdges.Add({ a, b });
        }

    // fill simple loops by centroid fan
    for (auto& E : BoundaryEdges)
    {
        FVector A = OutVertices[E.Key];
        FVector B = OutVertices[E.Value];
        FVector Mid = (A + B) * 0.5f;
        FVector n = FVector::CrossProduct(A - Mid, B - Mid).GetSafeNormal();
        FVector C = Mid + n * (VoxelSize * 0.2f);
        int32 id = OutVertices.Add(C);
        OutTriangles.Append({ E.Key, E.Value, id });
    }

    // --- Normals
    OutNormals.SetNumZeroed(OutVertices.Num());
    for (int i = 0; i + 2 < OutTriangles.Num(); i += 3)
    {
        int32 a = OutTriangles[i], b = OutTriangles[i + 1], c = OutTriangles[i + 2];
        FVector n = FVector::CrossProduct(OutVertices[b] - OutVertices[a], OutVertices[c] - OutVertices[a]);
        OutNormals[a] += n; OutNormals[b] += n; OutNormals[c] += n;
    }
    for (FVector& SafeN : OutNormals)
        SafeN = -SafeN.GetSafeNormal();

    UE_LOG(LogTemp, Log, TEXT("DC mesh: %d verts, %d tris"), OutVertices.Num(), OutTriangles.Num() / 3);
}






void UMarchingCubes::GenerateMCMeshFromGrid(
	USparseVoxelGrid* InVoxelGrid,
	const FVector& Origin,
	float VoxelSize,
	TArray<FVector>& OutVertices,
	TArray<int32>& OutTriangles,
	TArray<FVector>& OutNormals
)
{
    if (!InVoxelGrid)
    {
        UE_LOG(LogTemp, Error, TEXT("Invalid VoxelGrid in GenerateMeshFromGrid!"));
        return;
    }

    // Initialize height cache on the game thread
    if (!IsHeightCacheValid(Origin, VoxelSize))
    {
        InitializeHeightCache(Origin, VoxelSize);
    }

    // Alignment offset for center-aligned chunks
    FVector TotalOffset = FVector(FVoxelConversion::LocalVoxelSize * 0.25F - FVoxelConversion::ChunkWorldSize * 0.5f);
    int32   N           = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;

    // Precache height values per XY column
    TArray<float> HeightValues;
    HeightValues.SetNumZeroed(N * N);
    for (int32 x = 0; x < N; ++x)
    {
        for (int32 y = 0; y < N; ++y)
        {
            FVector WorldPos           = Origin + FVector(x * VoxelSize, y * VoxelSize, 0);
            HeightValues[y * N + x]    = GetCachedHeight(WorldPos);
        }
    }

    // Track which cells have explicit voxels for debugging
    TArray<FIntVector> CellsWithExplicitVoxels;
    TArray<FIntVector> BelowTerrainCellsWithAirVoxels;

    // Identify cells that contain explicit air below the terrain (first pass)
    TSet<FIntVector> CellsWithAirVoxelsBelowTerrain;
    for (int32 x = 0; x < N; ++x)
    {
        for (int32 y = 0; y < N; ++y)
        {
            float TerrainHeight = HeightValues[y * N + x];
            for (int32 z = 0; z < N; ++z)
            {
                float MaxZ         = Origin.Z + (z * VoxelSize) + VoxelSize;
                bool  bBelowTerrain = MaxZ < TerrainHeight;
                if (!bBelowTerrain)
                    continue;

                // Check if any of the eight corners has explicit air
                for (int32 i = 0; i < 8; ++i)
                {
                    FIntVector Corner = FIntVector(x, y, z) + GetCornerOffset(i);
                    if (InVoxelGrid->VoxelData.Contains(Corner))
                    {
                        if (InVoxelGrid->GetVoxel(Corner.X, Corner.Y, Corner.Z) > 0.f)
                        {
                            CellsWithAirVoxelsBelowTerrain.Add(FIntVector(x, y, z));
                            break;
                        }
                    }
                }
            }
        }
    }

    // Mesh generation: iterate over every cell in the chunk
    TMap<FVector, int32> VertexCache;

    // New: store per-vertex landscape normals for snapped vertices (key = vertex index in OutVertices)
    TMap<int32, FVector> SnappedVertexNormals;

    for (int32 x = 0; x < N; ++x)
    {
        for (int32 y = 0; y < N; ++y)
        {
            float TerrainHeight = HeightValues[y * N + x];

            for (int32 z = 0; z < N; ++z)
            {
                // Determine if the entire cell is below the heightfield
                float MinZ        = Origin.Z + z * VoxelSize;
                float MaxZ        = MinZ + VoxelSize;
                bool  bBelowTerrain = MaxZ < TerrainHeight;

                // Determine if any explicit voxel exists in the cell
                bool bHasExplicitVoxels         = false;
                bool bHasAirVoxelsBelowTerrain  = false;

                for (int32 iCorner = 0; iCorner < 8 && !bHasExplicitVoxels; ++iCorner)
                {
                    FIntVector CornerCoords = FIntVector(x, y, z) + GetCornerOffset(iCorner);
                    if (InVoxelGrid->VoxelData.Contains(CornerCoords))
                    {
                        bHasExplicitVoxels = true;
                        // If this explicit voxel is air and below terrain, flag it
                        FVector CornerWorld = Origin + FVector(CornerCoords) * VoxelSize;
                        if (CornerWorld.Z < TerrainHeight)
                        {
                            if (InVoxelGrid->GetVoxel(CornerCoords.X, CornerCoords.Y, CornerCoords.Z) > 0)
                            {
                                bHasAirVoxelsBelowTerrain = true;
                            }
                        }
                    }
                }

                if (bHasExplicitVoxels)
                    CellsWithExplicitVoxels.Add(FIntVector(x, y, z));
                if (bBelowTerrain && bHasAirVoxelsBelowTerrain)
                    BelowTerrainCellsWithAirVoxels.Add(FIntVector(x, y, z));

                // Decide whether to process this cell (you already have logic for this)
                bool bShouldProcess = bHasExplicitVoxels;
                if (!bShouldProcess && bBelowTerrain)
                {
                    for (int32 dx = -1; dx <= 1 && !bShouldProcess; ++dx)
                    for (int32 dy = -1; dy <= 1 && !bShouldProcess; ++dy)
                    for (int32 dz = -1; dz <= 1 && !bShouldProcess; ++dz)
                    {
                        if (dx == 0 && dy == 0 && dz == 0) continue;
                        FIntVector Adj(x + dx, y + dy, z + dz);
                        if (Adj.X < 0 || Adj.Y < 0 || Adj.Z < 0 || Adj.X >= N || Adj.Y >= N || Adj.Z >= N)
                            continue;
                        if (CellsWithAirVoxelsBelowTerrain.Contains(Adj))
                            bShouldProcess = true;
                    }
                }
                else if (!bShouldProcess)
                {
                    if (MinZ <= TerrainHeight + VoxelSize)
                        bShouldProcess = true;
                }
                if (!bShouldProcess)
                    continue;

                // Set up corner positions and SDF values
                FVector CornerWSPositions[8];
                float   CornerSDFValues[8];

                // Precompute constant sign if no explicit voxels exist
                // All corners get the same sign so no sign change occurs
                bool  bUniformCell    = !bHasExplicitVoxels;
                float UniformSDFSign  = bBelowTerrain ? -1.0f : 1.0f;

                for (int32 iCorner = 0; iCorner < 8; ++iCorner)
                {
                    // Corner world position
                    FIntVector CornerCoords = FIntVector(x, y, z) + GetCornerOffset(iCorner);
                    FVector    P            = Origin + FVector(CornerCoords.X * VoxelSize,
                                                               CornerCoords.Y * VoxelSize,
                                                               CornerCoords.Z * VoxelSize);
                    CornerWSPositions[iCorner] = P;

                    // If explicit, use the explicit value
                    if (InVoxelGrid->VoxelData.Contains(CornerCoords))
                    {
                        CornerSDFValues[iCorner] = InVoxelGrid->GetVoxel(CornerCoords.X, CornerCoords.Y, CornerCoords.Z);
                        continue;
                    }

                    // If the cell has no explicit voxels at all, keep uniform SDF sign
                    if (bUniformCell)
                    {
                        CornerSDFValues[iCorner] = UniformSDFSign;
                        continue;
                    }

                    // Otherwise compute a continuous base SDF (positive above, negative below)
                    float LocalTerrainHeight = GetCachedHeight(P);
                    float VerticalDelta      = P.Z - LocalTerrainHeight;
                    const float MaxDepth     = VoxelSize * 2.f;
                    float BaseSDF            = FMath::Clamp(VerticalDelta / MaxDepth, -1.f, 1.f);

                    // If below terrain, search for air voxels and adjust
                    float FinalSDF = BaseSDF;
                    if (BaseSDF < 0.f)
                    {
                        bool  bFoundAir    = false;
                        float MinAirDist   = FLT_MAX;
                        const int32 SearchRadius = 2;
                        const float MaxInfluence = SearchRadius * VoxelSize;

                        // Look around for explicit air voxels that are below the terrain
                        for (int32 dx = -SearchRadius; dx <= SearchRadius; ++dx)
                        for (int32 dy = -SearchRadius; dy <= SearchRadius; ++dy)
                        for (int32 dz = -SearchRadius; dz <= SearchRadius; ++dz)
                        {
                            if (dx == 0 && dy == 0 && dz == 0) continue;
                            FIntVector SearchCoords = CornerCoords + FIntVector(dx, dy, dz);
                            if (InVoxelGrid->VoxelData.Contains(SearchCoords))
                            {
                                float NeighVal = InVoxelGrid->GetVoxel(SearchCoords.X, SearchCoords.Y, SearchCoords.Z);
                                if (NeighVal > 0)
                                {
                                    FVector NeighborPos   = Origin + FVector(SearchCoords) * VoxelSize;
                                    float   NeighborTerrain = GetCachedHeight(NeighborPos);
                                    if (NeighborPos.Z < NeighborTerrain)
                                    {
                                        float Dist = FVector(dx, dy, dz).Size() * VoxelSize;
                                        if (Dist < MinAirDist)
                                        {
                                            MinAirDist = Dist;
                                            bFoundAir  = true;
                                        }
                                    }
                                }
                            }
                        }

                        // Use the cavity smoothing if an air voxel is nearby
                        if (bFoundAir && MinAirDist < MaxInfluence)
                        {
                            float t      = FMath::Clamp(MinAirDist / MaxInfluence, 0.f, 1.f);
                            float AirSDF = FMath::Lerp(-1.f, 0.f, 1.f - t);
                            FinalSDF     = FMath::Max(BaseSDF, AirSDF);
                        }
                    }
                    CornerSDFValues[iCorner] = FinalSDF;
                }

                // Build the marching cubes configuration and emit triangles
                TArray<float> SDFValues;
                SDFValues.Append(CornerSDFValues, 8);
                int32 CubeIndex = CalculateMarchingCubesIndex(SDFValues);
                if (CubeIndex == 0 || CubeIndex == 255)
                    continue;

                for (int32 tri = 0; TriangleConnectionTable[CubeIndex][tri] != -1; tri += 3)
                {
                    FVector TriangleVerts[3];
                    for (int32 vi = 0; vi < 3; ++vi)
                    {
                        int32 EdgeIndex        = TriangleConnectionTable[CubeIndex][tri + vi];
                        FVector V0             = CornerWSPositions[EdgeConnection[EdgeIndex][0]];
                        FVector V1             = CornerWSPositions[EdgeConnection[EdgeIndex][1]];
                        float   S0             = CornerSDFValues[EdgeConnection[EdgeIndex][0]];
                        float   S1             = CornerSDFValues[EdgeConnection[EdgeIndex][1]];
                        FVector Interp         = InterpolateVertex(V0, V1, S0, S1);

                        // ---------------------------
                        // SNAP exactly: if the vertex is under the surface by < 1 voxel,
                        // set Z to the cached surface height (preserve X/Y). Also compute
                        // the landscape normal for later normal blending.
                        // ---------------------------
                        bool bSnappedThisVert = false;
                        FVector SnappedLandscapeNormal = FVector::ZeroVector;
                        {
                            float LocalHeightAtInterp = GetCachedHeight(Interp);
                            float DepthUnderSurface = LocalHeightAtInterp - Interp.Z; // positive if Interp is below surface

                            const float Epsilon = 1e-4f;
                            if (DepthUnderSurface > Epsilon && DepthUnderSurface < VoxelSize)
                            {
                                // Snap Z exactly to the surface
                                Interp.Z = LocalHeightAtInterp;
                                bSnappedThisVert = true;

                                // Estimate landscape normal from finite differences (same approach as earlier)
                                FVector SampleOffsetX(VoxelSize, 0.f, 0.f);
                                FVector SampleOffsetY(0.f, VoxelSize, 0.f);

                                float hL = GetCachedHeight(Interp - SampleOffsetX);
                                float hR = GetCachedHeight(Interp + SampleOffsetX);
                                float hD = GetCachedHeight(Interp - SampleOffsetY);
                                float hU = GetCachedHeight(Interp + SampleOffsetY);

                                float dHdX = (hR - hL) / (2.f * VoxelSize);
                                float dHdY = (hU - hD) / (2.f * VoxelSize);

                                FVector LandscapeNormal = FVector(-dHdX, -dHdY, 1.f).GetSafeNormal();

                                // We'll store the *negated* landscape normal later to match your final
                                // inversion step (so blending is applied in the same space).
                                SnappedLandscapeNormal = -LandscapeNormal;
                            }
                        }
                        // ---------------------------

                        FVector FinalVertBeforeTransition = ApplyLandscapeTransition(Interp);

                        // Add to mesh with caching
                        FVector FinalVert = FinalVertBeforeTransition + TotalOffset;
                        int32*  Cached    = VertexCache.Find(FinalVert);
                        if (Cached)
                        {
                            int32 Index = *Cached;
                            OutTriangles.Add(Index);

                            // If this interpolation was snapped, record/overwrite the stored normal for this index.
                            if (bSnappedThisVert)
                            {
                                SnappedVertexNormals.Add(Index, SnappedLandscapeNormal);
                            }
                        }
                        else
                        {
                            int32 NewIndex = OutVertices.Add(FinalVert);
                            VertexCache.Add(FinalVert, NewIndex);
                            OutTriangles.Add(NewIndex);

                            if (bSnappedThisVert)
                            {
                                SnappedVertexNormals.Add(NewIndex, SnappedLandscapeNormal);
                            }
                        }
                    }
                }
            }
        }
    }

    // Smooth per-vertex normals (existing algorithm)
    OutNormals.SetNum(OutVertices.Num());
    for (FVector& Normal : OutNormals)
        Normal = FVector::ZeroVector;

    for (int32 i = 0; i < OutTriangles.Num(); i += 3)
    {
        int32 i0 = OutTriangles[i];
        int32 i1 = OutTriangles[i + 1];
        int32 i2 = OutTriangles[i + 2];
        const FVector& v0 = OutVertices[i0];
        const FVector& v1 = OutVertices[i1];
        const FVector& v2 = OutVertices[i2];
        FVector Edge1 = v1 - v0;
        FVector Edge2 = v2 - v0;
        FVector Face  = FVector::CrossProduct(Edge1, Edge2);
        float   Area2 = Face.Size();
        if (Area2 > SMALL_NUMBER)
        {
            FVector UnitFace = Face / Area2;
            FVector Weighted = UnitFace * Area2;
            OutNormals[i0] += Weighted;
            OutNormals[i1] += Weighted;
            OutNormals[i2] += Weighted;
        }
    }

    // Normalize and invert (keeps exactly the same final direction as your version)
    for (FVector& outNormal : OutNormals)
    {
        outNormal = -outNormal.GetSafeNormal();
    }

    // ---------------------------
    // NEW: blend snapped vertex normals toward the landscape normal to avoid lighting seams.
    // The blend weight favors the landscape normal so lighting matches the terrain.
    // ---------------------------
    const float LandscapeBlendWeight = 0.85f; // how strongly to match the landscape normal (0..1)
    for (const TPair<int32, FVector>& Pair : SnappedVertexNormals)
    {
        int32 Index = Pair.Key;
        if (!OutNormals.IsValidIndex(Index)) continue;
        FVector LandscapeNegNormal = Pair.Value; // already negated when stored
        FVector Blended = FMath::Lerp(OutNormals[Index], LandscapeNegNormal, LandscapeBlendWeight).GetSafeNormal();
        OutNormals[Index] = Blended;
    }
    // ---------------------------

    UE_LOG(LogTemp, Log,
           TEXT("Generated mesh: %d vertices, %d triangles, %d cells with explicit voxels, %d below-terrain cells with air"),
           OutVertices.Num(), OutTriangles.Num() / 3,
           CellsWithExplicitVoxels.Num(), BelowTerrainCellsWithAirVoxels.Num());
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
		if (DiggerDebug::Mesh() || DiggerDebug::Islands())
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
FVector UMarchingCubes::InterpolateVertex(
	const FVector& P1, const FVector& P2, float SDF1, float SDF2)
{
	// Linear interpolation of zero‑crossing along the edge:contentReference[oaicite:3]{index=3}.
	// Clamp t to [0,1] and guard against nearly identical SDF values.
	if (FMath::IsNearlyEqual(SDF1, SDF2, KINDA_SMALL_NUMBER))
	{
		return (P1 + P2) * 0.5f;
	}
	const float t = FMath::Clamp((0.f - SDF1) / (SDF2 - SDF1), 0.f, 1.f);
	return P1 + t * (P2 - P1);
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
