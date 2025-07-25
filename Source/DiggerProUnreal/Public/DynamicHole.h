// ADynamicHole.h
#pragma once

#include "CoreMinimal.h"
#include "HoleShapeLibrary.h"
#include "SelectableBase.h"
#include "DynamicHole.generated.h"

class ADiggerManager;


UCLASS(Blueprintable, BlueprintType)
class DIGGERPROUNREAL_API ADynamicHole : public ASelectableBase
{
	GENERATED_BODY()

public:
	ADynamicHole();

	// The hole shape type (can be chosen from the HoleShapeLibrary)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hole Properties")
	FHoleShape HoleShape;
	

	// Reference to the owning chunk of the hole
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chunk")
	class UVoxelChunk* OwningChunk;

	// Update the hole's mesh according to the selected shape type
	UFUNCTION(BlueprintCallable, Category = "Hole")
	void UpdateHoleMesh();

	// Set the owning chunk for the hole
	void SetOwningChunk(UVoxelChunk* NewChunk);

	// Sync the hole's properties with the chunk's data
	void SyncWithChunk();

protected:
	virtual void BeginPlay() override;

private:
	int32 HoleID; // Hole ID assigned by the chunk
	FIntVector CurrentChunkCoords; // For detecting if the hole has moved between chunks
	FIntVector PreviousChunkCoords; // For detecting if the hole has moved between chunks

	UPROPERTY()
	ADiggerManager* DiggerManager;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hole Mesh", meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* HoleMeshComponent;

	// Update the mesh from the HoleShape library
	void SetMeshForShape(EHoleShapeType ShapeType);
	virtual void OnConstruction(const FTransform& Transform) override;
	UVoxelChunk* FindOwningChunk(const FIntVector& ChunkCoords) const;


public:
	[[nodiscard]] ::ADiggerManager* GetDiggerManager() 
	{
		return DiggerManager;
	}

	void SetDiggerManager(ADiggerManager* InDiggerManager)
	{
		this->DiggerManager = InDiggerManager;
	}
};
