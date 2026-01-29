#pragma once

#include <CapsuleBrushShape.h>
#include <ConeBrushShape.h>
#include <CubeBrushShape.h>
#include <CylinderBrushShape.h>
#include <PyramidBrushShape.h>
#include <SphereBrushShape.h>
#include <TorusBrushShape.h>

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
UCLASS(BlueprintType)
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

	UVoxelBrushShape* UHoleShapeLibrary::CreateBrushShape(EHoleShapeType ShapeType)
	{
		switch (ShapeType)
		{
		case EHoleShapeType::Sphere:
			return NewObject<USphereBrushShape>(this);
		case EHoleShapeType::Cube:
			return NewObject<UCubeBrushShape>(this);
		case EHoleShapeType::Cylinder:
			return NewObject<UCylinderBrushShape>(this);
		case EHoleShapeType::Capsule:
			return NewObject<UCapsuleBrushShape>(this);
		case EHoleShapeType::Cone:
			return NewObject<UConeBrushShape>(this);
		case EHoleShapeType::Torus:
			return NewObject<UTorusBrushShape>(this);
		case EHoleShapeType::Pyramid:
			return NewObject<UPyramidBrushShape>(this);
		default:
			return nullptr;
		}
	}

};