// DiggerCharacterBase.cpp
#include "DiggerCharacterBase.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "DiggerManager.h"
#include "Components/InputComponent.h"

ADiggerCharacterBase::ADiggerCharacterBase()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ADiggerCharacterBase::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	PlayerInputComponent->BindAction("DigAction", IE_Pressed, this, &ADiggerCharacterBase::DigAtCursor);
}

void ADiggerCharacterBase::DigAtCursor()
{
	FBrushStroke Stroke;
	Stroke.BrushPosition = GetCursorWorldLocation();
	Stroke.BrushRadius = DesiredRadius;
	Stroke.BrushRotation = FRotator::ZeroRotator;
	Stroke.HoleShape = DefaultHoleShape;

	if (DiggerManager)
	{
		DiggerManager->HandleHoleSpawn(Stroke);
	}
}

FVector ADiggerCharacterBase::GetCursorWorldLocation() const
{
	FVector WorldLocation, WorldDirection;
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (PC && PC->DeprojectMousePositionToWorld(WorldLocation, WorldDirection))
	{
		FHitResult Hit;
		FVector Start = WorldLocation;
		FVector End = Start + WorldDirection * 10000.f;
		if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility))
		{
			return Hit.Location;
		}
	}
	return GetActorLocation(); // Fallback
}
