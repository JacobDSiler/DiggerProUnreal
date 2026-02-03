#include "MarchingCubes.h"
#include "MarchingCubesTables.h" // <--- THE NEW HEADER
#include "DiggerLandscapeCache.h"
#include "DiggerManager.h"
#include "DiggerSettings.h"      // <--- THE RUNTIME SETTINGS
#include "ProceduralMeshComponent.h"
#include "SparseVoxelGrid.h"
#include "VoxelChunk.h"
#include "VoxelConversion.h"
#include "Async/Async.h"

// ----------------------------------------------------------------------------------
// HELPERS
// ----------------------------------------------------------------------------------

// Wrapper to access the new static table cleanly
FIntVector UMarchingCubes::GetCornerOffset(int32 Index)
{
	if (Index >= 0 && Index < 8) return MarchingCubesTables::CornerOffsets[Index];
	return FIntVector::ZeroValue;
}

// ----------------------------------------------------------------------------------
// LIFECYCLE
// ----------------------------------------------------------------------------------

UMarchingCubes::UMarchingCubes()
	: MyVoxelChunk(nullptr), DiggerManager(nullptr), bHeightCacheInitialized(false), CachedVoxelSize(0)
	, CachedChunkSize(0)
{
}

UMarchingCubes::UMarchingCubes(const FObjectInitializer& ObjectInitializer, const UVoxelChunk* VoxelChunk)
	: UObject(ObjectInitializer)
	, MyVoxelChunk(VoxelChunk), DiggerManager(nullptr), bHeightCacheInitialized(false), CachedVoxelSize(0)
	, CachedChunkSize(0)
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
	if (!Chunk) return;
	GenerateMeshSyncronous(Chunk);
}

void UMarchingCubes::GenerateMeshSyncronous(UVoxelChunk* Chunk)
{
	if (!Chunk) return;
	if (!DiggerManager) DiggerManager = Chunk->GetDiggerManager();

	USparseVoxelGrid* Grid = Chunk->GetSparseVoxelGrid();
	if (!Grid) return;

	FVector Origin = FVoxelConversion::ChunkToWorld(Chunk->GetChunkCoordinates());
	float VoxelSize = FVoxelConversion::LocalVoxelSize;
	
	TArray<FVector> Verts;
	TArray<int32> Tris;
	TArray<FVector> Normals;

	GenerateMeshFromGridSyncronous(Grid, Origin, VoxelSize, Verts, Tris, Normals);

	if (Verts.Num() > 0)
	{
		Chunk->UpdateMeshFromData(Verts, Tris, Normals);
	}
	else
	{
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
	int32 N = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
	TArray<float> Heights = CaptureHeightMap(Origin, VoxelSize, N);

	GenerateMeshFromGrid(
		InVoxelGrid->VoxelData,
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
    const UDiggerSettings* Settings = UDiggerSettings::Get();

    // 1. Settings
    const float ShellRadiusWorld = Settings ? Settings->SkirtRadiusWorld : 300.0f;
    const float ClipBias = Settings ? Settings->SkirtClipBias : 2.0f;

    const int32 Dim = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
    const int32 N = Dim;
    const int32 HeightMapWidth = N + 1;

    if (VoxelData.Num() == 0)
    {
        return;
    }

    // --- SAFETY CHECK: HEIGHTMAP ---
    const int32 ExpectedSize = (N + 1) * (N + 1);
    TArray<float> FallbackHeights;
    const TArray<float>* PtrHeights = &HeightValues;

    if (HeightValues.Num() != ExpectedSize)
    {
        FallbackHeights.Init(UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT, ExpectedSize);
        PtrHeights = &FallbackHeights;
    }
    const TArray<float>& SafeHeights = *PtrHeights;

    // --- 2. ACTIVE MASK ---
    TArray<bool> ActiveColumns;
    ActiveColumns.SetNumZeroed(N * N);

    auto GetIdx2D = [&](int32 X, int32 Y) { return (X * N) + Y; };

    int32 RadiusVal = FMath::CeilToInt(ShellRadiusWorld / VoxelSize);
    int32 Padding = RadiusVal + 2;

    for (const auto& Pair : VoxelData)
    {
        FIntVector P = Pair.Key;

        int32 MinX = FMath::Clamp(P.X - Padding, 0, N - 1);
        int32 MaxX = FMath::Clamp(P.X + Padding, 0, N - 1);
        int32 MinY = FMath::Clamp(P.Y - Padding, 0, N - 1);
        int32 MaxY = FMath::Clamp(P.Y + Padding, 0, N - 1);

        for (int32 sx = MinX; sx <= MaxX; ++sx)
        {
            for (int32 sy = MinY; sy <= MaxY; ++sy)
            {
                ActiveColumns[GetIdx2D(sx, sy)] = true;
            }
        }
    }

    // --- 3. VERTEX CACHE (Integer key, chunk‑safe) ---
    // We quantize to a grid based on VoxelSize to avoid float precision issues.
    TMap<FIntVector, int32> VertexCache;
    VertexCache.Reserve(N * N * N / 4);

    const float VertexQuantScale = 1.0f / (VoxelSize * 0.25f); // 1/4 voxel precision

    auto MakeKey = [&](const FVector& V)
    {
        return FIntVector(
            FMath::RoundToInt(V.X * VertexQuantScale),
            FMath::RoundToInt(V.Y * VertexQuantScale),
            FMath::RoundToInt(V.Z * VertexQuantScale)
        );
    };

    // --- 4. HEIGHT LOOKUP ---
    auto GetHeightAt = [&](const FVector& Pos) -> float
    {
        FVector Local = Pos - Origin;
        float GX = Local.X / VoxelSize;
        float GY = Local.Y / VoxelSize;

        int32 X0 = FMath::Clamp(FMath::FloorToInt(GX), 0, N);
        int32 Y0 = FMath::Clamp(FMath::FloorToInt(GY), 0, N);
        int32 X1 = FMath::Min(X0 + 1, N);
        int32 Y1 = FMath::Min(Y0 + 1, N);

        float H00 = SafeHeights[Y0 * HeightMapWidth + X0];
        float H10 = SafeHeights[Y0 * HeightMapWidth + X1];
        float H01 = SafeHeights[Y1 * HeightMapWidth + X0];
        float H11 = SafeHeights[Y1 * HeightMapWidth + X1];

        const float InvalidH = UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT + 1.0f;
        if (H00 <= InvalidH || H10 <= InvalidH || H01 <= InvalidH || H11 <= InvalidH)
            return InvalidH;

        float LerpX1 = FMath::Lerp(H00, H10, GX - (float)X0);
        float LerpX2 = FMath::Lerp(H01, H11, GX - (float)X0);
        return FMath::Lerp(LerpX1, LerpX2, GY - (float)Y0);
    };

    // Global safety floor: guarantees closure even if heightmap is garbage.
    const float GlobalFloorZ = Origin.Z - ShellRadiusWorld * 2.0f - 4.0f * VoxelSize;

    // --- 5. MAIN LOOP ---
    for (int32 x = 0; x < N; ++x)
    {
        for (int32 y = 0; y < N; ++y)
        {
            if (!ActiveColumns[GetIdx2D(x, y)])
                continue;

            for (int32 z = 0; z < N; ++z)
            {
                FVector CornerWS[8];
                float CornerSDF[8];

                bool bAllSolid = true;
                bool bAllAir = true;

                for (int32 i = 0; i < 8; i++)
                {
                    FIntVector LocalCoord = FIntVector(x, y, z) + GetCornerOffset(i);
                    CornerWS[i] = Origin + FVector(LocalCoord) * VoxelSize;

                    float SDF = 0.0f;

                    // 1. Explicit SDF (edits)
                    if (const FVoxelData* Data = VoxelData.Find(LocalCoord))
                    {
                        SDF = Data->SDFValue;
                    }
                    else
                    {
                        // 2. Implicit landscape SDF
                        float H = GetHeightAt(CornerWS[i]);
                        const float InvalidH = UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT + 1.0f;

                        if (H <= InvalidH)
                        {
                            // Soft fail → deep solid
                            SDF = -2.0f;
                        }
                        else
                        {
                            float Dist = CornerWS[i].Z - H;
                            float BiasedDist = Dist + ClipBias;

                            // Local floor relative to landscape
                            float LocalFloorZ = H - ShellRadiusWorld - 2.0f * VoxelSize;
                            if (CornerWS[i].Z < LocalFloorZ)
                            {
                                SDF = -2.0f;
                            }
                            else
                            {
                                SDF = BiasedDist / VoxelSize;
                            }
                        }
                    }

                    // 3. Global floor SDF (absolute safety net)
                    if (CornerWS[i].Z < GlobalFloorZ)
                    {
                        SDF = FMath::Min(SDF, -2.0f);
                    }

                    // Final clamp and store
                    SDF = FMath::Clamp(SDF, -1.0f, 1.0f);
                    CornerSDF[i] = SDF;

                    if (SDF > 0.0f) bAllSolid = false;
                    else bAllAir = false;
                }

                if (bAllSolid || bAllAir)
                    continue;

                int32 CubeIndex = CalculateMarchingCubesIndex(TArray<float>(CornerSDF, 8));
                if (CubeIndex == 0 || CubeIndex == 255)
                    continue;

                const auto& ConnectionTable = MarchingCubesTables::TriangleConnectionTable;
                const auto& EdgeTable = MarchingCubesTables::EdgeConnection;

                for (int32 i = 0; ConnectionTable[CubeIndex][i] != -1; i += 3)
                {
                    FVector TriVerts[3];

                    for (int32 j = 0; j < 3; ++j)
                    {
                        int32 EdgeIdx = ConnectionTable[CubeIndex][i + j];

                        FVector P1 = CornerWS[EdgeTable[EdgeIdx][0]];
                        FVector P2 = CornerWS[EdgeTable[EdgeIdx][1]];
                        float S1 = CornerSDF[EdgeTable[EdgeIdx][0]];
                        float S2 = CornerSDF[EdgeTable[EdgeIdx][1]];

                        // Interpolation must be robust to nearly equal SDFs
                        FVector Interp = InterpolateVertex(P1, P2, S1, S2);
                        FIntVector Key = MakeKey(Interp);

                        int32* CacheIdx = VertexCache.Find(Key);
                        if (CacheIdx)
                        {
                            OutTriangles.Add(*CacheIdx);
                        }
                        else
                        {
                            int32 NewIdx = OutVertices.Add(Interp);
                            VertexCache.Add(Key, NewIdx);
                            OutTriangles.Add(NewIdx);
                        }
                    }
                }
            }
        }
    }

    // --- NORMALS ---
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
		for (float& Val : Heights) Val = UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT;
	}

	return Heights;
}

FVector UMarchingCubes::InterpolateVertex(
	const FVector& P1,
	const FVector& P2,
	float SDF1,
	float SDF2
)
{
	// If signs are identical, no surface crosses this edge.
	// Return midpoint to keep things deterministic.
	if ((SDF1 > 0.0f && SDF2 > 0.0f) || (SDF1 < 0.0f && SDF2 < 0.0f))
	{
		return (P1 + P2) * 0.5f;
	}

	float Delta = SDF2 - SDF1;

	// If the delta is extremely small, lerp is unstable.
	// Use midpoint to avoid cracks.
	const float Eps = 1e-6f;
	if (FMath::Abs(Delta) < Eps)
	{
		return (P1 + P2) * 0.5f;
	}

	// Compute interpolation factor
	float T = -SDF1 / Delta;

	// Clamp to avoid overshoot due to floating point noise
	T = FMath::Clamp(T, 0.0f, 1.0f);

	// Symmetric interpolation (avoids P1/P2 ordering differences)
	FVector Result = P1 + (P2 - P1) * T;

	return Result;
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

	float LandscapeZ = DiggerManager->GetLandscapeHeightAt(VertexWS);
	if (LandscapeZ <= (UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT + 1.0f)) return VertexWS;

	float DistanceToSurface = FMath::Abs(VertexWS.Z - LandscapeZ);
	// Note: Transition constants should ideally also move to Settings or Namespace
	float LocalTransitionHeight = 100.0f; 
	float LocalTransitionSharpness = 2.0f;

	if (DistanceToSurface < LocalTransitionHeight)
	{
		float Alpha = DistanceToSurface / LocalTransitionHeight;
		float BlendAlpha = FMath::Pow(Alpha, LocalTransitionSharpness);
		float NewZ = FMath::Lerp(LandscapeZ, VertexWS.Z, BlendAlpha);
		FVector Result = VertexWS;
		Result.Z = NewZ;
		return Result;
	}
	return VertexWS;
}

// ----------------------------------------------------------------------------------
// MESH RECONSTRUCTION
// ----------------------------------------------------------------------------------

void UMarchingCubes::ReconstructMeshSection(int32 SectionIndex, const TArray<FVector>& OutVertices, const TArray<int32>& OutTriangles, const TArray<FVector>& Normals) const 
{
	if (!DiggerManager || !DiggerManager->ProceduralMesh) return;
	if (SectionIndex < 0) return;
	if (OutVertices.Num() == 0) return;

	TArray<FVector2D> UVs;
	TArray<FColor> Colors;
	TArray<FProcMeshTangent> Tangents;

	DiggerManager->ProceduralMesh->CreateMeshSection(
		SectionIndex,
		OutVertices,
		OutTriangles,
		Normals,
		UVs,
		Colors,
		Tangents,
		true
	);

	DiggerManager->ProceduralMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	if (DiggerManager->GetTerrainMaterial())
	{
		DiggerManager->ProceduralMesh->SetMaterial(SectionIndex, DiggerManager->GetTerrainMaterial());
	}

	if (OnMeshReady.IsBound())
	{
		OnMeshReady.Execute();
	}
}

void UMarchingCubes::GenerateMeshForIsland(USparseVoxelGrid* IslandGrid, const FVector& Origin, float VoxelSize, int32 IslandId)
{
	if (!IslandGrid) return;

	TArray<FVector> Verts;
	TArray<int32> Tris;
	TArray<FVector> Normals;

	int32 N = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
	TArray<float> Heights = CaptureHeightMap(Origin, VoxelSize, N);

	GenerateMeshFromGrid(IslandGrid->VoxelData, Origin, VoxelSize, Heights, Verts, Tris, Normals);

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
	bHeightCacheInitialized = true;
	CachedChunkOrigin = ChunkOrigin;
	CachedVoxelSize = VoxelSize;
}

float UMarchingCubes::GetCachedHeight(const FVector& WorldPosition) const
{
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
	return bHeightCacheInitialized && CachedChunkOrigin.Equals(ChunkOrigin, 1.0f);
}

void UMarchingCubes::ClearSectionAndRebuildMesh(int32 SectionIndex, FIntVector ChunkCoord)
{
	if (DiggerManager && DiggerManager->ProceduralMesh)
	{
		DiggerManager->ProceduralMesh->ClearMeshSection(SectionIndex);
	}
}