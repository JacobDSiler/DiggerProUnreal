// SelectableBase.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SelectableBase.generated.h"

UCLASS(BlueprintType, Blueprintable)
class DIGGERPROUNREAL_API ASelectableBase : public AActor
{
	GENERATED_BODY()

public:
	ASelectableBase();
	~ASelectableBase();

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor")
	bool bIsSelectableInEditor = true;
    
	// Store original selectability state
	bool bOriginalSelectability = true;
    
	// Whether this actor should become unselectable in Digger mode
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Digger Mode")
	bool bHideInDiggerMode = true;

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    
	// Override this to control editor selectability
	virtual bool CanEditChange(const FProperty* InProperty) const override;
    
	UFUNCTION(BlueprintCallable, Category = "Editor")
	void SetSelectableInEditor(bool bSelectable);
    
	// Called when digger mode state changes
	UFUNCTION()
	void OnDiggerModeStateChanged(bool bDiggerModeActive);

#if WITH_EDITOR
	virtual void PostActorCreated() override;
	virtual void PostLoad() override;
#endif

private:
	void BindToDiggerModeEvents();
	void UnbindFromDiggerModeEvents();
	void UpdateSelectabilityForDiggerMode(bool bDiggerModeActive);
};