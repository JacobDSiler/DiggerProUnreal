# Digger — Claude Code Context

## Project Overview

**Digger** is a UE5.2 editor plugin and runtime system for real-time landscape sculpting using a sparse voxel grid + Marching Cubes mesh fused seamlessly with UE's `ALandscapeProxy`. It ships as two UE modules:

| Module | Type | Key files |
|--------|------|-----------|
| `Digger` | Runtime | `DiggerManager`, `VoxelChunk`, `MarchingCubes`, `SparseVoxelGrid`, `VoxelBrushShape`, `DynamicHole`, `IslandActor`, `DynamicLightActor`, `DiggerIslandRuntimeSubsystem` |
| `DiggerEditor` | Editor-only | `DiggerEdMode`, `DiggerEdModeToolkit`, `DiggerEditorSettings`, `DiggerEditorEventHub` |

All output files live in `Plugins/Digger/Source/`.

---

## Core Architecture

### SDF Convention
- **Positive SDF = AIR** (above landscape surface or inside a dug cavity)
- **Negative SDF = SOLID** (below landscape surface or inside added material)
- Zero crossing = isosurface (the visible mesh)
- Implicit baseline: `SDF = (worldZ - landscapeHeight) / voxelSize` for unset voxels
- A `kSubSurfaceBias = 0.35f` voxel-unit negative offset is applied near the surface to keep the implicit isosurface just below the landscape mesh (prevents poke-through)

### Grid / Chunk Sizes
```
ChunkSize    = 32   (voxel grid squares per chunk axis, UPROPERTY on ADiggerManager)
Subdivisions = 4    (subdivisions per TerrainGridSize unit)
TerrainGridSize = 100 cm  (1 UU = 1 cm in UE)
LocalVoxelSize  = TerrainGridSize / Subdivisions = 25 cm
N = ChunkSize * Subdivisions = 128 voxels per chunk axis
Pad = 2  (neighbour overlap for seam-correct MC stencil)
```
`FVoxelConversion` (static helper) centralises all world↔chunk↔voxel coordinate conversions.

### Mesh Pipeline per Chunk
```
ApplyBrushStroke()          — game thread, writes SDF deltas to SparseVoxelGrid
  → MarkDirtyWithBounds()   — adds chunk to DirtyChunkCoords + CollisionDirtyCoords
    → ProcessDirtyChunks()  — called every Tick (60 fps), mesh-only pass
      → UpdateIfDirty()
        → GenerateMesh()    — async: dispatches MC to AnyBackgroundThread
          → UpdateMeshFromData()  — game thread callback, calls CreateMeshSection
    → ProcessDirtyChunksLoop()  — 0.1s timer, mesh + idle collision cook
      → RebuildCollisionIfNeeded(bIdleFullRebuild=true)
```

### Section Indices
- Each `UVoxelChunk` gets a unique `SectionIndex` from `GDiggerGlobalChunkID` (file-scope static in `VoxelChunk.cpp`)
- Collision-only section = `SectionIndex + 10000` (invisible, physics-only)
- `UVoxelChunk::ResetGlobalChunkID()` resets to 0 — called inside `ClearAllVoxelData()` only

### VoxelAABB Rules (GenerateMesh)
Three rules determine how much of a chunk the MC loop processes:

- **Rule A** (chunk has own authored voxels): tight AABB around **air voxels only** (SDF > 0). Solid voxels are excluded to prevent AABB depth from growing with accumulated history. Expanded `Pad` voxels in XY, ±1 voxel in Z.
- **Rule B** (seam-helper, no own voxels): thin face-slab of `Pad` voxels on each face touching a neighbour with data.
- **Rule C** (`bBakeFullChunkVolume = true`): invalid `FBox` sentinel = full chunk, used for baking.

---

## Key Classes

### `ADiggerManager` (Runtime, `DiggerManager.h/.cpp`)
The central actor. Owns:
- `ProceduralMeshComponent* ProceduralMesh` — all chunk sections rendered here
- `TMap<FIntVector, UVoxelChunk*> ChunkMap` — all loaded chunks
- `TSet<FIntVector> DirtyChunkCoords` — O(dirty) mesh rebuild set
- `TSet<FIntVector> CollisionDirtyCoords` — O(dirty) collision cook set
- `TArray<FIntVector> PendingChunksToLoad / PendingMeshBuild / PendingCollisionBuild` — 3-phase streaming queues
- `FOnLoadProgressUpdated OnLoadProgressUpdated` — **public** multicast delegate; broadcasts `(float Pct 0..1/-1=cancel, int32 ChunkCount)` during streaming. Listened to by `DiggerEdMode` which owns the Slate notification (Slate is not linked to the runtime module).
- `TOptional<float> GetLandscapeHeightAt_Internal(FVector)` — **public** — used by `UDiggerIslandRuntimeSubsystem` for grounding checks. Returns `TOptional<float>` (empty = no landscape).
- `TArray<FIslandData> DetectUnifiedIslands()` — **public** — returns full island list with all physical voxel instances. Called by `UDiggerIslandRuntimeSubsystem::OnBrushStrokeCompleted`.
- `void RemoveUnifiedIslandVoxels(const FIslandData&)` — **public** — used by the subsystem for AutoRemove policy and fragment cleanup.

**Important lifecycle:**
- `BeginPlay` → `ClearAllVoxelData()` + `LoadAllChunks("Default")` (PIE always starts clean from disk)
- `EndPlay(EndPlayInEditor)` → does NOT clear; rebuilds mesh for all live chunks 0.3s deferred
- `EndPlay(Destroyed/LevelTransition)` → `ClearAllVoxelData()`
- `PostRegisterAllComponents` + `PostEditMove` → `EditorDeferredInit()` (guarded by `bEditorInitDone`)
- `EditorDeferredInit()` → 0.5s deferred `LoadAllChunks("Default")` + `SpawnOrUpdateBedrockFloor()`
- `OnPreSaveWorld` (Ctrl+S) → `SaveAllChunks()` — bound via `AddWeakLambda` in `PostRegisterAllComponents`

**Undo/Redo system:**
- `BeginStroke(Label)` — opens a pending action group
- `RecordChunkAction(FDiggerChunkAction)` — accumulates chunk deltas into `PendingAction`
- `CommitPendingAction()` — pushes to `UndoStack`, clears `RedoStack`
- `Undo()` / `Redo()` — replay via `ApplyChunkAction(bApplyNew=false/true)` on each chunk
- `bHasPendingAction` — guards self-commit in `ApplyBrushInEditor` (only commits if no open stroke)
- `ApplyBrushInEditor` calls `CommitPendingAction()` only when `!bHasPendingAction` (standalone call, not a continuous painting stroke)

**What NOT to put back on `ADiggerManager`:**
The old `WITH_EDITOR` island delegate block (`OnIslandsDetectionStarted`, `OnIslandDetected`, `BroadcastIslandDetected`) has been removed. All island event routing now flows through `UDiggerIslandRuntimeSubsystem` → `UDiggerEditorEventHub`. Do not re-add direct island delegates to this class.

### `UVoxelChunk` (Runtime, `VoxelChunk.h/.cpp`)
`UObject` (not `AActor`) — no engine Tick. Manually ticked by `ADiggerManager::ProcessDirtyChunks`.
- `USparseVoxelGrid* SparseVoxelGrid`
- `UMarchingCubes* MarchingCubesGenerator`
- `TArray<TWeakObjectPtr<ADynamicHole>> SpawnedHoleInstances`
- `TArray<FSpawnedHoleData> HoleDataArray` — serialised to binary save file
- `int32 SectionIndex` — assigned at construction from `GDiggerGlobalChunkID`
- Constructor branches on `HasAnyFlags(RF_ClassDefaultObject)`: CDO uses `CreateDefaultSubobject`, runtime uses `NewObject`
- `FBox DirtyWorldBounds` — accumulated world-space AABB of in-flight stroke. Consumed and reset by `GenerateMesh()` / `GenerateMeshSyncronous()`. Union-accumulated across rapid strokes so none are lost.
- `MarkDirtyWithBounds(FBox)` — unions incoming bounds into `DirtyWorldBounds`, then calls `MarkDirty()`. Pass `FBox(ForceInit)` for a full remesh.
- `ApplyChunkAction(FDiggerChunkAction, bApplyNew, bRecordForUndo)` — undo/redo primitive. Restores old or new SDF values, invalidates density cache, calls `ForceUpdate()`.

**Async mesh path (normal gameplay):**
`GenerateMesh()` snapshots `DirtyWorldBounds`, checks `IsHeightCacheValid` and calls `CacheHeightMap` if stale, snapshots neighbour voxel data (26-neighbour merge), then dispatches `GenerateMeshFromGrid(..., BoundsSnapshot)` to `AnyBackgroundThreadNormalTask`. Result posted back to game thread via `UpdateMeshFromData`.

### `UMarchingCubes` (Runtime, `MarchingCubes.h/.cpp`)
- `CacheHeightMap(Origin, VoxelSize, N)` — synchronous landscape height sampling, stores result in `CachedHeightValues`. Call before async dispatch. Uses `Pad=2` extended grid.
- `IsHeightCacheValid(Origin, VoxelSize)` — returns true when `bHeightCacheInitialized && CachedChunkOrigin.Equals(Origin, 1.0f)`.
- `GetCachedHeightValues()` — returns the flat `TArray<float>` cached by `CacheHeightMap`.
- `GenerateMeshFromGrid(..., FBox BoundsHint)` — AABB-aware overload. `BoundsHint` is forwarded to the MC traversal loop to restrict work to the dirty region. Currently delegates to full remesh; partial-update warm-start from `CachedDensityGrid` is the next planned optimisation.
- `InvalidateDensityCache()` — call after any direct voxel write that bypasses the brush path (e.g. Undo/Redo).
- Step 1 fills density grid from landscape baseline + voxel overlay
- Step 1c does Laplacian smoothing on the density grid before marching

### `FDiggerEdMode` (Editor, `DiggerEdMode.h/.cpp`)
- `GetMouseWorldHit()` — the main raycast. Uses `SmartTrace` first, falls back to landscape.
  - **Fallback behaviour**: when `bFallbackToLandscapeWhenNoMeshHit = true` (default), always accepts the landscape hit. When suppressed (ray through tunnel into sky), redirects to `GetLandscapeHeightAt(P.XY)` synthetic hit rather than hiding the brush.
- `Tick()` — calls `UpdatePreviewAtCursor()` + continuous paint + scroll momentum + camera focus animation
- `bCameraFollowsBrush` — gates `CaptureMouse(true)` in Tick; toggled by **L key**
- `OnDiggerLoadProgress(float, int32)` — handles `OnLoadProgressUpdated` delegate; owns the `TWeakPtr<SNotificationItem> LoadProgressNotification`
- `FocusCameraOnBrushPreview()` — **F key** (when nothing selected). Smoothly animates the viewport camera to an unobstructed viewpoint looking at the current brush preview center. See **Focus-on-Preview (F key)** section below for full design details.

### `SmartTrace` (`VoxelBrushShape.cpp`)
**CLOSED CANON — never modify.** Recursive trace through holes. Key behaviour:
- Hits landscape → if inside a hole volume, jumps past landscape by `LandscapeThickness / cos(theta)` where theta = ray angle from down-vector (variable jump, clamped 5–90 cm)
- Hits hole BP actor → ignores and continues
- Hits voxel mesh / other → returns hit
- `bStartInHole` + `bHitInHole` flags control skip logic

---

## Island Detection Refactor (Three-Layer Architecture)

This is a major in-progress refactor. The old approach routed island events directly through `ADiggerManager` WITH_EDITOR delegates. The new architecture has three clean layers.

### Layer 1 — `ADiggerManager` (runtime, unchanged role)
- `DetectUnifiedIslands()` — pure data function. Scans all chunks, builds the deduplicated island list with all physical voxel instances. **Does not broadcast anything.** Returns `TArray<FIslandData>`.
- `RemoveUnifiedIslandVoxels(island)` — removes all voxels belonging to an island.
- `GetLandscapeHeightAt_Internal(worldPos)` — returns `TOptional<float>` for grounding checks.
- `ApplyBrushInEditor(bDig)` — after `ApplyBrushToAllChunks`, dispatches to the subsystem:
  ```cpp
  if (UWorld* W = GetWorld())
      if (auto* IslandSys = W->GetSubsystem<UDiggerIslandRuntimeSubsystem>())
          IslandSys->OnBrushStrokeCompleted(this, /*bIsUndoRedo=*/false);
  ```
  Wrapped in `#if WITH_EDITOR`. Also calls `CommitPendingAction()` when `!bHasPendingAction`.

### Layer 2 — `UDiggerIslandRuntimeSubsystem` (Runtime module, `Digger/`)
**File:** `DiggerIslandRuntimeSubsystem.h/.cpp`
**Base:** `UWorldSubsystem`
**Module:** `Digger` (runtime — safe in packaged builds)

Owns the full island pipeline:
1. Calls `DetectUnifiedIslands()` on the manager.
2. Silently removes fragments below `AutoCleanupMinVoxels` (fires `OnIslandFragmentCleaned`).
3. Checks each island against the landscape via `IsIslandGrounded()` (sets `FIslandData::bIsGrounded`).
4. Applies `FloatPolicy` to confirmed floating islands.
5. Fires runtime delegates consumed by Layer 3.

**Config properties** (saved to Game ini):
| Property | Type | Default | Meaning |
|----------|------|---------|---------|
| `FloatPolicy` | `EIslandFloatPolicy` | `Ignore` | What to do with floating islands |
| `AutoCleanupMinVoxels` | `int32` | `0` | Fragment threshold — 0 = disabled |
| `GroundingTolerance` | `float` | `50 cm` | Z-margin above landscape for grounding |
| `bCheckGrounding` | `bool` | `true` | Enable grounding check |

**Float policy enum** (`EIslandFloatPolicy`):
- `Ignore` — leave floating islands in the voxel grid
- `AutoRemove` — silently erase from the grid
- `ConvertToPhysics` — spawn `AIslandActor` with physics, erase voxels
- `ConvertToStatic` — spawn `AIslandActor` as static, erase voxels

**Runtime delegates** (bound by Layer 3):
```cpp
FOnIslandScanStarted          OnIslandScanStarted;       // fires once per scan, before any islands
FOnIslandDetected_Runtime     OnIslandDetected;          // fires per island (post fragment filter)
FOnIslandHandled_Runtime      OnIslandHandled;           // fires when float policy acted
FOnIslandFragmentCleaned_Runtime OnIslandFragmentCleaned; // fires for silently removed fragments
```

**Entry points:**
- `OnBrushStrokeCompleted(Manager, bIsUndoRedo)` — called by `ApplyBrushInEditor`. No-op when `bIsUndoRedo=true`.
- `ScanIslands(Manager)` — force scan, fires all delegates. Called by the "Detect Islands" button in the toolkit.

**`ShouldCreateSubsystem`:** Only creates for `Editor`, `PIE`, `Game` world types. Not created for preview or temp worlds.

**Grounding check logic** (`IsIslandGrounded`):
```
for each VoxelInstance in Island.VoxelInstances:
    VoxelWorld = FVoxelConversion::GlobalVoxelToWorld(Instance.GlobalVoxel)
    LandscapeZ = Manager->GetLandscapeHeightAt_Internal(VoxelWorld)  // TOptional
    if !LandscapeZ.IsSet()  →  return true  (safe default, no landscape)
    if VoxelWorld.Z <= LandscapeZ + GroundingTolerance  →  return true
return false  (every voxel is above terrain — confirmed floating)
```

### Layer 3 — `UDiggerEditorEventHub` (DiggerEditor module, editor-only)
**File:** `Core/DiggerEditorEventHub.h/.cpp`
**Base:** `UEditorSubsystem`
**Module:** `DiggerEditor` (editor-only — never included from runtime code)

Bridges the runtime pipeline to Slate UI. Binds to `UDiggerIslandRuntimeSubsystem` delegates, re-fires them as editor-safe delegates (always game-thread, guaranteed Slate-safe via `AsyncTask(GameThread,...)` if needed).

**Connection API:**
```cpp
void ConnectToManager(ADiggerManager*)              // bind manager-level events
void ConnectToIslandSubsystem(UDiggerIslandRuntimeSubsystem*)  // bind island pipeline
void DisconnectAll()                                // unbind everything
void BroadcastMessage(FString)                      // general status message
```

**Editor delegates** (bound by `FDiggerEdModeToolkit`):
```cpp
FOnDiggerMessage               OnDiggerMessage;
FOnIslandScanStartedEditor     OnIslandScanStartedEditor;   // → ClearIslands()
FOnIslandDetectedEditor        OnIslandDetectedEditor;      // → AddIsland()
FOnIslandHandledEditor         OnIslandHandledEditor;
FOnIslandFragmentCleanedEditor OnIslandFragmentCleanedEditor;
```

Both `ConnectToManager` and `ConnectToIslandSubsystem` unbind stale handles before rebinding — safe to call repeatedly from `OnManagerRespawned`.

### `FDiggerEdModeToolkit` changes for the island refactor

**Destructor:**
```cpp
DisconnectIslandHub();  // replaces old Manager->OnIslandDetected.RemoveAll(this)
```

**`Init()`:**
```cpp
Manager = GetDiggerManager();
ConnectIslandHub();  // replaces old BindIslandDelegates()
```

**`OnManagerRespawned()`:**
```cpp
ConnectIslandHub();  // replaces old BindIslandDelegates()
```

**New methods (replace `BindIslandDelegates`):**
```cpp
void ConnectIslandHub();    // gets Hub and IslandSys, connects both, binds editor delegates
void DisconnectIslandHub(); // removes Handle_HubScanStarted and Handle_HubIslandDetected
```

**`ConnectIslandHub` implementation pattern:**
```cpp
void FDiggerEdModeToolkit::ConnectIslandHub()
{
    auto* Hub = GEditor->GetEditorSubsystem<UDiggerEditorEventHub>();
    if (!Hub) return;

    // Disconnect stale handles before re-binding
    DisconnectIslandHub();

    // Connect the hub to the current manager and world subsystem
    Hub->ConnectToManager(Manager);
    if (Manager)
        if (UWorld* W = Manager->GetWorld())
            Hub->ConnectToIslandSubsystem(W->GetSubsystem<UDiggerIslandRuntimeSubsystem>());

    // Bind toolkit UI callbacks to hub's editor-side delegates
    Handle_HubScanStarted  = Hub->OnIslandScanStartedEditor.AddSP(
        this, &FDiggerEdModeToolkit::ClearIslands);
    Handle_HubIslandDetected = Hub->OnIslandDetectedEditor.AddSP(
        this, &FDiggerEdModeToolkit::AddIsland);
}

void FDiggerEdModeToolkit::DisconnectIslandHub()
{
    auto* Hub = GEditor->GetEditorSubsystem<UDiggerEditorEventHub>();
    if (!Hub) return;
    if (Handle_HubScanStarted.IsValid())
    {
        Hub->OnIslandScanStartedEditor.Remove(Handle_HubScanStarted);
        Handle_HubScanStarted.Reset();
    }
    if (Handle_HubIslandDetected.IsValid())
    {
        Hub->OnIslandDetectedEditor.Remove(Handle_HubIslandDetected);
        Handle_HubIslandDetected.Reset();
    }
}
```

**`MakeIslandsSection()` additions:** Add a config row above the island grid with:
- A "Detect Islands" button that calls `IslandSys->ScanIslands(Manager)` directly (for manual scans)
- A numeric spinner for `IslandSys->AutoCleanupMinVoxels` (label: "Min Voxels (auto-clean)")
- A combo box for `IslandSys->FloatPolicy` (enum: Ignore / Auto Remove / To Physics / To Static)

### `FIslandData` struct additions
Added to `DiggerManager.h` (the struct definition):
```cpp
// Set by UDiggerIslandRuntimeSubsystem during grounding check.
// Default true = safe/unprocessed. False = confirmed floating.
UPROPERTY(BlueprintReadWrite)
bool bIsGrounded = true;
```
Also added to the constructor initialiser list: `, bIsGrounded(true)`.

### Removed from `ADiggerManager`
The following `WITH_EDITOR` block has been deleted from `DiggerManager.h`:
```cpp
// DELETED — do not re-add
DECLARE_MULTICAST_DELEGATE(FIslandsDetectionStartedEvent);
FIslandsDetectionStartedEvent OnIslandsDetectionStarted;
DECLARE_MULTICAST_DELEGATE_OneParam(FIslandDetectedEvent, const FIslandData&);
FIslandDetectedEvent OnIslandDetected;
void BroadcastIslandDetected(const FIslandData& Island) { ... }
```

The old Step 5 broadcast loop inside `DetectUnifiedIslands()` has also been removed. `DetectUnifiedIslands` now simply builds and returns `FinalIslands` — all broadcasting is the subsystem's responsibility.

### Event flow summary
```
Brush stroke ends
  → ADiggerManager::ApplyBrushInEditor()
      → ApplyBrushToAllChunks()
      → CommitPendingAction() [if !bHasPendingAction]
      → UDiggerIslandRuntimeSubsystem::OnBrushStrokeCompleted(Manager, bIsUndoRedo=false)
          → DetectUnifiedIslands()          [returns raw island list]
          → Fragment filter loop            [fires OnIslandFragmentCleaned, removes voxels]
          → OnIslandScanStarted.Broadcast() [clears editor list]
          → Per-island:
              → IsIslandGrounded()          [sets bIsGrounded]
              → OnIslandDetected.Broadcast()  [adds to editor list]
              → HandleFloatingIsland()      [AutoRemove / ConvertToPhysics / ConvertToStatic]
                  → OnIslandHandled.Broadcast()
          ↓
  → UDiggerEditorEventHub (bound via AddUObject)
      → OnIslandScanStartedEditor.Broadcast()  → FDiggerEdModeToolkit::ClearIslands()
      → OnIslandDetectedEditor.Broadcast()     → FDiggerEdModeToolkit::AddIsland()

Undo / Redo:
  → ADiggerManager::Undo() / Redo()
      → ApplyChunkAction(bApplyNew=false/true)  [restores voxel state]
      → Does NOT call OnBrushStrokeCompleted    [no new islands created]
```

---

## FastDebugRenderer Upgrade

**File:** `Utils/FastDebugRenderer.h/.cpp`

The old `DrawBoxLinesBatch` called `DrawDebugLine()` in a loop — no actual GPU batching. The new implementation uses `ULineBatchComponent` for true single-call GPU submission.

**Key API:**
```cpp
// Draw N axis-aligned boxes in one GPU call. Pass FRotator::ZeroRotator for grid-aligned voxels.
void DrawBoxesRotated(UWorld*, TArray<FVector> Centers, FVector HalfExtent,
                      FLinearColor, float Duration, FRotator Rotation);

// Internal flush — only places that touch ULineBatchComponent
void FlushLines(UWorld*);   // builds TArray<FBatchedLine>, calls LBC->DrawLines() once
void FlushPoints(UWorld*);  // same for points
```

**Performance:** 2000 voxels = 24,000 line segments = 1 GPU call (was 24,000 `DrawDebugLine` calls).

`UFastDebugSubsystem` remains a `UWorldSubsystem`. `UFastDebugRenderer` remains a `UCLASS`. `SparseVoxelGrid::RenderVoxels()` already uses `UFastDebugSubsystem::Get()` — no changes needed there.

---

## Save / Load System

**File format** (binary, per-chunk):
1. Voxel data (`USparseVoxelGrid::SerializeToArchive`)
2. Holes: `[MagicSentinel=-0x444947] [FileVersion] [HoleCount]` then per hole: `Location, Rotation, Scale, ShapeByte, HoleUID`
3. Lights: `[LightCount]` then per light: `FVector, FLinearColor, float intensity`

**Save path**: `FPaths::ProjectSavedDir() / "DiggerChunks" / SaveFileName / ChunkCoords.bin"`

**Save triggers**: `OnPreSaveWorld` (Ctrl+S in editor) → `SaveAllChunks()`

**Load triggers**:
- Editor startup: `EditorDeferredInit()` → 0.5s timer → `LoadAllChunks("Default")`
- PIE start: `BeginPlay()` → 0.5s timer → `LoadAllChunks("Default")`
- After PIE ends: 0.3s timer → `MarkDirty()` on all live chunks (data already in memory)

---

## Bedrock Floor

An invisible `UBoxComponent` (ECC_Visibility, QueryOnly) spawned by `SpawnOrUpdateBedrockFloor()`:
- Z position: `lowestLandscapeZ - BedrockDepthBelowLandscape` (default 800 cm below)
- XY half-extent: `BedrockHalfExtentXY` (default 50,000 cm = 500 m)
- Gives SmartTrace a real blocking hit inside unmeshed tunnels → brush stays visible
- Stored as `AActor* BedrockFloorActor` (Transient, RF_Transient, hidden from outliner)
- Settings: `DiggerManager` Details panel → **Digger | Bedrock**

---

## Editor Settings (`UDiggerEditorSettings`)

Located: **Project Settings → Digger → Editor Preview Settings**

Key settings relevant to brush behaviour:

| Setting | Default | Effect |
|---------|---------|--------|
| `bFallbackToLandscapeWhenNoMeshHit` | `true` | Always snap brush to landscape when no voxel mesh hit. Turn off only for fully pre-baked worlds. |
| `TunnelSuppressionAngleDeg` | `60°` | Only active when fallback=false. Rays flatter than this over air voxels are suppressed. |
| `bCameraFollowsBrush` | `true` | Viewport tracks brush during painting. L key toggles. |
| `BedrockDepthBelowLandscape` | `800 cm` | How far below landscape the bedrock collision sits. |
| `bSmoothBrushLandscapeAware` | `false` | Smooth brush respects landscape height boundary. |
| `bSnapPreviewToGrid` | `true` | Brush preview snaps to landscape surface. |

---

## Toolkit UI (`FDiggerEdModeToolkit`)

**Quick Toggles strip** (always visible, top of panel):
- **Camera [L]** — `bCameraFollowsBrush`, persisted to settings
- **Landscape Fallback** — `bFallbackToLandscapeWhenNoMeshHit`, persisted to settings

**Expandable sections** (in order):
1. Brush Tools (shape, parameters, rotation, offset, custom brush)
2. Environment (worklight, brush preview light)
3. Additional Tools (hole optimisation, debug flags)

Both quick-toggle checkboxes read live from `UDiggerEditorSettings::Get()` and write via `GetMutableDefault<>()->SaveConfig()` on change.

---

## Focus-on-Preview (F key)

Pressing **F** with nothing selected and Digger EdMode active smoothly moves the editor camera to a clear viewpoint looking at the current brush preview center. This is a pure editor UX feature — no runtime code is involved.

### Trigger
- Handled in `FDiggerInputProcessor::HandleKeyDownEvent` (the global Slate input processor, same as R/O/P/C/X/Y/Z).
- Only fires when `GEditor->GetSelectedActors()->Num() == 0`. If anything is selected, the key falls through to UE's built-in frame-selection (`return false`).
- `IE_Repeat` is suppressed — holding F does nothing.

### Animation
Smooth camera interpolation is driven by `FDiggerEdMode::Tick()` (section 5b), **not** by any timer or async task. State lives entirely in `FDiggerEdMode` private members:

| Member | Type | Purpose |
|--------|------|---------|
| `bFocusAnimActive` | `bool` | Gates the Tick animation block |
| `FocusAnimAlpha` | `float` | 0→1 progress over `FocusAnimDuration` |
| `FocusAnimDuration` | `float` | 0.35s — snappy but trackable |
| `FocusAnimStartLocation/Rotation` | `FVector/FRotator` | Captured at F-press time |
| `FocusTargetLocation/Rotation` | `FVector/FRotator` | Computed by `FocusCameraOnBrushPreview()` |

Tick uses a **smooth-step curve** (`S = 3t²−2t³`) for ease-in/out feel. `bFocusAnimActive` is cleared on completion, on `StartTracking`, and on `StartContinuousApplication` so painting never fights the animation.

### `FocusCameraOnBrushPreview()` algorithm

1. **Target**: `BrushCache.CachedBrushPreviewCenter` (kept live by `UpdatePreviewAtCursor`).
2. **Viewport**: iterates `GEditor->GetAllViewportClients()`, picks first perspective + visible client.
3. **Ideal distance**: `max(BrushRadius, 50) × 2.5` — frames the full brush with padding.
4. **Landscape probe**: vertical line trace + 4-point ring fallback at 40% radius (same ring logic used throughout the codebase). Records `LandscapeZ` and `bSolidLandscape`.
   - `bSolidLandscape = true` → landscape mesh found; camera must stay above `LandscapeZ + 50cm`.
   - `bSolidLandscape = false` → probe fell through an open hole; underground camera positions are permitted.
5. **Candidate generation**: 5 discrete elevation rings sampled in azimuth using a Fibonacci-offset spread:

   | Elevation | Samples | BandScore | Rationale |
   |-----------|---------|-----------|-----------|
   | 25° | 12 | +4 | Primary — natural digging perspective |
   | 40° | 10 | +2 | Secondary — slightly steeper |
   | 15° | 8 | +1 | Shallow — lower priority |
   | 55° | 8 | 0 | Steep — fallback |
   | 70° | 6 | −2 | Very steep — last resort |

   Plus the **current camera direction** as an extra candidate with `BaseScore = +20` (dominant — strongly prefer staying put if already valid).

6. **Scoring** (additive, higher = better):
   - `+10` clear sightline (unblocked sweep)
   - `+0..4` `BandScore` from table above
   - `+20` "stay put" bonus (current camera direction candidate only)
   - `±1` direction similarity to current camera (minimise movement)
   - `0 to −4` zenith penalty: linear ramp from 0 at Z=0.75 to −4 at Z=1.0, discourages near-straight-down positions
   - Partially-blocked candidates (>60% clear distance): no clear bonus, scaled by `(ClearDist/IdealDist) × 5`

7. **Landscape floor rejection**: any candidate whose position would be below `MinCamZ` is skipped before the sweep. Adjusted (pulled-back) positions are also checked.

8. **Rotation**: **yaw is preserved exactly** from `ViewportClient->GetViewRotation().Yaw` at F-press time. Only pitch is recomputed geometrically (`atan2(-ΔZ, horizDist)`), then **clamped to [−45°, −30°]** (UE convention: negative = looking down). Roll is always 0. This matches UE's own frame-selection behaviour — pressing F never spins the artist.

9. **Fallback** (no candidate passed): camera placed directly above target at `IdealDist`, pitch −80°, `PreFocusYaw`.

10. **Safety clamp**: belt-and-suspenders final check — if `BestCamPos.Z < MinCamZ`, clamp Z and recompute rotation.

### What NOT to change
- The `MakeYawPreservingRot` lambda captures `PreFocusYaw` once at the top of candidate scoring. Do not move it or recompute yaw per-candidate.
- The pitch clamp `[-45°, -30°]` is intentional UX. Do not remove it or widen it to allow straight-down results.
- The `+20` "stay put" bonus must remain higher than the `+10` clear-sightline bonus so re-pressing F from a valid position is nearly a no-op.
- `bFocusAnimActive = false` cancels in `StartTracking` and `StartContinuousApplication` — keep both cancel sites.

---

## ApplyBrushStroke Performance Notes

`UVoxelChunk::ApplyBrushStroke` runs on the **game thread**. Key optimisations in place:

1. **Steps 2+3 use flat `TArray<float>`** instead of `TMap` — contiguous memory, no per-element hashing. SDFSnapshot indexed by `(X-SnapX0) + SnapSX*(Y-SnapY0) + SnapSX*SnapSY*(Z-SnapZ0)`.
2. **Column heights cached once per (X,Y)** and reused across all Z in that column.
3. **Laplacian: 1 pass** (was 3). MC Step 1c handles additional smoothing at render time.
4. **`Modify()` gated** behind `GIsTransacting` — not called on every stroke tick.
5. **`RoutingPadding = BrushRadius + 1×VoxelSize`** (was +3×) — fewer chunks created per stroke.
6. **`DirtyChunkCoords` iteration uses `TInlineAllocator<8>`** — avoids heap allocation for the common 1-3 chunk case.
7. **Height cache per-chunk** — `MarchingCubesGenerator->CacheHeightMap()` is called once; `IsHeightCacheValid()` guards re-capture. Saves (N+1)² landscape queries per remesh.

---

## Collision System

- Cook only fires **when idle** (`bIsEditorPainting = false`) via `ProcessDirtyChunksLoop` (0.1s timer)
- Cook is **O(dirty)** — only iterates `CollisionDirtyCoords`, not full `ChunkMap`
- `MarkDirtyWithBounds()` and `RequestImmediateCollisionRebuild()` both register into `CollisionDirtyCoords`
- `bNeedsFullCollisionRebuild` flag cleared only after successful idle cook
- Section: `SectionIndex + 10000`, `ECC_Visibility` blocked, `CreateMeshSection(bCreateCollision=true)`

---

## Hole System

- **`ADynamicHole`** — pool-managed actors. Pool size pre-warmed in `EditorDeferredInit` / `BeginPlay`.
- Pool actors are hidden + teleported to `(0, 0, -9,999,999)` when idle.
- `IsInsideHole(FVector)` — used by SmartTrace and fallback suppression.
- `SpawnHoleFromData()` calls `SpawnedHoleInstances.AddUnique()` to register.
- `SaveChunkData()` skips hidden actors and sentinel Z positions.
- `DiggerManager->EnsureHoleShapeLibrary()` must be called before `PrepareShapeData()`.

---

## Known Constraints / Rules

1. **SmartTrace is CLOSED CANON** — `VoxelBrushShape.cpp::SmartTrace` and `RecursiveTraceThroughHoles_Internal` must never be modified without explicit instruction.
2. **`UVoxelChunk` is `UObject`, not `AActor`** — no engine Tick; no `GetWorld()` without going through `DiggerManager`.
3. **`CreateDefaultSubobject` only in CDO constructor** — runtime chunks use `NewObject<>()`. Branch on `HasAnyFlags(RF_ClassDefaultObject)`.
4. **No `FSlateNotificationManager` in the runtime `Digger` module** — Slate is only linked in `DiggerEditor`. Use `OnLoadProgressUpdated` delegate to communicate progress to the editor module.
5. **`ClearAllVoxelData()` resets `GDiggerGlobalChunkID`** — must not be called outside of genuine teardown. `EndPlay(EndPlayInEditor)` does NOT call it.
6. **`bIsEditorPainting`** — must be `true` during continuous strokes and `false` on mouse-up. Guards collision cook and `CaptureMouse`.
7. **No island delegates on `ADiggerManager`** — `OnIslandsDetectionStarted`, `OnIslandDetected`, and `BroadcastIslandDetected` have been permanently removed. All island events route through `UDiggerIslandRuntimeSubsystem` → `UDiggerEditorEventHub`. Do not re-add them.
8. **`DiggerIslandRuntimeSubsystem.h` is runtime** — it lives in the `Digger` module and must not include any editor headers. The `WITH_EDITOR` include of this file in `DiggerManager.cpp` is intentional and correct.
9. **`DiggerEditorEventHub.h` is editor-only** — lives in `DiggerEditor` module. Must never be included by any file in the `Digger` (runtime) module.
10. **Undo/Redo does not trigger island detection** — `ApplyChunkAction` (the undo/redo primitive) restores voxel state but does NOT call `OnBrushStrokeCompleted`. The island list is only refreshed on genuine brush strokes or manual "Detect Islands" button press.

---

## File Checklist (all in `Plugins/Digger/Source/`)

### Runtime (`Digger/`)
| File | Status |
|------|--------|
| `VoxelChunk.h/.cpp` | Current — flat array SDF snapshot, air-voxel AABB, 1-pass Laplacian, async mesh with height cache + BoundsSnapshot |
| `MarchingCubes.h/.cpp` | Current — BoundsHint restriction, CacheHeightMap/IsHeightCacheValid, Step 1c Laplacian, ClipBias |
| `SparseVoxelGrid.h/.cpp` | Current |
| `DiggerManager.h/.cpp` | **Refactored** — island delegates removed, bIsGrounded on FIslandData, subsystem dispatch in ApplyBrushInEditor, broadcast loop stripped from DetectUnifiedIslands |
| `DiggerIslandRuntimeSubsystem.h/.cpp` | **New** — Layer 2 of island refactor, EIslandFloatPolicy, grounding check, fragment filter |
| `Utils/FastDebugRenderer.h/.cpp` | **Upgraded** — true GPU batching via ULineBatchComponent, DrawBoxesRotated |
| `VoxelBrushShape.cpp` | Current (user's version — CLOSED CANON) |
| `DynamicHole.h/.cpp` | Current |
| `DynamicLightActor.h/.cpp` | Current |
| `IslandActor.h/.cpp` | Current |
| `FSpawnedHoleData.h` | Current |
| `FSavedLightData.h` | Current |
| `SpawnedHoleDataVersion.h` | Current |
| `DiggerHistory.h` | Current |

### Editor (`DiggerEditor/`)
| File | Status |
|------|--------|
| `DiggerEdMode.h/.cpp` | Current — landscape fallback redirect, bedrock, L-key camera toggle, progress notification handler, F-key focus-on-preview |
| `DiggerEdModeToolkit.h` | **Refactored** — BindIslandDelegates removed, ConnectIslandHub/DisconnectIslandHub + FDelegateHandle members added, UDiggerEditorEventHub forward declared |
| `DiggerEdModeToolkit.cpp` | **Refactored** — DiggerEditorEventHub + DiggerIslandRuntimeSubsystem includes added; destructor, Init, OnManagerRespawned updated; BindIslandDelegates replaced with ConnectIslandHub/DisconnectIslandHub; MakeIslandsSection expanded with Detect button + config controls |
| `Core/DiggerEditorEventHub.h/.cpp` | **New** — Layer 3 of island refactor, UEditorSubsystem relay, game-thread-safe re-broadcast |
| `DiggerEditorSettings.h/.cpp` | Current — bFallbackToLandscapeWhenNoMeshHit, bCameraFollowsBrush, bedrock settings |

