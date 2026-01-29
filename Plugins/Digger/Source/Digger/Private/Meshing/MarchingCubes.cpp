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
    
	// 1. Settings Setup
	const float ShellRadiusWorld = Settings ? Settings->SkirtRadiusWorld : 300.0f;
	const float ClipBias = Settings ? Settings->SkirtClipBias : 2.0f; 
	// We removed VerticalSearchBand. It causes holes in deep digs.

    const int32 Dim = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
    const int32 N = Dim; 
    const int32 HeightMapWidth = N + 1;
    FVector TotalOffset = FVector::ZeroVector;

    // --- 2. THE MASK (Active Columns) ---
    TArray<bool> ActiveColumns;
    ActiveColumns.SetNumZeroed(N * N);

    auto GetIdx2D = [&](int32 X, int32 Y) { return (X * N) + Y; };

    int32 RadiusVal = FMath::CeilToInt(ShellRadiusWorld / VoxelSize);
    // Add extra padding to ensure we cross the chunk boundary for seamless stitching
    int32 RadiusSq = FMath::Square(RadiusVal + 2); 

    // If no data, empty chunk (unless you support ghost data, then remove this check)
    if (VoxelData.Num() == 0) return;

    // Splat the mask
    for (const auto& Pair : VoxelData)
    {
        FIntVector P = Pair.Key;
        
        // We expand the search area to ensure we hit the chunk borders (0 and N-1)
        // clamping *after* calculation ensures we mark the edges.
        int32 MinX = FMath::Clamp(P.X - RadiusVal - 2, 0, N - 1);
        int32 MaxX = FMath::Clamp(P.X + RadiusVal + 2, 0, N - 1);
        int32 MinY = FMath::Clamp(P.Y - RadiusVal - 2, 0, N - 1);
        int32 MaxY = FMath::Clamp(P.Y + RadiusVal + 2, 0, N - 1);

        for (int32 sx = MinX; sx <= MaxX; ++sx)
        {
            for (int32 sy = MinY; sy <= MaxY; ++sy)
            {
                int32 Idx = GetIdx2D(sx, sy);
                if (ActiveColumns[Idx]) continue; 

                int32 Dist2D = FMath::Square(sx - P.X) + FMath::Square(sy - P.Y);
                if (Dist2D <= RadiusSq)
                {
                    ActiveColumns[Idx] = true;
                }
            }
        }
    }

	// --- 3. VERTEX CACHE (Missing Piece) ---
	TMap<FVector, int32> VertexCache;
	VertexCache.Reserve(N * N * N / 4);

	// --- 4. Helper Lambda ---
	auto GetHeightAt = [&](const FVector& Pos) -> float
	{                                                                                                             
        FVector Local = Pos - Origin;
        float GX = Local.X / VoxelSize;
        float GY = Local.Y / VoxelSize;

        int32 X0 = FMath::Clamp(FMath::FloorToInt(GX), 0, N);
        int32 Y0 = FMath::Clamp(FMath::FloorToInt(GY), 0, N);
        int32 X1 = FMath::Min(X0 + 1, N); 
        int32 Y1 = FMath::Min(Y0 + 1, N);

        // Safety: If heightmap is uninitialized or invalid
        if (Y0 * HeightMapWidth + X0 >= HeightValues.Num()) return UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT;
        
        float H00 = HeightValues[Y0 * HeightMapWidth + X0];
        float H10 = HeightValues[Y0 * HeightMapWidth + X1];
        float H01 = HeightValues[Y1 * HeightMapWidth + X0];
        float H11 = HeightValues[Y1 * HeightMapWidth + X1];

        // "Wall of Death" Check: If any corner is invalid, the whole quad is invalid.
        // This prevents spikes when the landscape is loading/unloading.
        const float InvalidH = UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT + 1.0f;
        if (H00 <= InvalidH || H10 <= InvalidH || H01 <= InvalidH || H11 <= InvalidH) 
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
            // Skip Inactive Columns
            if (!ActiveColumns[GetIdx2D(x, y)]) continue; 

            // Get Height for column sanity check (optional, but good for skipping sky)
            FVector ColPos = Origin + FVector(x, y, 0) * VoxelSize;
            float ColHeight = GetHeightAt(ColPos);
            
            // If landscape is invalid here, skip.
            if (ColHeight <= (UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT + 1.0f)) continue;

            for (int32 z = 0; z < N; ++z)
            {
                FVector CornerWSPositions[8];
                float CornerSDFValues[8];
                bool bAllSolid = true;
                bool bAllAir = true;

                // --- CORNER PROCESSING ---
                for (int32 i = 0; i < 8; i++)
                {
                    FIntVector LocalCoord = FIntVector(x, y, z) + GetCornerOffset(i);
                    CornerWSPositions[i] = Origin + FVector(LocalCoord) * VoxelSize;

                    // 1. Explicit Data (The Hole)
                    if (const FVoxelData* Data = VoxelData.Find(LocalCoord))
                    {
                        CornerSDFValues[i] = Data->SDFValue;
                    }
                    else
                    {
                        // 2. Implicit Landscape (The Skirt)
                        float H = GetHeightAt(CornerWSPositions[i]);

                        if (H <= (UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT + 1.0f))
                        {
                            CornerSDFValues[i] = -1.0f; // Bedrock
                        }
                        else
                        {
                            // Standard Implicit: Z - Height
                            float Dist = CornerWSPositions[i].Z - H;
                            
                            // Apply ClipBias to push mesh slightly under terrain
                            float BiasedDist = Dist + ClipBias;
                            
                            // Clamp to -1..1 range
                            CornerSDFValues[i] = FMath::Clamp(BiasedDist / VoxelSize, -1.0f, 1.0f);
                        }
                    }

                    // Optimization Flags
                    if (CornerSDFValues[i] > 0.0f) bAllSolid = false;
                    else bAllAir = false;
                }

                // --- THE CRITICAL FIX: REMOVED VERTICAL BAND CHECK ---
                // We rely entirely on bAllSolid / bAllAir. 
                // If it's all solid (deep underground below hole), we skip.
                // If it's all air (high above hole), we skip.
                // But if it's the FLOOR of the hole (Solid -> Air transition), we generate.
                
                if (bAllSolid || bAllAir) continue;

                // --- TRIANGULATION ---
                int32 CubeIndex = CalculateMarchingCubesIndex(TArray<float>(CornerSDFValues, 8));
                if (CubeIndex == 0 || CubeIndex == 255) continue;

                const auto& ConnectionTable = MarchingCubesTables::TriangleConnectionTable;
                const auto& EdgeTable = MarchingCubesTables::EdgeConnection;

                for (int32 i = 0; ConnectionTable[CubeIndex][i] != -1; i += 3)
                {
                    FVector TriVerts[3];
                    for (int32 j = 0; j < 3; ++j)
                    {
                        int32 EdgeIdx = ConnectionTable[CubeIndex][i + j];
                        
                        FVector P1 = CornerWSPositions[EdgeTable[EdgeIdx][0]];
                        FVector P2 = CornerWSPositions[EdgeTable[EdgeIdx][1]];
                        float S1 = CornerSDFValues[EdgeTable[EdgeIdx][0]];
                        float S2 = CornerSDFValues[EdgeTable[EdgeIdx][1]];

                        FVector Interp = InterpolateVertex(P1, P2, S1, S2);
                        TriVerts[j] = Interp + TotalOffset;

                        // Add vertex (using cache or new)
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

    // Normal generation... (Same as before)
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