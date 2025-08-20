// Copyright Epic Games, Inc. All Rights Reserved.

#include "DiggerProUnrealGameMode.h"
#include "DiggerProUnrealCharacter.h"
#include "UObject/ConstructorHelpers.h"

ADiggerProUnrealGameMode::ADiggerProUnrealGameMode()
{
	// set default pawn class to our Blueprinted character
	/*static ConstructorHelpers::FClassFinder<APawn> PlayerPawnBPClass(TEXT("/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter"));
	if (PlayerPawnBPClass.Class != NULL)
	{
		DefaultPawnClass = PlayerPawnBPClass.Class;
	}*/
}
