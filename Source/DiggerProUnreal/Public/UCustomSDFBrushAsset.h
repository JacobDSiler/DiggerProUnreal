#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UCustomSDFBrushAsset.generated.h"

UCLASS(BlueprintType)
class DIGGERPROUNREAL_API UCustomSDFBrushAsset : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDF")
    TArray<float> SDFValues;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDF")
    FIntVector Dimensions;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDF")
    float VoxelSize;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SDF")
    FVector OriginOffset;
};

