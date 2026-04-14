// DiggerIslandRuntimeSubsystem.h — Layer 2 of the island detection architecture.
// Runtime module (Digger/) — safe in packaged builds. No editor headers allowed.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DiggerIslandRuntimeSubsystem.generated.h"

class ADiggerManager;
struct FIslandData;

/**
 * What to do with confirmed floating islands.
 */
UENUM(BlueprintType)
enum class EIslandFloatPolicy : uint8
{
	Ignore           UMETA(DisplayName = "Ignore"),
	AutoRemove       UMETA(DisplayName = "Auto Remove"),
	ConvertToPhysics UMETA(DisplayName = "Convert to Physics"),
	ConvertToStatic  UMETA(DisplayName = "Convert to Static")
};

// ---------------------------------------------------------------------------
// Delegate signatures
// ---------------------------------------------------------------------------
DECLARE_MULTICAST_DELEGATE(FOnIslandScanStarted);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnIslandDetected_Runtime, const FIslandData&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnIslandHandled_Runtime, const FIslandData&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnIslandFragmentCleaned_Runtime, const FIslandData&);

/**
 * Owns the full island-processing pipeline:
 *   1. Calls ADiggerManager::DetectUnifiedIslands() for raw data
 *   2. Filters fragments below AutoCleanupMinVoxels
 *   3. Grounding checks against the landscape
 *   4. Applies FloatPolicy to confirmed floating islands
 *   5. Broadcasts delegates consumed by UDiggerEditorEventHub (Layer 3)
 */
UCLASS(Config=Game)
class DIGGER_API UDiggerIslandRuntimeSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	// -----------------------------------------------------------------------
	// Configuration (saved to Game ini via Config specifier)
	// -----------------------------------------------------------------------

	/** What to do with floating islands after detection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Detection", Config)
	EIslandFloatPolicy FloatPolicy = EIslandFloatPolicy::Ignore;

	/** Islands with fewer voxels than this are silently removed (0 = disabled). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Detection", Config, meta = (ClampMin = "0"))
	int32 AutoCleanupMinVoxels = 0;

	/** Z-margin (cm) above landscape surface for grounding check. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Detection", Config, meta = (ClampMin = "0"))
	float GroundingTolerance = 50.0f;

	/** When true, each island is tested against the landscape for grounding. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Detection", Config)
	bool bCheckGrounding = true;

	// -----------------------------------------------------------------------
	// Runtime delegates (bound by UDiggerEditorEventHub in Layer 3)
	// -----------------------------------------------------------------------

	FOnIslandScanStarted                OnIslandScanStarted;
	FOnIslandDetected_Runtime           OnIslandDetected;
	FOnIslandHandled_Runtime            OnIslandHandled;
	FOnIslandFragmentCleaned_Runtime    OnIslandFragmentCleaned;

	// -----------------------------------------------------------------------
	// Entry points
	// -----------------------------------------------------------------------

	/**
	 * Called by ADiggerManager::ApplyBrushInEditor after a brush stroke.
	 * No-op when bIsUndoRedo is true (undo/redo does not trigger island detection).
	 */
	void OnBrushStrokeCompleted(ADiggerManager* Manager, bool bIsUndoRedo);

	/**
	 * Force a full island scan. Called by the "Detect Islands" toolkit button.
	 */
	void ScanIslands(ADiggerManager* Manager);

	// -----------------------------------------------------------------------
	// UWorldSubsystem interface
	// -----------------------------------------------------------------------

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

private:

	/**
	 * Returns true if at least one voxel in the island touches or is below
	 * the landscape surface (within GroundingTolerance).
	 */
	bool IsIslandGrounded(ADiggerManager* Manager, const FIslandData& Island) const;

	/**
	 * Applies FloatPolicy to a confirmed floating island.
	 */
	void HandleFloatingIsland(ADiggerManager* Manager, FIslandData& Island);
};
