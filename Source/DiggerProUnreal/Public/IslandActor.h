// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IslandActor.generated.h"

class UProceduralMeshComponent;

UCLASS()
class DIGGERPROUNREAL_API AIslandActor : public AActor
{
	GENERATED_BODY()
	
public:	

	// The procedural mesh component for this island
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Island")
	UProceduralMeshComponent* ProcMesh;

	void ApplyPhysics();
	void RemovePhysics();
	AIslandActor();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

};

