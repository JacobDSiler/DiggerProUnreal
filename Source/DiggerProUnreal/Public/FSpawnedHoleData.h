#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FSpawnedHoleData.generated.h"

USTRUCT(BlueprintType)
struct FSpawnedHoleData
{
	GENERATED_BODY()

	// The class of the Blueprint to spawn
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSubclassOf<AActor> HoleBPClass;

	// Transform data
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector Location;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FRotator Rotation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector Scale;

	// Optional: store the spawned instance if needed
	UPROPERTY(Transient)
	AActor* SpawnedInstance;

	// Constructor
	FSpawnedHoleData()
		: HoleBPClass(nullptr)
		, Location(FVector::ZeroVector)
		, Rotation(FRotator::ZeroRotator)
		, Scale(FVector(1.0f))
		, SpawnedInstance(nullptr)
	{}
};
