#pragma once

#include "CoreMinimal.h"
#include "BrushPushMode.h" // Including the shared Push Mode Struct

UENUM()
enum class EDiggerMainMode : uint8
{
	Sculpt,
	Rotate,
	Offset,
	Busy
};

UENUM()
enum class EDiggerAxisMode : uint8
{
	None,
	X,
	Y,
	Z
};

