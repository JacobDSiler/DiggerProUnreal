#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "FSpawnedHoleData.generated.h"

USTRUCT(BlueprintType)
struct FSpawnedHoleData
{
	GENERATED_BODY()

	UPROPERTY()
	FVector Location;

	UPROPERTY()
	FRotator Rotation;

	UPROPERTY()
	FVector Scale;

	FSpawnedHoleData() = default;

	FSpawnedHoleData(const FVector& InLoc, const FRotator& InRot, const FVector& InScale)
		: Location(InLoc), Rotation(InRot), Scale(InScale)
	{}
};


// 🔥 This must be outside of the struct definition:
FORCEINLINE FArchive& operator<<(FArchive& Ar, FSpawnedHoleData& Hole)
{
	Ar << Hole.Location;
	Ar << Hole.Rotation;
	Ar << Hole.Scale;
	return Ar;
}

