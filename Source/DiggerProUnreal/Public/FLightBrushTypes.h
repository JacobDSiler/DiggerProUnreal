#pragma once

UENUM(BlueprintType)
enum class ELightBrushType : uint8
{
	Point     UMETA(DisplayName = "Point Light"),
	Spot      UMETA(DisplayName = "Spot Light"),
	Directional UMETA(DisplayName = "Directional Light")
	// Add more as needed
};