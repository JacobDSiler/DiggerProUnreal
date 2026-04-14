#include "VoxelDualContourer.h"
#include "VoxelConversion.h"   // FVoxelConversion — for LocalVoxelToWorld fallback

// ─────────────────────────────────────────────────────────────────────────────
//  Cube corner / edge tables  (standard Marching Cubes convention)
//
//  Corner bit layout:  bit0=+X  bit1=+Y  bit2=+Z
//
//      7─6
//     /| /|
//    4─5 |     Corner 0 = cell min corner
//    | 3─2     Corner 7 = cell max corner
//    |/ |/
//    0─1
// ─────────────────────────────────────────────────────────────────────────────

static constexpr int32 GCX[8] = { 0,1,1,0, 0,1,1,0 };
static constexpr int32 GCY[8] = { 0,0,1,1, 0,0,1,1 };
static constexpr int32 GCZ[8] = { 0,0,0,0, 1,1,1,1 };

// 12 edges: [cornerA, cornerB]
static constexpr int32 GEA[12] = { 0,1,2,3, 4,5,6,7, 0,1,2,3 };
static constexpr int32 GEB[12] = { 1,2,3,0, 5,6,7,4, 4,5,6,7 };

// ─────────────────────────────────────────────────────────────────────────────
//  BuildDenseGrid
//
//  This is the most important function in the mesher.
//
//  Your sparse map only stores voxels that have been explicitly modified.
//  Everything else follows the landscape: below terrain = solid, above = air.
//  MC resolves this in GetVoxel() via a landscape height query, but that runs
//  on the game thread. We replicate that logic here using the HeightValues
//  array that was already captured on the game thread before the async task.
//
//  HeightValues layout: flat array, size (N+1)², indexed [Y*(N+1) + X].
//  This matches exactly what CaptureHeightMap / CacheHeightMap produces.
// ─────────────────────────────────────────────────────────────────────────────

void FVoxelDualContourer::BuildDenseGrid(
    const TMap<FIntVector, FVoxelData>& CombinedData,
    const TArray<float>&                HeightValues,
    const FVector&                      Origin,
    float                               VoxelSize,
    int32                               N,
    int32                               Stride,     // = N + 1
    TArray<float>&                      OutGrid)
{
    const int32 Total         = Stride * Stride * Stride;
    const bool  bHaveHeights  = (HeightValues.Num() == Stride * Stride);

    OutGrid.SetNumUninitialized(Total);

    // ── Pass A: fill baseline from landscape height ───────────────────────────
    // Every grid point gets its "unmodified terrain" SDF value.
    // Points with no height data default to AIR so we never produce phantom solid.
    for (int32 Z = 0; Z < Stride; ++Z)
    for (int32 Y = 0; Y < Stride; ++Y)
    for (int32 X = 0; X < Stride; ++X)
    {
        float BaseSDF = 1.f;   // default = air

        if (bHaveHeights)
        {
            // Height array is indexed by (X, Y) in the horizontal plane.
            // Clamp to valid range — the array covers [0, N] in X and Y.
            const int32 HX = FMath::Clamp(X, 0, N);
            const int32 HY = FMath::Clamp(Y, 0, N);
            const float TerrainWorldZ = HeightValues[HY * Stride + HX];

            // World-space Z of this grid point
            const float PointWorldZ = Origin.Z + Z * VoxelSize;

            // Match GetVoxel()'s convention exactly:
            //   below terrain → SDF_SOLID (-1), above → SDF_AIR (+1)
            BaseSDF = (PointWorldZ < TerrainWorldZ) ? -1.f : 1.f;
        }

        OutGrid[Idx(X, Y, Z, Stride)] = BaseSDF;
    }

    // ── Pass B: stamp explicitly-modified voxels ──────────────────────────────
    // These override the landscape baseline wherever a brush has been applied.
    // Keys outside [0, N] are neighbour boundary data — clamp them to the
    // grid edge so they influence the boundary cells correctly.
    for (const auto& Pair : CombinedData)
    {
        const int32 X = FMath::Clamp(Pair.Key.X, 0, N);
        const int32 Y = FMath::Clamp(Pair.Key.Y, 0, N);
        const int32 Z = FMath::Clamp(Pair.Key.Z, 0, N);

        // Only overwrite if the stored value is meaningfully different from air
        // (rejects near-zero junk entries that can appear at chunk edges).
        if (FMath::IsFinite(Pair.Value.SDFValue))
            OutGrid[Idx(X, Y, Z, Stride)] = Pair.Value.SDFValue;
    }

    // ── Pass C: narrow-band Laplacian smoothing ───────────────────────────────
    // Softens the hard ±1 step at the landscape baseline into a gradient so
    // that finite-difference normals are well-defined at the isosurface.
    // Only cells within BandWidth of the iso are touched — deep solid/air
    // cells are left completely unchanged.
    //
    // Two passes is the right balance: enough to create a gradient,
    // not so many that the surface drifts from where voxels were painted.
    constexpr float BandWidth    = 1.5f;
    constexpr float SmoothWeight = 0.35f;

    TArray<float> Tmp = OutGrid;

    for (int32 Pass = 0; Pass < 2; ++Pass)
    {
        for (int32 Z = 1; Z < Stride-1; ++Z)
        for (int32 Y = 1; Y < Stride-1; ++Y)
        for (int32 X = 1; X < Stride-1; ++X)
        {
            const float V = Tmp[Idx(X,Y,Z,Stride)];
            if (FMath::Abs(V) >= BandWidth) continue;

            const float Avg =
               (Tmp[Idx(X+1,Y,  Z,  Stride)] + Tmp[Idx(X-1,Y,  Z,  Stride)] +
                Tmp[Idx(X,  Y+1,Z,  Stride)] + Tmp[Idx(X,  Y-1,Z,  Stride)] +
                Tmp[Idx(X,  Y,  Z+1,Stride)] + Tmp[Idx(X,  Y,  Z-1,Stride)]) / 6.f;

            OutGrid[Idx(X,Y,Z,Stride)] = FMath::Lerp(V, Avg, SmoothWeight);
        }
        if (Pass == 0) Tmp = OutGrid;   // only need to refresh once between passes
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  GridNormal  —  central finite differences on the dense grid
// ─────────────────────────────────────────────────────────────────────────────

FVector FVoxelDualContourer::GridNormal(
    const TArray<float>& G, int32 X, int32 Y, int32 Z, int32 S)
{
    // Safe: callers already guard X/Y/Z against [1, Stride-2]
    const float Dx = G[Idx(X+1,Y,  Z,  S)] - G[Idx(X-1,Y,  Z,  S)];
    const float Dy = G[Idx(X,  Y+1,Z,  S)] - G[Idx(X,  Y-1,Z,  S)];
    const float Dz = G[Idx(X,  Y,  Z+1,S)] - G[Idx(X,  Y,  Z-1,S)];

    const FVector Grad(Dx, Dy, Dz);
    if (Grad.IsNearlyZero(1e-6f))
    {
        // Degenerate gradient (flat region or fully uniform neighbourhood).
        // Return a world-up vector as a safe fallback — this vertex will end
        // up at the cell centroid anyway due to the QEF fallback.
        return FVector::UpVector;
    }
    return Grad.GetSafeNormal();
}

// ─────────────────────────────────────────────────────────────────────────────
//  InterpolateEdge  —  find world-space isosurface crossing position
// ─────────────────────────────────────────────────────────────────────────────

FVector FVoxelDualContourer::InterpolateEdge(
    const FVector& P0, float D0,
    const FVector& P1, float D1,
    float IsoValue)
{
    const float Denom = D0 - D1;
    if (FMath::IsNearlyZero(Denom, 1e-6f)) return (P0 + P1) * 0.5f;
    const float T = FMath::Clamp((D0 - IsoValue) / Denom, 0.f, 1.f);
    return FMath::Lerp(P0, P1, T);
}

// ─────────────────────────────────────────────────────────────────────────────
//  BuildCellQEF  —  find all sign-changing edges and accumulate plane constraints
// ─────────────────────────────────────────────────────────────────────────────

bool FVoxelDualContourer::BuildCellQEF(
    const TArray<float>& Grid,
    int32 X, int32 Y, int32 Z, int32 Stride,
    float VoxelSize, const FVector& Origin,
    float IsoValue,
    FQEFSolver& OutQEF,
    FVector&    OutAvgNormal)
{
    float   D[8];
    FVector W[8];

    for (int32 c = 0; c < 8; ++c)
    {
        const int32 Cx = X + GCX[c];
        const int32 Cy = Y + GCY[c];
        const int32 Cz = Z + GCZ[c];
        D[c] = Grid[Idx(Cx, Cy, Cz, Stride)];
        W[c] = Origin + FVector(Cx, Cy, Cz) * VoxelSize;
    }

    OutQEF.Reset();
    FVector NormalAccum = FVector::ZeroVector;
    int32   Count       = 0;

    for (int32 e = 0; e < 12; ++e)
    {
        const int32 A = GEA[e];
        const int32 B = GEB[e];

        const bool bAIn = (D[A] < IsoValue);
        const bool bBIn = (D[B] < IsoValue);
        if (bAIn == bBIn) continue;

        // Crossing position
        const FVector HitPos = InterpolateEdge(W[A], D[A], W[B], D[B], IsoValue);

        // Normal: sample from the corner closest to the isosurface
        const int32 BestC = (FMath::Abs(D[A] - IsoValue) <= FMath::Abs(D[B] - IsoValue)) ? A : B;
        const int32 NX    = X + GCX[BestC];
        const int32 NY    = Y + GCY[BestC];
        const int32 NZ    = Z + GCZ[BestC];

        // Guard: central differences require at least 1 cell border
        if (NX < 1 || NX >= Stride-1 ||
            NY < 1 || NY >= Stride-1 ||
            NZ < 1 || NZ >= Stride-1)
        {
            // Border corner — use a simple sign-based fallback normal
            const FVector Dir = (W[B] - W[A]).GetSafeNormal();
            const FVector N   = bAIn ? Dir : -Dir;
            OutQEF.AddConstraint(HitPos, N);
            NormalAccum += N;
        }
        else
        {
            const FVector N = GridNormal(Grid, NX, NY, NZ, Stride);
            OutQEF.AddConstraint(HitPos, N);
            NormalAccum += N;
        }

        ++Count;
    }

    if (Count == 0) return false;
    OutAvgNormal = NormalAccum.GetSafeNormal();
    if (OutAvgNormal.IsNearlyZero()) OutAvgNormal = FVector::UpVector;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  EmitQuad  —  two triangles from the four cells sharing one sign-changing edge
// ─────────────────────────────────────────────────────────────────────────────

void FVoxelDualContourer::EmitQuad(
    TArray<int32>&                 Indices,
    const TMap<FIntVector, int32>& CellToVertex,
    const FIntVector& C0, const FIntVector& C1,
    const FIntVector& C2, const FIntVector& C3,
    bool bFlip)
{
    const int32* I0 = CellToVertex.Find(C0);
    const int32* I1 = CellToVertex.Find(C1);
    const int32* I2 = CellToVertex.Find(C2);
    const int32* I3 = CellToVertex.Find(C3);

    // All four surrounding cells must have sign-changing content.
    // Boundary quads where one cell is outside [0,N) are correctly skipped here.
    if (!I0 || !I1 || !I2 || !I3) return;

    if (!bFlip)
    {
        Indices.Append({ *I0, *I1, *I2,
                         *I0, *I2, *I3 });
    }
    else
    {
        Indices.Append({ *I0, *I2, *I1,
                         *I0, *I3, *I2 });
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  GenerateMesh  —  public entry point
// ─────────────────────────────────────────────────────────────────────────────

void FVoxelDualContourer::GenerateMesh(
    const TMap<FIntVector, FVoxelData>& CombinedData,
    const FVector&                      Origin,
    float                               VoxelSize,
    int32                               N,
    const TArray<float>&                HeightValues,
    TArray<FVector>&                    OutVertices,
    TArray<int32>&                      OutIndices,
    TArray<FVector>&                    OutNormals,
    float                               IsoValue,
    double                              SvdThreshold)
{
    OutVertices.Reset();
    OutIndices.Reset();
    OutNormals.Reset();

    // ── Sanity checks ─────────────────────────────────────────────────────────
    if (N <= 0)
    {
        UE_LOG(LogTemp, Error, TEXT("[DC] N=%d is invalid."), N);
        return;
    }
    if (VoxelSize <= 0.f)
    {
        UE_LOG(LogTemp, Error, TEXT("[DC] VoxelSize=%.3f is invalid."), VoxelSize);
        return;
    }

    const int32 Stride     = N + 1;
    const int32 ExpectedHW = Stride * Stride;

    if (HeightValues.Num() != ExpectedHW)
    {
        // Height array is malformed — log and fall back to air-only baseline.
        // This produces a mesh for dig-carved regions even without terrain data.
        UE_LOG(LogTemp, Warning,
            TEXT("[DC] HeightValues size mismatch: got %d expected %d. "
                 "Terrain baseline will be treated as all-air."),
            HeightValues.Num(), ExpectedHW);
    }

    // ── Step 1: Build dense (N+1)³ SDF grid ──────────────────────────────────
    TArray<float> Grid;
    BuildDenseGrid(CombinedData, HeightValues, Origin, VoxelSize, N, Stride, Grid);

    // ── Step 2 (DC Pass 1): Solve QEF per sign-changing cell ─────────────────
    // Reserve a conservative estimate. Surface cells are ~10-20% of total.
    TMap<FIntVector, int32> CellToVertex;
    CellToVertex.Reserve(FMath::Max(64, N * N * N / 8));

    for (int32 CZ = 0; CZ < N; ++CZ)
    for (int32 CY = 0; CY < N; ++CY)
    for (int32 CX = 0; CX < N; ++CX)
    {
        FQEFSolver QEF;
        FVector    AvgNormal;

        if (!BuildCellQEF(Grid, CX, CY, CZ, Stride, VoxelSize, Origin, IsoValue, QEF, AvgNormal))
            continue;

        const FVector CellMin = Origin + FVector(CX,   CY,   CZ  ) * VoxelSize;
        const FVector CellMax = Origin + FVector(CX+1, CY+1, CZ+1) * VoxelSize;

        FVector Vertex;
        QEF.Solve(Vertex, CellMin, CellMax, SvdThreshold);

        const int32 VertIdx = OutVertices.Add(Vertex);
        OutNormals.Add(AvgNormal);
        CellToVertex.Add(FIntVector(CX, CY, CZ), VertIdx);
    }

    if (OutVertices.Num() == 0) return;

    // ── Step 3 (DC Pass 2): Stitch quads along sign-changing edges ───────────
    //
    // Iterate over every grid POINT (not cell). For each of the 3 positive-axis
    // edges, check for a sign change and emit the quad from the 4 surrounding cells.
    //
    // The cell coordinates in the quad can be negative (e.g. when GY=0, GY-1=-1).
    // EmitQuad silently skips quads where any cell is missing — this correctly
    // handles boundary edges where one cell is outside [0,N) and has no vertex.
    //
    // Quad cell layouts (GX,GY,GZ = the "lower" grid point on the edge):
    //
    //  X-edge (GX,GY,GZ)→(GX+1,...):  cells (GX,GY,GZ), (GX,GY-1,GZ),
    //                                         (GX,GY-1,GZ-1), (GX,GY,GZ-1)
    //  Y-edge (GX,GY,GZ)→(...,GY+1,...): cells (GX,GY,GZ), (GX-1,GY,GZ),
    //                                           (GX-1,GY,GZ-1), (GX,GY,GZ-1)
    //  Z-edge (GX,GY,GZ)→(...,GZ+1):  cells (GX,GY,GZ), (GX-1,GY,GZ),
    //                                         (GX-1,GY-1,GZ), (GX,GY-1,GZ)

    OutIndices.Reserve(OutVertices.Num() * 6);   // 6 indices per vertex on average

    for (int32 GZ = 0; GZ <= N; ++GZ)
    for (int32 GY = 0; GY <= N; ++GY)
    for (int32 GX = 0; GX <= N; ++GX)
    {
        const float DA  = Grid[Idx(GX, GY, GZ, Stride)];
        const bool  bIn = (DA < IsoValue);

        if (GX < N)
        {
            const bool bBIn = (Grid[Idx(GX+1, GY, GZ, Stride)] < IsoValue);
            if (bIn != bBIn)
            {
                EmitQuad(OutIndices, CellToVertex,
                    FIntVector(GX, GY,   GZ  ),
                    FIntVector(GX, GY-1, GZ  ),
                    FIntVector(GX, GY-1, GZ-1),
                    FIntVector(GX, GY,   GZ-1),
                    /*bFlip=*/ bIn);
            }
        }

        if (GY < N)
        {
            const bool bBIn = (Grid[Idx(GX, GY+1, GZ, Stride)] < IsoValue);
            if (bIn != bBIn)
            {
                EmitQuad(OutIndices, CellToVertex,
                    FIntVector(GX,   GY, GZ  ),
                    FIntVector(GX-1, GY, GZ  ),
                    FIntVector(GX-1, GY, GZ-1),
                    FIntVector(GX,   GY, GZ-1),
                    /*bFlip=*/ !bIn);
            }
        }

        if (GZ < N)
        {
            const bool bBIn = (Grid[Idx(GX, GY, GZ+1, Stride)] < IsoValue);
            if (bIn != bBIn)
            {
                EmitQuad(OutIndices, CellToVertex,
                    FIntVector(GX,   GY,   GZ),
                    FIntVector(GX-1, GY,   GZ),
                    FIntVector(GX-1, GY-1, GZ),
                    FIntVector(GX,   GY-1, GZ),
                    /*bFlip=*/ bIn);
            }
        }
    }
}