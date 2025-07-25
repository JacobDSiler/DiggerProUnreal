#include "DebugDrawInstanced.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"

UDebugDrawInstanced* UDebugDrawInstanced::Instance = nullptr;

UDebugDrawInstanced::UDebugDrawInstanced()
{
    // Initialize instanced mesh component
    InstancedMeshComponent = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("InstancedMeshComponent"));
    InstancedMeshComponent->SetMobility(EComponentMobility::Static);
    InstancedMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void UDebugDrawInstanced::Initialize(UStaticMesh* CubeMesh)
{
    if (CubeMesh)
    {
        DebugCubeMesh = CubeMesh;
    }
    else
    {
        // Fallback to a default cube mesh if none is provided
        UE_LOG(LogTemp, Warning, TEXT("No cube mesh provided for instanced drawing."));
    }

    // Create instance for global access
    if (!Instance)
    {
        Instance = NewObject<UDebugDrawInstanced>();
    }

    // Set up the instanced mesh component
    InstancedMeshComponent->SetStaticMesh(DebugCubeMesh);
}

void UDebugDrawInstanced::DrawSphere(const FVector& Location, float Radius)
{
    const int32 SphereSegments = 32;
    for (int32 i = 0; i < SphereSegments; ++i)
    {
        for (int32 j = 0; j < SphereSegments; ++j)
        {
            FVector Position = Location + FVector(FMath::Cos(i * 2 * PI / SphereSegments) * Radius, 
                                                   FMath::Sin(i * 2 * PI / SphereSegments) * Radius, 
                                                   FMath::Sin(j * PI / SphereSegments) * Radius);
            FTransform Transform(Position);
            InstancedMeshComponent->AddInstance(Transform);
        }
    }
}

void UDebugDrawInstanced::DrawBox(const FVector& Location, const FVector& Extent)
{
    // Draw a box using instanced mesh
    FTransform Transform(Location);
    InstancedMeshComponent->AddInstance(Transform);
}

void UDebugDrawInstanced::DrawCylinder(const FVector& Location, float Radius, float Height)
{
    const int32 CylinderSegments = 32;
    for (int32 i = 0; i < CylinderSegments; ++i)
    {
        float Angle = i * 2 * PI / CylinderSegments;
        FVector Position = Location + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f);
        FTransform Transform(Position);
        InstancedMeshComponent->AddInstance(Transform);
    }
}

void UDebugDrawInstanced::ClearInstances()
{
    if (InstancedMeshComponent)
    {
        InstancedMeshComponent->ClearInstances();
    }
}

