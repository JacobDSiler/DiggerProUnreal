#include "DiggerBlueprintLibrary.h"

#include "DiggerManager.h"

#include "DiggerBlueprintLibrary.h"
#include "DiggerManager.h" // Required to see the class definition
#include "VoxelBrushShape.h" // Required to see the ActiveBrush definition

bool UDiggerBlueprintLibrary::DiggerSmartTrace(const UObject* WorldContextObject, FVector Start, FVector End, FHitResult& OutHit)
{
	// 1. Use our new elegant static accessor
	ADiggerManager* Mgr = ADiggerManager::FindDiggerManager(WorldContextObject);

	// 2. Validate Manager and Brush
	if (IsValid(Mgr))
	{
		// 3. Lazy Initialization (Robustness)
		// If the manager exists but hasn't made a brush yet (e.g. game just started),
		// we shouldn't fail. We should make a temporary one to do the math.
		if (!IsValid(Mgr->ActiveBrush))
		{
			// Create a transient brush just for this calculation
			Mgr->ActiveBrush = NewObject<UVoxelBrushShape>(Mgr, UVoxelBrushShape::StaticClass());
			Mgr->ActiveBrush->InitializeBrush(EVoxelBrushType::Sphere, 100.f, FVector::ZeroVector, Mgr);
		}

		// 4. Perform the Trace
		if (IsValid(Mgr->ActiveBrush))
		{
			FHitResult SmartHit = Mgr->ActiveBrush->SmartTrace(Start, End);
			if (SmartHit.bBlockingHit)
			{
				OutHit = SmartHit;
				return true;
			}
		}
	}
	else
	{
		// Optional logging
		// UE_LOG(LogTemp, Warning, TEXT("DiggerSmartTrace: No DiggerManager found in scene."));
	}
    
	// Fallback
	return false;
}
