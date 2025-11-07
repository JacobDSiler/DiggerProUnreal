// DiggerCharacterBase.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "FBrushStroke.h" // Your custom struct
#include "FHoleShape.h"
#include "DiggerCharacterBase.generated.h"

class ADiggerManager;

UCLASS()
class DIGGERPROUNREAL_API ADiggerCharacterBase : public ACharacter
{
	GENERATED_BODY()

public:
	ADiggerCharacterBase();

protected:
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	UFUNCTION(BlueprintCallable, Category = Brush)
	virtual void DigAtCursor();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Digging")
	float DesiredRadius = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Digging")
	TEnumAsByte<EHoleShapeType> DefaultHoleShape = EHoleShapeType::Cylinder;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Digging")
	ADiggerManager* DiggerManager;

	FVector GetCursorWorldLocation() const;
};
