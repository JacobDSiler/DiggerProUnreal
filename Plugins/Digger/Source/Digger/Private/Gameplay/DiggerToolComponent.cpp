#include "DiggerToolComponent.h"

UDiggerToolComponent::UDiggerToolComponent(): CachedManager(nullptr)
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDiggerToolComponent::EnsureManager()
{
	// Use your static helper!
	if (!CachedManager)
	{
		CachedManager = ADiggerManager::FindDiggerManager(this);
	}
}

bool UDiggerToolComponent::DigAtLocation(FVector Origin, FVector Direction)
{
	EnsureManager();
	if (!CachedManager) 
	{
		UE_LOG(LogTemp, Warning, TEXT("DiggerTool: No DiggerManager found!"));
		return false;
	}

	// Call the generic function we just made
	return CachedManager->PerformDig(
		Origin, 
		Direction, 
		TraceDistance, 
		BrushRadius, 
		BrushFalloff, 
		BrushType, 
		true // bIsDigging = true
	);
}

bool UDiggerToolComponent::AddAtLocation(FVector Origin, FVector Direction, bool bIsDigging)
{
	EnsureManager();
	if (!CachedManager) return false;

	return CachedManager->PerformDig(
		Origin, 
		Direction, 
		TraceDistance, 
		BrushRadius, 
		BrushFalloff, 
		BrushType, 
		bIsDigging
	);
}