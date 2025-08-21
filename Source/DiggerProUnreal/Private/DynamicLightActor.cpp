#include "DynamicLightActor.h"
#include "Engine/World.h"
#include "Components/SceneComponent.h"
#if WITH_EDITOR
#include "Editor.h"
#endif

ADynamicLightActor::ADynamicLightActor()
{
	PrimaryActorTick.bCanEverTick = false;

	// Make a root component.
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	
	// Ensure we have a root
	if (!GetRootComponent())
	{
		USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
		SetRootComponent(Root);
	}
}

void ADynamicLightActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// If a light component already exists, make sure it's attached and registered
	if (LightComponent)
	{
		if (!LightComponent->IsRegistered())
		{
			LightComponent->RegisterComponent();
		}

		if (LightComponent->GetAttachParent() != GetRootComponent())
		{
			LightComponent->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
		}
	}

	AssignOutlinerFolder();
}

void ADynamicLightActor::InitializeFromBrush(const FBrushStroke& BrushStroke)
{
	// Cache values from the brush
	CachedType      = BrushStroke.LightType;
	CachedColor     = BrushStroke.LightColor;
	CachedAngle     = BrushStroke.BrushAngle;

	// Remap your UI ranges → usable engine ranges (tweak to taste)
	// Strength: 0–1  → Intensity
	// Radius:   0–256→ 0–5000cm
	// Falloff:  0–1  → 100–5000cm (treat as attenuation span)
	const float Strength01 = FMath::Clamp(BrushStroke.BrushStrength, 0.f, 1.f);
	const float Radius01   = FMath::Clamp(BrushStroke.BrushRadius / 256.f, 0.f, 1.f);
	const float Falloff01  = FMath::Clamp(BrushStroke.BrushFalloff, 0.f, 1.f);

	// Pick sensible defaults per type (UE5: point/spot use lumens; directional uses lux)
	switch (CachedType)
	{
	case ELightBrushType::Point:
		CachedIntensity = FMath::Lerp(200.f, 50000.f, Strength01);   // lumens
		break;
	case ELightBrushType::Spot:
		CachedIntensity = FMath::Lerp(300.f, 75000.f, Strength01);   // lumens
		break;
	case ELightBrushType::Directional:
		CachedIntensity = FMath::Lerp(0.5f, 15.f, Strength01);       // lux
		break;
	default:
		CachedIntensity = FMath::Lerp(200.f, 50000.f, Strength01);
		break;
	}

	CachedRadius  = FMath::Lerp(50.f, 5000.f, Radius01);            // attenuation radius
	CachedFalloff = FMath::Lerp(100.f, 5000.f, Falloff01);          // we’ll map to attenuation as well

	// Put the ACTOR at the brush transform so chunk tracking uses the actor transform
	const FVector  WorldPos = BrushStroke.BrushPosition + BrushStroke.BrushOffset;
	const FRotator WorldRot = BrushStroke.BrushRotation;
	SetActorLocation(WorldPos);
	SetActorRotation(WorldRot);

	// (Re)create the light component of the correct type, attach, and configure
	CreateOrUpdateLightComponent();

	AssignOutlinerFolder();
}

void ADynamicLightActor::CreateOrUpdateLightComponent()
{
	// Destroy previous component if type changed
	if (LightComponent)
	{
		const bool bTypeMismatch =
			(CachedType == ELightBrushType::Point      && !Cast<UPointLightComponent>(LightComponent)) ||
			(CachedType == ELightBrushType::Spot       && !Cast<USpotLightComponent>(LightComponent))  ||
			(CachedType == ELightBrushType::Directional&& !Cast<UDirectionalLightComponent>(LightComponent));

		if (bTypeMismatch)
		{
			LightComponent->DestroyComponent();
			LightComponent = nullptr;
		}
	}

	// Create the right component if missing
	if (!LightComponent)
	{
		switch (CachedType)
		{
		case ELightBrushType::Point:
			LightComponent = NewObject<UPointLightComponent>(this, TEXT("PointLight"));
			break;
		case ELightBrushType::Spot:
			LightComponent = NewObject<USpotLightComponent>(this, TEXT("SpotLight"));
			break;
		case ELightBrushType::Directional:
			LightComponent = NewObject<UDirectionalLightComponent>(this, TEXT("DirectionalLight"));
			break;
		default:
			LightComponent = NewObject<UPointLightComponent>(this, TEXT("PointLight"));
			break;
		}

		if (LightComponent)
		{
			LightComponent->SetupAttachment(RootComponent);
			LightComponent->RegisterComponent();

			// Match actor rotation & position
			LightComponent->SetWorldLocation(GetActorLocation());
			LightComponent->SetWorldRotation(GetActorRotation());
		}
	}

	// Common settings so lights actually light the world
	LightComponent->SetVisibility(true);
	LightComponent->SetLightColor(CachedColor);
	LightComponent->SetIntensity(CachedIntensity);
	LightComponent->SetMobility(EComponentMobility::Movable);
	LightComponent->bAffectsWorld = true;
	LightComponent->CastShadows = true;
	// Set all the transient flags
	LightComponent->SetFlags(RF_Transient);
	LightComponent->ClearFlags(RF_Transactional | RF_Public);

	// Type-specific settings
	if (UPointLightComponent* Point = Cast<UPointLightComponent>(LightComponent))
	{
		Point->SetAttenuationRadius(CachedRadius);
		Point->bUseInverseSquaredFalloff = true;      // physically correct
		Point->SetSourceRadius(10.f);
	}
	else if (USpotLightComponent* Spot = Cast<USpotLightComponent>(LightComponent))
	{
		Spot->SetAttenuationRadius(CachedRadius);
		Spot->bUseInverseSquaredFalloff = true;
		Spot->SetInnerConeAngle(FMath::Clamp(CachedAngle * 0.7f, 0.0f, 89.0f));
		Spot->SetOuterConeAngle(FMath::Clamp(CachedAngle,        1.0f, 89.0f));
		Spot->SetSourceRadius(8.f);
		// Direction is actor’s forward; component uses relative (0) so it follows actor rotation
		Spot->SetRelativeRotation(FRotator::ZeroRotator);
	}
	else if (UDirectionalLightComponent* Dir = Cast<UDirectionalLightComponent>(LightComponent))
	{
		// Directional light ignores attenuation. Uses actor rotation for direction.
		Dir->SetUseTemperature(false);
		Dir->SetCastShadows(true);
		Dir->SetRelativeRotation(FRotator::ZeroRotator);
	}
}

void ADynamicLightActor::AssignOutlinerFolder()
{
#if WITH_EDITOR
	// Nest folders is supported: "Parent/Child"
	// Prefer SetFolderPath_Recursively when available; fall back to SetFolderPath.
#if ENGINE_MAJOR_VERSION >= 5
	if (HasAllFlags(RF_Transient) || !GEditor) { return; } // your SelectableBase handles transiency, but guard anyway
	const FName NewFolderPath = FName(TEXT("Digger/DynamicLights"));
	// Some engine versions expose SetFolderPath_Recursively; if not, SetFolderPath is fine.
	if (GetClass()->FindFunctionByName(TEXT("SetFolderPath_Recursively")))
	{
		// Call via function pointer to avoid compile differences
		this->SetFolderPath_Recursively(NewFolderPath);
	}
	else
	{
		this->SetFolderPath(NewFolderPath);
	}
#else
	this->SetFolderPath(FName(TEXT("Digger/DynamicLights")));
#endif
#endif // WITH_EDITOR
}

void ADynamicLightActor::InitLight(ELightBrushType LightType)
{
	CachedType = LightType;
	CreateOrUpdateLightComponent();
}


