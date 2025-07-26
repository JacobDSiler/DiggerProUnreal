#include "DynamicLightActor.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/World.h"
#include "BrushStroke.h" // Your brush stroke struct

ADynamicLightActor::ADynamicLightActor()
{
    bIsEditorOnlyActor = true;
    PrimaryActorTick.bCanEverTick = false;

    // default values
    LightType = ELightBrushType::Point;
    LightColor = FLinearColor::White;
    Intensity = 1000.0f;
    Radius = 300.0f;
    Falloff = 500.0f;
    Angle = 45.0f;
}

void ADynamicLightActor::InitializeFromBrush(const FBrushStroke& BrushStroke)
{
    LightType = BrushStroke.LightType;
    LightColor = BrushStroke.LightColor;
    Intensity = BrushStroke.BrushStrength;
    Radius = BrushStroke.BrushRadius;
    Falloff = BrushStroke.BrushFalloff;
    Angle = BrushStroke.BrushAngle;

    SetActorLocation(BrushStroke.BrushPosition);
    SetActorRotation(BrushStroke.BrushRotation);
    CreateLightComponent();
}

void ADynamicLightActor::InitializeFromSavedData(const FSavedLightData& Saved)
{
    LightType = Saved.LightType;
    LightColor = Saved.LightColor;
    Intensity = Saved.Intensity;
    Radius = Saved.Radius;
    Falloff = Saved.Falloff;
    Angle = Saved.Angle;

    SetActorLocation(Saved.Location);
    SetActorRotation(Saved.Rotation);
    SetActorScale3D(Saved.Scale);
    CreateLightComponent();
}

void ADynamicLightActor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    CreateLightComponent();
}

void ADynamicLightActor::CreateLightComponent()
{
    ULightComponent* LightComp = nullptr;

    // Clear existing components
    TArray<UActorComponent*> Components = GetComponents().Array();
    for (UActorComponent* Comp : Components)
    {
        if (Cast<ULightComponent>(Comp))
        {
            Comp->DestroyComponent();
        }
    }

    switch (LightType)
    {
        case ELightBrushType::Point:
        {
            UPointLightComponent* PointComp = NewObject<UPointLightComponent>(this);
            PointComp->RegisterComponent();
            PointComp->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
            PointComp->SetIntensity(Intensity);
            PointComp->SetAttenuationRadius(Falloff);
            PointComp->SetSourceRadius(Radius);
            PointComp->SetLightColor(LightColor);
            break;
        }
        case ELightBrushType::Spot:
        {
            USpotLightComponent* SpotComp = NewObject<USpotLightComponent>(this);
            SpotComp->RegisterComponent();
            SpotComp->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
            SpotComp->SetIntensity(Intensity);
            SpotComp->SetAttenuationRadius(Falloff);
            SpotComp->SetSourceRadius(Radius);
            SpotComp->SetLightColor(LightColor);
            SpotComp->SetOuterConeAngle(Angle);
            SpotComp->SetInnerConeAngle(FMath::Max(Angle - 15.0f, 5.0f));
            break;
        }
        case ELightBrushType::Directional:
        {
            UDirectionalLightComponent* DirComp = NewObject<UDirectionalLightComponent>(this);
            DirComp->RegisterComponent();
            DirComp->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
            DirComp->SetIntensity(Intensity);
            DirComp->SetLightColor(LightColor);
            break;
        }
    }
}
