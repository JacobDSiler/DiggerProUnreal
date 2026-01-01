#pragma once


#include "DiggerBlueprintLibrary.generated.h"

UCLASS()
class DIGGER_API UDiggerBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// This calls your Manager's SmartTrace logic, exposing it to any BP
	UFUNCTION(BlueprintCallable, Category = "Digger|Trace", meta = (WorldContext = "WorldContextObject"))
	static bool DiggerSmartTrace(const UObject* WorldContextObject, FVector Start, FVector End, FHitResult& OutHit);
};