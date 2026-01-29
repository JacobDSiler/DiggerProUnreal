#include "DiggerLightComponent.h"
#include "DiggerManager.h"
#include "Kismet/GameplayStatics.h"

UDiggerLightComponent::UDiggerLightComponent(): TargetLight(nullptr), CachedManager(nullptr), TargetIntensity(0),
                                                CurrentIntensity(0)
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UDiggerLightComponent::BeginPlay()
{
	Super::BeginPlay();

	// Auto-find light if not assigned
	if (!TargetLight)
	{
		TargetLight = GetOwner()->FindComponentByClass<ULightComponent>();
	}

	if (TargetLight)
	{
		// Store original intensity so we can fade to it
		TargetIntensity = TargetLight->Intensity;
		TargetLight->SetIntensity(0.0f); // Start off
		CurrentIntensity = 0.0f;
	}

	CachedManager = ADiggerManager::FindDiggerManager(this);
}

void UDiggerLightComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!TargetLight || !CachedManager) return;

	FVector MyLoc = GetOwner()->GetActorLocation();
    
	// Get Terrain Height
	float TerrainZ = CachedManager->GetLandscapeHeightAt(MyLoc);
    
	// Logic: Am I below the terrain?
	bool bShouldBeOn = MyLoc.Z < (TerrainZ - ActivationDepthOffset);

	// Fade Logic
	float Goal = bShouldBeOn ? TargetIntensity : 0.0f;
    
	if (FadeSpeed > 0.0f)
	{
		CurrentIntensity = FMath::FInterpTo(CurrentIntensity, Goal, DeltaTime, FadeSpeed);
	}
	else
	{
		CurrentIntensity = Goal;
	}

	TargetLight->SetIntensity(CurrentIntensity);
}