// DynamicHole.cpp

#include "DynamicHole.h"
#include "DiggerManager.h"
#include "HoleShapeLibrary.h"
#include "VoxelChunk.h"
#include "Components/StaticMeshComponent.h"

ADynamicHole::ADynamicHole()
{
	// Initialize default values
	OwningChunk = nullptr;
	HoleID = -1;
	
	// Initialize components
	HoleMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HoleMesh"));
	RootComponent = HoleMeshComponent;

	// Initialize default hole shape
	HoleShape = FHoleShape(EHoleShapeType::Sphere);
}

void ADynamicHole::BeginPlay()
{
	Super::BeginPlay();

	// Set the mesh when the hole is spawned
	UpdateHoleMesh();
}

// ADynamicHole.cpp

void ADynamicHole::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Check if the hole has moved
	FVector HoleLocation = GetActorLocation();
	CurrentChunkCoords = FVoxelConversion::WorldToChunk(HoleLocation);

	if (OwningChunk && PreviousChunkCoords != CurrentChunkCoords)
	{
		// Hole has moved, unregister from the previous chunk and register in the new one
		if (OwningChunk)
		{
			OwningChunk->RemoveHoleFromChunk(this);
		}
        
		// Update the chunk reference
		OwningChunk = FindOwningChunk(CurrentChunkCoords);
		OwningChunk->AddHoleToChunk(this);

		PreviousChunkCoords = CurrentChunkCoords;
	}
}

void ADynamicHole::SetOwningChunk(UVoxelChunk* NewChunk)
{
	// Set the new chunk and update the hole's ID
	if (OwningChunk != NewChunk)
	{
		// Remove from old chunk
		if (OwningChunk)
		{
			OwningChunk->RemoveHoleFromChunk(this);
		}

		// Set the new chunk
		OwningChunk = NewChunk;

		// Assign a new HoleID from the chunk
		HoleID = OwningChunk->GenerateHoleID(); // Generate a unique Hole ID

		// Add to the new chunk
		if (OwningChunk)
		{
			OwningChunk->AddHoleToChunk(this);
		}
	}
}


UVoxelChunk* ADynamicHole::FindOwningChunk(const FIntVector& ChunkCoords) const
{
	// Find the chunk using the coordinates (you can use WorldToChunk or a similar method)
	if (!DiggerManager)
		return nullptr;
	
	return DiggerManager->GetOrCreateChunkAtChunk(ChunkCoords);
}




void ADynamicHole::UpdateHoleMesh()
{
	if (OwningChunk)
	{
		SetMeshForShape(HoleShape.ShapeType);
	}
}

void ADynamicHole::SetMeshForShape(EHoleShapeType ShapeType)
{
	if (DiggerManager)
	{
		UHoleShapeLibrary* HoleLibrary = DiggerManager->HoleShapeLibrary;
		if (HoleLibrary)
		{
			UStaticMesh* NewMesh = HoleLibrary->GetMeshForShape(ShapeType);
			if (NewMesh)
			{
				HoleMeshComponent->SetStaticMesh(NewMesh);
			}
		}
	}
}




void ADynamicHole::SyncWithChunk()
{
	if (OwningChunk)
	{
		FVector HoleLocation = GetActorLocation();
		FRotator HoleRotation = GetActorRotation();
		FVector HoleScale = GetActorScale();

		// Send updated hole data to the chunk
		OwningChunk->SaveHoleData(HoleLocation, HoleRotation, HoleScale);
	}
}
