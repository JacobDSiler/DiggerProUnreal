// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DiggerHistory.h"
#include "IslandActor.generated.h"

class UProceduralMeshComponent;
class ADiggerManager;
class UVoxelChunk;

UCLASS()
class DIGGER_API AIslandActor : public AActor
{
	GENERATED_BODY()
	
public:	

	// The procedural mesh component for this island
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Island")
	UProceduralMeshComponent* ProcMesh;

	// -------------------------------------------------------------------------
	// Chunk ownership (same pattern as DynamicHole / DynamicLightActor)
	// -------------------------------------------------------------------------

	/** Globally unique ID assigned by ADiggerManager::AllocateActorUID(). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chunk")
	int32 ActorUID = INDEX_NONE;

	/** Chunk this island currently belongs to. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chunk")
	UVoxelChunk* OwningChunk = nullptr;

	void SetOwningChunk(UVoxelChunk* NewChunk) { OwningChunk = NewChunk; }
	void SetDiggerManager(ADiggerManager* InManager) { DiggerManager = InManager; }
	ADiggerManager* GetDiggerManager() const { return DiggerManager; }

	void ApplyPhysics();
	void RemovePhysics();

	AIslandActor();

protected:
	virtual void BeginPlay() override;

#if WITH_EDITOR
	virtual void PostEditMove(bool bFinished) override;
#endif

public:	
	virtual void Tick(float DeltaTime) override;

private:
	FIntVector CurrentChunkCoords  = FIntVector::ZeroValue;
	FIntVector PreviousChunkCoords = FIntVector::ZeroValue;
	FTransform PreMoveTransform;

	UPROPERTY()
	ADiggerManager* DiggerManager = nullptr;

	UVoxelChunk* FindOwningChunk(const FIntVector& ChunkCoords) const;
};