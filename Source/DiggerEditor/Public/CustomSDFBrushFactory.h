#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "CustomSDFBrushFactory.generated.h"

UCLASS()
class DIGGEREDITOR_API UCustomSDFBrushFactory : public UFactory
{
    GENERATED_BODY()
public:
    UCustomSDFBrushFactory();

    virtual UObject* FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName,
        EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled) override;
};

