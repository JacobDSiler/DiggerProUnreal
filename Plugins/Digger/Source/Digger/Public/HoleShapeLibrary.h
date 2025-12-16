#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h" // <--- NEEDED for UDataAsset
#include "DiggerDebug.h"
#include "FHoleShape.h"
#include "HoleShapeLibrary.generated.h"

// HoleShapeLibrary.h
USTRUCT(BlueprintType)
struct FHoleMeshMapping
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EHoleShapeType ShapeType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UStaticMesh* Mesh;

	FHoleMeshMapping()
		: ShapeType(EHoleShapeType::Sphere),
		  Mesh(nullptr)
	{}
};

// CHANGE: Inherit from UDataAsset instead of UObject
UCLASS(Blueprintable, BlueprintType)
class DIGGER_API UHoleShapeLibrary : public UDataAsset 
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hole Shapes")
	TArray<FHoleMeshMapping> ShapeMeshMappings;

	UFUNCTION(BlueprintCallable, Category = "Hole Shapes")
	UStaticMesh* GetMeshForShape(EHoleShapeType Shape) const
	{
		if (DiggerDebug::Holes())
		{
			UE_LOG(LogTemp, Warning, TEXT("Getting a MeshForShape to spawn a hole!"));
		}
		for (const auto& Mapping : ShapeMeshMappings)
		{
			if (Mapping.ShapeType == Shape)
			{
				return Mapping.Mesh;
			}
		}
		return nullptr;
	}
};