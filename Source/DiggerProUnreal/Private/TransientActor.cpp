#include "TransientActor.h"

ATransientActor::ATransientActor()
{
	PrimaryActorTick.bCanEverTick = false;
    
	// Keep it completely normal - no special behavior in constructor
	SetReplicates(false);
	SetCanBeDamaged(false);
}

void ATransientActor::BeginPlay()
{
	Super::BeginPlay();
	// No special behavior here either
}

void ATransientActor::MakeTransient()
{
	// Only call this method for runtime-spawned actors
	SetFlags(RF_Transient | RF_DuplicateTransient | RF_NonPIEDuplicateTransient);
	ClearFlags(RF_Transactional | RF_Public);
	bIsEditorPreviewActor = true;
    
	// Apply to components
	for (UActorComponent* Component : GetComponents())
	{
		if (Component)
		{
			Component->SetFlags(RF_Transient | RF_DuplicateTransient);
			Component->ClearFlags(RF_Transactional | RF_Public);
		}
	}
}