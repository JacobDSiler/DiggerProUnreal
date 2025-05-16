// VoxelBrushTypes.h
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "VoxelBrushTypes.generated.h"

UENUM(BlueprintType)
enum class EVoxelBrushType : uint8 {
	Cube,
	Sphere,
	Cone,
	Cylinder,
	Capsule,
	Smooth,
	Debug,
	Custom,
	AdvancedCube,
	Torus,
	Pyramid,
	Icosphere,
	Stairs
};

