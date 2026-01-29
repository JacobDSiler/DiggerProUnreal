#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DiggerDynamicsComponent.generated.h"

UCLASS( ClassGroup=(Digger), meta=(BlueprintSpawnableComponent) )
class DIGGER_API UDiggerDynamicsComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	UDiggerDynamicsComponent();

protected:
	virtual void BeginPlay() override;

	// The Primitive (Capsule/Mesh) we are modifying
	UPROPERTY()
	UPrimitiveComponent* UpdatedPrimitive;

	// The profile to use when NOT in a hole (auto-detected on start)
	UPROPERTY(VisibleAnywhere, Category="Digger Collision")
	FName DefaultProfileName;

	// The profile to use when INSIDE a hole
	UPROPERTY(EditAnywhere, Category="Digger Collision")
	FName HoleProfileName = FName("DiggerTraverser");

	// Counter for nested holes (overlapping two holes at once)
	int32 HoleOverlapCount = 0;

	UFUNCTION()
	void OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnOverlapEnd(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);
};