#include "VoxelChunk.h"

#include "DiggerDebug.h"
#include "DiggerManager.h"
#include "DynamicHole.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "HLSLTypeAliases.h"
#include "DynamicHolesHelpers.h"
#include "MarchingCubes.h"
#include "SparseVoxelGrid.h"
#include "VoxelBrushTypes.h"
#include "VoxelConversion.h"
#include "VoxelLogManager.h"
#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "Landscape.h"
#include "Utils/FastDebugRenderer.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDeviceNull.h"
#include "Serialization/BufferArchive.h"
#include "SceneInterface.h"
#include "VoxelDualContourer.h"
#include "Materials/MaterialInstanceDynamic.h"

struct FSpawnedHoleData;

struct FVoxelStrokeInfo
{
	FIntVector Coords;
	FVector WorldPos;
	float TerrainHeight;
};


// =============================================================================
// CONSTRUCTION / LIFECYCLE
// =============================================================================

UVoxelChunk::UVoxelChunk()
	: ChunkCoordinates(FIntVector::ZeroValue),
	  SectionIndex(0),
	  TerrainGridSize(100),
	  Subdivisions(4),
	  bIsDirty(false),
	  bSuppressDirty(false),
	  DirtyWorldBounds(ForceInit),
	  DiggerManager(nullptr),
	  SparseVoxelGrid(nullptr)
{
	// CreateDefaultSubobject only works during CDO construction (i.e. the very
	// first time the engine builds the class default object).  Every subsequent
	// NewObject<UVoxelChunk>() call — including all runtime chunk creation after
	// a ClearAllVoxelData — must use NewObject instead.  We branch on
	// HasAnyFlags(RF_ClassDefaultObject) so the CDO still gets subobjects via
	// the normal path, while runtime instances get them via NewObject.
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		SparseVoxelGrid        = CreateDefaultSubobject<USparseVoxelGrid>(TEXT("SparseVoxelGrid"));
		MarchingCubesGenerator = CreateDefaultSubobject<UMarchingCubes>(TEXT("MarchingCubesGenerator"));
	}
	else
	{
		SparseVoxelGrid        = NewObject<USparseVoxelGrid>(this, TEXT("SparseVoxelGrid"));
		MarchingCubesGenerator = NewObject<UMarchingCubes>(this, TEXT("MarchingCubesGenerator"));
	}

	if (MarchingCubesGenerator)
		MarchingCubesGenerator->SetOwningChunk(this);
}

void UVoxelChunk::Tick(float DeltaTime)
{
	UpdateIfDirty();
}

void UVoxelChunk::RebuildCollisionIfNeeded(bool bIdleFullRebuild)
{
	// Never cook collision while the user is actively painting.
	// CreateMeshSection(bCreateCollision=true) is a synchronous physics cook
	// on the game thread — even the AABB-culled subset stalls the brush.
	// We only cook when idle (bIdleFullRebuild=true) so the timer fires once
	// after the stroke ends, giving accurate collision without any interactive lag.
	if (!bIdleFullRebuild) return;

	const bool bNeedsAny = bNeedsCollisionRebuild || bNeedsFullCollisionRebuild;
	if (!bNeedsAny || !ProceduralMeshComponent) return;

	// Always do a full cook on idle — the AABB-culled path was only useful
	// while painting, which we now skip entirely.  Full cook gives accurate
	// collision everywhere and only runs when the user has stopped brushing.
	const bool bDoFullCook = true;

	FProcMeshSection* Existing = ProceduralMeshComponent->GetProcMeshSection(SectionIndex);
	if (!Existing)
	{
		ProceduralMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		bNeedsCollisionRebuild = false;
		CollisionDirtyBounds   = FBox(ForceInit);
		return;
	}

	// AABB-culled collision cook:
	// Only submit triangles whose centroid falls within CollisionDirtyBounds
	// (expanded by 2 voxel sizes for safety).  For a small brush on a large
	// chunk this submits a tiny fraction of the total triangle count, making
	// the physics cook proportional to brush size rather than chunk size.
	//
	// We use a dedicated collision section (SectionIndex + 1 offset via a
	// separate PMC section reserved for collision-only geometry) — actually
	// the simplest correct approach for PMC is to re-submit the full section
	// but with only the culled triangles having bCreateCollision=true.
	// PMC cooks collision for all triangles in a section atomically, so we
	// must submit a trimmed section.  We use a separate collision section
	// index to avoid overwriting the visual geometry.
	const int32 CollisionSection = SectionIndex + 10000; // high offset avoids visual section

	// bDoFullCook: ignore AABB, cook all triangles.
	// bNeedsCollisionRebuild: cook only the dirty AABB (fast, proportional to brush size).
	const FBox  CookBounds = (!bDoFullCook && CollisionDirtyBounds.IsValid)
		? CollisionDirtyBounds.ExpandBy(FVoxelConversion::LocalVoxelSize * 2.f)
		: FBox(ForceInit); // invalid = cook all triangles

	// Build vertex/index arrays, culling triangles outside CookBounds.
	TArray<FVector>          Verts;
	TArray<int32>            Tris;
	TArray<FVector>          Normals;
	TArray<FVector2D>        UVs;
	TArray<FColor>           Colors;
	TArray<FProcMeshTangent> Tangents;

	const TArray<FProcMeshVertex>& SrcVerts = Existing->ProcVertexBuffer;
	const TArray<uint32>&          SrcIdx   = Existing->ProcIndexBuffer;
	const int32 TriCount = SrcIdx.Num() / 3;

	// Map from original vertex index → new packed index (avoid duplicates).
	TMap<int32, int32> VertRemap;
	VertRemap.Reserve(FMath::Min(SrcVerts.Num(), 512));

	for (int32 t = 0; t < TriCount; ++t)
	{
		const int32 i0 = (int32)SrcIdx[t * 3 + 0];
		const int32 i1 = (int32)SrcIdx[t * 3 + 1];
		const int32 i2 = (int32)SrcIdx[t * 3 + 2];

		// Cull by triangle centroid.
		if (CookBounds.IsValid)
		{
			const FVector Centroid = (SrcVerts[i0].Position +
			                          SrcVerts[i1].Position +
			                          SrcVerts[i2].Position) / 3.f;
			if (!CookBounds.IsInside(Centroid))
				continue;
		}

		// Pack referenced vertices.
		auto Pack = [&](int32 Orig) -> int32
		{
			if (int32* Found = VertRemap.Find(Orig)) return *Found;
			const int32 NewIdx = Verts.Num();
			Verts.Add(SrcVerts[Orig].Position);
			Normals.Add(SrcVerts[Orig].Normal);
			VertRemap.Add(Orig, NewIdx);
			return NewIdx;
		};

		Tris.Add(Pack(i0));
		Tris.Add(Pack(i1));
		Tris.Add(Pack(i2));
	}

	if (Verts.Num() > 0)
	{
		ProceduralMeshComponent->CreateMeshSection(
			CollisionSection, Verts, Tris, Normals,
			UVs, Colors, Tangents, /*bCreateCollision=*/true);

		// Make the collision section invisible — it exists only for physics.
		ProceduralMeshComponent->SetMeshSectionVisible(CollisionSection, false);
	}

	bNeedsCollisionRebuild = false;
	CollisionDirtyBounds   = FBox(ForceInit);
	if (bDoFullCook)
		bNeedsFullCollisionRebuild = false; // full cook done — collision is authoritative
}


// =============================================================================
// INITIALIZATION
// =============================================================================

// Global section ID counter — extern so ResetGlobalChunkID() can zero it on clear.
static int32 GDiggerGlobalChunkID = 0;

void UVoxelChunk::ResetGlobalChunkID()
{
	GDiggerGlobalChunkID = 0;
}

void UVoxelChunk::SetUniqueSectionIndex()
{
	SectionIndex = GDiggerGlobalChunkID;
	if (DiggerDebug::Mesh() || DiggerDebug::Chunks())
		UE_LOG(LogTemp, Warning, TEXT("SectionID Set to: %i for ChunkCoordinates X=%d Y=%d Z=%d"),
			SectionIndex, ChunkCoordinates.X, ChunkCoordinates.Y, ChunkCoordinates.Z);
	GDiggerGlobalChunkID++;
}


void UVoxelChunk::InitializeChunk(const FIntVector& InChunkCoordinates, ADiggerManager* InDiggerManager)
{
    ChunkCoordinates = InChunkCoordinates;

    if (InDiggerManager)
    {
        FVoxelConversion::InitFromConfig(
            InDiggerManager->ChunkSize,
            InDiggerManager->Subdivisions,
            InDiggerManager->TerrainGridSize,
            InDiggerManager->GetActorLocation()
        );
    }

    if (DiggerDebug::Chunks())
        UE_LOG(LogTemp, Warning, TEXT("Chunk created at X: %i Y: %i Z: %i"),
            ChunkCoordinates.X, ChunkCoordinates.Y, ChunkCoordinates.Z);

    HoleBP = InDiggerManager ? InDiggerManager->DynamicHoleClass : nullptr;

    if (SparseVoxelGrid)
        SparseVoxelGrid->Initialize(this);

    if (!DiggerManager)
        DiggerManager = InDiggerManager;

    if (!DiggerManager)
        DiggerManager = ADiggerManager::FindDiggerManager(World);

    if (!DiggerManager)
    {
        if (DiggerDebug::Manager() || DiggerDebug::Verbose())
            UE_LOG(LogTemp, Error, TEXT("DiggerManager is null during chunk initialization!"));
        return;
    }

    SparseVoxelGrid->InitializeDiggerManager();
    SetUniqueSectionIndex();

    MarchingCubesGenerator->SetDiggerManager(DiggerManager);

    ChunkSize       = FVoxelConversion::ChunkSize;
    TerrainGridSize = FVoxelConversion::TerrainGridSize;
    Subdivisions    = FVoxelConversion::Subdivisions;

    VoxelSize = FVoxelConversion::LocalVoxelSize;
    if (VoxelSize <= 0)
        VoxelSize = 25;

    World = DiggerManager->GetWorldFromManager();

    if (!SparseVoxelGrid)
    {
        if (DiggerDebug::Voxels())
            UE_LOG(LogTemp, Error, TEXT("SparseVoxelGrid passed to InitializeChunk is null!"));
        return;
    }

    if (DiggerDebug::Chunks())
        UE_LOG(LogTemp, Warning, TEXT("Initializing new chunk at position X=%d Y=%d Z=%d"),
            ChunkCoordinates.X, ChunkCoordinates.Y, ChunkCoordinates.Z);

    DiggerManager->ChunkMap.Add(ChunkCoordinates, this);

    if (DiggerDebug::Chunks())
        UE_LOG(LogTemp, Warning, TEXT("Chunk added to ChunkMap at position: X=%d Y=%d Z=%d"),
            ChunkCoordinates.X, ChunkCoordinates.Y, ChunkCoordinates.Z);
}


void UVoxelChunk::InitializeMeshComponent(UProceduralMeshComponent* MeshComponent)
{
	ProceduralMeshComponent = MeshComponent;
}


void UVoxelChunk::InitializeDiggerManager(ADiggerManager* InDiggerManager)
{
	if (!DiggerManager) DiggerManager = InDiggerManager;
	if (!HoleShapeLibrary && DiggerManager->HoleShapeLibrary)
	{
		if (DiggerDebug::Chunks() || DiggerDebug::Holes())
			UE_LOG(LogTemp, Warning, TEXT("Chunk HoleShapeLibrary set successfully from the manager!"));
		HoleShapeLibrary = DiggerManager->HoleShapeLibrary;
	}
}


// =============================================================================
// MESH UPDATE
// =============================================================================

static void UploadMesh(UProceduralMeshComponent* PMC, int32 Section,
                       const TArray<FVector>& Verts, const TArray<int32>& Tris,
                       const TArray<FVector>& Normals, bool /*bBuildCollision*/)
{
    static const TArray<FVector2D>         EmptyUVs;
    static const TArray<FColor>            EmptyColors;
    static const TArray<FProcMeshTangent>  EmptyTangents;

    // Always upload WITHOUT collision on the hot path — the cook is deferred
    // to ProcessDirtyChunksLoop (0.1s timer) via bNeedsCollisionRebuild.
    // This keeps the brush stroke latency low while ensuring collision is
    // rebuilt shortly after each stroke for SmartTrace ECC_Visibility hits.
    PMC->CreateMeshSection(Section, Verts, Tris, Normals,
                           EmptyUVs, EmptyColors, EmptyTangents, /*bCreateCollision=*/false);
}

void UVoxelChunk::UpdateMeshFromData(const TArray<FVector>& Vertices,
                                      const TArray<int32>&   Triangles,
                                      const TArray<FVector>& Normals)
{
	if (!IsInGameThread())
	{
		AsyncTask(ENamedThreads::GameThread, [this, Vertices, Triangles, Normals]()
		{
			UpdateMeshFromData(Vertices, Triangles, Normals);
		});
		return;
	}

	if (!ProceduralMeshComponent)
	{
		if (DiggerManager && DiggerManager->ProceduralMesh)
			ProceduralMeshComponent = DiggerManager->ProceduralMesh;
		else
			return;
	}

	UploadMesh(ProceduralMeshComponent, SectionIndex, Vertices, Triangles, Normals, false);

	if (DiggerDebug::Mesh())
		UE_LOG(LogTemp, Log, TEXT("[History] UpdateMeshFromData chunk section %d: %d verts, %d tris"),
			SectionIndex, Vertices.Num(), Triangles.Num());

	// Collision properties are set once on first upload; repeating them every
	// stroke triggers extra component re-registration overhead.
	if (Vertices.Num() > 0 && !bCollisionPropertiesInitialised)
	{
		ProceduralMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		ProceduralMeshComponent->SetCollisionObjectType(ECC_WorldDynamic);
		ProceduralMeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
		ProceduralMeshComponent->bUseComplexAsSimpleCollision = true;
		bCollisionPropertiesInitialised = true;
	}
	// Defer the actual collision geometry rebuild to Tick (bNeedsCollisionRebuild).
	bNeedsCollisionRebuild = (Vertices.Num() > 0);

	if (DiggerManager)
		if (UMaterialInterface* Mat = DiggerManager->GetTerrainMaterial())
			if (ProceduralMeshComponent->GetMaterial(SectionIndex) != Mat)
				ProceduralMeshComponent->SetMaterial(SectionIndex, Mat);

	OnMeshReady(ChunkCoordinates, SectionIndex);
}


// =============================================================================
// DIRTY MARKING
// =============================================================================

void UVoxelChunk::MarkDirty()
{
	if (bSuppressDirty) return;   // writing phase — defer until all chunks stroked
	bIsDirty = true;
	if (DiggerManager)
		DiggerManager->RegisterDirtyChunk(ChunkCoordinates);
	else
		UE_LOG(LogTemp, Error, TEXT("DiggerManager Not Set properly to register a dirty chunk!"));
}

void UVoxelChunk::MarkDirtyWithBounds(const FBox& WorldBounds)
{
	// Union with any already-pending bounds so rapid multi-stroke sequences
	// are never lost.  An invalid incoming box means "full chunk required".
	if (WorldBounds.IsValid)
	{
		if (DirtyWorldBounds.IsValid)
			DirtyWorldBounds += WorldBounds;
		else
			DirtyWorldBounds = WorldBounds;

		// Accumulate for AABB-culled collision cook — successive strokes
		// grow the box; it resets in RebuildCollisionIfNeeded after each cook.
		CollisionDirtyBounds  = CollisionDirtyBounds.IsValid
			? CollisionDirtyBounds + WorldBounds
			: WorldBounds;
		bNeedsCollisionRebuild     = true;
		bNeedsFullCollisionRebuild = true; // cleared only after idle full cook
		// Register in the manager's O(dirty) collision set so
		// ProcessDirtyChunksLoop doesn't have to scan the full ChunkMap.
		if (DiggerManager)
			DiggerManager->CollisionDirtyCoords.Add(ChunkCoordinates);
	}

	MarkDirty();
}

void UVoxelChunk::UpdateIfDirty()
{
	if (bIsDirty)
	{
		GenerateMesh();
		bIsDirty = false;
	}
}

void UVoxelChunk::ForceUpdate()
{
	if (!IsInGameThread())
	{
		if (DiggerDebug::Mesh())
			UE_LOG(LogTemp, Warning, TEXT("ForceUpdate called from non-game thread, dispatching to game thread"));
		AsyncTask(ENamedThreads::GameThread, [this]()
		{
			if (IsValid(this)) this->ForceUpdate();
		});
		return;
	}

	if (DiggerDebug::Mesh())
		UE_LOG(LogTemp, Warning, TEXT("UVoxelChunk::ForceUpdate - Starting mesh regeneration"));

	GenerateMeshSyncronous();
	bIsDirty = false;

	if (DiggerDebug::Mesh())
		UE_LOG(LogTemp, Warning, TEXT("UVoxelChunk::ForceUpdate - Completed"));
}

void UVoxelChunk::RefreshSectionMesh()
{
	UMarchingCubes* Cubes = GetMarchingCubesGenerator();
	if (Cubes)
	{
		FIntVector ChunkCoords = ChunkCoordinates;
		Cubes->ClearSectionAndRebuildMesh(GetSectionIndex(), ChunkCoords);
	}
	bIsDirty = true;
}


// =============================================================================
// MESH GENERATION — MARCHING CUBES (ASYNC)
// =============================================================================

void UVoxelChunk::GenerateMesh()
{
	if (!SparseVoxelGrid)
	{
		if (DiggerDebug::Voxels())
			UE_LOG(LogTemp, Error, TEXT("SparseVoxelGrid is null!"));
		return;
	}

	// --- Island Detection (gated) ---
	// DetectIslands is a full BFS over every authored voxel — O(N) per remesh.
	// Only run it when the island debug flag is active or the islands feature is
	// enabled; never on every interactive stroke.
	if (DiggerDebug::Islands() && SparseVoxelGrid)
	{
		TArray<FIslandData> Islands = SparseVoxelGrid->DetectIslands(0.0f);
		if (Islands.Num() > 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("Island detection: %d islands found!"), Islands.Num());
			for (int32 i = 0; i < Islands.Num(); ++i)
				UE_LOG(LogTemp, Warning, TEXT("  Island %d: %d voxels"), i, Islands[i].VoxelCount);
		}
	}

	if (!MarchingCubesGenerator)
	{
		if (DiggerDebug::Mesh())
			UE_LOG(LogTemp, Error, TEXT("MarchingCubesGenerator is nullptr"));
		return;
	}

	// Consume and reset the pending AABB bounds.
	FBox BoundsSnapshot = DirtyWorldBounds;
	DirtyWorldBounds = FBox(ForceInit);

    const FVector Origin          = FVoxelConversion::ChunkToWorld(ChunkCoordinates);
    const float   VoxelSizeSnapshot = VoxelSize;
    const int32   N               = ChunkSize * Subdivisions;
    const int32   Pad             = 2;

    // ------------------------------------------------------------------
    // Height map — landscape doesn't move at runtime so we cache it once
    // per chunk.  CaptureHeightMap fires (N+1)² landscape queries on the
    // game thread; skipping the rebuild on every stroke is the single
    // biggest perf win available here.
    // ------------------------------------------------------------------
    if (!MarchingCubesGenerator->IsHeightCacheValid(Origin, VoxelSizeSnapshot))
        MarchingCubesGenerator->CacheHeightMap(Origin, VoxelSizeSnapshot, N);

    TArray<float> LocalHeights = MarchingCubesGenerator->GetCachedHeightValues();

    // 1. Snapshot own data — use const ref copy constructor (avoids manual per-element iteration)
    TMap<FIntVector, FVoxelData> CombinedData(SparseVoxelGrid->GetVoxelDataRef());

    // ------------------------------------------------------------------
    // EARLY-OUT: skip MC when there is nothing authored here.
    // Gated on bBakeFullChunkVolume — when that flag is true the caller
    // wants a full solid-volume mesh (e.g. BakeFullTerrain / BakeTargetChunk)
    // so we bypass the check entirely.
    // ------------------------------------------------------------------
    const bool bForceFullVolume = DiggerManager && DiggerManager->bBakeFullChunkVolume;
    if (!bForceFullVolume)
    {
        bool bHasAnyData = (CombinedData.Num() > 0);

        if (!bHasAnyData && DiggerManager)
        {
            for (int32 x = -1; x <= 1 && !bHasAnyData; x++)
            for (int32 y = -1; y <= 1 && !bHasAnyData; y++)
            for (int32 z = -1; z <= 1 && !bHasAnyData; z++)
            {
                if (x == 0 && y == 0 && z == 0) continue;
                FIntVector NeighborCoords = ChunkCoordinates + FIntVector(x, y, z);
                if (UVoxelChunk** NeighborPtr = DiggerManager->ChunkMap.Find(NeighborCoords))
                {
                    if (UVoxelChunk* Neighbor = *NeighborPtr)
                    {
                        if (USparseVoxelGrid* NeighborGrid = Neighbor->GetSparseVoxelGrid())
                        {
                            for (const auto& Pair : NeighborGrid->VoxelData)
                            {
                                const FIntVector& Loc = Pair.Key;
                                bool bMatchX = (x == 0) || (x ==  1 && Loc.X < Pad) || (x == -1 && Loc.X >= N - Pad);
                                bool bMatchY = (y == 0) || (y ==  1 && Loc.Y < Pad) || (y == -1 && Loc.Y >= N - Pad);
                                bool bMatchZ = (z == 0) || (z ==  1 && Loc.Z < Pad) || (z == -1 && Loc.Z >= N - Pad);
                                if (bMatchX && bMatchY && bMatchZ)
                                {
                                    bHasAnyData = true;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }

        if (!bHasAnyData)
        {
            // Nothing to render — clear any stale mesh and bail out.
            AsyncTask(ENamedThreads::GameThread, [this]()
            {
                if (IsValid(this))
                    UpdateMeshFromData({}, {}, {});
            });
            return;
        }
    } // end !bForceFullVolume early-out

    // 2. Merge 26-neighbour boundary data (always — needed for seam correctness)
    if (DiggerManager)
    {
        for (int32 x = -1; x <= 1; x++)
        for (int32 y = -1; y <= 1; y++)
        for (int32 z = -1; z <= 1; z++)
        {
            if (x == 0 && y == 0 && z == 0) continue;

            FIntVector NeighborCoords = ChunkCoordinates + FIntVector(x, y, z);
            if (UVoxelChunk** NeighborPtr = DiggerManager->ChunkMap.Find(NeighborCoords))
            {
                if (UVoxelChunk* Neighbor = *NeighborPtr)
                {
                    if (USparseVoxelGrid* NeighborGrid = Neighbor->GetSparseVoxelGrid())
                    {
                        FIntVector CoordShift(x * N, y * N, z * N);
                        for (const auto& Pair : NeighborGrid->VoxelData)
                        {
                            const FIntVector& Loc = Pair.Key;
                            bool bMatchX = (x == 0) || (x ==  1 && Loc.X < Pad) || (x == -1 && Loc.X >= N - Pad);
                            bool bMatchY = (y == 0) || (y ==  1 && Loc.Y < Pad) || (y == -1 && Loc.Y >= N - Pad);
                            bool bMatchZ = (z == 0) || (z ==  1 && Loc.Z < Pad) || (z == -1 && Loc.Z >= N - Pad);
                            if (bMatchX && bMatchY && bMatchZ)
                                CombinedData.Add(Loc + CoordShift, Pair.Value);
                        }
                    }
                }
            }
        }
    }

    // 3. Compute voxel AABB for the MC loop restriction.
    //
    // Rules:
    //   A. Chunk has OWN authored voxels → tight AABB around them, expanded
    //      downward to the chunk floor so solid infill below air cells is always
    //      included (prevents sky-window gaps below dug tunnels).
    //
    //   B. Chunk has NO own voxels but passes the neighbour-data check (seam
    //      helper) → use the neighbour boundary AABB only, so we generate just
    //      enough seam-correction geometry without rendering the full underground
    //      block visible in the viewport.
    //
    //   C. bBakeFullChunkVolume=true → sentinel (full chunk), used for bake.
    FBox VoxelAABB(ForceInit); // ForceInit = invalid = full chunk sentinel

    if (!bForceFullVolume && SparseVoxelGrid)
    {
        const bool bHasOwnVoxels = (SparseVoxelGrid->VoxelData.Num() > 0);

        if (bHasOwnVoxels)
        {
            // Rule A: build AABB from AIR voxels only (SDF > 0).
            //
            // Why only air: solid voxels (SDF < 0) accumulate across many
            // strokes spanning large Z ranges and cause the AABB to grow
            // progressively deeper with each dig, producing the tall
            // underground boxes and increasing hitch time per stroke.
            // Air voxels define the actual cavity shape — solid voxels
            // outside the dig region are handled by the implicit landscape
            // baseline and do not need explicit MC work.
            for (const auto& Pair : SparseVoxelGrid->VoxelData)
            {
                if (Pair.Value.SDFValue > 0.f) // air only
                {
                    const FVector WorldPos = Origin + FVector(Pair.Key) * VoxelSizeSnapshot;
                    VoxelAABB += WorldPos;
                }
            }

            // If no air voxels, fall back to all voxels (e.g. pure add-mode
            // strokes where only solid voxels are written).
            if (!VoxelAABB.IsValid)
            {
                for (const auto& Pair : SparseVoxelGrid->VoxelData)
                {
                    const FVector WorldPos = Origin + FVector(Pair.Key) * VoxelSizeSnapshot;
                    VoxelAABB += WorldPos;
                }
            }

            if (VoxelAABB.IsValid)
            {
                // XY: expand by Pad voxels for gradient stencil.
                const float XYExpand = VoxelSizeSnapshot * Pad;
                VoxelAABB.Min.X -= XYExpand;
                VoxelAABB.Min.Y -= XYExpand;
                VoxelAABB.Max.X += XYExpand;
                VoxelAABB.Max.Y += XYExpand;

                // Z: one voxel each way — enough to capture the surface
                // transition without generating deep underground slabs.
                VoxelAABB.Max.Z += VoxelSizeSnapshot;
                VoxelAABB.Min.Z -= VoxelSizeSnapshot;
            }
        }
        else
        {
            // Rule B: no own voxels — this chunk is only here because a
            // neighbour's boundary slab touches it.  Restrict MC to a thin
            // slab at the chunk boundary so we don't render the full
            // underground block.  The slab is (Pad+1) voxels wide on each
            // side that touches a neighbour with data.
            // Seam slab: exactly Pad voxels — minimum for stencil correctness.
            const float SlabWidth = VoxelSizeSnapshot * Pad;
            const float ChunkWorldSize = N * VoxelSizeSnapshot;

            // Scan neighbours to find which faces are active.
            if (DiggerManager)
            {
                for (int32 x = -1; x <= 1; x++)
                for (int32 y = -1; y <= 1; y++)
                for (int32 z = -1; z <= 1; z++)
                {
                    if (x == 0 && y == 0 && z == 0) continue;
                    FIntVector NC = ChunkCoordinates + FIntVector(x, y, z);
                    UVoxelChunk** NPtr = DiggerManager->ChunkMap.Find(NC);
                    if (!NPtr || !*NPtr) continue;
                    USparseVoxelGrid* NGrid = (*NPtr)->GetSparseVoxelGrid();
                    if (!NGrid || NGrid->VoxelData.Num() == 0) continue;

                    // Neighbour has data — include the face slab on our side.
                    FBox FaceSlab(ForceInit);
                    FaceSlab.Min = Origin - FVector(SlabWidth);
                    FaceSlab.Max = Origin + FVector(ChunkWorldSize + SlabWidth);

                    // Restrict to just the face touching this neighbour.
                    if (x == -1) FaceSlab.Max.X = Origin.X + SlabWidth;
                    if (x ==  1) FaceSlab.Min.X = Origin.X + ChunkWorldSize - SlabWidth;
                    if (y == -1) FaceSlab.Max.Y = Origin.Y + SlabWidth;
                    if (y ==  1) FaceSlab.Min.Y = Origin.Y + ChunkWorldSize - SlabWidth;
                    if (z == -1) FaceSlab.Max.Z = Origin.Z + SlabWidth;
                    if (z ==  1) FaceSlab.Min.Z = Origin.Z + ChunkWorldSize - SlabWidth;

                    VoxelAABB = VoxelAABB.IsValid ? (VoxelAABB + FaceSlab) : FaceSlab;
                }
            }
            // If no active neighbour face found, VoxelAABB stays invalid
            // which means the early-out above should have caught this case.
        }
    }

    // 4. Launch async background task.
    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask,
        [this, Generator = MarchingCubesGenerator, CombinedData, Origin,
         VoxelSizeSnapshot, LocalHeights, VoxelAABB]()
    {
        TArray<FVector> Verts;
        TArray<int32>   Tris;
        TArray<FVector> Normals;

        Generator->GenerateMeshFromGrid(
            CombinedData,
            Origin,
            VoxelSizeSnapshot,
            LocalHeights,
            VoxelAABB,   // valid = restricted loop, invalid = full chunk
            Verts, Tris, Normals
        );

        AsyncTask(ENamedThreads::GameThread, [this, Verts, Tris, Normals]()
        {
            if (IsValid(this))
            {
                this->UpdateMeshFromData(Verts, Tris, Normals);
                this->OnMarchingMeshComplete();
            }
        });
    });

	if (DiggerManager)
		DiggerManager->BeginMeshUpdateBatch();
}


// =============================================================================
// MESH GENERATION — MARCHING CUBES (SYNC)
// =============================================================================

void UVoxelChunk::GenerateMeshSyncronous()
{
    if (!IsInGameThread())
    {
        if (DiggerDebug::Mesh())
            UE_LOG(LogTemp, Error, TEXT("GenerateMeshSyncronous called from non-game thread!"));
        return;
    }

    if (!SparseVoxelGrid)
    {
        if (DiggerDebug::Mesh() || DiggerDebug::Error() || DiggerDebug::Voxels())
            UE_LOG(LogTemp, Error, TEXT("SparseVoxelGrid is null!"));
        return;
    }

    if (!MarchingCubesGenerator)
    {
        if (DiggerDebug::Mesh())
            UE_LOG(LogTemp, Error, TEXT("MarchingCubesGenerator is nullptr in GenerateMeshSyncronous"));
        return;
    }

    if (DiggerDebug::Mesh())
        UE_LOG(LogTemp, Warning, TEXT("GenerateMeshSyncronous - Starting mesh generation"));

    // --- Island Detection (gated) — same guard as async path ---
    if (DiggerDebug::Islands() && SparseVoxelGrid)
    {
        TArray<FIslandData> Islands = SparseVoxelGrid->DetectIslands(0.0f);
        if (Islands.Num() > 0)
        {
            UE_LOG(LogTemp, Warning, TEXT("Island detection: %d islands found!"), Islands.Num());
            for (int32 i = 0; i < Islands.Num(); ++i)
                UE_LOG(LogTemp, Warning, TEXT("  Island %d: %d voxels"), i, Islands[i].VoxelCount);
        }
    }

    // Consume and reset pending AABB bounds — forward to the mesher
    FBox BoundsSnapshot = DirtyWorldBounds;
    DirtyWorldBounds = FBox(ForceInit);

    if (DiggerDebug::Mesh())
        UE_LOG(LogTemp, Warning, TEXT("Starting marching cubes generation (sync)"));

    MarchingCubesGenerator->GenerateMeshSyncronous(this, BoundsSnapshot);
}


// =============================================================================
// MESH GENERATION — DUAL CONTOURING (ASYNC)
// Structural mirror of GenerateMesh() — same neighbour merge, same async
// pattern — but calls FVoxelDualContourer instead of MarchingCubesGenerator.
// Swap the call in UpdateIfDirty() to test DC output.
// =============================================================================

// =============================================================================
// MESH GENERATION — DUAL CONTOURING (ASYNC)
// Replace the existing GenerateMeshDC() in VoxelChunk.cpp with this.
// The only meaningful change from the previous version is that HeightValues
// is now captured on the game thread and forwarded to the mesher, exactly
// as GenerateMesh() does for the MC path.
// =============================================================================

void UVoxelChunk::GenerateMeshDC()
{
    if (!SparseVoxelGrid)
    {
        UE_LOG(LogTemp, Error, TEXT("[DC] SparseVoxelGrid is null!"));
        return;
    }

    if (!MarchingCubesGenerator)
    {
        UE_LOG(LogTemp, Error, TEXT("[DC] MarchingCubesGenerator is null — needed for height cache."));
        return;
    }

    const FVector Origin        = FVoxelConversion::ChunkToWorld(ChunkCoordinates);
    const float   VoxelSizeSnap = VoxelSize;
    const int32   N             = ChunkSize * Subdivisions;
    const int32   Pad           = 2;

    // ── Height map (game thread — same as GenerateMesh) ───────────────────────
    // This is the critical fix: DC needs the landscape baseline to correctly
    // determine whether unset voxels are solid (below terrain) or air (above).
    if (!MarchingCubesGenerator->IsHeightCacheValid(Origin, VoxelSizeSnap))
        MarchingCubesGenerator->CacheHeightMap(Origin, VoxelSizeSnap, N);

    TArray<float> LocalHeights = MarchingCubesGenerator->GetCachedHeightValues();

    // ── 1. Snapshot own data (const ref copy — avoids manual per-element iteration)
    TMap<FIntVector, FVoxelData> CombinedData(SparseVoxelGrid->GetVoxelDataRef());

    // ── 2. Merge 26-neighbour boundary slabs (Pad layers, same as GenerateMesh)
    if (DiggerManager)
    {
        for (int32 x = -1; x <= 1; x++)
        for (int32 y = -1; y <= 1; y++)
        for (int32 z = -1; z <= 1; z++)
        {
            if (x == 0 && y == 0 && z == 0) continue;

            const FIntVector NeighborCoords = ChunkCoordinates + FIntVector(x, y, z);
            UVoxelChunk** NeighborPtr = DiggerManager->ChunkMap.Find(NeighborCoords);
            if (!NeighborPtr || !*NeighborPtr) continue;

            USparseVoxelGrid* NeighborGrid = (*NeighborPtr)->GetSparseVoxelGrid();
            if (!NeighborGrid) continue;

            const FIntVector CoordShift(x * N, y * N, z * N);
            for (const auto& Pair : NeighborGrid->VoxelData)
            {
                const FIntVector& Loc = Pair.Key;
                const bool bX = (x == 0) || (x ==  1 && Loc.X < Pad) || (x == -1 && Loc.X >= N - Pad);
                const bool bY = (y == 0) || (y ==  1 && Loc.Y < Pad) || (y == -1 && Loc.Y >= N - Pad);
                const bool bZ = (z == 0) || (z ==  1 && Loc.Z < Pad) || (z == -1 && Loc.Z >= N - Pad);
                if (bX && bY && bZ)
                    CombinedData.Add(Loc + CoordShift, Pair.Value);
            }
        }
    }

    // ── 3. Background thread ──────────────────────────────────────────────────
    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask,
        [this,
         CombinedData = MoveTemp(CombinedData),
         LocalHeights = MoveTemp(LocalHeights),
         Origin, VoxelSizeSnap, N]()
    {
        TArray<FVector> Verts;
        TArray<int32>   Tris;
        TArray<FVector> Normals;

        FVoxelDualContourer::GenerateMesh(
            CombinedData,
            Origin,
            VoxelSizeSnap,
            N,
            LocalHeights,   // ← landscape baseline: the fix that makes it work
            Verts, Tris, Normals
        );

        // ── 4. Game thread upload — identical to MC path ──────────────────────
        AsyncTask(ENamedThreads::GameThread, [this, Verts, Tris, Normals]()
        {
            if (IsValid(this))
            {
                this->UpdateMeshFromData(Verts, Tris, Normals);
                this->OnMarchingMeshComplete();
            }
        });
    });

    if (DiggerManager)
        DiggerManager->BeginMeshUpdateBatch();
}


// =============================================================================
// MESH READY CALLBACK
// =============================================================================

void UVoxelChunk::OnMarchingMeshComplete() const
{
	if (DiggerDebug::Mesh() || DiggerDebug::Holes())
		UE_LOG(LogTemp, Log, TEXT("OnMarchingMeshComplete: Chunk %s fully committed."),
			*ChunkCoordinates.ToString());
}

void UVoxelChunk::OnMeshReady(FIntVector Coord, int32 SectionIdx)
{
	if (Coord != ChunkCoordinates)
		return;

	if (!HoleShapeLibrary && DiggerManager)
		HoleShapeLibrary = DiggerManager->GetHoleShapeLibrary();

	if (!HoleShapeLibrary)
	{
		UE_LOG(LogTemp, Error, TEXT("OnMeshReady FAILED: HoleShapeLibrary is NULL for chunk %s"),
			*ChunkCoordinates.ToString());
		return;
	}

	for (const TWeakObjectPtr<ADynamicHole>& HolePtr : SpawnedHoleInstances)
	{
		ADynamicHole* Hole = HolePtr.Get();
		if (!Hole) continue;

		UStaticMeshComponent* MeshComp = Hole->GetHoleMeshComponent();
		if (!MeshComp) continue;

		Hole->PrepareShapeData();

		EHoleShapeType ShapeType = Hole->HoleShape.ShapeType;
		UStaticMesh* HoleMesh = HoleShapeLibrary->GetMeshForShape(ShapeType);
		if (!HoleMesh) continue;

		// Only call SetStaticMesh/SetMaterial when the mesh actually changes.
		// These calls invalidate render state; calling them every remesh when
		// nothing changed was the biggest per-hole cost in OnMeshReady.
		if (MeshComp->GetStaticMesh() != HoleMesh)
		{
			MeshComp->SetStaticMesh(HoleMesh);

			if (Hole->WriterMaterial)
			{
				const int32 SlotCount = HoleMesh->GetStaticMaterials().Num();
				for (int32 i = 0; i < SlotCount; i++)
					MeshComp->SetMaterial(i, Hole->WriterMaterial);
			}
		}
	}

	if (DiggerManager)
		DiggerManager->NotifyChunkMeshComplete(ChunkCoordinates);
}


// =============================================================================
// VOXEL MODIFICATION REPORT
// =============================================================================

void UVoxelChunk::ReportVoxelModification(const FVoxelModificationReport& Report)
{
	OnVoxelsModified.Broadcast(Report);

	if (ADiggerManager* Mgr = GetDiggerManager())
		Mgr->OnVoxelsModified.Broadcast(Report);
}


// =============================================================================
// HOLE MANAGEMENT
// =============================================================================

static void SetHoleListedInOutliner(AActor* Actor, bool bListed)
{
#if WITH_EDITOR
	if (!Actor) return;
	static FBoolProperty* ListedProp = CastField<FBoolProperty>(
		AActor::StaticClass()->FindPropertyByName(FName("bListedInSceneOutliner"))
	);
	if (ListedProp && ListedProp->GetPropertyValue_InContainer(Actor) != bListed)
	{
		Actor->Modify();
		ListedProp->SetPropertyValue_InContainer(Actor, bListed);
	}
#endif
}


void UVoxelChunk::SpawnHoleFromData(const FSpawnedHoleData& HoleData, int32 ForceUID)
{
    if (!DiggerManager)
    {
        if (DiggerDebug::Manager() || DiggerDebug::Holes() || DiggerDebug::Error())
            UE_LOG(LogTemp, Error, TEXT("SpawnHoleFromData: DiggerManager is null"));
        return;
    }

    if (!HoleShapeLibrary)
    {
        DiggerManager->EnsureHoleShapeLibrary();
        HoleShapeLibrary = DiggerManager->GetHoleShapeLibrary();
        if (!HoleShapeLibrary)
        {
            if (DiggerDebug::Holes() || DiggerDebug::Error())
                UE_LOG(LogTemp, Error, TEXT("SpawnHoleFromData: HoleShapeLibrary is not set"));
            return;
        }
    }

    TSubclassOf<AActor> HoleClass = DiggerManager->GetDynamicHoleClass();
    if (!HoleClass)
    {
        const UDiggerSettings* Settings = UDiggerSettings::Get();
        if (Settings)
            HoleClass = Settings->DefaultHoleActorClass.LoadSynchronous();
    }
    if (!HoleClass)
    {
        if (DiggerDebug::Holes() || DiggerDebug::Error())
            UE_LOG(LogTemp, Error, TEXT("SpawnHoleFromData: Cannot spawn hole — no class specified"));
        return;
    }

    if (!GetWorld())
    {
        if (DiggerDebug::Context() || DiggerDebug::Holes() || DiggerDebug::Error())
            UE_LOG(LogTemp, Error, TEXT("SpawnHoleFromData: GetWorld() returned null"));
        return;
    }

    // ── Pool path ───────────────────────────────────────────────────────────
    // Acquire a pre-allocated actor from the pool instead of calling SpawnActor.
    // SpawnActor triggers Blueprint construction, Outliner registration, and
    // physics setup — all game-thread stalls.  Pool actors are already live;
    // we just reposition them.
    ADynamicHole* DynamicHole = DiggerManager
        ? DiggerManager->AcquireHoleFromPool(HoleClass)
        : nullptr;

    if (!DynamicHole)
    {
        // Pool exhausted and growth failed — fall back to a direct spawn so we
        // never silently drop a hole.
        FActorSpawnParameters FallbackParams;
        FallbackParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        FallbackParams.ObjectFlags = RF_Transient;
        DynamicHole = GetWorld()->SpawnActor<ADynamicHole>(
            HoleClass, HoleData.Location, HoleData.Rotation, FallbackParams);

        if (!DynamicHole)
        {
            if (DiggerDebug::Holes() || DiggerDebug::Error())
                UE_LOG(LogTemp, Error, TEXT("SpawnHoleFromData: Pool and fallback spawn both failed"));
            return;
        }
    }

    // Pool actors are hidden by default — re-expose via outliner if needed.
    AActor* SpawnedHole = DynamicHole;
#if WITH_EDITOR
	if (GIsEditor)
	{
		bool bShowHoles = false;
		GConfig->GetBool(
			TEXT("/Script/DiggerEditor.DiggerEditorSettings"),
			TEXT("bShowDynamicHolesFolder"),
			bShowHoles,
			GEditorPerProjectIni
		);
		if (bShowHoles)
		{
			SpawnedHole->SetFolderPath(FName("Digger/DynamicHoles"));
			SetHoleListedInOutliner(SpawnedHole, true);
		}
		else
		{
			SpawnedHole->SetFolderPath(NAME_None);
			SetHoleListedInOutliner(SpawnedHole, false);
		}
	}
#endif

    DynamicHole->SetDiggerManager(DiggerManager);
    DynamicHole->HoleShape     = HoleData.Shape;
    DynamicHole->HoleShapeType = HoleData.Shape.ShapeType;
    DynamicHole->SetActorLocation(HoleData.Location);
    DynamicHole->SetActorRotation(HoleData.Rotation);
    DynamicHole->SetActorScale3D(HoleData.Scale);
    DynamicHole->SetOwningChunk(this);

    // PrepareShapeData() guards on DiggerManager->HoleShapeLibrary being non-null.
    // EnsureHoleShapeLibrary() was already called above for the chunk's own
    // HoleShapeLibrary, but PrepareShapeData reads it via the manager — so we
    // must guarantee the manager's copy is populated too before calling it.
    // Without this the guard silently exits, BrushShapeInstance stays null,
    // ContainsPoint falls back to a fixed 100-unit sphere, IsInsideHole returns
    // false for landscape hits inside the hole, and the brush snaps to surface.
    DiggerManager->EnsureHoleShapeLibrary();

    DynamicHole->PrepareShapeData();

    // Assign and register UID
    {
        int32 AssignedUID = (ForceUID != INDEX_NONE) ? ForceUID
                          : (HoleData.HoleUID != INDEX_NONE) ? HoleData.HoleUID
                          : (DiggerManager ? DiggerManager->AllocateActorUID() : INDEX_NONE);
        DynamicHole->HoleUID = AssignedUID;
        if (AssignedUID != INDEX_NONE)
        {
            HolesByUID.Add(AssignedUID, DynamicHole);
            if (DiggerManager)
                DiggerManager->RegisterActorUID(AssignedUID, DynamicHole);
        }
    }

    // Register in SpawnedHoleInstances so SaveChunkData can serialise this hole
    // and IsInsideHole / OnMeshReady can find it.  Without this the hole exists
    // visually but is invisible to the save system and is lost on editor restart.
    SpawnedHoleInstances.AddUnique(DynamicHole);

    if (DiggerDebug::Verbose() || DiggerDebug::Holes())
        UE_LOG(LogTemp, Warning,
            TEXT("SpawnHoleFromData: Hole registered to chunk %s (chunk ptr=%p), that chunk now has %d holes"),
            *ChunkCoordinates.ToString(), this, SpawnedHoleInstances.Num());

    if (DiggerDebug::Holes())
        UE_LOG(LogTemp, Warning,
            TEXT("SpawnHoleFromData: Spawned hole %s at %s for chunk %s (Shape=%s)"),
            *DynamicHole->GetName(),
            *HoleData.Location.ToString(),
            *ChunkCoordinates.ToString(),
            *UEnum::GetValueAsString(HoleData.Shape.ShapeType));

#if WITH_EDITOR
    if (GIsEditor)
    {
        FString NewLabel = FString::Printf(
            TEXT("HoleBP_%s"),
            *UEnum::GetValueAsString(HoleData.Shape.ShapeType));
        SpawnedHole->SetActorLabel(NewLabel, /*bMarkDirty=*/false);
    }
#endif
}


void UVoxelChunk::SaveHoleData(const FVector& Location, const FRotator& Rotation, const FVector& Scale)
{
	HoleDataArray.Add(FSpawnedHoleData(Location, Rotation, Scale));
}


void UVoxelChunk::CaptureLightForSave(AActor* LightActor)
{
	FSavedLightData Data;
	Data.CaptureFromLightActor(LightActor);
	SavedLights.Add(Data);
}

void UVoxelChunk::ClearSavedLights()
{
	SavedLights.Empty();
}

void UVoxelChunk::RestoreAllHoles()
{
	for (const FSpawnedHoleData& HoleData : HoleDataArray)
		SpawnHoleFromData(HoleData);
}


void UVoxelChunk::DebugDrawChunk()
{
	if (!World) World = DiggerManager->GetWorldFromManager();
	if (!World) return;

	const FVector ChunkCenter = FVector(ChunkCoordinates) * ChunkSize * TerrainGridSize;
	const FVector ChunkExtent = FVector(ChunkSize * TerrainGridSize / 2.0f);
	FAST_DEBUG_BOX(ChunkCenter, ChunkExtent, FLinearColor::Red);
}

void UVoxelChunk::DebugPrintVoxelData() const
{
	if (!DiggerDebug::Chunks() || !DiggerDebug::Voxels()) return;
	if (!SparseVoxelGrid)
	{
		if (DiggerDebug::Voxels() || DiggerDebug::Error())
			UE_LOG(LogTemp, Error, TEXT("SparseVoxelGrid is null in DebugPrintVoxelData"));
		return;
	}
	if (DiggerDebug::Chunks() || DiggerDebug::Voxels() || DiggerDebug::Error())
		UE_LOG(LogTemp, Log, TEXT("Voxel Data for Chunk at %s:"), *GetChunkCoords().ToString());
	for (const auto& Pair : SparseVoxelGrid->VoxelData)
	{
		if (DiggerDebug::Voxels())
			UE_LOG(LogTemp, Log, TEXT("Voxel at (%d,%d,%d): Value = %f"),
				Pair.Key.X, Pair.Key.Y, Pair.Key.Z, Pair.Value.SDFValue);
	}
}


// =============================================================================
// SAVE / LOAD
// =============================================================================

bool UVoxelChunk::SaveChunkData(const FString& FilePath)
{
	// Build HoleDataArray from the live actors in SpawnedHoleInstances, but
	// skip any actor that is hidden (pooled) or sitting at the sentinel position.
	// This prevents pool actors with stale/zeroed transforms from corrupting the
	// save file.  We also skip actors whose UID is INDEX_NONE (returned to pool).
	SpawnedHoleInstances.RemoveAll([](const TWeakObjectPtr<ADynamicHole>& HolePtr)
	{
		return !HolePtr.IsValid();
	});

	HoleDataArray.Empty();
	HoleDataArray.Reserve(SpawnedHoleInstances.Num());

	for (const TWeakObjectPtr<ADynamicHole>& HolePtr : SpawnedHoleInstances)
	{
		ADynamicHole* Actor = HolePtr.Get();
		if (!Actor) continue;

		// Skip pool actors: hidden flag and sentinel Z position both indicate
		// this slot was returned to the pool and has no valid save data.
		if (Actor->IsHidden()) continue;
		if (Actor->GetActorLocation().Z < -999999.f) continue;

		FSpawnedHoleData NewData;
		NewData.Location        = Actor->GetActorLocation();
		NewData.Rotation        = Actor->GetActorRotation();
		NewData.Scale           = Actor->GetActorScale3D();
		NewData.Shape.ShapeType = Actor->HoleShapeType;
		NewData.HoleUID         = Actor->HoleUID;
		HoleDataArray.Add(NewData);
	}

	FBufferArchive ToBinary;
	ToBinary.SetIsPersistent(true);
	ToBinary.SetEngineVer(FEngineVersion::Current());
	ToBinary.SetCustomVersions(FCurrentCustomVersions::GetAll());

	if (!SparseVoxelGrid || !SparseVoxelGrid->SerializeToArchive(ToBinary))
		return false;

	// --- Write Holes (versioned) ---
	// Layout: [MagicSentinel int32] [FileVersion int32] [HoleCount int32]
	//         then per hole: Location, Rotation, Scale, ShapeByte, HoleUID
	const int32 MagicSentinel = -0x444947;
	const int32 FileVersion   = FSpawnedHoleDataCustomVersion::FileVersion_Current;
	int32 HoleCount = HoleDataArray.Num();
	ToBinary << const_cast<int32&>(MagicSentinel);
	ToBinary << const_cast<int32&>(FileVersion);
	ToBinary << HoleCount;
	for (FSpawnedHoleData& Hole : HoleDataArray)
	{
		ToBinary << Hole.Location;
		ToBinary << Hole.Rotation;
		ToBinary << Hole.Scale;
		uint8 ShapeByte = (uint8)Hole.Shape.ShapeType;
		ToBinary << ShapeByte;
		ToBinary << Hole.HoleUID;  // V2: stable identity for undo/redo
	}

	int32 LightCount = SavedLights.Num();
	ToBinary << LightCount;
	for (FSavedLightData& Light : SavedLights)
		ToBinary << Light;

	if (FFileHelper::SaveArrayToFile(ToBinary, *FilePath))
	{
		SavedLights.Empty();
		ToBinary.FlushCache();
		ToBinary.Empty();
		return true;
	}
	return false;
}

bool UVoxelChunk::LoadChunkData(const FString& FilePath)
{
	return LoadChunkData(FilePath, false);
}

bool UVoxelChunk::LoadChunkData(const FString& FilePath, bool bOverwrite)
{
    if (!DiggerManager) DiggerManager = ADiggerManager::FindDiggerManager(World);
    if (DiggerManager)
    {
        DiggerManager->EnsureHoleShapeLibrary();
        HoleShapeLibrary = DiggerManager->GetHoleShapeLibrary();
        DiggerManager->EnsureDefaultHoleBP();
        HoleBP = DiggerManager->DynamicHoleClass;
    }

    if (!FPaths::FileExists(FilePath)) return false;

    TArray<uint8> BinaryArray;
    if (!FFileHelper::LoadFileToArray(BinaryArray, *FilePath)) return false;

    FMemoryReader FromBinary(BinaryArray, true);
    FromBinary.Seek(0);
    FromBinary.SetIsPersistent(true);
    FromBinary.SetEngineVer(FEngineVersion::Current());
    FromBinary.SetCustomVersions(FCurrentCustomVersions::GetAll());

    // --- 1. Load Voxels ---
    USparseVoxelGrid* TempGrid = NewObject<USparseVoxelGrid>();
    if (!TempGrid->SerializeFromArchive(FromBinary)) return false;

    if (bOverwrite && SparseVoxelGrid)
    {
        SparseVoxelGrid->VoxelData = TempGrid->VoxelData;
        ClearSpawnedHoles();
        HoleDataArray.Empty();
    }
    else if (SparseVoxelGrid)
    {
        for (const auto& Pair : TempGrid->VoxelData)
            SparseVoxelGrid->VoxelData.Add(Pair.Key, Pair.Value);
    }

    if (FromBinary.AtEnd()) { MarkDirty(); return true; }

    // --- 2. Load Holes (three-tier version handling) ---
    //
    // Tier 0 — absolute legacy (no magic header):
    //   The first int32 in the hole section IS HoleCount.  No shape data.
    //
    // Tier 1 — magic header, FileVersion 0 (AddedShapeField):
    //   [MagicSentinel] [FileVersion=0] [HoleCount] then per hole:
    //   Location, Rotation, Scale, ShapeByte.
    //
    // Tier 2 — magic header, FileVersion 1 (AddedHoleUID):
    //   Same as Tier 1 but each hole also stores an int32 HoleUID.
    //   Old files load with HoleUID = INDEX_NONE; SpawnHoleFromData
    //   allocates a fresh UID at runtime so actor tracking is unaffected.

    int32 CheckValue = 0;
    FromBinary << CheckValue;

    int32 HoleCount   = 0;
    int32 FileVersion = -1;          // -1 = legacy (no header)
    const int32 MagicHeader = -0x444947;

    if (CheckValue == MagicHeader)
    {
        FromBinary << FileVersion;   // 0 = AddedShapeField, 1 = AddedHoleUID
        FromBinary << HoleCount;
    }
    else
    {
        // Absolute legacy: CheckValue IS HoleCount, no shape, no UID.
        FileVersion = -1;
        HoleCount   = CheckValue;
        if (DiggerDebug::IO())
            UE_LOG(LogTemp, Warning, TEXT("LoadChunkData: Legacy file (no magic header). Holes default to Sphere, no UIDs."));
    }

    const bool bHasShape  = (FileVersion >= FSpawnedHoleDataCustomVersion::FileVersion_AddedShapeField);
    const bool bHasHoleUID = (FileVersion >= FSpawnedHoleDataCustomVersion::FileVersion_AddedHoleUID);

    for (int32 i = 0; i < HoleCount; ++i)
    {
        if (FromBinary.AtEnd()) break;

        FSpawnedHoleData Hole;
        FromBinary << Hole.Location;
        FromBinary << Hole.Rotation;
        FromBinary << Hole.Scale;

        if (bHasShape)
        {
            uint8 ShapeByte = 0;
            FromBinary << ShapeByte;
            Hole.Shape.ShapeType = (EHoleShapeType)ShapeByte;
        }
        else
        {
            Hole.Shape.ShapeType = EHoleShapeType::Sphere;
        }

        if (bHasHoleUID)
        {
            FromBinary << Hole.HoleUID;
        }
        else
        {
            // Pre-V2: leave INDEX_NONE; SpawnHoleFromData will assign a fresh UID.
            Hole.HoleUID = INDEX_NONE;
        }

        if (!bHolesPreLoaded)
        {
            HoleDataArray.Add(Hole);
            SpawnHoleFromData(Hole);   // allocates UID if Hole.HoleUID == INDEX_NONE
        }
        // When bHolesPreLoaded: holes already in HoleDataArray from Phase 1
        // (LoadChunkHolesOnly). Just skip to advance the archive past hole bytes.
    }

    // --- 3. Load Lights ---
    if (FromBinary.AtEnd()) { MarkDirty(); return true; }

    int32 LightCount = 0;
    FromBinary << LightCount;

    UWorld* CurrentWorld = GetWorld() ? GetWorld() : (DiggerManager ? DiggerManager->GetWorld() : nullptr);
    for (int32 i = 0; i < LightCount; ++i)
    {
        if (FromBinary.AtEnd()) break;
        FSavedLightData LightData;
        FromBinary << LightData;
        if (CurrentWorld)
        {
            // Pass DiggerManager so the spawned light gets a UID and enters
            // the undo registry.  Safe to pass nullptr — SpawnLightActor
            // handles it gracefully.
            AActor* NewLight = LightData.SpawnLightActor(CurrentWorld, DiggerManager);
            if (DiggerManager && NewLight) DiggerManager->SpawnedLights.Add(NewLight);
        }
    }

    MarkDirty();
    return true;
}

bool UVoxelChunk::LoadChunkHolesOnly(const FString& FilePath)
{
    if (!DiggerManager) DiggerManager = ADiggerManager::FindDiggerManager(World);
    if (DiggerManager)
    {
        DiggerManager->EnsureHoleShapeLibrary();
        HoleShapeLibrary = DiggerManager->GetHoleShapeLibrary();
        DiggerManager->EnsureDefaultHoleBP();
        HoleBP = DiggerManager->DynamicHoleClass;
    }

    if (!FPaths::FileExists(FilePath)) return false;

    TArray<uint8> BinaryArray;
    if (!FFileHelper::LoadFileToArray(BinaryArray, *FilePath)) return false;

    FMemoryReader FromBinary(BinaryArray, true);
    FromBinary.Seek(0);
    FromBinary.SetIsPersistent(true);
    FromBinary.SetEngineVer(FEngineVersion::Current());
    FromBinary.SetCustomVersions(FCurrentCustomVersions::GetAll());

    // Skip past voxel data — deserialize into a temporary grid we discard.
    USparseVoxelGrid* TempGrid = NewObject<USparseVoxelGrid>();
    if (!TempGrid->SerializeFromArchive(FromBinary)) return false;

    if (FromBinary.AtEnd()) return true; // no holes in this file

    // --- Read holes (same three-tier parsing as LoadChunkData) ---
    int32 CheckValue = 0;
    FromBinary << CheckValue;

    int32 HoleCount   = 0;
    int32 FileVersion = -1;
    const int32 MagicHeader = -0x444947;

    if (CheckValue == MagicHeader)
    {
        FromBinary << FileVersion;
        FromBinary << HoleCount;
    }
    else
    {
        FileVersion = -1;
        HoleCount   = CheckValue;
    }

    if (HoleCount == 0) return true;

    const bool bHasShape   = (FileVersion >= FSpawnedHoleDataCustomVersion::FileVersion_AddedShapeField);
    const bool bHasHoleUID = (FileVersion >= FSpawnedHoleDataCustomVersion::FileVersion_AddedHoleUID);

    for (int32 i = 0; i < HoleCount; ++i)
    {
        if (FromBinary.AtEnd()) break;

        FSpawnedHoleData Hole;
        FromBinary << Hole.Location;
        FromBinary << Hole.Rotation;
        FromBinary << Hole.Scale;

        if (bHasShape)
        {
            uint8 ShapeByte = 0;
            FromBinary << ShapeByte;
            Hole.Shape.ShapeType = (EHoleShapeType)ShapeByte;
        }
        else
        {
            Hole.Shape.ShapeType = EHoleShapeType::Sphere;
        }

        if (bHasHoleUID)
            FromBinary << Hole.HoleUID;
        else
            Hole.HoleUID = INDEX_NONE;

        HoleDataArray.Add(Hole);
        SpawnHoleFromData(Hole);
    }

    bHolesPreLoaded = true;
    return true;
}


// =============================================================================
// HOLE HELPERS
// =============================================================================

void UVoxelChunk::ClearSpawnedHoles()
{
	// Return actors to the pool instead of destroying them.
	// Destroy() is deferred a frame and triggers Outliner / GC bookkeeping;
	// hiding and re-pooling is near-free.
	for (const TWeakObjectPtr<ADynamicHole>& HolePtr : SpawnedHoleInstances)
	{
		ADynamicHole* Hole = HolePtr.Get();
		if (!Hole) continue;

		// Deregister UID before returning so a future acquire gets a clean slate.
		if (Hole->HoleUID != INDEX_NONE && DiggerManager)
			DiggerManager->UnregisterActorUID(Hole->HoleUID);

		if (DiggerManager)
			DiggerManager->ReturnHoleToPool(Hole);
		else
			Hole->Destroy();   // fallback: no manager, just destroy
	}
	SpawnedHoleInstances.Empty();
	HolesByUID.Empty();   // UIDs were deregistered above; clear local map too
}

void UVoxelChunk::SpawnHoleMeshes()
{
	if (!World || !HoleBP) return;

	while (SpawnedHoleInstances.Num() < HoleDataArray.Num())
		SpawnedHoleInstances.Add(nullptr);

	for (int32 i = 0; i < HoleDataArray.Num(); ++i)
	{
		ADynamicHole* HoleActor = SpawnedHoleInstances[i].Get();
		if (!HoleActor) continue;
		SpawnHoleFromData(HoleDataArray[i]);
	}
}

void UVoxelChunk::RegenerateHolesFromData()
{
	SpawnedHoleInstances.RemoveAll([](const TWeakObjectPtr<ADynamicHole>& HolePtr)
	{
		return !HolePtr.IsValid();
	});

	if (!HoleBP)
	{
		DiggerManager->EnsureDefaultHoleBP();
		if (!HoleBP)
		{
			UE_LOG(LogTemp, Error, TEXT("HoleBP is not set in RegenerateHolesFromData"));
			return;
		}
	}

	if (SpawnedHoleInstances.Num() >= HoleDataArray.Num())
		return;

	UE_LOG(LogTemp, Log, TEXT("Regenerating %d holes for Chunk %s"), HoleDataArray.Num(), *ChunkCoordinates.ToString());
	for (const FSpawnedHoleData& Data : HoleDataArray)
		SpawnHoleFromData(Data);
}

void UVoxelChunk::AddHoleToChunk(ADynamicHole* Hole)
{
	if (!Hole)
	{
		if (DiggerDebug::Holes())
			UE_LOG(LogTemp, Error,
				TEXT("AddHoleToChunk: FAILED — Hole pointer is NULL for chunk %s"),
				*ChunkCoordinates.ToString());
		return;
	}

	SpawnedHoleInstances.AddUnique(Hole);

	// Keep UID map in sync
	if (Hole->HoleUID != INDEX_NONE)
		HolesByUID.Add(Hole->HoleUID, Hole);

	if (DiggerDebug::Holes())
		UE_LOG(LogTemp, Warning,
			TEXT("AddHoleToChunk: Registered hole %s to chunk %s (Total: %d)."),
			*Hole->GetName(), *ChunkCoordinates.ToString(), SpawnedHoleInstances.Num());

	MarkDirty();
}

void UVoxelChunk::RemoveHoleFromChunk(ADynamicHole* Hole)
{
	if (!Hole) return;
	SpawnedHoleInstances.Remove(Hole);

	// Remove from UID map
	if (Hole->HoleUID != INDEX_NONE)
		HolesByUID.Remove(Hole->HoleUID);

	if (DiggerDebug::Holes() || DiggerDebug::Chunks())
		UE_LOG(LogTemp, Warning,
			TEXT("RemoveHoleFromChunk: Unregistered hole %s from chunk %s"),
			*Hole->GetName(), *ChunkCoordinates.ToString());
}

int32 UVoxelChunk::GenerateHoleID()
{
	return HoleIDCounter++;
}

bool UVoxelChunk::DestroyHoleByUID(int32 UID)
{
    TWeakObjectPtr<ADynamicHole>* Found = HolesByUID.Find(UID);
    if (!Found) return false;

    ADynamicHole* Hole = Found->Get();
    HolesByUID.Remove(UID);

    if (IsValid(Hole))
    {
        SpawnedHoleInstances.Remove(Hole);

        // Remove matching HoleDataArray entry by location proximity
        // (exact UID match not stored in FSpawnedHoleData yet for old data;
        //  prefer UID field if set)
        const FVector HoleLoc = Hole->GetActorLocation();
        for (int32 i = HoleDataArray.Num() - 1; i >= 0; --i)
        {
            if ((HoleDataArray[i].HoleUID != INDEX_NONE && HoleDataArray[i].HoleUID == UID) ||
                FVector::DistSquared(HoleDataArray[i].Location, HoleLoc) < 1.f)
            {
                HoleDataArray.RemoveAt(i);
                break;
            }
        }

        Hole->Destroy();
        return true;
    }

    return false;
}

void UVoxelChunk::UnregisterHoleUID(int32 UID)
{
    HolesByUID.Remove(UID);
}


// =============================================================================
// SPAWN HELPERS
// =============================================================================

AActor* UVoxelChunk::SpawnTransientActor(UWorld* InWorld, TSubclassOf<AActor> ActorClass, FVector Location, FRotator Rotation, FVector Scale)
{
	if (!InWorld || !ActorClass) return nullptr;

#if WITH_EDITOR
	bool bWasLevelDirty = false;
	ULevel* Level = InWorld->GetCurrentLevel();
	if (Level && GIsEditor)
		bWasLevelDirty = Level->GetPackage()->IsDirty();
#endif

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.bNoFail            = true;
	SpawnParams.ObjectFlags       |= RF_Transient;
	SpawnParams.bDeferConstruction = false;

	AActor* Spawned = InWorld->SpawnActor<AActor>(ActorClass, Location, Rotation, SpawnParams);
	if (Spawned)
	{
		Spawned->SetActorScale3D(Scale);
		Spawned->SetFlags(RF_Transient);
		Spawned->ClearFlags(RF_Transactional | RF_Public);
		Spawned->SetActorHiddenInGame(false);
		Spawned->bIsEditorPreviewActor = true;
		Spawned->SetReplicates(false);
		Spawned->SetCanBeDamaged(false);
		Spawned->SetFlags(RF_DuplicateTransient | RF_NonPIEDuplicateTransient);

#if WITH_EDITOR
		if (GIsEditor)
		{
			Spawned->ClearFlags(RF_Transactional);
			Spawned->Modify(false);
			for (UActorComponent* Component : Spawned->GetComponents().Array())
			{
				if (Component)
				{
					Component->SetFlags(RF_Transient | RF_DuplicateTransient | RF_NonPIEDuplicateTransient);
					Component->ClearFlags(RF_Transactional | RF_Public);
				}
			}
			if (Level && !bWasLevelDirty)
				Level->GetPackage()->SetDirtyFlag(false);
		}
#endif

		Spawned->SetActorLabel(TEXT("Runtime Hole"));
	}
	return Spawned;
}

void UVoxelChunk::SpawnHole(
    TSubclassOf<AActor> HoleBPClass,
    FVector Location, FRotator Rotation, FVector Scale,
    EHoleShapeType ShapeType)
{
    if (!DiggerManager)
    {
        DiggerManager = ADiggerManager::FindDiggerManager(World);
        if (!DiggerManager)
        {
            UE_LOG(LogTemp, Error, TEXT("SpawnHole: No DiggerManager found"));
            return;
        }
    }

    if (!HoleBPClass)
    {
        DiggerManager->EnsureDefaultHoleBP();
        HoleBPClass = DiggerManager->GetDynamicHoleClass();
    }
    if (!HoleBPClass)
    {
        UE_LOG(LogTemp, Error, TEXT("SpawnHole: No valid HoleBPClass after manager ensure"));
        return;
    }
    HoleBP = HoleBPClass;

    UWorld* UseWorld = World ? World : GetWorld();
    if (!UseWorld)
    {
        UE_LOG(LogTemp, Error, TEXT("SpawnHole: Invalid World"));
        return;
    }

#if WITH_EDITOR
    if (GIsEditor) DiggerManager->EnsureHoleShapeLibrary();
#endif

    AActor* SpawnedHole = nullptr;

#if WITH_EDITOR
    const bool bIsEditorPreview = (GIsEditor && !UseWorld->HasBegunPlay());
    if (bIsEditorPreview)
    {
        if (GEditor)
        {
            UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
            if (EditorWorld)
            {
                SpawnedHole = SpawnTransientActor(EditorWorld, HoleBPClass, Location, Rotation, Scale);
                if (SpawnedHole)
                {
                    SpawnedHole->SetActorLabel(FString::Printf(TEXT("HoleBP_%d"), FMath::RandRange(0, 999999)));
                    SpawnedHole->Modify();
                }
            }
        }
    }
    else
#endif
    {
        SpawnedHole = SpawnTransientActor(UseWorld, HoleBPClass, Location, Rotation, Scale);
    }

    if (!SpawnedHole)
    {
        UE_LOG(LogTemp, Error, TEXT("SpawnHole: Failed to spawn HoleBP at %s"), *Location.ToString());
        return;
    }

    FHoleShape Shape;
    Shape.ShapeType = ShapeType;
    FSpawnedHoleData HoleData{ Location, Rotation, Scale };
    HoleData.Shape = Shape;
    HoleDataArray.Add(HoleData);

    if (DiggerDebug::Holes())
        UE_LOG(LogTemp, Log, TEXT("Spawned HoleBP at %s with shape %s"),
            *Location.ToString(), *UEnum::GetValueAsString(ShapeType));
}

bool UVoxelChunk::RemoveNearestHole(FVector Location, float MaxDistance)
{
	int32 NearestIndex = INDEX_NONE;
	float ClosestDistSqr = MaxDistance * MaxDistance;

	for (int32 i = 0; i < SpawnedHoleInstances.Num(); ++i)
	{
		ADynamicHole* HoleActor = SpawnedHoleInstances[i].Get();
		if (!HoleActor) continue;
		float DistSqr = FVector::DistSquared(HoleActor->GetActorLocation(), Location);
		if (DistSqr < ClosestDistSqr)
		{
			ClosestDistSqr = DistSqr;
			NearestIndex   = i;
		}
	}

	if (NearestIndex != INDEX_NONE)
	{
		ADynamicHole* HoleActor = SpawnedHoleInstances[NearestIndex].Get();
		if (IsValid(HoleActor)) HoleActor->Destroy();
		if (HoleDataArray.IsValidIndex(NearestIndex))
			HoleDataArray.RemoveAt(NearestIndex);
		SpawnedHoleInstances.RemoveAt(NearestIndex);
		return true;
	}
	return false;
}

void UVoxelChunk::MergeHoles()
{
    const float MergeOverlapFactor = 0.5f;
    bool bMerged = true;

    while (bMerged)
    {
        bMerged = false;
        for (int32 i = 0; i < SpawnedHoles.Num() && !bMerged; ++i)
        {
            ADynamicHole* A = SpawnedHoles[i];
            if (!IsValid(A)) continue;
            for (int32 j = i + 1; j < SpawnedHoles.Num() && !bMerged; ++j)
            {
                ADynamicHole* B = SpawnedHoles[j];
                if (!IsValid(B)) continue;
                if (A->HoleShape.ShapeType != B->HoleShape.ShapeType) continue;

                const FVector ALoc = A->GetActorLocation();
                const FVector BLoc = B->GetActorLocation();
                const float AR   = A->GetEffectiveRadius();
                const float BR   = B->GetEffectiveRadius();
                const float Dist = FVector::Dist(ALoc, BLoc);

                if (Dist < (AR + BR) * MergeOverlapFactor)
                {
                    FVector NewCenter = (ALoc + BLoc) * 0.5f;
                    float NewRadius   = FMath::Min((Dist * 0.5f) + FMath::Max(AR, BR), (AR + BR) * 0.9f);
                    SpawnMergedHole(NewCenter, NewRadius, A->HoleShape.ShapeType);
                    A->Destroy();
                    B->Destroy();
                    SpawnedHoles.RemoveAt(j);
                    SpawnedHoles.RemoveAt(i);
                    bMerged = true;
                }
            }
        }
    }
}

void UVoxelChunk::SpawnMergedHole(FVector Center, float Radius, EHoleShapeType ShapeType)
{
	if (ShapeType != EHoleShapeType::Sphere)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("SpawnMergedHole: Primitive merge unsupported for shape type %d. Skipping."),
			(int32)ShapeType);
		return;
	}

	if (!GetWorld()) return;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ADynamicHole* NewHole = GetWorld()->SpawnActor<ADynamicHole>(
		ADynamicHole::StaticClass(), Center, FRotator::ZeroRotator, Params);

	if (!IsValid(NewHole)) return;
	NewHole->HoleShape.ShapeType = EHoleShapeType::Sphere;
	float ScaleFromRadius = Radius / GetDefault<ADynamicHole>()->GetBaseRadius();
	NewHole->SetActorScale3D(FVector(ScaleFromRadius));
	SpawnedHoles.Add(NewHole);
	MarkDirty();
}

void UVoxelChunk::DedupHoles()
{
	UE_LOG(LogTemp, Warning, TEXT("=== DedupHoles: %d holes ==="), SpawnedHoleInstances.Num());
	for (int32 i = 0; i < FMath::Min(SpawnedHoleInstances.Num(), 10); ++i)
	{
		ADynamicHole* H = SpawnedHoleInstances[i].Get();
		if (!IsValid(H)) { UE_LOG(LogTemp, Warning, TEXT("  [%d] INVALID"), i); continue; }
		FVector Loc = H->GetActorLocation();
		FVector Scl = H->GetActorScale3D();
		float   Rad = H->GetEffectiveRadius();
		UE_LOG(LogTemp, Warning,
			TEXT("  [%d] Shape=%d  Loc=(%.1f,%.1f,%.1f)  Scale=(%.4f,%.4f,%.4f)  EffRadius=%.2f  CachedRadius=%.2f"),
			i, (int32)H->HoleShape.ShapeType,
			Loc.X, Loc.Y, Loc.Z, Scl.X, Scl.Y, Scl.Z,
			Rad, H->CachedStroke.BrushRadius);
	}
}

void UVoxelChunk::DeclutterHoles()
{
	UE_LOG(LogTemp, Warning, TEXT("=== DeclutterHoles: %d holes ==="), SpawnedHoleInstances.Num());
	if (SpawnedHoleInstances.Num() >= 2)
	{
		ADynamicHole* A = SpawnedHoleInstances.Last(0).Get();
		ADynamicHole* B = SpawnedHoleInstances.Last(1).Get();
		if (IsValid(A) && IsValid(B))
		{
			float AR   = A->GetEffectiveRadius();
			float BR   = B->GetEffectiveRadius();
			float Dist = FVector::Dist(A->GetActorLocation(), B->GetActorLocation());
			UE_LOG(LogTemp, Warning, TEXT("  Sample pair — AR=%.2f  BR=%.2f  Dist=%.2f"), AR, BR, Dist);
			UE_LOG(LogTemp, Warning,
				TEXT("  Containment: Dist+AR*0.85 = %.2f  vs  BR = %.2f  → %s"),
				Dist + AR * 0.85f, BR,
				(Dist + AR * 0.85f <= BR) ? TEXT("WOULD REMOVE A") : TEXT("keeps A"));
		}
	}
}


// =============================================================================
// BRUSH APPLICATION
// =============================================================================

void UVoxelChunk::MulticastApplyBrushStroke_Implementation(const FBrushStroke& Stroke)
{
	ApplyBrushStroke(Stroke);
}


void UVoxelChunk::HandleSmoothBrush(
    const FBrushStroke& Stroke,
    const float LocalVoxelSize,
    const FVector ChunkOrigin,
    bool& bModified,
    int32 X, int32 Y, int32 Z,
    FVector VoxelWorldPos,
    float CurrentSDF)
{
    const float SurfaceThreshold = 3.0f;
    if (FMath::Abs(CurrentSDF) > SurfaceThreshold) return;

    float Sum  = 0.f;
    int32 Count = 0;

    const int Offsets[6][3] = {
        { 1, 0, 0 }, { -1, 0, 0 },
        { 0, 1, 0 }, {  0,-1, 0 },
        { 0, 0, 1 }, {  0, 0,-1 }
    };

    for (int i = 0; i < 6; i++)
    {
        int32 NX = X + Offsets[i][0];
        int32 NY = Y + Offsets[i][1];
        int32 NZ = Z + Offsets[i][2];
        float NeighborSDF;

        if (SparseVoxelGrid->HasVoxelAt(NX, NY, NZ))
        {
            NeighborSDF = SparseVoxelGrid->GetVoxel(NX, NY, NZ);
            Count++;
        }
        else
        {
            FVector NeighborWorldPos = ChunkOrigin + FVector(NX, NY, NZ) * LocalVoxelSize;
            float NeighborTerrainHeight = DiggerManager->GetLandscapeHeightAt(NeighborWorldPos);
            NeighborSDF = (NeighborWorldPos.Z > NeighborTerrainHeight) ? 5.0f : -5.0f;
        }
        Sum += NeighborSDF;
    }

    if (Count == 0) return;

    float Avg = Sum / 6.0f;

    float TerrainHeight  = DiggerManager->GetLandscapeHeightAt(VoxelWorldPos);
    float BaselineSDF    = (VoxelWorldPos.Z - TerrainHeight) / LocalVoxelSize;

    const float LandscapeInfluence = 0.35f;
    float Target  = FMath::Lerp(Avg, BaselineSDF, LandscapeInfluence);

    float Distance = (VoxelWorldPos - Stroke.BrushPosition).Size();
    float t        = Distance / Stroke.BrushRadius;
    float Falloff  = FMath::Exp(-FMath::Square(t * 2.5f));

    float SurfaceWeight = 1.0f - FMath::Clamp(FMath::Abs(CurrentSDF) / SurfaceThreshold, 0.f, 1.f);
    SurfaceWeight       = FMath::Square(SurfaceWeight);

    float SmoothFactor  = Stroke.BrushStrength * Falloff * SurfaceWeight * 8.f;
    float NewSDF        = FMath::Lerp(CurrentSDF, Target, SmoothFactor);

    SparseVoxelGrid->SetVoxel(X, Y, Z, NewSDF, false);
    bModified = true;
}


float UVoxelChunk::GetSDFSafe(const FIntVector& Pos, float LocalVoxelSize, const FVector& ChunkOrigin) const
{
    if (SparseVoxelGrid->HasVoxelAt(Pos.X, Pos.Y, Pos.Z))
        return SparseVoxelGrid->GetVoxel(Pos.X, Pos.Y, Pos.Z);

    FVector WorldPos     = ChunkOrigin + FVector(Pos) * LocalVoxelSize;
    float TerrainHeight  = DiggerManager->GetLandscapeHeightAt(WorldPos);
    return (WorldPos.Z - TerrainHeight) / LocalVoxelSize;
}


// =============================================================================
// UVoxelChunk::ApplyBrushStroke
//
// Core responsibilities (in order):
//   1.  Compute the voxel-space AABB for this brush hit.
//   2.  Cache column heights + snapshot baseline SDFs (unified single pass).
//   3.  Run per-voxel brush logic (Smooth / Noise / Dig+Add) into WriteBuffer.
//   4.  Post-write Laplacian surface-smoothing pass on the WriteBuffer.
//   5.  Snapshot old voxel state for the history system (pre-flush).
//   6.  Flush WriteBuffer → SparseVoxelGrid; redirect ghost voxels to owner chunk.
//   7.  Push the chunk action onto the undo stack.
//   8.  Optionally generate a natural dirt rim around new air pockets
//       (only when the "Natural Rim" seam style is active).
//   9.  Broadcast the modification report and mark dirty with a tight AABB.
//  10.  Propagate seam dirty flags to affected neighbours.
// =============================================================================

void UVoxelChunk::ApplyBrushStroke(const FBrushStroke& Stroke)
{
    UVoxelBrushShape* BrushShape = DiggerManager
        ? DiggerManager->GetBrushShapeForType(Stroke.BrushType) : nullptr;
    if (!DiggerManager || !BrushShape || !SparseVoxelGrid) return;

    // Only call Modify() when an explicit editor transaction is open.
    // Modify() serialises the entire UObject for undo — calling it every
    // brush tick is expensive and redundant since RecordChunkAction already
    // captures deltas explicitly.
#if WITH_EDITOR
    // Only call Modify() when UE's transaction system is actively recording.
    // Modify() serialises the entire UObject state for undo — calling it
    // every brush tick is expensive and redundant since RecordChunkAction
    // already captures deltas.  GIsTransacting is the engine global that is
    // true whenever a Begin/EndModify transaction is open.
    if (GIsTransacting)
        Modify();
#endif

    const float   LocalVoxelSize = FVoxelConversion::LocalVoxelSize;
    const FVector ChunkOrigin    = FVoxelConversion::ChunkToWorld(ChunkCoordinates);
    const int32   ChunkDim       = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
    const int32   GhostPad       = 2;

    // -------------------------------------------------------------------------
    // STEP 1 — Voxel-space AABB for this stroke
    // -------------------------------------------------------------------------
    const FVector BrushWorldBounds = CalculateBrushBounds(Stroke);
    const FVector LocalMin = (Stroke.BrushPosition - BrushWorldBounds) - ChunkOrigin;
    const FVector LocalMax = (Stroke.BrushPosition + BrushWorldBounds) - ChunkOrigin;

    const int32 StartX = FMath::Max(FMath::FloorToInt(LocalMin.X / LocalVoxelSize), -GhostPad);
    const int32 EndX   = FMath::Min(FMath::CeilToInt (LocalMax.X / LocalVoxelSize), ChunkDim + GhostPad);
    const int32 StartY = FMath::Max(FMath::FloorToInt(LocalMin.Y / LocalVoxelSize), -GhostPad);
    const int32 EndY   = FMath::Min(FMath::CeilToInt (LocalMax.Y / LocalVoxelSize), ChunkDim + GhostPad);
    const int32 StartZ = FMath::Max(FMath::FloorToInt(LocalMin.Z / LocalVoxelSize), -GhostPad);
    const int32 EndZ   = FMath::Min(FMath::CeilToInt (LocalMax.Z / LocalVoxelSize), ChunkDim + GhostPad);

    if (StartX >= EndX || StartY >= EndY || StartZ >= EndZ) return;

    // -------------------------------------------------------------------------
    // STEP 2 — SDF snapshot: flat array instead of TMap.
    // Indexed by (X-SnapX0) + strideX*(Y-SnapY0) + strideX*strideY*(Z-SnapZ0).
    // One contiguous allocation — no per-element hashing overhead.
    // -------------------------------------------------------------------------
    const int32 SnapX0 = StartX - 1, SnapX1 = EndX + 1;
    const int32 SnapY0 = StartY - 1, SnapY1 = EndY + 1;
    const int32 SnapZ0 = StartZ - 1, SnapZ1 = EndZ + 1;
    const int32 SnapSX = SnapX1 - SnapX0 + 1;
    const int32 SnapSY = SnapY1 - SnapY0 + 1;
    const int32 SnapSZ = SnapZ1 - SnapZ0 + 1;

    // -------------------------------------------------------------------------
    // STEP 2a — Column height cache (single pass for the full snapshot range).
    //
    // We query landscape heights ONCE for all columns in [SnapX0..SnapX1] ×
    // [SnapY0..SnapY1].  This cache is reused by both the SDF snapshot (Step 2b)
    // and the main brush loop (Step 4), eliminating the previous double-query.
    // -------------------------------------------------------------------------
    TArray<float> ColumnHeights;
    ColumnHeights.SetNumUninitialized(SnapSX * SnapSY);
    for (int32 X = SnapX0; X <= SnapX1; ++X)
    for (int32 Y = SnapY0; Y <= SnapY1; ++Y)
    {
        const FVector ColumnPos = ChunkOrigin + FVector(X * LocalVoxelSize, Y * LocalVoxelSize, 0.f);
        ColumnHeights[(X-SnapX0) + SnapSX*(Y-SnapY0)] = DiggerManager->GetLandscapeHeightAt(ColumnPos);
    }

    // Safe accessor for the column height cache.
    auto GetCachedColumnHeight = [&](int32 X, int32 Y) -> float
    {
        if (X < SnapX0 || X > SnapX1 || Y < SnapY0 || Y > SnapY1)
        {
            const FVector ColumnPos = ChunkOrigin + FVector(X * LocalVoxelSize, Y * LocalVoxelSize, 0.f);
            return DiggerManager->GetLandscapeHeightAt(ColumnPos);
        }
        return ColumnHeights[(X-SnapX0) + SnapSX*(Y-SnapY0)];
    };

    // -------------------------------------------------------------------------
    // STEP 2b — SDF snapshot: flat array instead of TMap.
    // Indexed by (X-SnapX0) + strideX*(Y-SnapY0) + strideX*strideY*(Z-SnapZ0).
    // One contiguous allocation — no per-element hashing overhead.
    // -------------------------------------------------------------------------
    TArray<float> SDFFlat;
    SDFFlat.SetNumUninitialized(SnapSX * SnapSY * SnapSZ);

    for (int32 X = SnapX0; X <= SnapX1; ++X)
    for (int32 Y = SnapY0; Y <= SnapY1; ++Y)
    {
        const float ColH = GetCachedColumnHeight(X, Y);

        for (int32 Z = SnapZ0; Z <= SnapZ1; ++Z)
        {
            const int32  Idx          = (X-SnapX0) + SnapSX*(Y-SnapY0) + SnapSX*SnapSY*(Z-SnapZ0);
            const FVector VoxelWorldPos = ChunkOrigin + FVector(X, Y, Z) * LocalVoxelSize;
            const bool   bIsGhost     = (X < 0 || X >= ChunkDim ||
                                         Y < 0 || Y >= ChunkDim ||
                                         Z < 0 || Z >= ChunkDim);
            float SDF;
            if (bIsGhost)
                SDF = DiggerManager->GetWorldSDF(VoxelWorldPos);
            else if (SparseVoxelGrid->HasVoxelAt(X, Y, Z))
                SDF = SparseVoxelGrid->GetVoxel(X, Y, Z);
            else
                SDF = (VoxelWorldPos.Z - ColH) / LocalVoxelSize;
            SDFFlat[Idx] = SDF;
        }
    }

    // Safe accessor — clamps to flat array bounds and falls back gracefully.
    auto GetSnapshotSDF = [&](int32 X, int32 Y, int32 Z) -> float
    {
        if (X < SnapX0 || X > SnapX1 || Y < SnapY0 || Y > SnapY1 ||
            Z < SnapZ0 || Z > SnapZ1)
        {
            const FVector VP  = ChunkOrigin + FVector(X, Y, Z) * LocalVoxelSize;
            return (VP.Z - GetCachedColumnHeight(X, Y)) / LocalVoxelSize;
        }
        return SDFFlat[(X-SnapX0) + SnapSX*(Y-SnapY0) + SnapSX*SnapSY*(Z-SnapZ0)];
    };

    // -------------------------------------------------------------------------
    // STEP 3 — Main per-voxel brush loop → WriteBuffer
    // -------------------------------------------------------------------------
    bool  bModified   = false;
    int32 VoxelsDug   = 0;
    int32 VoxelsAdded = 0;
    TMap<FIntVector, float> WriteBuffer;
    WriteBuffer.Reserve((EndX - StartX) * (EndY - StartY) * (EndZ - StartZ) / 4);

    for (int32 X = StartX; X < EndX; ++X)
    for (int32 Y = StartY; Y < EndY; ++Y)
    {
        float TerrainHeight = GetCachedColumnHeight(X, Y);

        // If the landscape query failed (column outside landscape bounds at a
        // chunk seam), fall back to the nearest valid cardinal sample.
        // Skipping the column entirely leaves an uncleared ridge of voxels.
        if (TerrainHeight <= (UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT + 1.f))
        {
            const int32 CardinalOffsets[][2] = { {1,0}, {-1,0}, {0,1}, {0,-1} };
            for (const auto& Off : CardinalOffsets)
            {
                const float H = GetCachedColumnHeight(X + Off[0], Y + Off[1]);
                if (H > (UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT + 1.f))
                {
                    TerrainHeight = H;
                    break;
                }
            }
            if (TerrainHeight <= (UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT + 1.f))
                continue; // truly outside landscape — nothing to sculpt
        }

        for (int32 Z = StartZ; Z < EndZ; ++Z)
        {
            const FIntVector LocalCoord(X, Y, Z);
            const FVector    VoxelWorldPos = ChunkOrigin + FVector(LocalCoord) * LocalVoxelSize;

            if (!BrushShape->IsWithinBounds(VoxelWorldPos, Stroke)) continue;

            // Implicit landscape baseline SDF.
            // We apply a small negative bias so the implicit isosurface (SDF=0)
            // always falls just BELOW the landscape mesh, preventing the voxel
            // mesh from poking through the landscape surface.
            // The bias is half a voxel — small enough to be invisible but large
            // enough to keep the MC isosurface consistently sub-surface.
            const float RawBaselineSDF = (VoxelWorldPos.Z - TerrainHeight) / LocalVoxelSize;
            // Only bias near the surface (|SDF| < 1 voxel). Deep solid and
            // high air are unaffected.
            constexpr float kSubSurfaceBias = 0.35f; // voxel units
            const float BaselineSDF = (FMath::Abs(RawBaselineSDF) < 1.5f)
                ? RawBaselineSDF - kSubSurfaceBias
                : RawBaselineSDF;
            const float CurrentSDF  = GetSnapshotSDF(X, Y, Z);

            // -----------------------------------------------------------------
            // SMOOTH BRUSH
            // -----------------------------------------------------------------
            if (Stroke.BrushType == EVoxelBrushType::Smooth)
            {
                const float Distance = FVector::Dist(VoxelWorldPos, Stroke.BrushPosition);
                if (Distance > Stroke.BrushRadius) continue;

                const float Alpha    = FMath::Pow(1.f - (Distance / Stroke.BrushRadius), Stroke.BrushFalloff);
                const float Strength = Alpha * Stroke.BrushStrength;
                if (Strength <= SMALL_NUMBER) continue;

                const int32 Offsets[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
                float Sum = 0.f;
                for (int32 i = 0; i < 6; ++i)
                    Sum += GetSnapshotSDF(X + Offsets[i][0], Y + Offsets[i][1], Z + Offsets[i][2]);
                const float NeighborAvg = Sum / 6.f;

                float FinalSDF;
                if (Stroke.bDig)
                {
                    const float Smoothed           = FMath::Lerp(CurrentSDF, NeighborAvg, 0.5f);
                    const float LandscapeInfluence = DiggerManager->bSmoothLandscapeAware ? 0.2f : 0.f;
                    const float Target             = FMath::Lerp(Smoothed, BaselineSDF, LandscapeInfluence);
                    FinalSDF                       = FMath::Lerp(CurrentSDF, Target, Strength);
                }
                else
                {
                    const float Detail = CurrentSDF - NeighborAvg;
                    FinalSDF = FMath::Clamp(CurrentSDF + Detail * Strength, -5.f, 5.f);
                }
                WriteBuffer.Add(LocalCoord, FinalSDF);
            }
            // -----------------------------------------------------------------
            // NOISE BRUSH
            // -----------------------------------------------------------------
            else if (Stroke.BrushType == EVoxelBrushType::Noise)
            {
                const float Distance = FVector::Dist(VoxelWorldPos, Stroke.BrushPosition);
                if (Distance > Stroke.BrushRadius) continue;

                const float Alpha    = FMath::Pow(1.f - (Distance / Stroke.BrushRadius), Stroke.BrushFalloff);
                const float Strength = Alpha * Stroke.BrushStrength;
                if (Strength <= SMALL_NUMBER) continue;

                const float Noise      = FMath::PerlinNoise3D(VoxelWorldPos * 0.03f);
                const float NoiseDelta = Noise * Strength;
                const float NewSDF     = Stroke.bDig
                    ? FMath::Min(CurrentSDF + NoiseDelta,  5.f)
                    : FMath::Max(CurrentSDF - NoiseDelta, -5.f);

                const bool bIsGhost = (X < 0 || X >= ChunkDim || Y < 0 || Y >= ChunkDim || Z < 0 || Z >= ChunkDim);
                if (!bIsGhost || FMath::Abs(NewSDF - CurrentSDF) > 0.001f)
                {
                    WriteBuffer.Add(LocalCoord, NewSDF);
                    Stroke.bDig ? ++VoxelsDug : ++VoxelsAdded;
                }
            }
            // -----------------------------------------------------------------
            // DIG / ADD BRUSH
            // -----------------------------------------------------------------
            else
            {
                const float BrushSDF  = BrushShape->CalculateSDF(VoxelWorldPos, Stroke, TerrainHeight);
                const bool  bIsGhost  = (X < 0 || X >= ChunkDim || Y < 0 || Y >= ChunkDim || Z < 0 || Z >= ChunkDim);
                const bool  bHasVoxel = SparseVoxelGrid->HasVoxelAt(LocalCoord.X, LocalCoord.Y, LocalCoord.Z);

                // Skip voxels above the surface that the brush cannot affect.
                const bool bOutsideSurface = Stroke.bDig ? (BrushSDF < 0.f) : (BrushSDF > 0.f);
                if (bOutsideSurface && BaselineSDF >= 0.f) continue;

                float NewSDF = bHasVoxel
                    ? (Stroke.bDig ? FMath::Max(BrushSDF, CurrentSDF) : FMath::Min(BrushSDF, CurrentSDF))
                    : BrushSDF;

                NewSDF = FMath::Clamp(NewSDF, -5.f, 5.f);
                if (bHasVoxel && FMath::IsNearlyEqual(NewSDF, CurrentSDF, 0.001f)) continue;

                if (!bIsGhost)
                    Stroke.bDig ? ++VoxelsDug : ++VoxelsAdded;

                WriteBuffer.Add(LocalCoord, NewSDF);
            }
        } // Z
    } // X, Y

    // -------------------------------------------------------------------------
    // STEP 4 — Post-write Laplacian surface-smoothing pass on WriteBuffer
    //
    // Only runs on Dig/Add strokes.  The mesher's own Step 1c handles the
    // majority of visible smoothing at render time; this pre-pass cleans up
    // sharp SDF discontinuities at the authored boundary before they are stored.
    // -------------------------------------------------------------------------
    // Single Laplacian pass — MC Step 1c already smooths the density grid,
    // so we only need one pre-pass to remove the sharpest authored boundary
    // discontinuities.  Three iterations + three SmoothBuffer allocations
    // were the second-largest per-stroke cost after the snapshot.
    if (Stroke.BrushType != EVoxelBrushType::Smooth &&
        Stroke.BrushType != EVoxelBrushType::Noise  &&
        WriteBuffer.Num() > 0)
    {
        const float SurfaceBand    = LocalVoxelSize * 4.f;
        constexpr float SmoothStr  = 0.5f; // lighter — MC handles the rest

        auto GetCombinedSDF = [&](int32 X, int32 Y, int32 Z) -> float
        {
            if (const float* W = WriteBuffer.Find(FIntVector(X, Y, Z)))
                return *W;
            return GetSnapshotSDF(X, Y, Z);
        };

        // Collect updates first so reads don't see half-written values.
        TArray<TPair<FIntVector, float>> Updates;
        Updates.Reserve(WriteBuffer.Num());
        for (const auto& Pair : WriteBuffer)
        {
            const float V = Pair.Value;
            if (FMath::Abs(V) > SurfaceBand) continue;
            const int32 X = Pair.Key.X, Y = Pair.Key.Y, Z = Pair.Key.Z;
            const float Avg = (GetCombinedSDF(X+1,Y,Z) + GetCombinedSDF(X-1,Y,Z) +
                               GetCombinedSDF(X,Y+1,Z) + GetCombinedSDF(X,Y-1,Z) +
                               GetCombinedSDF(X,Y,Z+1) + GetCombinedSDF(X,Y,Z-1)) / 6.f;
            Updates.Add({ Pair.Key,
                FMath::Clamp(FMath::Lerp(V, Avg, SmoothStr), -5.f, 5.f) });
        }
        for (const auto& U : Updates)
            WriteBuffer[U.Key] = U.Value;
    }

    // -------------------------------------------------------------------------
    // STEP 5 — Pre-flush history snapshot
    //
    // OldSDF and bWasWritten must be read from the sparse grid BEFORE SetVoxel
    // overwrites it.  After the flush these values would be wrong.
    // -------------------------------------------------------------------------
    FDiggerChunkAction ChunkAction(ChunkCoordinates);
    if (DiggerManager && WriteBuffer.Num() > 0)
    {
        const int32 ChunkDimH = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
        ChunkAction.SourceStroke = Stroke;

        for (const auto& Pair : WriteBuffer)
        {
            const FIntVector& Coord = Pair.Key;
            if (Coord.X < 0 || Coord.X >= ChunkDimH ||
                Coord.Y < 0 || Coord.Y >= ChunkDimH ||
                Coord.Z < 0 || Coord.Z >= ChunkDimH)
                continue;

            const bool  bWasWritten = SparseVoxelGrid->HasVoxelAt(Coord.X, Coord.Y, Coord.Z);
            const float OldSDF      = bWasWritten
                ? SparseVoxelGrid->GetVoxel(Coord.X, Coord.Y, Coord.Z)
                : GetSnapshotSDF(Coord.X, Coord.Y, Coord.Z);
            ChunkAction.AddDelta(Coord, OldSDF, Pair.Value, bWasWritten);
        }
    }

    // -------------------------------------------------------------------------
    // STEP 6 — Flush WriteBuffer → SparseVoxelGrid
    //
    // Ghost voxels (outside [0, ChunkDim)) are redirected to the owning chunk's
    // sparse grid.  Storing them at negative coords in this chunk's grid would
    // break the neighbour-merge seam logic in GenerateMesh.
    // -------------------------------------------------------------------------
    if (WriteBuffer.Num() > 0)
    {
        const bool bIsDig = (Stroke.BrushType != EVoxelBrushType::Smooth) && Stroke.bDig;

        for (const auto& Pair : WriteBuffer)
        {
            const FIntVector& LocalCoord = Pair.Key;
            const float       NewSDF     = Pair.Value;
            const bool        bIsGhost   = (LocalCoord.X < 0 || LocalCoord.X >= ChunkDim ||
                                            LocalCoord.Y < 0 || LocalCoord.Y >= ChunkDim ||
                                            LocalCoord.Z < 0 || LocalCoord.Z >= ChunkDim);
            if (!bIsGhost)
            {
                SparseVoxelGrid->SetVoxel(LocalCoord.X, LocalCoord.Y, LocalCoord.Z, NewSDF, bIsDig);
            }
            else if (DiggerManager)
            {
                // Redirect to the owning chunk.
                const FVector    WorldPos    = ChunkOrigin + FVector(LocalCoord) * LocalVoxelSize;
                const FIntVector OwnerCoords = FVoxelConversion::WorldToChunk(WorldPos);
                if (UVoxelChunk** OwnerPtr = DiggerManager->ChunkMap.Find(OwnerCoords))
                {
                    if (UVoxelChunk* Owner = *OwnerPtr)
                    {
                        if (USparseVoxelGrid* OwnerGrid = Owner->GetSparseVoxelGrid())
                        {
                            const FIntVector OwnerLocal = FVoxelConversion::WorldToLocalVoxel(WorldPos);
                            OwnerGrid->SetVoxel(OwnerLocal.X, OwnerLocal.Y, OwnerLocal.Z, NewSDF, bIsDig);
                        }
                    }
                }
                // If the owner chunk does not exist yet, discard — the brush
                // router (ApplyBrushToAllChunks) will have applied the stroke
                // directly to that chunk already.
            }
        }
        bModified = true;
    }

    // -------------------------------------------------------------------------
    // STEP 7 — Push chunk action onto the undo stack
    // -------------------------------------------------------------------------
    if (bModified && DiggerManager && ChunkAction.Deltas.Num() > 0)
        DiggerManager->RecordChunkAction(MoveTemp(ChunkAction));

    // -------------------------------------------------------------------------
    // STEP 8 — Natural rim seam generation
    //
    // EditorBrushHiddenSeam == false → seamless mode (default) → skip rim.
    // EditorBrushHiddenSeam == true  → natural rim mode         → build rim.
    //
    // We pass bSeamlessMode = !bHiddenSeam so that GenerateNaturalLandscapeSeamRim's
    // guard (if bSeamlessMode) fires correctly in the default case, keeping the
    // function at zero cost unless the user has explicitly opted into Natural Rim.
    // -------------------------------------------------------------------------
    if (Stroke.bDig && bModified && WriteBuffer.Num() > 0)
    {
        const bool bHiddenSeam   = DiggerManager->GetEditorBrushHiddenSeamType();
        const bool bSeamlessMode = !bHiddenSeam;

        TArray<FIntVector> AirVoxels;
        AirVoxels.Reserve(WriteBuffer.Num());
        for (const auto& Pair : WriteBuffer)
        {
            if (Pair.Value > 0.f)
                AirVoxels.Add(Pair.Key);
        }

        if (AirVoxels.Num() > 0)
            GenerateNaturalLandscapeSeamRim(AirVoxels, bSeamlessMode);
    }

    // -------------------------------------------------------------------------
    // STEP 9 — Broadcast the modification report and mark dirty with AABB
    // -------------------------------------------------------------------------
    if (bModified && DiggerManager)
    {
        const float SeamSafetyMargin = GhostPad * LocalVoxelSize * 2.f;

        FVoxelModificationReport Report;
        Report.ChunkCoordinates = ChunkCoordinates;
        Report.BrushPosition    = Stroke.BrushPosition;
        Report.BrushRadius      = Stroke.BrushRadius + SeamSafetyMargin;
        Report.VoxelsDug        = VoxelsDug;
        Report.VoxelsAdded      = VoxelsAdded;
        DiggerManager->OnVoxelsModified.Broadcast(Report);

        // Build a tight world-space AABB from the written voxels, expanded by
        // 3 voxels so the marching cubes gradient stencil always has room.
        FBox StrokeBounds(ForceInit);
        for (const auto& Pair : WriteBuffer)
            StrokeBounds += ChunkOrigin + FVector(Pair.Key) * LocalVoxelSize;

        if (StrokeBounds.IsValid)
            StrokeBounds = StrokeBounds.ExpandBy(LocalVoxelSize * 3.f);

        MarkDirtyWithBounds(StrokeBounds);

        // ---------------------------------------------------------------------
        // STEP 10 — Propagate seam dirty flags to affected neighbours
        //
        // Any voxel written within SeamPad cells of this chunk's boundary
        // makes the adjacent chunk's mesh stale.  Mark it dirty so it remeshes
        // using the updated boundary SDF values, preventing visible cracks.
        // ---------------------------------------------------------------------
        {
            const int32 ChunkN  = ChunkSize * Subdivisions;
            const int32 SeamPad = 2;
            TSet<FIntVector> TouchedNeighbours;

            for (const auto& Pair : WriteBuffer)
            {
                const FIntVector& V = Pair.Key;
                if (V.X <  SeamPad)          TouchedNeighbours.Add(FIntVector(-1,  0,  0));
                if (V.X >= ChunkN - SeamPad)  TouchedNeighbours.Add(FIntVector( 1,  0,  0));
                if (V.Y <  SeamPad)          TouchedNeighbours.Add(FIntVector( 0, -1,  0));
                if (V.Y >= ChunkN - SeamPad)  TouchedNeighbours.Add(FIntVector( 0,  1,  0));
                if (V.Z <  SeamPad)          TouchedNeighbours.Add(FIntVector( 0,  0, -1));
                if (V.Z >= ChunkN - SeamPad)  TouchedNeighbours.Add(FIntVector( 0,  0,  1));
            }

            for (const FIntVector& Dir : TouchedNeighbours)
            {
                const FIntVector NeighborCoords = ChunkCoordinates + Dir;
                if (UVoxelChunk** NeighborPtr = DiggerManager->ChunkMap.Find(NeighborCoords))
                    if (UVoxelChunk* Neighbor = *NeighborPtr)
                        Neighbor->MarkDirty();
            }
        }
    }
}

// =============================================================================
// UVoxelChunk::CalculateBrushBounds
// =============================================================================

FVector UVoxelChunk::CalculateBrushBounds(const FBrushStroke& Stroke) const
{
	auto CalculateRotatedBounds = [](const FVector& HalfExtents, const FRotator& Rotation, float Falloff) -> FVector
	{
		static constexpr float EPSILON = 0.01f;

		if (Rotation.IsNearlyZero())
			return HalfExtents + Falloff + FVector(EPSILON);

		TArray<FVector> Corners = {
			FVector(-1,-1,-1), FVector(-1,-1, 1),
			FVector(-1, 1,-1), FVector(-1, 1, 1),
			FVector( 1,-1,-1), FVector( 1,-1, 1),
			FVector( 1, 1,-1), FVector( 1, 1, 1)
		};

		FBox RotatedBox(EForceInit::ForceInit);
		for (const FVector& Corner : Corners)
			RotatedBox += Rotation.RotateVector(Corner * HalfExtents);

		return RotatedBox.GetExtent() + Falloff + FVector(EPSILON);
	};

	switch (Stroke.BrushType)
	{
	case EVoxelBrushType::Sphere:
	case EVoxelBrushType::Icosphere:
	case EVoxelBrushType::Smooth:
	case EVoxelBrushType::Noise:
		return FVector(Stroke.BrushRadius + Stroke.BrushFalloff);

	case EVoxelBrushType::Cube:
		{
			const FVector HalfExtents = Stroke.bUseAdvancedCubeBrush
				? FVector(Stroke.AdvancedCubeHalfExtentX,
				          Stroke.AdvancedCubeHalfExtentY,
				          Stroke.AdvancedCubeHalfExtentZ)
				: FVector(Stroke.BrushRadius);
			return CalculateRotatedBounds(HalfExtents, Stroke.BrushRotation, Stroke.BrushFalloff);
		}

	case EVoxelBrushType::Cylinder:
	case EVoxelBrushType::Capsule:
		{
			const FVector HalfExtents = FVector(
				Stroke.BrushRadius,
				Stroke.BrushRadius,
				Stroke.BrushLength * 0.5f);
			return CalculateRotatedBounds(HalfExtents, Stroke.BrushRotation, Stroke.BrushFalloff);
		}

	case EVoxelBrushType::Cone:
	case EVoxelBrushType::Pyramid:
		{
			const float AngleRad    = FMath::DegreesToRadians(Stroke.BrushAngle);
			const float RadiusAtBase = Stroke.BrushLength * FMath::Tan(AngleRad);
			const FVector HalfExtents = FVector(RadiusAtBase, RadiusAtBase, Stroke.BrushLength * 0.5f);
			return CalculateRotatedBounds(HalfExtents, Stroke.BrushRotation, Stroke.BrushFalloff);
		}

	case EVoxelBrushType::Torus:
		{
			const float OuterRadius = Stroke.BrushRadius + Stroke.TorusInnerRadius;
			const FVector HalfExtents = FVector(OuterRadius, OuterRadius, Stroke.BrushRadius);
			return CalculateRotatedBounds(HalfExtents, Stroke.BrushRotation, Stroke.BrushFalloff);
		}

	default:
		return FVector(Stroke.BrushRadius + Stroke.BrushFalloff);
	}
}


// =============================================================================
// UVoxelChunk::ApplyChunkAction
//
// Applies a recorded FDiggerChunkAction to this chunk's sparse voxel grid.
// Used by the undo/redo system:
//   bApplyNew = true  → Redo: write Delta.NewSDF
//   bApplyNew = false → Undo: write Delta.OldSDF (or remove voxel if !bWasWritten)
// bRecordForUndo: when true the inverse action is pushed onto the undo stack
// so that a history-replay stroke can itself be undone.
// =============================================================================

void UVoxelChunk::ApplyChunkAction(const FDiggerChunkAction& Action, bool bApplyNew, bool bRecordForUndo)
{
    if (!SparseVoxelGrid) return;

    const int32 ChunkDim = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;

    // Build the inverse action for bRecordForUndo before we touch the grid.
    FDiggerChunkAction InverseAction(ChunkCoordinates);
    if (bRecordForUndo)
        InverseAction.SourceStroke = Action.SourceStroke;

    FBox DirtyBounds(ForceInit);
    const FVector Origin = FVoxelConversion::ChunkToWorld(ChunkCoordinates);
    const float   VoxSz  = FVoxelConversion::LocalVoxelSize;

    for (const FVoxelDelta& Delta : Action.Deltas)
    {
        const FIntVector& C = Delta.VoxelCoord;

        // Guard: skip coords outside this chunk's valid range.
        if (C.X < 0 || C.X >= ChunkDim ||
            C.Y < 0 || C.Y >= ChunkDim ||
            C.Z < 0 || C.Z >= ChunkDim)
            continue;

        if (bRecordForUndo)
        {
            // Capture current state before overwriting it.
            const bool  bExists = SparseVoxelGrid->HasVoxelAt(C.X, C.Y, C.Z);
            const float CurSDF  = bExists ? SparseVoxelGrid->GetVoxel(C.X, C.Y, C.Z) : 0.f;
            InverseAction.AddDelta(C, CurSDF, bApplyNew ? Delta.NewSDF : Delta.OldSDF, bExists);
        }

        if (bApplyNew)
        {
            // Redo: write the new SDF value.
            SparseVoxelGrid->SetVoxel(C.X, C.Y, C.Z, Delta.NewSDF, /*bDig=*/false);
        }
        else
        {
            // Undo: restore the old state.
            if (Delta.bWasWritten)
                SparseVoxelGrid->SetVoxel(C.X, C.Y, C.Z, Delta.OldSDF, /*bDig=*/false);
            else
                SparseVoxelGrid->RemoveVoxel(C);
        }

        DirtyBounds += Origin + FVector(C) * VoxSz;
    }

    if (bRecordForUndo && DiggerManager && InverseAction.Deltas.Num() > 0)
        DiggerManager->RecordChunkAction(MoveTemp(InverseAction));

    // Mark dirty with a tight AABB so only the affected region remeshes.
    if (DirtyBounds.IsValid)
        MarkDirtyWithBounds(DirtyBounds.ExpandBy(VoxSz * 3.f));
    else
        MarkDirty();
}

void UVoxelChunk::BakeSingleBrushStroke(FBrushStroke StrokeToBake)
{
	if (SparseVoxelGrid) { /* placeholder */ }
}


// =============================================================================
// SHELL / ISLAND / UTILITY
// =============================================================================

// =============================================================================
// UVoxelChunk::GenerateNaturalLandscapeSeamRim
// =============================================================================

void UVoxelChunk::GenerateNaturalLandscapeSeamRim(
    const TArray<FIntVector>& AirVoxels,
    bool                      bSeamlessMode)
{
    if (AirVoxels.IsEmpty() || bSeamlessMode) return;

    const FVector ChunkOrigin    = FVoxelConversion::ChunkToWorld(ChunkCoordinates);
    const float   LocalVoxSize   = FVoxelConversion::LocalVoxelSize;
    const int32   VoxelsPerChunk = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
    const float   HalfVoxel      = LocalVoxSize * 0.5f;

    static constexpr int32 RimRadiusVoxels  = 5;
    static constexpr float PeakHeightVoxels = 4.f;

    TMap<FIntPoint, float> HeightCache;
    HeightCache.Reserve(256);

    auto GetHeightFast = [&](int32 CX, int32 CY) -> float
    {
        const FIntPoint Key(CX, CY);
        if (float* V = HeightCache.Find(Key)) return *V;
        if (!DiggerManager) return -1.e30f;
        const FVector ColPos = ChunkOrigin + FVector(CX * LocalVoxSize, CY * LocalVoxSize, 0.f);
        const float   H      = DiggerManager->GetLandscapeHeightAt(ColPos);
        HeightCache.Add(Key, H);
        return H;
    };

    TSet<FIntPoint> AirXY;
    AirXY.Reserve(AirVoxels.Num());
    for (const FIntVector& V : AirVoxels)
        AirXY.Add(FIntPoint(V.X, V.Y));

    TMap<FIntPoint, int32> AirFloorZ;
    AirFloorZ.Reserve(AirXY.Num());
    for (const FIntVector& V : AirVoxels)
    {
        int32& Floor = AirFloorZ.FindOrAdd(FIntPoint(V.X, V.Y), INT_MAX);
        Floor = FMath::Min(Floor, V.Z);
    }

    static const FIntPoint CardinalXY[4] = {{1,0},{-1,0},{0,1},{0,-1}};

    TSet<FIntPoint> EdgeColumns;
    for (const FIntPoint& AXY : AirXY)
    {
        for (const FIntPoint& Dir : CardinalXY)
        {
            if (!AirXY.Contains(AXY + Dir))
            {
                EdgeColumns.Add(AXY);
                break;
            }
        }
    }

    TMap<FIntPoint, int32> RimDist;
    RimDist.Reserve(EdgeColumns.Num() * RimRadiusVoxels * 4);

    TArray<FIntPoint> BFSFrontier;
    BFSFrontier.Reserve(EdgeColumns.Num());

    for (const FIntPoint& EC : EdgeColumns)
    {
        if (!AirXY.Contains(EC))
        {
            RimDist.Add(EC, 0);
            BFSFrontier.Add(EC);
        }
    }
    for (const FIntPoint& EC : EdgeColumns)
    {
        for (const FIntPoint& Dir : CardinalXY)
        {
            const FIntPoint N = EC + Dir;
            if (!AirXY.Contains(N) && !RimDist.Contains(N))
            {
                RimDist.Add(N, 0);
                BFSFrontier.Add(N);
            }
        }
    }

    for (int32 Dist = 1; Dist <= RimRadiusVoxels; ++Dist)
    {
        TArray<FIntPoint> NextFrontier;
        for (const FIntPoint& P : BFSFrontier)
        {
            for (const FIntPoint& Dir : CardinalXY)
            {
                const FIntPoint N = P + Dir;
                if (!AirXY.Contains(N) && !RimDist.Contains(N))
                {
                    RimDist.Add(N, Dist);
                    NextFrontier.Add(N);
                }
            }
        }
        BFSFrontier = MoveTemp(NextFrontier);
        if (BFSFrontier.IsEmpty()) break;
    }

    for (const auto& Pair : RimDist)
    {
        const FIntPoint& Col  = Pair.Key;
        const int32      Dist = Pair.Value;

        if (Col.X < -1 || Col.X > VoxelsPerChunk ||
            Col.Y < -1 || Col.Y > VoxelsPerChunk) continue;

        const float TerrainH = GetHeightFast(Col.X, Col.Y);
        if (TerrainH <= -1.e30f) continue;

        const float T       = static_cast<float>(Dist) / static_cast<float>(RimRadiusVoxels);
        const float InvT    = 1.f - T;
        const float Profile = InvT * InvT * (2.f * InvT - 1.f) + InvT;
        const float RimPeak = FMath::Clamp(Profile, 0.f, 1.f);

        float SlopeBoost = 0.f;
        {
            float MaxDrop = 0.f;
            for (const FIntPoint& Dir : CardinalXY)
            {
                const float NH = GetHeightFast(Col.X + Dir.X, Col.Y + Dir.Y);
                if (NH > -1.e30f)
                    MaxDrop = FMath::Max(MaxDrop, (TerrainH - NH) / LocalVoxSize);
            }
            SlopeBoost = FMath::Clamp(MaxDrop * 0.5f, 0.f, 2.f);
        }

        const float RimHeightVox = PeakHeightVoxels * RimPeak + SlopeBoost;
        if (RimHeightVox < 0.05f) continue;

        const float RimTopZ = TerrainH + RimHeightVox * LocalVoxSize;

        const int32 ZStart = FMath::FloorToInt((TerrainH - LocalVoxSize - ChunkOrigin.Z) / LocalVoxSize);
        const int32 ZEnd   = FMath::CeilToInt ((RimTopZ              - ChunkOrigin.Z) / LocalVoxSize);

        for (int32 Z = ZStart; Z <= ZEnd; ++Z)
        {
            if (Z < -1 || Z > VoxelsPerChunk) continue;

            const FIntVector Vox(Col.X, Col.Y, Z);

            if (const FVoxelData* Existing = SparseVoxelGrid->VoxelData.Find(Vox))
                if (Existing->SDFValue > 0.f) continue;

            const FVector VoxWorldPos(
                ChunkOrigin.X + Col.X * LocalVoxSize + HalfVoxel,
                ChunkOrigin.Y + Col.Y * LocalVoxSize + HalfVoxel,
                ChunkOrigin.Z +      Z * LocalVoxSize + HalfVoxel);

            const float DistFromTop = RimTopZ - VoxWorldPos.Z;
            float Alpha = FMath::Clamp(DistFromTop / (RimHeightVox * LocalVoxSize), 0.f, 1.f);
            Alpha = Alpha * Alpha * (3.f - 2.f * Alpha);
            const float SDF = FMath::Lerp(-0.08f, -1.f, Alpha);

            SparseVoxelGrid->SetVoxel(Vox, SDF, false);
        }
    }
}

void UVoxelChunk::BakeToStaticMesh(bool bEnableCollision, bool bEnableNanite, float DetailReduction, const FString& AssetName)
{
    if (!DiggerManager || !DiggerManager->ProceduralMesh)
    {
        UE_LOG(LogTemp, Error, TEXT("BakeToStaticMesh: No ProceduralMesh on DiggerManager."));
        return;
    }

    UProceduralMeshComponent* PMC = DiggerManager->ProceduralMesh;
    const FProcMeshSection* Section = PMC->GetProcMeshSection(SectionIndex);
    if (!Section)
    {
        UE_LOG(LogTemp, Warning, TEXT("BakeToStaticMesh: No mesh section at index %d."), SectionIndex);
        return;
    }

    // -------------------------------------------------------------------------
    // 1. Build FMeshDescription from the PMC section
    // -------------------------------------------------------------------------
    FMeshDescription MeshDesc;
    FStaticMeshAttributes Attributes(MeshDesc);
    Attributes.Register();

    TVertexAttributesRef<FVector3f> PositionAttr =
        Attributes.GetVertexPositions();
    TVertexInstanceAttributesRef<FVector3f> NormalAttr =
        Attributes.GetVertexInstanceNormals();
    TVertexInstanceAttributesRef<FVector2f> UVAttr =
        Attributes.GetVertexInstanceUVs();
    TPolygonGroupAttributesRef<FName> PolyGroupAttr =
        Attributes.GetPolygonGroupMaterialSlotNames();

    // One polygon group
    FPolygonGroupID PolyGroup = MeshDesc.CreatePolygonGroup();
    PolyGroupAttr[PolyGroup] = FName(TEXT("Material_0"));

    // Vertices
    const TArray<FProcMeshVertex>& ProcVerts = Section->ProcVertexBuffer;
    TArray<FVertexID> VertexIDs;
    VertexIDs.Reserve(ProcVerts.Num());
    for (const FProcMeshVertex& PV : ProcVerts)
    {
        FVertexID VID = MeshDesc.CreateVertex();
        PositionAttr[VID] = FVector3f(PV.Position);
        VertexIDs.Add(VID);
    }

    // Triangles
    const TArray<uint32>& Indices = Section->ProcIndexBuffer;
    for (int32 i = 0; i + 2 < Indices.Num(); i += 3)
    {
        TArray<FVertexInstanceID> TriInstances;
        for (int32 c = 0; c < 3; ++c)
        {
            uint32 Idx = Indices[i + c];
            FVertexInstanceID VIID = MeshDesc.CreateVertexInstance(VertexIDs[Idx]);
            NormalAttr[VIID] = FVector3f(ProcVerts[Idx].Normal);
            UVAttr[VIID]     = FVector2f(ProcVerts[Idx].UV0);
            TriInstances.Add(VIID);
        }
        MeshDesc.CreatePolygon(PolyGroup, TriInstances);
    }

    // -------------------------------------------------------------------------
    // 2. Build UStaticMesh from description (works at runtime AND in editor)
    // -------------------------------------------------------------------------
    FString MeshName = AssetName.IsEmpty()
        ? FString::Printf(TEXT("SM_VoxelChunk_%d_%d_%d"),
            ChunkCoordinates.X, ChunkCoordinates.Y, ChunkCoordinates.Z)
        : AssetName;

    UStaticMesh* BakedMesh = NewObject<UStaticMesh>(
        GetTransientPackage(), FName(*MeshName), RF_Transient);

    BakedMesh->GetStaticMaterials().Add(
        FStaticMaterial(PMC->GetMaterial(0), FName(TEXT("Material_0"))));

    UStaticMesh::FBuildMeshDescriptionsParams Params;
    Params.bMarkPackageDirty  = false;
    Params.bBuildSimpleCollision = bEnableCollision;
    Params.bFastBuild         = true;

    BakedMesh->BuildFromMeshDescriptions({ &MeshDesc }, Params);

#if ENGINE_MAJOR_VERSION >= 5
    if (bEnableNanite)
        BakedMesh->NaniteSettings.bEnabled = true;
#endif

    // -------------------------------------------------------------------------
    // 3. Spawn AStaticMeshActor in the level
    // -------------------------------------------------------------------------
    UWorld* LevelWorld = DiggerManager->GetWorld();
    if (!LevelWorld) return;

    const FVector ChunkWorldOrigin = DiggerManager->GetActorLocation()
        + FVector(ChunkCoordinates) * (float)(ChunkSize * VoxelSize);

    FActorSpawnParameters SpawnParams;
    SpawnParams.Name = FName(*MeshName);
    SpawnParams.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AStaticMeshActor* MeshActor = LevelWorld->SpawnActor<AStaticMeshActor>(
        AStaticMeshActor::StaticClass(),
        ChunkWorldOrigin, FRotator::ZeroRotator, SpawnParams);

    if (!MeshActor) return;

    UStaticMeshComponent* SMC = MeshActor->GetStaticMeshComponent();
    SMC->SetStaticMesh(BakedMesh);
    SMC->SetCollisionEnabled(bEnableCollision
        ? ECollisionEnabled::QueryAndPhysics
        : ECollisionEnabled::NoCollision);
    SMC->SetMaterial(0, PMC->GetMaterial(0));
    MeshActor->SetMobility(EComponentMobility::Static);

    UE_LOG(LogTemp, Log,
        TEXT("BakeToStaticMesh: Chunk (%d,%d,%d) → '%s' at %s"),
        ChunkCoordinates.X, ChunkCoordinates.Y, ChunkCoordinates.Z,
        *MeshName, *ChunkWorldOrigin.ToString());
}


// =============================================================================
// VOXEL ACCESSORS
// =============================================================================

USparseVoxelGrid* UVoxelChunk::GetSparseVoxelGrid() const
{
	return SparseVoxelGrid;
}

TMap<FIntVector, float> UVoxelChunk::GetActiveVoxels() const
{
	return GetSparseVoxelGrid()->GetAllVoxelsSDF();
}

void UVoxelChunk::SetVoxel(int32 X, int32 Y, int32 Z, const float SDFValue, bool& bDig) const
{
	if (SparseVoxelGrid)
		SparseVoxelGrid->SetVoxel(X, Y, Z, SDFValue, bDig);
}

void UVoxelChunk::SetVoxel(const FVector& Position, const float SDFValue, bool& bDig) const
{
	SetVoxel((int32)Position.X, (int32)Position.Y, (int32)Position.Z, SDFValue, bDig);
}

FVector UVoxelChunk::SnapToVoxelGrid(const FVector& WorldLocation) const
{
	const float GridSize = FVoxelConversion::LocalVoxelSize;
	return FVector(
		FMath::GridSnap(WorldLocation.X, GridSize),
		FMath::GridSnap(WorldLocation.Y, GridSize),
		FMath::GridSnap(WorldLocation.Z, GridSize)
	);
}