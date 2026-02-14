#pragma once

#include "CoreMinimal.h"
#include "SpawnedHoleDataVersion.h" // include the custom version
#include "fHoleShape.generated.h"

UENUM(BlueprintType)
enum class EHoleShapeType : uint8
{
	Sphere   UMETA(DisplayName = "Sphere"),
	Cube     UMETA(DisplayName = "Cube"),
	Cylinder UMETA(DisplayName = "Cylinder"),
	Capsule  UMETA(DisplayName = "Capsule"),
	Cone     UMETA(DisplayName = "Cone"),
	Torus    UMETA(DisplayName = "Torus"),
	Pyramid  UMETA(DisplayName = "Pyramid"),
};

USTRUCT(BlueprintType)
struct FHoleShape
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EHoleShapeType ShapeType = EHoleShapeType::Sphere;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Radius = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector Extents = FVector(50.0f, 50.0f, 50.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float InnerRadius = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Height = 200.0f;

	FHoleShape() = default;

	FHoleShape(EHoleShapeType InType)
		: ShapeType(InType)
	{}
};

// Versioned serialization for FHoleShape
FORCEINLINE FArchive& operator<<(FArchive& Ar, FHoleShape& Shape)
{
	// For now we only persist ShapeType; the rest are runtime defaults.
	// If you later decide to serialize Radius/Extents/etc., bump the custom version
	// and branch here based on Version.
	uint8 RawShape = static_cast<uint8>(Shape.ShapeType);
	Ar << RawShape;

	if (Ar.IsLoading())
	{
		Shape.ShapeType = static_cast<EHoleShapeType>(RawShape);
	}

	return Ar;
}
