
#pragma once
#include "LandscapeHoleManager.generated.h"
class ALandscape;

UCLASS(Blueprintable)
class DIGGERPROUNREAL_API ULandscapeHoleManager : public UObject
{
    GENERATED_BODY()

public:
    // The runtime virtual texture to store hole masks
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hole System")
    URuntimeVirtualTexture* HoleMaskTexture;

    // Material Instance that will be updated
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hole System")
    UMaterialInstanceDynamic* HoleMaskMaterial;

    // Size of the hole mask texture
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hole System")
    FIntPoint HoleMaskResolution;

    // Landscape reference
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hole System")
    ALandscape* TargetLandscape;

    // Initialize the system
    UFUNCTION(BlueprintCallable, Category = "Hole System")
    void Initialize();

    // Create a hole at the given location
    UFUNCTION(BlueprintCallable, Category = "Hole System")
    void CreateHole(FVector Location, float Radius, float FalloffDistance);

    // Remove a hole at the given location
    UFUNCTION(BlueprintCallable, Category = "Hole System")
    void RemoveHole(FVector Location, float Radius);

private:
    // Convert world position to UV coordinates for the mask texture
    FVector2D WorldToTextureUV(const FVector& WorldLocation) const;
};

