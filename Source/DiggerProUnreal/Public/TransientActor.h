// TransientActor.h
#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "TransientActor.generated.h"

UCLASS(BlueprintType, Blueprintable)
class DIGGERPROUNREAL_API ATransientActor : public AActor
{
	GENERATED_BODY()
    
public:    
	ATransientActor();
    
	// Call this to make the actor transient (only for runtime-spawned actors)
	UFUNCTION(BlueprintCallable)
	void MakeTransient();

protected:
	virtual void BeginPlay() override;
};
