#pragma once

#include "CoreMinimal.h"
#include "FHoleShape.h"
#include "UObject/NoExportTypes.h"
#include "FSpawnedHoleData.generated.h"


// FSpawnedHoleData.h
USTRUCT(BlueprintType)
struct FSpawnedHoleData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector Location;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FRotator Rotation;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector Scale;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FHoleShape Shape;

	
	FSpawnedHoleData()
		: Location(FVector::ZeroVector)
		, Rotation(FRotator::ZeroRotator)
		, Scale(FVector(1.0f, 1.0f, 1.0f))
	{}
	
	FSpawnedHoleData(const FVector& InLocation, const FRotator& InRotation, const FVector& InScale)
		   : Location(InLocation)
		   , Rotation(InRotation)
		   , Scale(InScale)
		   , Shape(EHoleShapeType::Sphere) // Default to Sphere
	   {}

	FSpawnedHoleData(const FVector& InLocation, const FRotator& InRotation, const FVector& InScale, const FHoleShape& InShape)
		: Location(InLocation)
		, Rotation(InRotation)
		, Scale(InScale)
		, Shape(InShape)
	{}
};



// 🔥 This must be outside of the struct definition:
// Serialization for FSpawnedHoleData
FORCEINLINE FArchive& operator<<(FArchive& Ar, FSpawnedHoleData& Hole)
{
	Ar << Hole.Shape.ShapeType;
	Ar << Hole.Location;
	Ar << Hole.Rotation;
	Ar << Hole.Scale;
	return Ar;
}