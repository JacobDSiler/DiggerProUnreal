// ADynamicHole.h
#pragma once

#include "CoreMinimal.h"
#include "FBrushStroke.h"
#include "HoleShapeLibrary.h"
#include "SelectableBase.h"
#include "DynamicHole.generated.h"

class ADiggerManager;
class UVoxelBrushShape;


UCLASS(BlueprintType, Blueprintable)
class DIGGER_API ADynamicHole : public ASelectableBase
{
	GENERATED_BODY()

public:
	ADynamicHole();
	bool ContainsPoint(const FVector& WorldPos) const;
	FVector ActorToLocal(const FVector& WorldPos) const;

public:
	// Add this simple setter
	void SetHoleMesh(UStaticMesh* NewMesh);

	UPROPERTY(Transient)
	FBrushStroke CachedStroke;

	// The hole shape type (can be chosen from the HoleShapeLibrary)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hole Properties")
	FHoleShape HoleShape;
	
	// Store the shape type so we can save it later
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hole Data")
	EHoleShapeType HoleShapeType = EHoleShapeType::Sphere;

	// The material to apply to create the hole (M_OpacityMask)
	// We set this default in the C++ constructor so it's auto-filled
	UPROPERTY(EditDefaultsOnly, Category = "Hole Config")
	UMaterialInterface* WriterMaterial;

	UPROPERTY()
	UVoxelBrushShape* BrushShapeInstance;

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

public:

	FVector GetBrushPosition() const
	{
		return GetActorLocation();
	}

	FRotator GetBrushRotation() const
	{
		return GetActorRotation();
	}

	FVector GetBrushScale() const
	{
		return GetActorScale3D();
	}

	float GetBrushRadius() const
	{
		return HoleShape.Radius; // or your actual radius field
	}

	FVector GetBrushExtents() const
	{
		return HoleShape.Extents; // cube, advanced cube, etc.
	}

	float GetBrushInnerRadius() const
	{
		return HoleShape.InnerRadius; // torus
	}

	float GetBrushHeight() const
	{
		return HoleShape.Height; // cylinder, cone, pyramid
	}


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
	
	ADiggerManager* GetDiggerManager() 
	{
		return DiggerManager;
	}

	void SetDiggerManager(ADiggerManager* InDiggerManager)
	{
		this->DiggerManager = InDiggerManager;
	}
};
