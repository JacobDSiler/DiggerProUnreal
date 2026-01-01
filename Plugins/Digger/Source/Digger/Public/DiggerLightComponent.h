#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/LightComponent.h"
#include "DiggerLightComponent.generated.h"

UCLASS( ClassGroup=(Digger), meta=(BlueprintSpawnableComponent) )
class DIGGER_API UDiggerLightComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	UDiggerLightComponent();

	// The light component to control. If empty, it tries to find one on the owner.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Digger Light")
	ULightComponent* TargetLight;

	// How far below the landscape surface before the light turns on?
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Digger Light")
	float ActivationDepthOffset = 50.0f; 

	// Smooth transition speed (0 = instant)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Digger Light")
	float FadeSpeed = 5.0f;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	class ADiggerManager* CachedManager;
	float TargetIntensity;
	float CurrentIntensity;
};