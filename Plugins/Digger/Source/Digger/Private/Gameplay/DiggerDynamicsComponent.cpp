#include "DiggerDynamicsComponent.h"
#include "DynamicHole.h" // Your hole class

UDiggerDynamicsComponent::UDiggerDynamicsComponent(): UpdatedPrimitive(nullptr)
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDiggerDynamicsComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (!Owner) return;

	// 1. Find the Collision Component (Root)
	UpdatedPrimitive = Cast<UPrimitiveComponent>(Owner->GetRootComponent());
	if (!UpdatedPrimitive)
	{
		UE_LOG(LogTemp, Warning, TEXT("DiggerDynamicsComponent: Owner root is not a Primitive (Mesh/Capsule). Logic won't work."));
		return;
	}

	// 2. Remember what the user set originally (e.g., "Pawn" or "Vehicle")
	DefaultProfileName = UpdatedPrimitive->GetCollisionProfileName();

	// 3. Bind Overlaps
	UpdatedPrimitive->OnComponentBeginOverlap.AddDynamic(this, &UDiggerDynamicsComponent::OnOverlapBegin);
	UpdatedPrimitive->OnComponentEndOverlap.AddDynamic(this, &UDiggerDynamicsComponent::OnOverlapEnd);
}

void UDiggerDynamicsComponent::OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// Check if we hit a Hole Actor
	if (OtherActor && OtherActor->IsA(ADynamicHole::StaticClass()))
	{
		// Increment Counter
		HoleOverlapCount++;

		// Transition: 0 -> 1 means we just entered our first hole
		if (HoleOverlapCount == 1)
		{
			if (UpdatedPrimitive)
			{
				UpdatedPrimitive->SetCollisionProfileName(HoleProfileName);
				// UE_LOG(LogTemp, Log, TEXT("Entered Hole: Switched to %s"), *HoleProfileName.ToString());
			}
		}
	}
}

void UDiggerDynamicsComponent::OnOverlapEnd(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (OtherActor && OtherActor->IsA(ADynamicHole::StaticClass()))
	{
		// Decrement Counter
		HoleOverlapCount--;

		// Safety clamp (shouldn't happen, but good practice)
		if (HoleOverlapCount < 0) HoleOverlapCount = 0;

		// Transition: 1 -> 0 means we left the last hole
		if (HoleOverlapCount == 0)
		{
			if (UpdatedPrimitive)
			{
				UpdatedPrimitive->SetCollisionProfileName(DefaultProfileName);
				// UE_LOG(LogTemp, Log, TEXT("Exited Hole: Switched back to %s"), *DefaultProfileName.ToString());
			}
		}
	}
}