// Fill out your copyright notice in the Description page of Project Settings.


#include "IslandActor.h"
#include "ProceduralMeshComponent.h"

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


// Sets default values
AIslandActor::AIslandActor()
{
	// Set this actor to call Tick() every frame if you need it (optional)
	// PrimaryActorTick.bCanEverTick = true;

	// Create the procedural mesh component and set as root
	ProcMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ProcMesh"));
	RootComponent = ProcMesh;
}

// Called when the game starts or when spawned
void AIslandActor::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void AIslandActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}


