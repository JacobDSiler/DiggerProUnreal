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

    /** Globally-unique ID matching ADynamicHole::HoleUID.
     *  INDEX_NONE for data deserialized from pre-V2 saves; a new UID is
     *  allocated at spawn time so the actor is still tracked correctly. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 HoleUID = INDEX_NONE;

    FSpawnedHoleData()
        : Location(FVector::ZeroVector)
        , Rotation(FRotator::ZeroRotator)
        , Scale(FVector(1.0f, 1.0f, 1.0f))
        , HoleUID(INDEX_NONE)
    {}

    FSpawnedHoleData(const FVector& InLocation, const FRotator& InRotation, const FVector& InScale)
        : Location(InLocation)
        , Rotation(InRotation)
        , Scale(InScale)
        , Shape(EHoleShapeType::Sphere)
        , HoleUID(INDEX_NONE)
    {}

    FSpawnedHoleData(const FVector& InLocation, const FRotator& InRotation, const FVector& InScale, const FHoleShape& InShape)
        : Location(InLocation)
        , Rotation(InRotation)
        , Scale(InScale)
        , Shape(InShape)
        , HoleUID(INDEX_NONE)
    {}
};

// ---------------------------------------------------------------------------
// Versioned serialisation for FSpawnedHoleData
//
// This operator is used when FSpawnedHoleData is serialized through an
// FArchive that carries custom-version info (e.g. FMemoryWriter/Reader with
// SetCustomVersions).  The chunk binary load path in VoxelChunk.cpp uses a
// separate manual field loop with its own FileVersion gate — see
// LoadChunkData() — which is the authoritative path for on-disk saves.
//
// Version history:
//   V0 (BeforeShapeField)  — Location, Rotation, Scale only.
//   V1 (AddedShapeField)   — + Shape.
//   V2 (AddedHoleUID)      — + HoleUID (int32).
//
// When loading a file written before V2, HoleUID is left as INDEX_NONE.
// SpawnHoleFromData() allocates a fresh UID at runtime so the actor is
// always tracked, regardless of whether the UID was persisted.
// ---------------------------------------------------------------------------
FORCEINLINE FArchive& operator<<(FArchive& Ar, FSpawnedHoleData& Hole)
{
    // Query the version this archive was written with.
    // Returns -1 when the GUID is absent (truly ancient archive with no
    // custom-version block at all).
    const int32 Version = Ar.CustomVer(FSpawnedHoleDataCustomVersion::GUID);

    // Location / Rotation / Scale are present in every version.
    Ar << Hole.Location;
    Ar << Hole.Rotation;
    Ar << Hole.Scale;

    if (Version >= FSpawnedHoleDataCustomVersion::AddedShapeField)
    {
        // V1+: Shape field present.
        Ar << Hole.Shape;
    }
    else
    {
        // V0 / pre-versioned: default to Sphere on load; nothing to write
        // (we never actually write V0 data but guard the load path anyway).
        if (Ar.IsLoading())
            Hole.Shape = FHoleShape(EHoleShapeType::Sphere);
    }

    if (Version >= FSpawnedHoleDataCustomVersion::AddedHoleUID)
    {
        // V2+: HoleUID present.
        Ar << Hole.HoleUID;
    }
    else
    {
        // Pre-V2: leave HoleUID as INDEX_NONE; SpawnHoleFromData will assign one.
        if (Ar.IsLoading())
            Hole.HoleUID = INDEX_NONE;
    }

    return Ar;
}