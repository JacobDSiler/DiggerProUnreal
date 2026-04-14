// Fill out your copyright notice in the Description page of Project Settings.

#include "IslandActor.h"
#include "DiggerManager.h"
#include "DiggerHistory.h"
#include "VoxelChunk.h"
#include "ProceduralMeshComponent.h"

AIslandActor::AIslandActor()
{
	PrimaryActorTick.bCanEverTick = false;
	ProcMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ProcMesh"));
	RootComponent = ProcMesh;
}

void AIslandActor::BeginPlay()
{
	Super::BeginPlay();
	CurrentChunkCoords  = FVoxelConversion::WorldToChunk(GetActorLocation());
	PreviousChunkCoords = CurrentChunkCoords;
}

void AIslandActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void AIslandActor::ApplyPhysics()
{
	if (ProcMesh)
	{
		ProcMesh->SetSimulatePhysics(true);
		ProcMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
}

void AIslandActor::RemovePhysics()
{
	if (ProcMesh)
	{
		ProcMesh->SetSimulatePhysics(false);
		ProcMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}

UVoxelChunk* AIslandActor::FindOwningChunk(const FIntVector& ChunkCoords) const
{
	if (!DiggerManager) return nullptr;
	return DiggerManager->GetOrCreateChunkAtCoords(ChunkCoords);
}

#if WITH_EDITOR
void AIslandActor::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	if (!bFinished)
	{
		PreMoveTransform = GetActorTransform();
		return;
	}

	// Determine new chunk
	FIntVector NewCoords = FVoxelConversion::WorldToChunk(GetActorLocation());

	if (NewCoords != CurrentChunkCoords)
	{
		// Transfer chunk ownership — islands don't need complex chunk-level
		// registration (no HoleDataArray equivalent) but we update the pointer
		// so tools that query OwningChunk get the right answer.
		UVoxelChunk* NewChunk = FindOwningChunk(NewCoords);
		SetOwningChunk(NewChunk);

		PreviousChunkCoords = CurrentChunkCoords;
		CurrentChunkCoords  = NewCoords;
	}

	// Record move for undo
	if (DiggerManager && ActorUID != INDEX_NONE)
	{
		FDiggerActorMoveRecord Rec;
		Rec.ActorType      = EDiggerActorType::Island;
		Rec.ActorUID       = ActorUID;
		Rec.PreTransform   = PreMoveTransform;
		Rec.PostTransform  = GetActorTransform();
		Rec.OldChunkCoords = PreviousChunkCoords;
		Rec.NewChunkCoords = CurrentChunkCoords;
		DiggerManager->RecordActorMove(MoveTemp(Rec));
	}
}
#endif