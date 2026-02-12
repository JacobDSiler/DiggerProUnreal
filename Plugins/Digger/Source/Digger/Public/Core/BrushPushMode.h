#pragma once

#include "CoreMinimal.h"
#include "BrushPushMode.generated.h"

UENUM(BlueprintType)
enum class EDiggerPushMode : uint8
{
	Ray        UMETA(DisplayName="Ray"),
	Normal     UMETA(DisplayName="Normal"),
	Blended    UMETA(DisplayName="Blended")
};
