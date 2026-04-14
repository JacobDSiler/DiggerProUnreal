// DiggerIslandRuntimeSubsystem.cpp — Layer 2 of the island detection architecture.

#include "DiggerIslandRuntimeSubsystem.h"
#include "DiggerManager.h"
#include "VoxelConversion.h"
#include "DiggerDebug.h"

// ---------------------------------------------------------------------------
// UWorldSubsystem interface
// ---------------------------------------------------------------------------

bool UDiggerIslandRuntimeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		const EWorldType::Type T = World->WorldType;
		return T == EWorldType::Editor
			|| T == EWorldType::PIE
			|| T == EWorldType::Game;
	}
	return false;
}

// ---------------------------------------------------------------------------
// Entry points
// ---------------------------------------------------------------------------

void UDiggerIslandRuntimeSubsystem::OnBrushStrokeCompleted(ADiggerManager* Manager, bool bIsUndoRedo)
{
	if (bIsUndoRedo || !IsValid(Manager))
	{
		return;
	}
	ScanIslands(Manager);
}

void UDiggerIslandRuntimeSubsystem::ScanIslands(ADiggerManager* Manager)
{
	if (!IsValid(Manager))
	{
		return;
	}

	// Step 1: Get raw island list from the manager (pure data, no side effects).
	TArray<FIslandData> Islands = Manager->DetectUnifiedIslands();

	// Step 2: Fragment filter — silently remove islands below the threshold.
	if (AutoCleanupMinVoxels > 0)
	{
		for (int32 i = Islands.Num() - 1; i >= 0; --i)
		{
			if (Islands[i].VoxelCount < AutoCleanupMinVoxels)
			{
				if (DiggerDebug::Islands())
				{
					UE_LOG(LogTemp, Log,
						TEXT("[IslandSubsystem] Fragment cleanup: %d voxels at %s (below threshold %d)"),
						Islands[i].VoxelCount, *Islands[i].Location.ToString(), AutoCleanupMinVoxels);
				}

				Manager->RemoveUnifiedIslandVoxels(Islands[i]);
				OnIslandFragmentCleaned.Broadcast(Islands[i]);
				Islands.RemoveAt(i);
			}
		}
	}

	// Step 3: Signal scan start (clears the editor island list).
	OnIslandScanStarted.Broadcast();

	// Step 4: Per-island processing.
	for (FIslandData& Island : Islands)
	{
		// Grounding check
		if (bCheckGrounding)
		{
			Island.bIsGrounded = IsIslandGrounded(Manager, Island);
		}

		// Broadcast to listeners (adds to editor island list).
		OnIslandDetected.Broadcast(Island);

		if (DiggerDebug::Islands())
		{
			UE_LOG(LogTemp, Log,
				TEXT("[IslandSubsystem] Island at %s — %d voxels, grounded=%s"),
				*Island.Location.ToString(), Island.VoxelCount,
				Island.bIsGrounded ? TEXT("true") : TEXT("false"));
		}

		// Apply float policy to confirmed floating islands.
		if (!Island.bIsGrounded)
		{
			HandleFloatingIsland(Manager, Island);
		}
	}
}

// ---------------------------------------------------------------------------
// Grounding check
// ---------------------------------------------------------------------------

bool UDiggerIslandRuntimeSubsystem::IsIslandGrounded(ADiggerManager* Manager, const FIslandData& Island) const
{
	for (const FVoxelInstance& Instance : Island.VoxelInstances)
	{
		const FVector VoxelWorld = FVoxelConversion::GlobalVoxelToWorld(Instance.GlobalVoxel);
		const TOptional<float> LandscapeZ = Manager->GetLandscapeHeightAt_Internal(VoxelWorld);

		if (!LandscapeZ.IsSet())
		{
			// No landscape at this position — treat as grounded (safe default).
			return true;
		}

		if (VoxelWorld.Z <= LandscapeZ.GetValue() + GroundingTolerance)
		{
			// This voxel touches or is below the landscape surface.
			return true;
		}
	}

	// Every voxel is above the terrain — confirmed floating.
	return false;
}

// ---------------------------------------------------------------------------
// Float policy dispatch
// ---------------------------------------------------------------------------

void UDiggerIslandRuntimeSubsystem::HandleFloatingIsland(ADiggerManager* Manager, FIslandData& Island)
{
	switch (FloatPolicy)
	{
	case EIslandFloatPolicy::Ignore:
		// No action.
		break;

	case EIslandFloatPolicy::AutoRemove:
		if (DiggerDebug::Islands())
		{
			UE_LOG(LogTemp, Log,
				TEXT("[IslandSubsystem] AutoRemove: erasing %d voxels at %s"),
				Island.VoxelCount, *Island.Location.ToString());
		}
		Manager->RemoveUnifiedIslandVoxels(Island);
		OnIslandHandled.Broadcast(Island);
		break;

	case EIslandFloatPolicy::ConvertToPhysics:
		if (DiggerDebug::Islands())
		{
			UE_LOG(LogTemp, Log,
				TEXT("[IslandSubsystem] ConvertToPhysics: spawning physics actor at %s"),
				*Island.Location.ToString());
		}
		Manager->ConvertIslandAtPositionToActor(Island.Location, /*bEnablePhysics=*/true, Island.ReferenceVoxel);
		OnIslandHandled.Broadcast(Island);
		break;

	case EIslandFloatPolicy::ConvertToStatic:
		if (DiggerDebug::Islands())
		{
			UE_LOG(LogTemp, Log,
				TEXT("[IslandSubsystem] ConvertToStatic: spawning static actor at %s"),
				*Island.Location.ToString());
		}
		Manager->ConvertIslandAtPositionToActor(Island.Location, /*bEnablePhysics=*/false, Island.ReferenceVoxel);
		OnIslandHandled.Broadcast(Island);
		break;
	}
}
