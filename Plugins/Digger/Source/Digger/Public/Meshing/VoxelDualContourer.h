#pragma once

#include "CoreMinimal.h"
#include "SparseVoxelGrid.h"

// Eigen lives at Plugins/Eigen/ (sibling plugin).
// In Digger.Build.cs:
//   PublicIncludePaths.Add(Path.Combine(PluginDirectory, "..", "Eigen"));
THIRD_PARTY_INCLUDES_START
#include "Dense"
THIRD_PARTY_INCLUDES_END

// ─────────────────────────────────────────────────────────────────────────────
//  QEF Solver  —  minimises E(x) = Σ (n_i · (x − p_i))²
//  One constraint per sign-changing edge: p_i = crossing, n_i = surface normal.
// ─────────────────────────────────────────────────────────────────────────────

struct FQEFSolver
{
    Eigen::Matrix3d ATA       = Eigen::Matrix3d::Zero();
    Eigen::Vector3d ATb       = Eigen::Vector3d::Zero();
    Eigen::Vector3d MassPoint = Eigen::Vector3d::Zero();
    int32           NumConstraints = 0;

    void Reset()
    {
        ATA = Eigen::Matrix3d::Zero();
        ATb = Eigen::Vector3d::Zero();
        MassPoint = Eigen::Vector3d::Zero();
        NumConstraints = 0;
    }

    FORCEINLINE void AddConstraint(const FVector& P, const FVector& N)
    {
        Eigen::Vector3d n(N.X, N.Y, N.Z);
        Eigen::Vector3d p(P.X, P.Y, P.Z);
        n.normalize();
        ATA       += n * n.transpose();
        ATb       += n * n.dot(p);
        MassPoint += p;
        ++NumConstraints;
    }

    /**
     * Solve for the vertex position inside this cell.
     * @param CellMin/CellMax  World-space AABB — result is clamped to this.
     * @param SvdThreshold     Lower = sharper features, higher = more stable.
     *                         0.1 is a safe production default.
     */
    void Solve(FVector& OutVertex,
               const FVector& CellMin, const FVector& CellMax,
               double SvdThreshold = 0.1) const
    {
        if (NumConstraints == 0)
        {
            // No crossings — place vertex at cell centre
            OutVertex = (CellMin + CellMax) * 0.5f;
            return;
        }

        const Eigen::Vector3d Centroid = MassPoint / static_cast<double>(NumConstraints);

        Eigen::JacobiSVD<Eigen::Matrix3d> SVD(ATA, Eigen::ComputeFullU | Eigen::ComputeFullV);
        SVD.setThreshold(SvdThreshold);

        // Solve in centroid-relative space for numerical stability
        const Eigen::Vector3d ShiftedB = ATb - ATA * Centroid;
        const Eigen::Vector3d Result   = Centroid + SVD.solve(ShiftedB);

        // Clamp to cell bounds — prevents blow-up on degenerate / under-constrained QEFs
        OutVertex.X = FMath::Clamp(static_cast<float>(Result.x()), CellMin.X, CellMax.X);
        OutVertex.Y = FMath::Clamp(static_cast<float>(Result.y()), CellMin.Y, CellMax.Y);
        OutVertex.Z = FMath::Clamp(static_cast<float>(Result.z()), CellMin.Z, CellMax.Z);

        // Final NaN/Inf guard
        if (!FMath::IsFinite(OutVertex.X) || !FMath::IsFinite(OutVertex.Y) || !FMath::IsFinite(OutVertex.Z))
            OutVertex = (CellMin + CellMax) * 0.5f;
    }
};


// ─────────────────────────────────────────────────────────────────────────────
//  FVoxelDualContourer
//
//  Production-ready dual contouring mesher for USparseVoxelGrid.
//
//  The signature mirrors UMarchingCubes::GenerateMeshFromGrid exactly,
//  including the HeightValues parameter, which is required to correctly
//  resolve the SDF of unset voxels (below landscape = solid, above = air).
//
//  Usage in GenerateMeshDC() — identical to GenerateMesh():
//
//    TArray<float> LocalHeights = MarchingCubesGenerator->GetCachedHeightValues();
//    FVoxelDualContourer::GenerateMesh(
//        CombinedData, Origin, VoxelSizeSnapshot, N, LocalHeights,
//        Verts, Tris, Normals);
// ─────────────────────────────────────────────────────────────────────────────

class DIGGER_API FVoxelDualContourer
{
public:

    /**
     * Generate a dual-contoured mesh from a pre-merged voxel snapshot.
     *
     * @param CombinedData   Own + neighbour boundary voxels (from GenerateMeshDC).
     * @param Origin         World-space origin of voxel (0,0,0) — ChunkToWorld().
     * @param VoxelSize      World-space size of one voxel.
     * @param N              Grid resolution = ChunkSize * Subdivisions.
     * @param HeightValues   Flat (N+1)² landscape height array from CaptureHeightMap.
     *                       Used to compute SDF for voxels absent from CombinedData.
     * @param OutVertices    World-space output vertices.
     * @param OutIndices     Triangle index list (CCW winding, matches MC output).
     * @param OutNormals     Per-vertex averaged surface normals.
     * @param IsoValue       Isosurface threshold. 0.0 is correct for your convention.
     * @param SvdThreshold   QEF sharpness. 0.1 = stable, 0.01 = sharper features.
     */
    static void GenerateMesh(
        const TMap<FIntVector, FVoxelData>& CombinedData,
        const FVector&                      Origin,
        float                               VoxelSize,
        int32                               N,
        const TArray<float>&                HeightValues,
        TArray<FVector>&                    OutVertices,
        TArray<int32>&                      OutIndices,
        TArray<FVector>&                    OutNormals,
        float                               IsoValue     = 0.f,
        double                              SvdThreshold = 0.1);

private:

    // ── Dense grid ───────────────────────────────────────────────────────────

    FORCEINLINE static int32 Idx(int32 X, int32 Y, int32 Z, int32 S)
    {
        return X + S * (Y + S * Z);
    }

    /**
     * Build a dense (N+1)³ SDF grid.
     *
     * Priority for each grid point:
     *   1. Explicitly stored in CombinedData  →  use stored SDFValue
     *   2. Below landscape surface            →  SDF_SOLID (-1)
     *   3. Above landscape surface            →  SDF_AIR   (+1)
     *
     * Then applies two narrow-band Laplacian passes to create a gradient
     * around the isosurface so finite-difference normals are well-defined.
     */
    static void BuildDenseGrid(
        const TMap<FIntVector, FVoxelData>& CombinedData,
        const TArray<float>&                HeightValues,
        const FVector&                      Origin,
        float                               VoxelSize,
        int32                               N,
        int32                               Stride,
        TArray<float>&                      OutGrid);

    // Finite-difference gradient at a grid point (safe: grid has 1-cell border)
    static FVector GridNormal(const TArray<float>& G, int32 X, int32 Y, int32 Z, int32 S);

    // Linearly interpolate to the isosurface crossing along one edge
    static FVector InterpolateEdge(
        const FVector& P0, float D0,
        const FVector& P1, float D1,
        float IsoValue);

    // Accumulate QEF constraints from all 12 edges of cell (X,Y,Z).
    // Returns false when no sign changes exist (cell is entirely inside or outside).
    static bool BuildCellQEF(
        const TArray<float>& Grid,
        int32 X, int32 Y, int32 Z, int32 Stride,
        float VoxelSize, const FVector& Origin,
        float IsoValue,
        FQEFSolver& OutQEF,
        FVector&    OutAvgNormal);

    // Emit two triangles (one quad) for a sign-changing edge.
    // All four surrounding cell vertices must exist — missing ones are silently skipped.
    static void EmitQuad(
        TArray<int32>&                 Indices,
        const TMap<FIntVector, int32>& CellToVertex,
        const FIntVector& C0, const FIntVector& C1,
        const FIntVector& C2, const FIntVector& C3,
        bool bFlip);
};