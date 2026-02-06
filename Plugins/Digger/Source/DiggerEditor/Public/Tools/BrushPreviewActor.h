#pragma once

#include "CoreMinimal.h"
#include "BrushPreviewLightComponent.h"
#include "GameFramework/Actor.h"
#include "BrushPreviewActor.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;

UENUM()
enum class EBrushPreviewShape : uint8
{
	Sphere,
	Box,
	Capsule,
	Cylinder,
	Cone,
	RoundBox,
	Ellipsoid,
	Torus
};

UCLASS(NotBlueprintable, NotPlaceable, Transient)
class DIGGEREDITOR_API ABrushPreviewActor : public AActor
{
	GENERATED_BODY()

public:
	ABrushPreviewActor();

	UPROPERTY(Transient)
	UStaticMeshComponent* PreviewMesh = nullptr;

	UPROPERTY(Transient)
	UMaterialInstanceDynamic* MID = nullptr;

	// Mode Indicator Members
	UPROPERTY(EditAnywhere, Category="Digger Preview")
	UTexture2D* SculptSprite;

	UPROPERTY(EditAnywhere, Category="Digger Preview")
	UTexture2D* RotateSprite;

	UPROPERTY(EditAnywhere, Category="Digger Preview")
	UTexture2D* OffsetSprite;

	UPROPERTY(VisibleAnywhere, Category="Digger Preview")
	UBillboardComponent* ModeIndicator;

	

	void Initialize(UStaticMesh* ShapeMesh, UMaterialInterface* BaseMat);

	void UpdatePreview(const FVector& CenterWS,
					   const FVector& RadiusXYZ,
					   float Falloff,
					   bool bAddMode,
					   float CellSize,
					   EBrushPreviewShape Shape,
					   const FQuat& RotationWS);

	void SetVisible(bool bVisible);

private:
	float MeshUnitRadius = 50.f;
	EBrushPreviewShape CurrentShape = EBrushPreviewShape::Sphere;

	void EnsureMeshesLoaded();
	void SetShape(EBrushPreviewShape NewShape);
};
