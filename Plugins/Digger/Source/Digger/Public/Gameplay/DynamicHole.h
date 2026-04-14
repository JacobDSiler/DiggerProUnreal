// ADynamicHole.h
#pragma once

#include "CoreMinimal.h"
#include "FBrushStroke.h"
#include "HoleShapeLibrary.h"
#include "SelectableBase.h"
#include "DynamicHole.generated.h"

class ADiggerManager;
class UVoxelBrushShape;
class URuntimeVirtualTexture;


UCLASS(BlueprintType, Blueprintable)
class DIGGER_API ADynamicHole : public ASelectableBase
{
	GENERATED_BODY()

public:
	ADynamicHole();
	bool ContainsPoint(const FVector& WorldPos) const;
	FVector ActorToLocal(const FVector& WorldPos) const;
	void ConfigureRVTRendering(URuntimeVirtualTexture* DiggerRVT);

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

	// Prepares BrushShapeInstance and CachedStroke without touching the mesh component.
	// Called by VoxelChunk::OnMeshReady immediately before it assigns the static mesh.
	void PrepareShapeData();

	// Set the owning chunk for the hole
	void SetOwningChunk(UVoxelChunk* NewChunk);
	void ValidateSpawnAgainstLandscape();

	// Sync the hole's properties with the chunk's data
	void SyncWithChunk();

	float GetEffectiveRadius() const
	{
		return CachedStroke.BrushRadius;
	}
	
	// Returns the unscaled base radius of this hole type (pre-scale).
	// Used by SpawnMergedHole to convert a world-space radius back into actor scale.
	float GetBaseRadius() const
	{
		// PrepareShapeData() always derives BrushRadius as BaseSize * Scale.GetMax()
		// where BaseSize is 100.0f. That is the authoritative base unit for all
		// hole shapes in this system.
		return 100.0f;
	}

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
	int32 HoleID; // Hole ID assigned by the chunk (legacy counter — use HoleUID for new code)

public:
	// Stable globally-unique ID assigned by ADiggerManager::AllocateActorUID().
	// INDEX_NONE until SetDiggerManager() assigns one during SpawnHoleFromData.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hole Data")
	int32 HoleUID = INDEX_NONE;

private:
	FIntVector CurrentChunkCoords;  // chunk the hole currently belongs to
	FIntVector PreviousChunkCoords; // chunk before most recent move

	// Transform captured at the start of a PostEditMove drag so the history
	// system can record the full pre→post transform delta on mouse-up.
	FTransform PreMoveTransform;

	UPROPERTY()
	ADiggerManager* DiggerManager;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hole Mesh", meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* HoleMeshComponent;

public:
	[[nodiscard]] UStaticMeshComponent* GetHoleMeshComponent() const
	{
		return HoleMeshComponent;
	}


private:
	// Update the mesh from the HoleShape library
	void SetMeshForShape(EHoleShapeType ShapeType);
	virtual void OnConstruction(const FTransform& Transform) override;
#if WITH_EDITOR
	virtual void PostEditMove(bool bFinished) override;
#endif
	UVoxelChunk* FindOwningChunk(const FIntVector& ChunkCoords) const;


public:
	
	ADiggerManager* GetDiggerManager() 
	{
		return DiggerManager;
	}

	void SetDiggerManager(ADiggerManager* InDiggerManager);
};