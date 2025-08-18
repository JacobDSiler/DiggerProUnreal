#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BrushPreviewActor.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

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

UCLASS(NotBlueprintable, NotPlaceable)
class DIGGEREDITOR_API ABrushPreviewActor : public AActor   // 👈 if in DiggerEditor module
{
	GENERATED_BODY()

public:
	ABrushPreviewActor();

	UPROPERTY(Transient)
	UStaticMeshComponent* PreviewMesh = nullptr;

	UPROPERTY(Transient)
	UMaterialInstanceDynamic* MID = nullptr;

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

	// Cached engine meshes (lazy-loaded)
	UStaticMesh* MeshSphere  = nullptr;
	UStaticMesh* MeshCube    = nullptr;
	UStaticMesh* MeshCapsule = nullptr;
	UStaticMesh* MeshCylinder= nullptr;
	UStaticMesh* MeshCone    = nullptr;

	void EnsureMeshesLoaded();
	void SetShape(EBrushPreviewShape NewShape);
};
