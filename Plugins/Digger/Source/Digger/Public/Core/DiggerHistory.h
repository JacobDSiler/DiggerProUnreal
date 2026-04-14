#pragma once

#include "CoreMinimal.h"
#include "FBrushStroke.h"      // USTRUCT with GENERATED_BODY — include before DiggerHistory.generated.h
#include "FSpawnedHoleData.h"  // USTRUCT with GENERATED_BODY — include before DiggerHistory.generated.h
#include "DiggerHistory.generated.h"

// =============================================================================
// ACTOR RECORD TYPES
// =============================================================================
//
// These complement the voxel-delta system.  They track the lifecycle and
// transforms of chunk-owned actors (DynamicHole, DynamicLightActor,
// IslandActor) so that Undo/Redo can destroy/respawn/teleport them just as
// cleanly as it restores voxel values.
//
// All three record types are embedded directly in FDiggerHistoryAction and
// applied by ADiggerManager::ApplyHistoryAction alongside the voxel deltas.
// =============================================================================

// -----------------------------------------------------------------------------
// EDiggerActorType — discriminator so the undo system knows which pool to look
// in when resolving a weak ptr that has gone stale (e.g. after level reload).
// -----------------------------------------------------------------------------
UENUM(BlueprintType)
enum class EDiggerActorType : uint8
{
    Hole         UMETA(DisplayName="Hole"),
    Light        UMETA(DisplayName="Light"),
    Island       UMETA(DisplayName="Island"),
};

// -----------------------------------------------------------------------------
// FDiggerActorSpawnRecord
//
// Records ONE actor that was spawned during an action.
// Undo  → Destroy the actor (look it up by UID in the chunk's registry).
// Redo  → Respawn via the serialised data stored here.
//
// For holes   : SpawnData is the FSpawnedHoleData used to create it.
// For lights  : Transform is enough; full re-init data not needed for redo
//               because the light component is recreated from CachedType etc.
// For islands : Redo is a no-op (islands are geometry; user must re-extract).
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// Actor record structs
//
// FSpawnedHoleData fields have no UPROPERTY() — UHT knows the type (included
// above) but doesn't need to register these internal fields for reflection.
// Everything else in these structs is plain POD that UHT handles fine.
// -----------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct DIGGER_API FDiggerActorSpawnRecord
{
    GENERATED_BODY()

    EDiggerActorType ActorType = EDiggerActorType::Hole;
    int32 ActorUID = INDEX_NONE;
    FIntVector OwningChunkCoords = FIntVector::ZeroValue;
    FTransform SpawnTransform;
    FSpawnedHoleData HoleData;

    FDiggerActorSpawnRecord() = default;
};

USTRUCT(BlueprintType)
struct DIGGER_API FDiggerActorMoveRecord
{
    GENERATED_BODY()

    EDiggerActorType ActorType = EDiggerActorType::Hole;
    int32 ActorUID = INDEX_NONE;
    FTransform PreTransform;
    FTransform PostTransform;
    FIntVector OldChunkCoords = FIntVector::ZeroValue;
    FIntVector NewChunkCoords = FIntVector::ZeroValue;

    FDiggerActorMoveRecord() = default;
};

USTRUCT(BlueprintType)
struct DIGGER_API FDiggerActorDestroyRecord
{
    GENERATED_BODY()

    EDiggerActorType ActorType = EDiggerActorType::Hole;
    int32 ActorUID = INDEX_NONE;
    FIntVector OwningChunkCoords = FIntVector::ZeroValue;
    FTransform Transform;
    FSpawnedHoleData HoleData;

    FDiggerActorDestroyRecord() = default;
};

// =============================================================================
// DIGGER HISTORY SYSTEM
// =============================================================================
//
// Architecture overview
// ---------------------
//
//  FVoxelDelta          — the atomic unit: one voxel coord, old SDF, new SDF.
//                         "Old" captures whether the voxel even existed before
//                         the stroke so undo can restore a truly unwritten state.
//
//  FDiggerChunkAction   — all deltas that affected one specific chunk in one
//                         logical user action (a single brush stroke tick).
//                         Also stores the originating FBrushStroke so the action
//                         can be replayed on a different chunk (ZBrush history copy).
//
//  FDiggerHistoryAction — one complete user action, spanning 1..N chunks.
//                         This is the unit that gets pushed onto the undo stack.
//
//  FDiggerHistory       — the full timeline for one chunk.  Every action that
//                         ever touched the chunk lives here in order.
//                         Used for the ZBrush-style per-chunk history panel.
//
//  ADiggerManager       — owns the global UndoStack / RedoStack (arrays of
//                         FDiggerHistoryAction) and exposes Undo() / Redo().
//
// Undo/Redo flow
// --------------
//  1. Before ApplyBrushStroke writes any voxel it snapshots the pre-state of
//     every voxel it is about to touch into a FDiggerHistoryAction.
//  2. After the flush the post-state is recorded.  The action is pushed onto
//     ADiggerManager::UndoStack and RedoStack is cleared.
//  3. Undo() pops the top FDiggerHistoryAction, iterates its chunk entries,
//     writes each delta's OldSDF back (or removes the voxel if it was not
//     written before), invalidates the DensityGrid cache for those chunks,
//     and calls MarkDirty() on each affected chunk.
//  4. Redo() works identically but applies NewSDF instead of OldSDF.
//
// ZBrush timeline copy flow (future)
// ------------------------------------
//  FDiggerHistory::Actions is a plain ordered array.  To copy a chunk's
//  history to another chunk, iterate Actions, for each FDiggerChunkAction
//  translate VoxelCoord by the offset between source and target chunk origins,
//  and call ApplyDeltasToChunk() on the target.  The FBrushStroke stored on
//  each action provides semantic context (brush type, radius, strength) for
//  higher-level replay / procedural variation.
//
// =============================================================================


// -----------------------------------------------------------------------------
// FVoxelDelta — atomic single-voxel change record
// -----------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct DIGGER_API FVoxelDelta
{
    GENERATED_BODY()

    // Local voxel coordinate within the chunk
    UPROPERTY()
    FIntVector VoxelCoord = FIntVector::ZeroValue;

    // SDF value before the stroke.  Only meaningful when bWasWritten == true.
    UPROPERTY()
    float OldSDF = 0.f;

    // SDF value after the stroke.
    UPROPERTY()
    float NewSDF = 0.f;

    // Was this voxel already present in the sparse grid before the stroke?
    // If false, undo must REMOVE the voxel rather than restore OldSDF.
    UPROPERTY()
    bool bWasWritten = false;

    FVoxelDelta() = default;
    FVoxelDelta(FIntVector InCoord, float InOld, float InNew, bool bExisted)
        : VoxelCoord(InCoord), OldSDF(InOld), NewSDF(InNew), bWasWritten(bExisted)
    {}
};


// -----------------------------------------------------------------------------
// FDiggerChunkAction — deltas for one chunk within one user action
// -----------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct DIGGER_API FDiggerChunkAction
{
    GENERATED_BODY()

    // Which chunk these deltas belong to
    UPROPERTY()
    FIntVector ChunkCoords = FIntVector::ZeroValue;

    // Every voxel that changed in this chunk during this action.
    UPROPERTY()
    TArray<FVoxelDelta> Deltas;

    // The originating brush stroke — used for ZBrush-style history replay
    // and timeline UI labelling. FBrushStroke is a proper USTRUCT declared
    // in FBrushStroke.h, included before DiggerHistory.generated.h above.
    UPROPERTY()
    FBrushStroke SourceStroke;

    /** @deprecated  Kept for binary save/load compatibility.  Not used by undo. */
    UPROPERTY()
    int32 HoleCountBefore = 0;

    // UIDs of holes spawned during this action — used by undo to destroy them.
    // Full FSpawnedHoleData is on the parent FDiggerHistoryAction::SpawnedActors.
    UPROPERTY()
    TArray<int32> HoleUIDsAdded;

    FDiggerChunkAction() = default;
    explicit FDiggerChunkAction(FIntVector InCoords) : ChunkCoords(InCoords) {}

    void AddDelta(FIntVector Coord, float OldSDF, float NewSDF, bool bWasWritten)
    {
        Deltas.Emplace(Coord, OldSDF, NewSDF, bWasWritten);
    }
};


// -----------------------------------------------------------------------------
// FDiggerHistoryAction — one complete user action (may span multiple chunks)
// -----------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct DIGGER_API FDiggerHistoryAction
{
    GENERATED_BODY()

    // Human-readable label shown in a future timeline UI ("Dig", "Add", "Smooth" …)
    UPROPERTY()
    FString ActionLabel;

    // Wall-clock time the action was committed (UTC).  Lets the timeline UI
    // show absolute timestamps and sort/filter by session.
    UPROPERTY()
    FDateTime Timestamp;

    // One entry per chunk that was modified.  Almost always 1; can be 2–4 when
    // a stroke straddles a chunk boundary and neighbour seam writes are recorded.
    UPROPERTY()
    TArray<FDiggerChunkAction> ChunkActions;

    // -------------------------------------------------------------------------
    // ACTOR RECORDS — spawns, moves, and destroys that happened during this
    // action.  Applied by ADiggerManager::ApplyHistoryAction.
    // -------------------------------------------------------------------------

    /** Actors spawned during this action (holes, lights, islands). */
    UPROPERTY()
    TArray<FDiggerActorSpawnRecord> SpawnedActors;

    /** Actors whose transform changed during this action. */
    UPROPERTY()
    TArray<FDiggerActorMoveRecord> MovedActors;

    /** Actors explicitly destroyed during this action. */
    UPROPERTY()
    TArray<FDiggerActorDestroyRecord> DestroyedActors;

    // Unique monotonic ID assigned by ADiggerManager.  Lets the timeline UI
    // reference actions by stable ID even after the stack is pruned.
    UPROPERTY()
    int32 ActionID = -1;

    FDiggerHistoryAction() = default;
    FDiggerHistoryAction(FString InLabel, int32 InID)
        : ActionLabel(MoveTemp(InLabel))
        , Timestamp(FDateTime::UtcNow())
        , ActionID(InID)
    {}

    // Find or create a chunk entry for the given coordinates.
    FDiggerChunkAction& GetOrAddChunkAction(FIntVector ChunkCoords)
    {
        for (FDiggerChunkAction& CA : ChunkActions)
            if (CA.ChunkCoords == ChunkCoords) return CA;
        ChunkActions.Emplace(ChunkCoords);
        return ChunkActions.Last();
    }

    bool IsEmpty() const
    {
        for (const FDiggerChunkAction& CA : ChunkActions)
            if (CA.Deltas.Num() > 0) return false;
        if (SpawnedActors.Num()   > 0) return false;
        if (MovedActors.Num()     > 0) return false;
        if (DestroyedActors.Num() > 0) return false;
        return true;
    }
};


// -----------------------------------------------------------------------------
// FDiggerHistory — the complete ordered timeline for a single chunk
//
// Lives on ADiggerManager, keyed by chunk coordinates.
// Each action that ever touched the chunk is appended here in order.
// This is the data source for a future per-chunk timeline / history panel and
// for ZBrush-style "copy this chunk's sculpt history onto another chunk".
// -----------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct DIGGER_API FDiggerHistory
{
    GENERATED_BODY()

    // All actions that have ever modified this chunk, oldest first.
    // This grows monotonically; it is NOT trimmed by undo/redo.
    // The undo stack is a separate concept — this is a full audit log.
    UPROPERTY()
    TArray<FDiggerChunkAction> Actions;

    // The index into Actions that represents the "current" position in the
    // timeline.  Undo moves it back; redo moves it forward.
    // -1 means "at the very start, before any actions".
    UPROPERTY()
    int32 HeadIndex = -1;

    void AppendAction(const FDiggerChunkAction& Action)
    {
        // Truncate any redo tail above HeadIndex
        if (HeadIndex < Actions.Num() - 1)
            Actions.SetNum(HeadIndex + 1);

        Actions.Add(Action);
        HeadIndex = Actions.Num() - 1;
    }

    bool CanUndo() const { return HeadIndex >= 0; }
    bool CanRedo() const { return HeadIndex < Actions.Num() - 1; }

    const FDiggerChunkAction* PeekUndo() const
    {
        if (!CanUndo()) return nullptr;
        return &Actions[HeadIndex];
    }

    const FDiggerChunkAction* PeekRedo() const
    {
        if (!CanRedo()) return nullptr;
        return &Actions[HeadIndex + 1];
    }

    // Total voxels ever authored across all actions (rough memory estimate helper)
    int32 TotalDeltaCount() const
    {
        int32 Total = 0;
        for (const FDiggerChunkAction& A : Actions) Total += A.Deltas.Num();
        return Total;
    }
};