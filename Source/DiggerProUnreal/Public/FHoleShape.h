#pragma once

#include "CoreMinimal.h"
#include "fHoleShape.generated.h"

UENUM(BlueprintType)
enum class EHoleShapeType : uint8
{
	Sphere      UMETA(DisplayName = "Sphere"),
	Cube      UMETA(DisplayName = "Cube"),
	Cylinder	UMETA(DisplayName = "Cylinder"),
	Capsule      UMETA(DisplayName = "Capsule "),
	Cone      UMETA(DisplayName = "Cone"),
	Torus      UMETA(DisplayName = "Torus"),
	IcoSphere      UMETA(DisplayName = "IcoSphere"),
	Pyramid      UMETA(DisplayName = "Pyramid"),
	Stairs      UMETA(DisplayName = "Stairs"),
};

USTRUCT(BlueprintType)
struct FHoleShape
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EHoleShapeType ShapeType = EHoleShapeType::Sphere;

	FHoleShape() = default;

	FHoleShape(EHoleShapeType InType)
		: ShapeType(InType)
	{}
};
// Serialization for FHoleShape
FORCEINLINE FArchive& operator<<(FArchive& Ar, FHoleShape& Shape)
{
	uint8 RawShape = static_cast<uint8>(Shape.ShapeType);
	Ar << RawShape;

	if (Ar.IsLoading())
	{
		Shape.ShapeType = static_cast<EHoleShapeType>(RawShape);
	}

	return Ar;
}
