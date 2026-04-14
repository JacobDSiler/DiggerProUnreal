#include "DynamicLightActor.h"
#include "DiggerManager.h"
#include "DiggerHistory.h"
#include "VoxelChunk.h"
#include "Engine/World.h"
#include "Components/SceneComponent.h"
#if WITH_EDITOR
#include "Editor.h"
#endif

ADynamicLightActor::ADynamicLightActor()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ADynamicLightActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (LightComponent)
	{
		if (!LightComponent->IsRegistered())
			LightComponent->RegisterComponent();

		if (LightComponent->GetAttachParent() != GetRootComponent())
			LightComponent->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	}

	// Track current chunk on first construction
	CurrentChunkCoords  = FVoxelConversion::WorldToChunk(GetActorLocation());
	PreviousChunkCoords = CurrentChunkCoords;

	AssignOutlinerFolder();
}

void ADynamicLightActor::InitializeFromBrush(const FBrushStroke& BrushStroke)
{
	CachedType      = BrushStroke.LightType;
	CachedColor     = BrushStroke.LightColor;
	CachedAngle     = BrushStroke.BrushAngle;

	const float Strength01 = FMath::Clamp(BrushStroke.BrushStrength, 0.f, 1.f);
	const float Radius01   = FMath::Clamp(BrushStroke.BrushRadius / 256.f, 0.f, 1.f);
	const float Falloff01  = FMath::Clamp(BrushStroke.BrushFalloff, 0.f, 1.f);

	switch (CachedType)
	{
	case ELightBrushType::Point:
		CachedIntensity = FMath::Lerp(200.f, 50000.f, Strength01); break;
	case ELightBrushType::Spot:
		CachedIntensity = FMath::Lerp(300.f, 75000.f, Strength01); break;
	case ELightBrushType::Directional:
		CachedIntensity = FMath::Lerp(0.5f, 15.f, Strength01);     break;
	default:
		CachedIntensity = FMath::Lerp(200.f, 50000.f, Strength01); break;
	}

	CachedRadius  = FMath::Lerp(50.f, 5000.f, Radius01);
	CachedFalloff = FMath::Lerp(100.f, 5000.f, Falloff01);

	const FVector  WorldPos = BrushStroke.BrushPosition + BrushStroke.BrushOffset;
	const FRotator WorldRot = BrushStroke.BrushRotation;
	SetActorLocation(WorldPos);
	SetActorRotation(WorldRot);

	CreateOrUpdateLightComponent();
	AssignOutlinerFolder();
}

void ADynamicLightActor::InitLight(ELightBrushType LightType)
{
	CachedType = LightType;
	CreateOrUpdateLightComponent();
}

// =============================================================================
// CHUNK OWNERSHIP
// =============================================================================

void ADynamicLightActor::SetOwningChunk(UVoxelChunk* NewChunk)
{
	if (OwningChunk == NewChunk) return;
	OwningChunk = NewChunk;
}

UVoxelChunk* ADynamicLightActor::FindOwningChunk(const FIntVector& ChunkCoords) const
{
	if (!DiggerManager) return nullptr;
	return DiggerManager->GetOrCreateChunkAtCoords(ChunkCoords);
}

// =============================================================================
// EDITOR MOVE — chunk re-assignment + undo record
// =============================================================================

#if WITH_EDITOR
void ADynamicLightActor::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	if (!bFinished)
	{
		// Drag start — snapshot transform before move
		PreMoveTransform = GetActorTransform();
		return;
	}

	// --- Drag end ---
	FIntVector NewCoords = FVoxelConversion::WorldToChunk(GetActorLocation());

	if (NewCoords != CurrentChunkCoords)
	{
		// Transfer chunk ownership
		UVoxelChunk* NewChunk = FindOwningChunk(NewCoords);
		SetOwningChunk(NewChunk);

		PreviousChunkCoords = CurrentChunkCoords;
		CurrentChunkCoords  = NewCoords;
	}

	// Record move for undo
	if (DiggerManager && ActorUID != INDEX_NONE)
	{
		FDiggerActorMoveRecord Rec;
		Rec.ActorType      = EDiggerActorType::Light;
		Rec.ActorUID       = ActorUID;
		Rec.PreTransform   = PreMoveTransform;
		Rec.PostTransform  = GetActorTransform();
		Rec.OldChunkCoords = PreviousChunkCoords;
		Rec.NewChunkCoords = CurrentChunkCoords;
		DiggerManager->RecordActorMove(MoveTemp(Rec));
	}
}
#endif

// =============================================================================
// LIGHT COMPONENT
// =============================================================================

void ADynamicLightActor::CreateOrUpdateLightComponent()
{
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

	if (!LightComponent)
	{
		switch (CachedType)
		{
		case ELightBrushType::Point:
			LightComponent = NewObject<UPointLightComponent>(this, TEXT("PointLight"));       break;
		case ELightBrushType::Spot:
			LightComponent = NewObject<USpotLightComponent>(this, TEXT("SpotLight"));         break;
		case ELightBrushType::Directional:
			LightComponent = NewObject<UDirectionalLightComponent>(this, TEXT("DirLight"));   break;
		default:
			LightComponent = NewObject<UPointLightComponent>(this, TEXT("PointLight"));       break;
		}

		if (LightComponent)
		{
			LightComponent->SetupAttachment(RootComponent);
			LightComponent->RegisterComponent();
			LightComponent->SetWorldLocation(GetActorLocation());
			LightComponent->SetWorldRotation(GetActorRotation());
		}
	}

	if (!LightComponent) return;

	LightComponent->SetVisibility(true);
	LightComponent->SetLightColor(CachedColor);
	LightComponent->SetIntensity(CachedIntensity);
	LightComponent->SetMobility(EComponentMobility::Movable);
	LightComponent->bAffectsWorld = true;
	LightComponent->CastShadows = true;
	LightComponent->SetFlags(RF_Transient);
	LightComponent->ClearFlags(RF_Transactional | RF_Public);

	if (UPointLightComponent* Point = Cast<UPointLightComponent>(LightComponent))
	{
		Point->SetAttenuationRadius(CachedRadius);
		Point->bUseInverseSquaredFalloff = true;
		Point->SetSourceRadius(10.f);
	}
	else if (USpotLightComponent* Spot = Cast<USpotLightComponent>(LightComponent))
	{
		Spot->SetAttenuationRadius(CachedRadius);
		Spot->bUseInverseSquaredFalloff = true;
		Spot->SetInnerConeAngle(FMath::Clamp(CachedAngle * 0.7f, 0.0f, 89.0f));
		Spot->SetOuterConeAngle(FMath::Clamp(CachedAngle, 1.0f, 89.0f));
		Spot->SetSourceRadius(8.f);
		Spot->SetRelativeRotation(FRotator::ZeroRotator);
	}
	else if (UDirectionalLightComponent* Dir = Cast<UDirectionalLightComponent>(LightComponent))
	{
		Dir->SetUseTemperature(false);
		Dir->SetCastShadows(true);
		Dir->SetRelativeRotation(FRotator::ZeroRotator);
	}
}

void ADynamicLightActor::AssignOutlinerFolder()
{
#if WITH_EDITOR
#if ENGINE_MAJOR_VERSION >= 5
	if (HasAllFlags(RF_Transient) || !GEditor) return;
	const FName NewFolderPath = FName(TEXT("Digger/DynamicLights"));
	if (GetClass()->FindFunctionByName(TEXT("SetFolderPath_Recursively")))
		this->SetFolderPath_Recursively(NewFolderPath);
	else
		this->SetFolderPath(NewFolderPath);
#else
	this->SetFolderPath(FName(TEXT("Digger/DynamicLights")));
#endif
#endif
}