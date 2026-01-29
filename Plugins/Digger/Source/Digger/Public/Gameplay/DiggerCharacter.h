#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "DiggerDynamicsComponent.h"
#include "DiggerLightComponent.h"
#include "DiggerToolComponent.h"
#include "DiggerCharacter.generated.h"

UCLASS()
class DIGGER_API ADiggerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ADiggerCharacter();

public:
	// Existing
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Digger")
	class UDiggerDynamicsComponent* DiggerDynamics;

	// NEW: Flashlight Logic
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Digger")
	class UDiggerLightComponent* DiggerSmartLight;

	// NEW: Runtime Digging
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Digger")
	class UDiggerToolComponent* DiggerTool;
};