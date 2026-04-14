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
#include "Async/ParallelFor.h"


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
	  , MyVoxelChunk(nullptr), DiggerManager(nullptr), bHeightCacheInitialized(false), CachedVoxelSize(0)
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

	FVector Origin = FVoxelConversion::ChunkToWorld(Chunk->GetChunkCoords());
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



// =============================================================================
// BOUNDSSHINT OVERLOAD — restricts MC traversal to a world-space AABB.
// Called by VoxelChunk::GenerateMesh (async path) and ForceUpdate.
// =============================================================================

void UMarchingCubes::GenerateMeshSyncronous(UVoxelChunk* Chunk, const FBox& BoundsHint)
{
	// BoundsHint reserved for future partial-update optimisation.
	// Always delegate to full-chunk sync remesh.
	GenerateMeshSyncronous(Chunk);
}


// =============================================================================
// CACHHEIGHTMAP — captures landscape heights and stores for reuse every remesh.
// =============================================================================

void UMarchingCubes::CacheHeightMap(const FVector& Origin, float VoxelSize, int32 GridResolution)
{
	CachedHeightValues      = CaptureHeightMap(Origin, VoxelSize, GridResolution, /*Pad=*/2);
	bHeightCacheInitialized = true;
	CachedChunkOrigin       = Origin;
	CachedVoxelSize         = VoxelSize;
}


// =============================================================================
// GenerateMeshFromGrid — AABB-aware overload (called from async VoxelChunk path)
//
// BoundsHint is a WORLD-SPACE box describing the region that actually has
// authored voxels.  When valid, the MC loop is restricted to that region so
// we never generate landscape-duplicate mesh outside the sculpted area.
// When invalid (default FBox(ForceInit)), sentinels are used → full chunk.
// =============================================================================

void UMarchingCubes::GenerateMeshFromGrid(
	const TMap<FIntVector, FVoxelData>& VoxelData,
	const FVector& Origin,
	float VoxelSize,
	const TArray<float>& HeightValues,
	const FBox& BoundsHint,
	TArray<FVector>& OutVertices,
	TArray<int32>& OutTriangles,
	TArray<FVector>& OutNormals)
{
	// Build the same Config the no-hint overload uses (reads Runtime/Editor settings).
	const UDiggerSettings* Runtime = UDiggerSettings::Get();
	FDiggerMeshConfig Config;

#if WITH_EDITOR
	const bool bIsPIE = (GEditor && GEditor->PlayWorld != nullptr);
	if (!bIsPIE)
	{
		if (FDiggerMeshConfig* Override = FDiggerMeshConfig::GetEditorPreviewOverride())
			Config = *Override;
		else
		{
			Config.ShadingMode = Runtime->ShadingMode;
			Config.SkirtRadius = Runtime->SkirtRadiusWorld;
			Config.ClipBias    = Runtime->SkirtClipBias;
			Config.IsoLevel    = 0.0f;
		}
	}
	else
#endif
	{
		Config.ShadingMode = Runtime->ShadingMode;
		Config.SkirtRadius = Runtime->SkirtRadiusWorld;
		Config.ClipBias    = Runtime->SkirtClipBias;
		Config.IsoLevel    = 0.0f;
	}

	// Convert world-space BoundsHint to voxel-space integer AABB.
	// We expand by 1 voxel so the MC stencil (which samples X+1 corners) never
	// clips a surface that sits exactly on the boundary of the authored region.
	FIntVector AABBMin(INT32_MIN), AABBMax(INT32_MAX); // sentinels = full chunk

	if (BoundsHint.IsValid && VoxelSize > 0.f)
	{
		const FVector LocalMin = (BoundsHint.Min - Origin) / VoxelSize;
		const FVector LocalMax = (BoundsHint.Max - Origin) / VoxelSize;

		// Shrink by 1 on min (floor) and expand by 1 on max (ceil) to be safe,
		// then clamp to [-1, ChunkSize) which is the MC traversal range.
		const int32 ChunkSize = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
		AABBMin = FIntVector(
			FMath::Clamp(FMath::FloorToInt(LocalMin.X) - 1, -1, ChunkSize),
			FMath::Clamp(FMath::FloorToInt(LocalMin.Y) - 1, -1, ChunkSize),
			FMath::Clamp(FMath::FloorToInt(LocalMin.Z) - 1, -1, ChunkSize));
		AABBMax = FIntVector(
			FMath::Clamp(FMath::CeilToInt(LocalMax.X)  + 1, -1, ChunkSize),
			FMath::Clamp(FMath::CeilToInt(LocalMax.Y)  + 1, -1, ChunkSize),
			FMath::Clamp(FMath::CeilToInt(LocalMax.Z)  + 1, -1, ChunkSize));
	}

	GenerateMesh_MarchingCubes(VoxelData, Origin, VoxelSize, HeightValues,
		Config, AABBMin, AABBMax, OutVertices, OutTriangles, OutNormals);
}


// =============================================================================
// PUBLIC CONFIG-FORWARDING OVERLOAD (used by GenerateMeshFromGrid async path)
// =============================================================================

void UMarchingCubes::GenerateMesh_MarchingCubes(
    const TMap<FIntVector, FVoxelData>& VoxelData,
    const FVector& Origin,
    float VoxelSize,
    const TArray<float>& HeightValues,
    const FDiggerMeshConfig& Config,
    TArray<FVector>& OutVertices,
    TArray<int32>& OutTriangles,
    TArray<FVector>& OutNormals)
{
	// Full chunk — use sentinel values
	const FIntVector AABBMin(INT32_MIN), AABBMax(INT32_MAX);
	GenerateMesh_MarchingCubes(VoxelData, Origin, VoxelSize, HeightValues,
		Config, AABBMin, AABBMax, OutVertices, OutTriangles, OutNormals);
}


// =============================================================================
// PRIVATE: SETTINGS-READING OVERLOAD (no AABB — full chunk)
// =============================================================================

void UMarchingCubes::GenerateMesh_MarchingCubes(
    const TMap<FIntVector, FVoxelData>& VoxelData,
    const FVector& Origin,
    float VoxelSize,
    const TArray<float>& HeightValues,
    TArray<FVector>& OutVertices,
    TArray<int32>& OutTriangles,
    TArray<FVector>& OutNormals)
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

	const int32 N = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
	TArray<float> Heights = CaptureHeightMap(Origin, VoxelSize, N, /*Pad=*/2);

	GenerateMeshFromGrid(
		InVoxelGrid->VoxelData, // still stored in global voxel coords
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
    const FIntVector& AABBMin,
    const FIntVector& AABBMax,
    TArray<FVector>& OutVertices,
    TArray<int32>& OutTriangles,
    TArray<FVector>& OutNormals
)
{
    OutVertices.Reset();
    OutTriangles.Reset();
    OutNormals.Reset();

    const float ShellRadiusWorld      = Config.SkirtRadius;
    const float ClipBias              = Config.ClipBias;
    const EDiggerShadingMode ShadingMode = Config.ShadingMode;

    const int32 ChunkSize     = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
    const int32 Pad           = 2;
    const int32 GridDim       = ChunkSize + (2 * Pad);
    const int32 TotalGridSize = GridDim * GridDim * GridDim;

    const int32 StrideY = GridDim;
    const int32 StrideZ = GridDim * GridDim;

    // HeightValues is a padded height map: (ChunkSize + 2*Pad + 1)^2 samples.
    // Index formula: (Y + Pad) * HMapWidth + (X + Pad), where X/Y in [-Pad, ChunkSize+Pad].
    // This means every pad cell gets the exact same real landscape value that the
    // neighbouring chunk would compute at that world position — no extrapolation,
    // so density and normals match at the boundary → no visible seam.
    const int32 HMapWidth    = ChunkSize + 2 * Pad + 1;  // padded width
    const bool  bHasHeights  = (HeightValues.Num() == (HMapWidth * HMapWidth));
    const float GlobalFloorZ = Origin.Z - ShellRadiusWorld - (VoxelSize * 4.0f);
    const float InvalidH     = UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT + 1.0f;

    TArray<float> DensityGrid;
    // Initialise entire grid to +2.0f (well above iso = 0, meaning "air").
    // Cells outside the sculpted fill region are never filled by Step 1, so
    // they must default to a safe value. +2.0f means MC finds no crossings
    // there (both sides of any cube will be positive = all-air → CubeIndex 0).
    DensityGrid.Init(2.0f, TotalGridSize);

    auto GetGridIdx = [&](int32 X, int32 Y, int32 Z)
    {
        return (X + Pad) + (Y + Pad) * StrideY + (Z + Pad) * StrideZ;
    };

    // ============================================================
    // SCULPTED-REGION LOOP BOUNDS
    //
    // When AABBMin/Max are real values (not sentinels), restrict Steps 1, 1c
    // and 2 to the authored region plus Pad so the density and normal grids
    // only do work where geometry will actually be emitted.
    //
    // We always include the full boundary strip [-1, 0) and
    // [ChunkSize, ChunkSize+1) so seam-correctness is maintained — those
    // cells are read by neighbouring chunks and must match.
    //
    // Rule:
    //   - Fill region  = clamp(AABBMin - Pad, -Pad, ChunkSize+Pad)
    //                  → clamp(AABBMax + Pad, -Pad, ChunkSize+Pad)
    //   - Always extend to include the immediate seam strip (-Pad and +Pad
    //     relative to [0,ChunkSize)) because neighbours read those cells.
    // ============================================================
    const bool bSculptedRegion = (AABBMin.X != INT32_MIN);  // false → full chunk

    // Fill bounds in voxel space (include Pad on every side of the AABB)
    const int32 FillX0 = bSculptedRegion ? FMath::Clamp(AABBMin.X - Pad, -Pad, ChunkSize + Pad) : -Pad;
    const int32 FillY0 = bSculptedRegion ? FMath::Clamp(AABBMin.Y - Pad, -Pad, ChunkSize + Pad) : -Pad;
    const int32 FillZ0 = bSculptedRegion ? FMath::Clamp(AABBMin.Z - Pad, -Pad, ChunkSize + Pad) : -Pad;
    const int32 FillX1 = bSculptedRegion ? FMath::Clamp(AABBMax.X + Pad, -Pad, ChunkSize + Pad) : ChunkSize + Pad;
    const int32 FillY1 = bSculptedRegion ? FMath::Clamp(AABBMax.Y + Pad, -Pad, ChunkSize + Pad) : ChunkSize + Pad;
    const int32 FillZ1 = bSculptedRegion ? FMath::Clamp(AABBMax.Z + Pad, -Pad, ChunkSize + Pad) : ChunkSize + Pad;

    // Smooth bounds: interior region clamped to [0, ChunkSize) intersected with AABB
    const int32 SmoothX0 = bSculptedRegion ? FMath::Clamp(AABBMin.X, 0, ChunkSize) : 0;
    const int32 SmoothY0 = bSculptedRegion ? FMath::Clamp(AABBMin.Y, 0, ChunkSize) : 0;
    const int32 SmoothZ0 = bSculptedRegion ? FMath::Clamp(AABBMin.Z, 0, ChunkSize) : 0;
    const int32 SmoothX1 = bSculptedRegion ? FMath::Clamp(AABBMax.X, 0, ChunkSize) : ChunkSize;
    const int32 SmoothY1 = bSculptedRegion ? FMath::Clamp(AABBMax.Y, 0, ChunkSize) : ChunkSize;
    const int32 SmoothZ1 = bSculptedRegion ? FMath::Clamp(AABBMax.Z, 0, ChunkSize) : ChunkSize;

    // ============================================================
    // STEP 1 — BASELINE LANDSCAPE SDF (restricted to fill region)
    // Each cell looks up its height directly from the padded height map —
    // identical values to what the neighbouring chunk computes for those cells.
    // ============================================================

    for (int32 Z = FillZ0; Z < FillZ1; ++Z)
    {
        const float WorldZ      = Origin.Z + Z * VoxelSize;
        const bool  bBelowFloor = (WorldZ < GlobalFloorZ);
        const int32 ZOffset     = (Z + Pad) * StrideZ;

        for (int32 Y = FillY0; Y < FillY1; ++Y)
        {
            const int32 YOffset = (Y + Pad) * StrideY;
            const int32 RowBase = ZOffset + YOffset;

            for (int32 X = FillX0; X < FillX1; ++X)
            {
                const int32 GridIndex = RowBase + (X + Pad);

                if (bBelowFloor)
                {
                    DensityGrid[GridIndex] = -2.0f;
                    continue;
                }

                float SDF = 2.0f;

                if (bHasHeights)
                {
                    // Direct padded lookup — X and Y are in [-Pad, ChunkSize+Pad),
                    // so (X+Pad) and (Y+Pad) are always in [0, HMapWidth).
                    const float H = HeightValues[(Y + Pad) * HMapWidth + (X + Pad)];

                    if (H > InvalidH)
                    {
                        const float Dist = WorldZ - H;

                        // Landscape SDF: positive above surface, negative below.
                        // We smoothly ramp to -2 over ShellRadiusWorld below the
                        // surface instead of hard-clamping, so the floor transition
                        // never creates a sharp rim at the chunk boundary edge.
                        const float RawSDF = (Dist + ClipBias) / VoxelSize;

                        // How far below the surface in voxel units (0 at surface, grows downward)
                        const float DepthVoxels = -RawSDF; // positive when underground
                        const float MaxDepth     = (ShellRadiusWorld / VoxelSize) + 2.0f;

                        SDF = (DepthVoxels > MaxDepth)
                            ? -2.0f
                            : FMath::Max(RawSDF, -2.0f);
                    }
                }

                DensityGrid[GridIndex] = SDF;
            }
        }
    }

    // ============================================================
    // STEP 1b — VOXEL OVERLAY
    // ============================================================
    //
    // Track which grid cells have been written by a voxel edit.
    // We use this in the smoothing step to know which cells are
    // "authored" (sphere SDF) vs "baseline" (terrain SDF), so we
    // can blend the seam between them.

    TArray<bool> bIsVoxelWritten;
    bIsVoxelWritten.SetNumZeroed(TotalGridSize);

    for (const auto& Pair : VoxelData)
    {
        const FIntVector& P = Pair.Key;

        if (P.X >= -Pad && P.X < ChunkSize + Pad &&
            P.Y >= -Pad && P.Y < ChunkSize + Pad &&
            P.Z >= -Pad && P.Z < ChunkSize + Pad)
        {
            const int32 Idx      = GetGridIdx(P.X, P.Y, P.Z);
            DensityGrid[Idx]     = Pair.Value.SDFValue;
            bIsVoxelWritten[Idx] = true;
        }
    }

    // ============================================================
    // STEP 1c — SMOOTH DENSITY GRID AT VOXEL/TERRAIN SEAM
    // ============================================================
    //
    // Two-phase strategy to avoid the "stiff seam" artifact:
    //
    // PHASE A — Interior [0, ChunkSize): run NumIterations of Laplacian
    //   smoothing. All 6 neighbours of every interior cell are also
    //   interior so every sample is smoothed consistently.
    //
    // PHASE B — Boundary strip [-1, 0) and [ChunkSize, ChunkSize+1):
    //   Run one additional pass AFTER the interior is fully smoothed.
    //   The strip cells now sample already-smoothed interior values on
    //   their inward-facing side, so the seam region gets the same
    //   effective smoothing as the rest of the chunk.
    //   (The outermost pad cells [-Pad, -1) and [ChunkSize+1, ChunkSize+Pad)
    //   are intentionally left raw — they are neighbour-chunk data that
    //   the neighbour is responsible for smoothing.)
    {
        const float SurfaceBand   = 2.5f;
        const int32 NumIterations = 3;
        const float SmoothAlpha   = 0.6f;

        TArray<float> Scratch;
        Scratch.SetNumUninitialized(TotalGridSize);

        auto LaplacianCell = [&](int32 X, int32 Y, int32 Z)
        {
            const int32 Idx = (Z + Pad) * StrideZ + (Y + Pad) * StrideY + (X + Pad);
            const float V   = DensityGrid[Idx];
            if (FMath::Abs(V) > SurfaceBand) return;
            const float Avg = (
                DensityGrid[Idx + 1]       + DensityGrid[Idx - 1]       +
                DensityGrid[Idx + StrideY] + DensityGrid[Idx - StrideY] +
                DensityGrid[Idx + StrideZ] + DensityGrid[Idx - StrideZ]
            ) / 6.0f;
            Scratch[Idx] = FMath::Lerp(V, Avg, SmoothAlpha);
        };

        // ---- PHASE A: interior only, restricted to sculpted region ----
        for (int32 Iter = 0; Iter < NumIterations; ++Iter)
        {
            FMemory::Memcpy(Scratch.GetData(), DensityGrid.GetData(), TotalGridSize * sizeof(float));

            for (int32 Z = SmoothZ0; Z < SmoothZ1; ++Z)
            for (int32 Y = SmoothY0; Y < SmoothY1; ++Y)
            for (int32 X = SmoothX0; X < SmoothX1; ++X)
                LaplacianCell(X, Y, Z);

            FMemory::Memcpy(DensityGrid.GetData(), Scratch.GetData(), TotalGridSize * sizeof(float));
        }

        // ---- PHASE B: boundary strip (one pass, after interior is smooth) ----
        // Covers X/Y/Z = -1 and X/Y/Z = ChunkSize faces.
        FMemory::Memcpy(Scratch.GetData(), DensityGrid.GetData(), TotalGridSize * sizeof(float));

        // X faces
        for (int32 Z = 0; Z < ChunkSize; ++Z)
        for (int32 Y = 0; Y < ChunkSize; ++Y)
        {
            LaplacianCell(-1,        Y, Z);
            LaplacianCell(ChunkSize, Y, Z);
        }
        // Y faces (exclude corners already done)
        for (int32 Z = 0; Z < ChunkSize; ++Z)
        for (int32 X = -1; X <= ChunkSize; ++X)
        {
            LaplacianCell(X, -1,        Z);
            LaplacianCell(X, ChunkSize, Z);
        }
        // Z faces (exclude edges already done)
        for (int32 Y = -1; Y <= ChunkSize; ++Y)
        for (int32 X = -1; X <= ChunkSize; ++X)
        {
            LaplacianCell(X, Y, -1);
            LaplacianCell(X, Y, ChunkSize);
        }

        FMemory::Memcpy(DensityGrid.GetData(), Scratch.GetData(), TotalGridSize * sizeof(float));
    }

    // ============================================================
    // STEP 2 — NORMAL GRID
    //
    // Seam normal strategy: at the outermost interior faces (X=0 and
    // X=ChunkSize-1, same for Y/Z), the standard central-difference stencil
    // (X-1, X+1) samples one interior cell and one pad cell.  The interior
    // cell differs between the two neighbours — chunk A uses A's X=1, chunk B
    // uses B's X=ChunkSize-2 — so the normals at the shared face disagree.
    //
    // Fix: at those boundary faces, replace the interior sample with the
    // next pad cell instead, i.e. use (X-2, X) or (X, X+2).  Both chunks
    // now evaluate the same two pad-region densities for that gradient
    // component, producing the same normal at the shared vertex.
    // ============================================================

    TArray<FVector> NormalGrid;
    if (ShadingMode == EDiggerShadingMode::OrganicGradient)
    {
        NormalGrid.Init(FVector::UpVector, TotalGridSize);  // safe default outside fill region

        auto SampleSDF = [&](int32 X, int32 Y, int32 Z) -> float
        {
            const int32 GX = FMath::Clamp(X + Pad, 0, GridDim - 1);
            const int32 GY = FMath::Clamp(Y + Pad, 0, GridDim - 1);
            const int32 GZ = FMath::Clamp(Z + Pad, 0, GridDim - 1);
            return DensityGrid[GX + GY * StrideY + GZ * StrideZ];
        };

        // Normal grid restricted to fill region (Pad-1 margin so stencil samples stay in-bounds).
        const int32 NormX0 = FMath::Max(FillX0, -Pad + 1);
        const int32 NormY0 = FMath::Max(FillY0, -Pad + 1);
        const int32 NormZ0 = FMath::Max(FillZ0, -Pad + 1);
        const int32 NormX1 = FMath::Min(FillX1, ChunkSize + Pad - 1);
        const int32 NormY1 = FMath::Min(FillY1, ChunkSize + Pad - 1);
        const int32 NormZ1 = FMath::Min(FillZ1, ChunkSize + Pad - 1);

        for (int32 Z = NormZ0; Z < NormZ1; ++Z)
        {
            const int32 ZOffset = (Z + Pad) * StrideZ;
            for (int32 Y = NormY0; Y < NormY1; ++Y)
            {
                const int32 YOffset = (Y + Pad) * StrideY;
                const int32 RowBase = ZOffset + YOffset;
                for (int32 X = NormX0; X < NormX1; ++X)
                {
                    const int32 Idx = RowBase + (X + Pad);

                    // X gradient: at seam faces, bias both samples into the pad
                    // so both chunks compute the same value for this component.
                    float nx;
                    if      (X == 0)            nx = SampleSDF(X,Y,Z)   - SampleSDF(X-2,Y,Z); // forward into pad
                    else if (X == ChunkSize-1)  nx = SampleSDF(X+2,Y,Z) - SampleSDF(X,Y,Z);   // backward into pad
                    else                        nx = SampleSDF(X+1,Y,Z) - SampleSDF(X-1,Y,Z);

                    float ny;
                    if      (Y == 0)            ny = SampleSDF(X,Y,Z)   - SampleSDF(X,Y-2,Z);
                    else if (Y == ChunkSize-1)  ny = SampleSDF(X,Y+2,Z) - SampleSDF(X,Y,Z);
                    else                        ny = SampleSDF(X,Y+1,Z) - SampleSDF(X,Y-1,Z);

                    float nz;
                    if      (Z == 0)            nz = SampleSDF(X,Y,Z)   - SampleSDF(X,Y,Z-2);
                    else if (Z == ChunkSize-1)  nz = SampleSDF(X,Y,Z+2) - SampleSDF(X,Y,Z);
                    else                        nz = SampleSDF(X,Y,Z+1) - SampleSDF(X,Y,Z-1);

                    FVector N(nx, ny, nz);
                    if (!N.Normalize()) N = FVector::UpVector;
                    NormalGrid[Idx] = N;
                }
            }
        }
    }

    // =============================================================================
// Drop this into GenerateMesh_MarchingCubes in place of the existing
// LoopX0/Y0/Z0/X1/Y1/Z1 declarations and the three nested for loops.
// Everything before (Steps 1, 1b, 1c, 2) and after (Step 4) is unchanged.
//
// The only change from the original is the four LoopX0/Y0/X1/Y1 constants.
// LoopZ0 and LoopZ1 stay at full range (-1 and ChunkSize) — floor geometry
// near LoopZ0 must not be skipped.
//
// bSculptedRegion and FillX0/Y0/X1/Y1 are already computed above this block
// (they are used in Steps 1, 1c, 2) so they are in scope here — no new
// variables are introduced.
// =============================================================================

    // ============================================================
    // STEP 3 — MARCHING CUBES
    //
    // X and Y bounds are restricted to the sculpted fill region + 1 stencil
    // voxel when bSculptedRegion is true.  Outside that range DensityGrid is
    // +2.0f (initialised above), so all 8 corners of every cube are positive
    // → CubeIndex 0 → no triangles.  Skipping those XY columns is safe.
    //
    // Z always traverses the full range [-1, ChunkSize) because floor geometry
    // near Z = -1 produces visible triangles and must not be skipped.
    //
    // When bSculptedRegion is false (full-chunk bake / ForceUpdate) these
    // evaluate to the original -1 / ChunkSize values — no behavioural change.
    // ============================================================

    const int32 LoopX0 = bSculptedRegion ? FMath::Max(FillX0 - 1, -1)        : -1;
    const int32 LoopY0 = bSculptedRegion ? FMath::Max(FillY0 - 1, -1)        : -1;
    const int32 LoopZ0 = -1;
    const int32 LoopX1 = bSculptedRegion ? FMath::Min(FillX1 + 1, ChunkSize) : ChunkSize;
    const int32 LoopY1 = bSculptedRegion ? FMath::Min(FillY1 + 1, ChunkSize) : ChunkSize;
    const int32 LoopZ1 = ChunkSize;

    TMap<FIntVector, int32> VertexCache;
    VertexCache.Reserve(ChunkSize * ChunkSize * 6);

    // Per-vertex flag: true if the vertex was generated from an edge that
    // crosses an authored additive voxel (negative SDF).  These vertices
    // must NOT be clamped to the landscape surface in Step 5 — they
    // represent geometry the user intentionally placed above the terrain.
    TArray<bool> bIsAdditiveVertex;

    auto MakeVertexKey = [&](int32 BaseIdx, int32 EdgeIdx)
    {
        return FIntVector(BaseIdx, EdgeIdx, 0);
    };

    // Helper: compute the grid index for a cube corner.
    auto CornerGridIdx = [&](int32 InBaseIdx, int32 CornerI)
    {
        const FIntVector Off = GetCornerOffset(CornerI);
        return InBaseIdx + Off.X + Off.Y * StrideY + Off.Z * StrideZ;
    };

    for (int32 X = LoopX0; X < LoopX1; ++X)
    {
        for (int32 Y = LoopY0; Y < LoopY1; ++Y)
        {
            for (int32 Z = LoopZ0; Z < LoopZ1; ++Z)
            {
                const int32 BaseIdx = GetGridIdx(X, Y, Z);

                float CornerSDF[8];
                CornerSDF[0] = DensityGrid[BaseIdx];
                CornerSDF[1] = DensityGrid[BaseIdx + 1];
                CornerSDF[2] = DensityGrid[BaseIdx + StrideY + 1];
                CornerSDF[3] = DensityGrid[BaseIdx + StrideY];
                CornerSDF[4] = DensityGrid[BaseIdx + StrideZ];
                CornerSDF[5] = DensityGrid[BaseIdx + StrideZ + 1];
                CornerSDF[6] = DensityGrid[BaseIdx + StrideZ + StrideY + 1];
                CornerSDF[7] = DensityGrid[BaseIdx + StrideZ + StrideY];

                int32 CubeIndex = 0;
                for (int32 i = 0; i < 8; ++i)
                    CubeIndex |= (CornerSDF[i] < Config.IsoLevel) << i;

                if (CubeIndex == 0 || CubeIndex == 255)
                    continue;

                const FVector BasePos  = Origin + FVector(X, Y, Z) * VoxelSize;
                const int32* TriEdges = MarchingCubesTables::TriangleConnectionTable[CubeIndex];

                for (int32 i = 0; TriEdges[i] != -1; i += 3)
                {
                    int32 VertIndices[3];

                    for (int32 v = 0; v < 3; ++v)
                    {
                        const int32 EdgeIdx = TriEdges[i + v];
                        const int32 v1      = MarchingCubesTables::EdgeConnection[EdgeIdx][0];
                        const int32 v2      = MarchingCubesTables::EdgeConnection[EdgeIdx][1];

                        const float S1 = CornerSDF[v1];
                        const float S2 = CornerSDF[v2];

                        float Alpha = 0.5f;
                        const float Den = FMath::Abs(S1 - Config.IsoLevel)
                                        + FMath::Abs(S2 - Config.IsoLevel);
                        if (Den > 1e-5f)
                            Alpha = FMath::Abs(S1 - Config.IsoLevel) / Den;

                        const FVector P1        = BasePos + FVector(GetCornerOffset(v1)) * VoxelSize;
                        const FVector P2        = BasePos + FVector(GetCornerOffset(v2)) * VoxelSize;
                        const FVector InterpPos = FMath::Lerp(P1, P2, Alpha);

                        // Check if this edge involves an authored additive voxel
                        // (negative SDF from a fill brush).  These vertices must
                        // not be clamped to the landscape surface.
                        const int32 GI1 = CornerGridIdx(BaseIdx, v1);
                        const int32 GI2 = CornerGridIdx(BaseIdx, v2);
                        const bool bEdgeAdditive =
                            (bIsVoxelWritten[GI1] && DensityGrid[GI1] < 0.f) ||
                            (bIsVoxelWritten[GI2] && DensityGrid[GI2] < 0.f);

                        if (ShadingMode == EDiggerShadingMode::FlatLowPoly)
                        {
                            VertIndices[v] = OutVertices.Add(InterpPos);
                            bIsAdditiveVertex.Add(bEdgeAdditive);
                        }
                        else
                        {
                            const FIntVector Key = MakeVertexKey(BaseIdx, EdgeIdx);

                            if (int32* Cached = VertexCache.Find(Key))
                            {
                                VertIndices[v] = *Cached;
                            }
                            else
                            {
                                const int32 NewIdx = OutVertices.Add(InterpPos);
                                bIsAdditiveVertex.Add(bEdgeAdditive);
                                VertexCache.Add(Key, NewIdx);
                                VertIndices[v] = NewIdx;

                                if (ShadingMode == EDiggerShadingMode::OrganicGradient)
                                {
                                    auto GetN = [&](int32 CI)
                                    {
                                        const FIntVector Off = GetCornerOffset(CI);
                                        return NormalGrid[BaseIdx
                                            + Off.X
                                            + Off.Y * StrideY
                                            + Off.Z * StrideZ];
                                    };

                                    FVector InterpNormal = FMath::Lerp(GetN(v1), GetN(v2), Alpha);
                                    InterpNormal.Normalize();
                                    OutNormals.Add(InterpNormal);
                                }
                            }
                        }
                    }

                    if (VertIndices[0] == VertIndices[1] ||
                        VertIndices[1] == VertIndices[2] ||
                        VertIndices[0] == VertIndices[2])
                        continue;

                    OutTriangles.Add(VertIndices[0]);
                    OutTriangles.Add(VertIndices[1]);
                    OutTriangles.Add(VertIndices[2]);

                    if (ShadingMode == EDiggerShadingMode::FlatLowPoly)
                    {
                        const FVector Edge1      = OutVertices[VertIndices[1]] - OutVertices[VertIndices[0]];
                        const FVector Edge2      = OutVertices[VertIndices[2]] - OutVertices[VertIndices[0]];
                        const FVector FaceNormal = FVector::CrossProduct(Edge2, Edge1).GetSafeNormal();

                        OutNormals.Add(FaceNormal);
                        OutNormals.Add(FaceNormal);
                        OutNormals.Add(FaceNormal);
                    }
                }
            }
        }
    }

    // ============================================================
    // STEP 4 — SURFACE GEOMETRY SHADING
    // ============================================================

    if (ShadingMode == EDiggerShadingMode::SurfaceGeometry)
    {
        WeldCloseVertices(OutVertices, OutTriangles);

        OutNormals.SetNumZeroed(OutVertices.Num());
        for (int32 i = 0; i < OutTriangles.Num(); i += 3)
        {
            const int32 i0 = OutTriangles[i];
            const int32 i1 = OutTriangles[i + 1];
            const int32 i2 = OutTriangles[i + 2];

            const FVector Edge1      = OutVertices[i1] - OutVertices[i0];
            const FVector Edge2      = OutVertices[i2] - OutVertices[i0];
            const FVector FaceNormal = FVector::CrossProduct(Edge2, Edge1);

            OutNormals[i0] += FaceNormal;
            OutNormals[i1] += FaceNormal;
            OutNormals[i2] += FaceNormal;
        }

        for (FVector& N : OutNormals)
            N.Normalize();
    }

    // ============================================================
    // STEP 5 — LANDSCAPE SURFACE CLAMP
    //
    // Vertices generated by MC interpolation near the landscape can
    // poke slightly above the surface due to grid discretisation,
    // ClipBias, and the smooth SDF gradient.  Clamp any vertex whose
    // Z exceeds the landscape height at its XY column so the voxel
    // mesh never protrudes above the landscape from below.
    //
    // EXCEPTION: vertices from additive brush strokes (fill mode)
    // are intentionally above the landscape.  Skip the clamp for
    // those so the user can build terrain above the surface.
    // ============================================================
    if (bHasHeights && OutVertices.Num() > 0)
    {
        for (int32 Vi = 0; Vi < OutVertices.Num(); ++Vi)
        {
            // Additive vertices must be allowed to protrude above the
            // landscape — they represent geometry the user placed there.
            if (Vi < bIsAdditiveVertex.Num() && bIsAdditiveVertex[Vi])
                continue;

            FVector& V = OutVertices[Vi];

            // Convert vertex world position to height-map sample coords.
            const float LocalX = (V.X - Origin.X) / VoxelSize;
            const float LocalY = (V.Y - Origin.Y) / VoxelSize;

            // Bilinear sample from the padded height map.
            const float FX = LocalX + (float)Pad;
            const float FY = LocalY + (float)Pad;

            const int32 IX = FMath::Clamp(FMath::FloorToInt(FX), 0, HMapWidth - 2);
            const int32 IY = FMath::Clamp(FMath::FloorToInt(FY), 0, HMapWidth - 2);
            const float FracX = FMath::Clamp(FX - (float)IX, 0.f, 1.f);
            const float FracY = FMath::Clamp(FY - (float)IY, 0.f, 1.f);

            const float H00 = HeightValues[IY       * HMapWidth + IX];
            const float H10 = HeightValues[IY       * HMapWidth + IX + 1];
            const float H01 = HeightValues[(IY + 1) * HMapWidth + IX];
            const float H11 = HeightValues[(IY + 1) * HMapWidth + IX + 1];

            // Skip if any corner is invalid (off-landscape).
            if (H00 <= InvalidH || H10 <= InvalidH || H01 <= InvalidH || H11 <= InvalidH)
                continue;

            const float LandscapeZ = FMath::Lerp(
                FMath::Lerp(H00, H10, FracX),
                FMath::Lerp(H01, H11, FracX),
                FracY);

            if (V.Z > LandscapeZ)
                V.Z = LandscapeZ;
        }
    }
}







// ----------------------------------------------------------------------------------
// HELPERS
// ----------------------------------------------------------------------------------

// =============================================================================
// CaptureHeightMap — OPTIMISED
//
// The original version called GetLandscapeHeightAt sequentially for every
// sample on every remesh. For a ChunkSize=32, Subdivisions=4, Pad=2 chunk
// that is (32*4 + 2*2 + 1)² = 133² = 17,689 calls per remesh. Even with the
// landscape cache doing a TMap lookup, this is the dominant per-remesh cost.
//
// Optimisation strategy:
//
// 1. PERSISTENT CACHE on UMarchingCubes:
//    The height map for a given chunk origin never changes unless the landscape
//    is sculpted (which Digger doesn't do). Cache the result keyed by
//    (ChunkOrigin, VoxelSize, GridResolution, Pad). On subsequent remeshes of
//    the same chunk, return the cached array immediately — zero sampling cost.
//    Cache is invalidated by ClearHeightCache() (called from RefreshLandscapeCache).
//
// 2. PARALLEL SAMPLING via ParallelFor:
//    When the cache is cold (first remesh of a chunk), sample all rows in
//    parallel. GetLandscapeHeightAt uses an FRWLock read-side so it is
//    thread-safe for concurrent reads. This cuts the first-time cost by
//    roughly the number of physical cores (typically 6-8x on a modern workstation).
//
// Result: first remesh of a chunk pays the parallel sampling cost once.
//         Every subsequent remesh returns immediately from cache.
// =============================================================================
 
// REVERT — exact original CaptureHeightMap from uploaded MarchingCubes.cpp
// Replace whatever is in your file with this verbatim. No cache, no changes.

TArray<float> UMarchingCubes::CaptureHeightMap(const FVector& Origin, float VoxelSize, int32 GridResolution, int32 Pad)
{
	const int32 SampleSize = GridResolution + 2 * Pad + 1;
	TArray<float> Heights;
	Heights.SetNumUninitialized(SampleSize * SampleSize);

	if (DiggerManager)
	{
		for (int32 y = 0; y < SampleSize; ++y)
		{
			for (int32 x = 0; x < SampleSize; ++x)
			{
				const FVector ColumnPos = Origin + FVector((x - Pad) * VoxelSize, (y - Pad) * VoxelSize, 0.0f);
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

void UMarchingCubes::ClearHeightCache()
{
	HeightCache.Empty();
	bHeightCacheInitialized = false;
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

// void UMarchingCubes::ReconstructMeshSection(int32 SectionIndex, const TArray<FVector>& OutVertices, const TArray<int32>& OutTriangles, const TArray<FVector>& Normals) const 
// {
// 	if (!DiggerManager || !DiggerManager->ProceduralMesh) return;
// 	if (SectionIndex < 0) return;
// 	if (OutVertices.Num() == 0) return;
//
// 	TArray<FVector2D> UVs;
// 	TArray<FColor> Colors;
// 	TArray<FProcMeshTangent> Tangents;
//
// 	DiggerManager->ProceduralMesh->CreateMeshSection(
// 		SectionIndex,
// 		OutVertices,
// 		OutTriangles,
// 		Normals,
// 		UVs,
// 		Colors,
// 		Tangents,
// 		true
// 	);
//
// 	DiggerManager->ProceduralMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
// 	if (DiggerManager->GetTerrainMaterial())
// 	{
// 		DiggerManager->ProceduralMesh->SetMaterial(SectionIndex, DiggerManager->GetTerrainMaterial());
// 	}
//
// 	// 🔔 NOW notify the chunk with proper data 
// 	if (OnMeshReady.IsBound()) 
// 	{ 
// 		const FIntVector ChunkCoord = (MyVoxelChunk ? MyVoxelChunk->GetChunkCoords() : FIntVector::ZeroValue); OnMeshReady.Execute(ChunkCoord, SectionIndex); 
// 	}
// }

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

// =============================================================================
// ClearHeightCache — invalidates the persistent height map cache.
// Call this from RefreshLandscapeCache() so a landscape sculpt or re-import
// forces all chunks to re-sample on their next remesh.
// =============================================================================
 
// void UMarchingCubes::ClearHeightCache()
// {
// 	HeightMapCache.Empty();
// 	bHeightCacheInitialized = false;
// }
 

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