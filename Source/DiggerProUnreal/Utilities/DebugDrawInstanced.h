#pragma once

#include "CoreMinimal.h"
#include "Engine/Classes/Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "UObject/NoExportTypes.h"
#include "DebugDrawInstanced.generated.h"

UCLASS()
class DIGGERPROUNREAL_API UDebugDrawInstanced : public UObject
{
	GENERATED_BODY()

public:
	static UDebugDrawInstanced* Instance;

	UPROPERTY()
	UInstancedStaticMeshComponent* InstancedMeshComponent;

	UPROPERTY(EditAnywhere, Category = "Debug Draw")
	UStaticMesh* DebugCubeMesh;

	UDebugDrawInstanced();
	void Initialize(UStaticMesh* CubeMesh);
	void DrawSphere(const FVector& Location, float Radius);
	void DrawBox(const FVector& Location, const FVector& Extent);
	void DrawCylinder(const FVector& Location, float Radius, float Height);
	void ClearInstances();
};

#define DEBUG_DRAW_INSTANCED(BrushType, HitLocation, ...) \
if (UDebugDrawInstanced::Instance) { \
UDebugDrawInstanced::Instance->Draw(BrushType, HitLocation, __VA_ARGS__); \
}
