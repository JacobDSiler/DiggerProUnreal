#pragma once

#include "CoreMinimal.h"
#include "FHoleShape.h"
#include "SpawnedHoleDataVersion.h"
#include "UObject/NoExportTypes.h"
#include "FSpawnedHoleData.generated.h"

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

// 🔥 Versioned serialization for FSpawnedHoleData
FORCEINLINE FArchive& operator<<(FArchive& Ar, FSpawnedHoleData& Hole)
{
    // Ask the archive which version of this data it knows about.
    const int32 Version = Ar.CustomVer(FSpawnedHoleDataCustomVersion::GUID);

    if (Version < FSpawnedHoleDataCustomVersion::AddedShapeField)
    {
        // LEGACY LAYOUT (Version 0):
        // Old saves only stored Location, Rotation, Scale.
        // Shape will stay at its default (Sphere, default params).
        Ar << Hole.Location;
        Ar << Hole.Rotation;
        Ar << Hole.Scale;

        // Do NOT touch Hole.Shape here; leave defaults.
    }
    else
    {
        // NEW LAYOUT (Version >= 1):
        // We store Location, Rotation, Scale, then Shape.
        Ar << Hole.Location;
        Ar << Hole.Rotation;
        Ar << Hole.Scale;
        Ar << Hole.Shape; // uses FHoleShape::operator<<
    }

    return Ar;
}
