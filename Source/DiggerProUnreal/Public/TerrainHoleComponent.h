// TerrainHoleComponent.h
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Landscape.h"
#include "TerrainHoleComponent.generated.h"

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class DIGGERPROUNREAL_API UTerrainHoleComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTerrainHoleComponent();

	UFUNCTION(BlueprintCallable, Category = "Terrain")
	void CreateHoleFromMesh(ALandscapeProxy* Landscape, UStaticMeshComponent* MeshComponent);

private:
	void ProjectMeshOntoLandscape(ALandscapeProxy* Landscape, UStaticMeshComponent* MeshComponent, TArray<FVector>& OutProjectedPoints);
	bool IsPointInsideMeshBounds(const FVector& Point, UStaticMeshComponent* MeshComponent);
};
