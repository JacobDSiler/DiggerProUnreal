#pragma once

#include "CoreMinimal.h"
#include "SelectableBase.h"
#include "FLightBrushTypes.h"
#include "FBrushStroke.h"
#include "DiggerHistory.h"
#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "DynamicLightActor.generated.h"

class ADiggerManager;
class UVoxelChunk;

UCLASS()
class DIGGER_API ADynamicLightActor : public ASelectableBase
{
	GENERATED_BODY()

public:
	ADynamicLightActor();

	void InitLight(ELightBrushType LightType);

	/** Initialize from a light brush stroke (editor or runtime) */
	void InitializeFromBrush(const FBrushStroke& BrushStroke);

	// -------------------------------------------------------------------------
	// Chunk ownership (mirrors DynamicHole pattern)
	// -------------------------------------------------------------------------

	/** Globally unique ID assigned by ADiggerManager::AllocateActorUID(). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chunk")
	int32 ActorUID = INDEX_NONE;

	/** Chunk this light currently belongs to (set by SpawnLight / PostEditMove). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chunk")
	UVoxelChunk* OwningChunk = nullptr;

	/** Assign owning chunk; removes from previous chunk if needed. */
	void SetOwningChunk(UVoxelChunk* NewChunk);

	void SetDiggerManager(ADiggerManager* InManager) { DiggerManager = InManager; }
	ADiggerManager* GetDiggerManager() const { return DiggerManager; }

protected:
	virtual void OnConstruction(const FTransform& Transform) override;

#if WITH_EDITOR
	virtual void PostEditMove(bool bFinished) override;
#endif

private:
	/** Creates (or recreates) the correct light component based on current cached data */
	void CreateOrUpdateLightComponent();

	/** Put this actor into a nice nested folder path in the World Outliner (editor only) */
	void AssignOutlinerFolder();

	/** Cached data from last InitializeFromBrush call */
	ELightBrushType CachedType = ELightBrushType::Point;
	FLinearColor    CachedColor = FLinearColor::White;
	float           CachedIntensity = 1000.f;
	float           CachedRadius = 500.f;
	float           CachedFalloff = 2.0f;
	float           CachedAngle = 45.f;

	FIntVector CurrentChunkCoords  = FIntVector::ZeroValue;
	FIntVector PreviousChunkCoords = FIntVector::ZeroValue;
	FTransform PreMoveTransform;

	UPROPERTY()
	ADiggerManager* DiggerManager = nullptr;

	UPROPERTY(Transient)
	ULightComponent* LightComponent = nullptr;

	UVoxelChunk* FindOwningChunk(const FIntVector& ChunkCoords) const;
};