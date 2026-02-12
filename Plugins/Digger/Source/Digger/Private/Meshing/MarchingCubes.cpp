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
    const UDiggerSettings* Runtime = UDiggerSettings::Get();

    FDiggerMeshConfig Config;

#if WITH_EDITOR
    // Detect if we are in PIE or Editor Preview
    const bool bIsPIE = (GEditor && GEditor->PlayWorld != nullptr);

    if (!bIsPIE)
    {
        // ------------------------------------------------------------
        // EDITOR PREVIEW MODE
        //
        // The editor module (DiggerEditor) is responsible for
        // providing a merged config. It injects it via a global
        // override before calling this function.
        //
        // If no override exists, fall back to runtime settings.
        // ------------------------------------------------------------

        if (FDiggerMeshConfig* Override = FDiggerMeshConfig::GetEditorPreviewOverride())
        {
            Config = *Override;
        }
        else
        {
            // Fallback: pure runtime settings
            Config.ShadingMode = Runtime->ShadingMode;
            Config.SkirtRadius = Runtime->SkirtRadiusWorld;
            Config.ClipBias    = Runtime->SkirtClipBias;
            Config.IsoLevel    = 0.0f;
        }
    }
    else
#endif
    {
        // ------------------------------------------------------------
        // PIE or Packaged → pure runtime settings
        // ------------------------------------------------------------
        Config.ShadingMode = Runtime->ShadingMode;
        Config.SkirtRadius = Runtime->SkirtRadiusWorld;
        Config.ClipBias    = Runtime->SkirtClipBias;
        Config.IsoLevel    = 0.0f;
    }

    // Forward to the real implementation
    GenerateMesh_MarchingCubes(
        VoxelData,
        Origin,
        VoxelSize,
        HeightValues,
        Config,
        OutVertices,
        OutTriangles,
        OutNormals
    );
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
    const FDiggerMeshConfig& Config,
    TArray<FVector>& OutVertices,
    TArray<int32>& OutTriangles,
    TArray<FVector>& OutNormals
)
{
    // ============================================================
    // LOCAL TEST FLAG — toggle hardened density pipeline
    // ============================================================
    const bool bUseHardenedDensity = true;
    // ============================================================

    OutVertices.Reset();
    OutTriangles.Reset();
    OutNormals.Reset();

    const float ShellRadiusWorld = Config.SkirtRadius;
    const float ClipBias         = Config.ClipBias;
    const EDiggerShadingMode ShadingMode = Config.ShadingMode;

    const int32 ChunkSize = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
    const int32 Pad       = 2;
    const int32 GridDim   = ChunkSize + (2 * Pad);
    const int32 TotalGridSize = GridDim * GridDim * GridDim;

    TArray<float> DensityGrid;
    DensityGrid.SetNumUninitialized(TotalGridSize);

    auto GetGridIdx = [&](int32 X, int32 Y, int32 Z)
    {
        return (X + Pad) + ((Y + Pad) * GridDim) + ((Z + Pad) * GridDim * GridDim);
    };

    const int32 HMapWidth   = ChunkSize + 1;
    const bool  bHasHeights = (HeightValues.Num() == (HMapWidth * HMapWidth));
    const float GlobalFloorZ = Origin.Z - ShellRadiusWorld - (VoxelSize * 4.0f);
    const float InvalidH     = UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT + 1.0f;

    if (bUseHardenedDensity)
    {
        // ---------------------------------------------
        // 1A. Baseline fill (with seam‑friendly extrapolation)
        // ---------------------------------------------
        for (int32 Z = -Pad; Z < ChunkSize + Pad; ++Z)
        {
            const float WorldZ = Origin.Z + (Z * VoxelSize);
            const bool bBelowFloor = (WorldZ < GlobalFloorZ);
            const int32 ZOffset = (Z + Pad) * GridDim * GridDim;

            for (int32 Y = -Pad; Y < ChunkSize + Pad; ++Y)
            {
                const int32 YOffset = (Y + Pad) * GridDim;

                for (int32 X = -Pad; X < ChunkSize + Pad; ++X)
                {
                    const int32 GridIndex = ZOffset + YOffset + (X + Pad);

                    if (bBelowFloor)
                    {
                        DensityGrid[GridIndex] = -2.0f;
                        continue;
                    }

                    float SDF = 2.0f;

                    if (bHasHeights)
                    {
                        int32 cX = FMath::Clamp(X, 0, ChunkSize);
                        int32 cY = FMath::Clamp(Y, 0, ChunkSize);

                        float H = HeightValues[cY * HMapWidth + cX];

                        // --- SEAM FIX: EXTRAPOLATION (keep this even in hardened path) ---

                        // X extrapolation
                        if (X < 0)
                        {
                            float H_Next = HeightValues[cY * HMapWidth + (cX + 1)];
                            H += (H - H_Next) * (float)(-X);
                        }
                        else if (X > ChunkSize)
                        {
                            float H_Prev = HeightValues[cY * HMapWidth + (cX - 1)];
                            H += (H - H_Prev) * (float)(X - ChunkSize);
                        }

                        // Y extrapolation
                        if (Y < 0)
                        {
                            float H0 = HeightValues[0 * HMapWidth + cX];
                            float H1 = HeightValues[1 * HMapWidth + cX];
                            H += (H0 - H1) * (float)(-Y);
                        }
                        else if (Y > ChunkSize)
                        {
                            float HN  = HeightValues[ChunkSize * HMapWidth + cX];
                            float HN1 = HeightValues[(ChunkSize - 1) * HMapWidth + cX];
                            H += (HN - HN1) * (float)(Y - ChunkSize);
                        }

                        // --- END SEAM FIX ---

                        if (H > InvalidH)
                        {
                            const float Dist        = WorldZ - H;
                            const float LocalFloorZ = H - ShellRadiusWorld - 2.0f * VoxelSize;

                            if (WorldZ < LocalFloorZ)
                                SDF = -2.0f;
                            else
                                SDF = (Dist + ClipBias) / VoxelSize;
                        }
                    }

                    DensityGrid[GridIndex] = SDF;
                }
            }
        }

        // Snapshot baseline landscape SDF BEFORE voxel edits
        TArray<float> LandscapeGrid = DensityGrid;

        // ---------------------------------------------
        // 1B. Voxel overlay (unchanged)
        // ---------------------------------------------
        for (const auto& Pair : VoxelData)
        {
            const FIntVector& P = Pair.Key;
            if (P.X >= -Pad && P.X < ChunkSize + Pad &&
                P.Y >= -Pad && P.Y < ChunkSize + Pad &&
                P.Z >= -Pad && P.Z < ChunkSize + Pad)
            {
                DensityGrid[GetGridIdx(P.X, P.Y, P.Z)] = Pair.Value.SDFValue;
            }
        }

        // ---------------------------------------------
        // 1C. Minimal poke‑through fix (only tiny upward sign flips)
        // ---------------------------------------------
        const float MaxPoke = 0.35f;      // how far above surface we tolerate
        const float MaxDepth = -0.75f;    // only near the surface, not deep interior

        for (int32 Z = 0; Z < ChunkSize; ++Z)
        for (int32 Y = 0; Y < ChunkSize; ++Y)
        for (int32 X = 0; X < ChunkSize; ++X)
        {
            const int32 Idx = GetGridIdx(X, Y, Z);

            const float L = LandscapeGrid[Idx]; // baseline landscape SDF
            const float V = DensityGrid[Idx];   // voxel‑modified SDF

            // Only touch: landscape solid, voxel air, small poke, near surface
            if (L < 0.0f && L > MaxDepth && V > 0.0f && V < MaxPoke)
            {
                // Snap back to just inside the surface (slightly negative)
                DensityGrid[Idx] = FMath::Min(-0.05f, L * 0.5f);
            }
        }
    }
    else
    {
        // ORIGINAL PATH (unchanged)
        for (int32 Z = -Pad; Z < ChunkSize + Pad; ++Z)
        {
            float WorldZ = Origin.Z + (Z * VoxelSize);
            bool bBelowFloor = (WorldZ < GlobalFloorZ);
            int32 ZOffset = (Z + Pad) * GridDim * GridDim;

            for (int32 Y = -Pad; Y < ChunkSize + Pad; ++Y)
            {
                int32 YOffset = (Y + Pad) * GridDim;

                for (int32 X = -Pad; X < ChunkSize + Pad; ++X)
                {
                    int32 GridIndex = ZOffset + YOffset + (X + Pad);
                    if (bBelowFloor) { DensityGrid[GridIndex] = -2.0f; continue; }

                    float SDF = 2.0f;
                    if (bHasHeights)
                    {
                        int32 cX = FMath::Clamp(X, 0, ChunkSize);
                        int32 cY = FMath::Clamp(Y, 0, ChunkSize);

                        float H = HeightValues[cY * HMapWidth + cX];

                        if (X < 0)
                        {
                            float H_Next = HeightValues[cY * HMapWidth + (cX + 1)];
                            H += (H - H_Next) * (float)(-X);
                        }
                        else if (X > ChunkSize)
                        {
                            float H_Prev = HeightValues[cY * HMapWidth + (cX - 1)];
                            H += (H - H_Prev) * (float)(X - ChunkSize);
                        }

                        if (Y < 0)
                        {
                            float H_Y0 = HeightValues[0 * HMapWidth + cX];
                            float H_Y1 = HeightValues[1 * HMapWidth + cX];
                            H += (H_Y0 - H_Y1) * (float)(-Y);
                        }
                        else if (Y > ChunkSize)
                        {
                            float H_YN   = HeightValues[ChunkSize * HMapWidth + cX];
                            float H_YN_1 = HeightValues[(ChunkSize - 1) * HMapWidth + cX];
                            H += (H_YN - H_YN_1) * (float)(Y - ChunkSize);
                        }

                        if (H > InvalidH)
                        {
                            float Dist = WorldZ - H;
                            float LocalFloorZ = H - ShellRadiusWorld - 2.0f * VoxelSize;
                            if (WorldZ < LocalFloorZ) SDF = -2.0f;
                            else SDF = (Dist + ClipBias) / VoxelSize;
                        }
                    }
                    DensityGrid[GridIndex] = SDF;
                }
            }
        }

        for (const auto& Pair : VoxelData)
        {
            const FIntVector& P = Pair.Key;
            if (P.X >= -Pad && P.X < ChunkSize + Pad &&
                P.Y >= -Pad && P.Y < ChunkSize + Pad &&
                P.Z >= -Pad && P.Z < ChunkSize + Pad)
            {
                DensityGrid[GetGridIdx(P.X, P.Y, P.Z)] = Pair.Value.SDFValue;
            }
        }
    }

    // --- STEP 2: PRE-CALCULATE NORMALS (unchanged) ---
    TArray<FVector> NormalGrid;
    if (ShadingMode == EDiggerShadingMode::OrganicGradient)
    {
        NormalGrid.SetNumUninitialized(TotalGridSize);

        for (int32 Z = -Pad + 1; Z < ChunkSize + Pad - 1; ++Z)
        {
            for (int32 Y = -Pad + 1; Y < ChunkSize + Pad - 1; ++Y)
            {
                for (int32 X = -Pad + 1; X < ChunkSize + Pad - 1; ++X)
                {
                    int32 Idx = GetGridIdx(X, Y, Z);

                    float nx = DensityGrid[Idx + 1]              - DensityGrid[Idx - 1];
                    float ny = DensityGrid[Idx + GridDim]        - DensityGrid[Idx - GridDim];
                    float nz = DensityGrid[Idx + (GridDim*GridDim)] - DensityGrid[Idx - (GridDim*GridDim)];

                    FVector N(nx, ny, nz);
                    if (!N.Normalize()) N = FVector::UpVector;
                    NormalGrid[Idx] = N;
                }
            }
        }
    }

    // --- STEP 3: MARCHING CUBES (unchanged) ---
    TMap<FIntVector, int32> VertexCache;
    VertexCache.Reserve(ChunkSize * ChunkSize * 2);
    const float VertexQuant = 1.0f / (VoxelSize * 0.01f);

    const int32 StrideY = GridDim;
    const int32 StrideZ = GridDim * GridDim;

    for (int32 X = 0; X < ChunkSize; ++X)
    {
        for (int32 Y = 0; Y < ChunkSize; ++Y)
        {
            for (int32 Z = 0; Z < ChunkSize; ++Z)
            {
                int32 BaseIdx = GetGridIdx(X, Y, Z);
                float CornerSDF[8];
                int32 CubeIndex = 0;

                CornerSDF[0] = DensityGrid[BaseIdx];
                CornerSDF[1] = DensityGrid[BaseIdx + 1];
                CornerSDF[2] = DensityGrid[BaseIdx + StrideY + 1];
                CornerSDF[3] = DensityGrid[BaseIdx + StrideY];
                CornerSDF[4] = DensityGrid[BaseIdx + StrideZ];
                CornerSDF[5] = DensityGrid[BaseIdx + StrideZ + 1];
                CornerSDF[6] = DensityGrid[BaseIdx + StrideZ + StrideY + 1];
                CornerSDF[7] = DensityGrid[BaseIdx + StrideZ + StrideY];

                for (int32 i = 0; i < 8; ++i)
                    if (CornerSDF[i] < Config.IsoLevel) CubeIndex |= (1 << i);

                if (CubeIndex == 0 || CubeIndex == 255) continue;

                FVector BasePos = Origin + FVector(X, Y, Z) * VoxelSize;
                const int32* TriEdges = MarchingCubesTables::TriangleConnectionTable[CubeIndex];

                for (int32 i = 0; TriEdges[i] != -1; i += 3)
                {
                    int32 VertIndices[3];

                    for (int32 v = 0; v < 3; ++v)
                    {
                        int32 EdgeIdx = TriEdges[i + v];
                        int32 v1 = MarchingCubesTables::EdgeConnection[EdgeIdx][0];
                        int32 v2 = MarchingCubesTables::EdgeConnection[EdgeIdx][1];

                        float S1 = CornerSDF[v1];
                        float S2 = CornerSDF[v2];
                        float Alpha = 0.5f;
                        if (FMath::Abs(S2 - S1) > 1e-5f)
                            Alpha = FMath::Abs(S1 - Config.IsoLevel) /
                                    (FMath::Abs(S1 - Config.IsoLevel) + FMath::Abs(S2 - Config.IsoLevel));

                        FVector P1 = BasePos + (FVector(GetCornerOffset(v1)) * VoxelSize);
                        FVector P2 = BasePos + (FVector(GetCornerOffset(v2)) * VoxelSize);
                        FVector InterpPos = FMath::Lerp(P1, P2, Alpha);

                        if (ShadingMode == EDiggerShadingMode::FlatLowPoly)
                        {
                            VertIndices[v] = OutVertices.Add(InterpPos);
                        }
                        else
                        {
                            FVector RelP = InterpPos - Origin;
                            FIntVector Key(
                                FMath::RoundToInt(RelP.X * VertexQuant),
                                FMath::RoundToInt(RelP.Y * VertexQuant),
                                FMath::RoundToInt(RelP.Z * VertexQuant)
                            );

                            if (int32* Cached = VertexCache.Find(Key))
                            {
                                VertIndices[v] = *Cached;
                            }
                            else
                            {
                                int32 NewIdx = OutVertices.Add(InterpPos);
                                VertexCache.Add(Key, NewIdx);
                                VertIndices[v] = NewIdx;

                                if (ShadingMode == EDiggerShadingMode::OrganicGradient)
                                {
                                    auto GetN = [&](int32 CI)
                                    {
                                        FIntVector Off = GetCornerOffset(CI);
                                        return NormalGrid[BaseIdx + Off.X + (Off.Y * StrideY) + (Off.Z * StrideZ)];
                                    };
                                    FVector InterpNormal = FMath::Lerp(GetN(v1), GetN(v2), Alpha);
                                    InterpNormal.Normalize();
                                    OutNormals.Add(InterpNormal);
                                }
                            }
                        }
                    }

                    OutTriangles.Add(VertIndices[0]);
                    OutTriangles.Add(VertIndices[1]);
                    OutTriangles.Add(VertIndices[2]);

                    if (ShadingMode == EDiggerShadingMode::FlatLowPoly)
                    {
                        FVector Edge1 = OutVertices[VertIndices[1]] - OutVertices[VertIndices[0]];
                        FVector Edge2 = OutVertices[VertIndices[2]] - OutVertices[VertIndices[0]];
                        FVector FaceNormal = FVector::CrossProduct(Edge2, Edge1).GetSafeNormal();
                        OutNormals.Add(FaceNormal);
                        OutNormals.Add(FaceNormal);
                        OutNormals.Add(FaceNormal);
                    }
                }
            }
        }
    }

    if (ShadingMode == EDiggerShadingMode::SurfaceGeometry)
    {
        WeldCloseVertices(OutVertices, OutTriangles);

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

float GetWeldThreshold()
{
#if WITH_EDITOR
	// If we are in PIE, use runtime settings
	if (GEditor && GEditor->PlayWorld)
	{
		return UDiggerSettings::Get()->WeldVertexThreshold;
	}

	// Otherwise, get the editor settings WITHOUT including the header
	static const FName ClassName = TEXT("/Script/DiggerEditor.DiggerEditorSettings");
	UClass* EditorSettingsClass = FindObject<UClass>(nullptr, *ClassName.ToString());

	if (EditorSettingsClass)
	{
		UObject* DefaultObj = EditorSettingsClass->GetDefaultObject();
		if (DefaultObj)
		{
			// Look up the property by name
			static const FName PropName = TEXT("WeldVertexThreshold");
			FProperty* Prop = EditorSettingsClass->FindPropertyByName(PropName);

			if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
			{
				return FloatProp->GetPropertyValue_InContainer(DefaultObj);
			}
		}
	}

	// Fallback if anything fails
	return 0.02f;
#else
	// Non-editor builds always use runtime settings
	return UDiggerSettings::Get()->WeldVertexThreshold;
#endif
}


void UMarchingCubes::WeldCloseVertices(
	TArray<FVector>& Vertices,
	TArray<int32>& Triangles)
{
	float WeldThreshold = GetWeldThreshold();
	const float Inv = 1.0f / WeldThreshold;

	TMap<FIntVector, int32> WeldMap;
	WeldMap.Reserve(Vertices.Num());

	TArray<int32> Remap;
	Remap.SetNumUninitialized(Vertices.Num());

	for (int32 i = 0; i < Vertices.Num(); ++i)
	{
		const FVector& V = Vertices[i];
		FIntVector Key(
			FMath::RoundToInt(V.X * Inv),
			FMath::RoundToInt(V.Y * Inv),
			FMath::RoundToInt(V.Z * Inv));

		if (int32* Existing = WeldMap.Find(Key))
		{
			Remap[i] = *Existing;
		}
		else
		{
			WeldMap.Add(Key, i);
			Remap[i] = i;
		}
	}

	for (int32& Idx : Triangles)
	{
		Idx = Remap[Idx];
	}
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

//Post Process Welding Setup Code!!!
struct FEdgeKey
{
	FIntVector ChunkCoord;
	uint8 EdgeAxis;   // 0=X,1=Y,2=Z
	int32 I, J;       // index along the edge

	bool operator==(const FEdgeKey& Other) const
	{
		return ChunkCoord == Other.ChunkCoord &&
			   EdgeAxis   == Other.EdgeAxis &&
			   I          == Other.I &&
			   J          == Other.J;
	}
};

uint32 GetTypeHash(const FEdgeKey& K)
{
	return HashCombine(
		HashCombine(GetTypeHash(K.ChunkCoord), GetTypeHash(K.EdgeAxis)),
		HashCombine(GetTypeHash(K.I), GetTypeHash(K.J)));
}


int32 UMarchingCubes::CalculateMarchingCubesIndex(const float CornerSDF[8])
{
	// If your original version used a TArray, just adapt it:
	int32 CubeIndex = 0;
	for (int32 i = 0; i < 8; i++)
	{
		if (CornerSDF[i] <= 0.0f) // solid = inside
		{
			CubeIndex |= (1 << i);
		}
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