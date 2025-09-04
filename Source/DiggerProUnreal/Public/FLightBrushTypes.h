#pragma once

UENUM(BlueprintType)
enum class ELightBrushType : uint8
{
	Point     UMETA(DisplayName = "Point Light"),
	Spot      UMETA(DisplayName = "Spot Light"),
	Directional UMETA(DisplayName = "Directional Light")
	// Add more as needed
};

FORCEINLINE FString GetLightTypeName(ELightBrushType LightType)
{
	switch (LightType)
	{
	case ELightBrushType::Point:
		return TEXT("Point Light");
	case ELightBrushType::Spot:
		return TEXT("Spot Light");
	case ELightBrushType::Directional:
		return TEXT("Directional Light");
	default:
		return TEXT("Unknown Light Type");
	}
}

