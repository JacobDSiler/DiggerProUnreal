#include "DynamicLightActor.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/BillboardComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "FBrushStroke.h"
#include "FLightBrushTypes.h"
#include "FSavedLightData.h"

ADynamicLightActor::ADynamicLightActor()
{
    PrimaryActorTick.bCanEverTick = false;
    bIsEditorOnlyActor = true;

    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
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
    if (LightComponent)
    {
        LightComponent->DestroyComponent();
        LightComponent = nullptr;
    }

    switch (LightType)
    {
        case ELightBrushType::Point:
        {
            UPointLightComponent* PointComp = NewObject<UPointLightComponent>(this, TEXT("PointLight"));
            PointComp->SetIntensity(8000.f);               // Bright enough
            PointComp->SetAttenuationRadius(1000.f);       // Covers a wide area
            PointComp->SetSourceRadius(25.f);              // Softens shadows
            PointComp->SetLightColor(FLinearColor::White);
            PointComp->Mobility = EComponentMobility::Movable;
            PointComp->SetVisibility(true);
            PointComp->bAffectsWorld = true;
            PointComp->CastShadows = true;
            PointComp->bUseInverseSquaredFalloff = true;
            LightComponent = PointComp;
            break;
        }
        case ELightBrushType::Spot:
        {
            USpotLightComponent* SpotComp = NewObject<USpotLightComponent>(this, TEXT("SpotLight"));
            SpotComp->RegisterComponent();
            SpotComp->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
            SpotComp->SetVisibility(true);
            SpotComp->Mobility = EComponentMobility::Movable;SpotComp->bAffectsWorld = true;
            SpotComp->SetIntensity(Intensity);
            SpotComp->SetAttenuationRadius(Falloff);
            SpotComp->SetSourceRadius(Radius);
            SpotComp->SetLightColor(LightColor);
            //SpotComp->SetOuterConeAngle(Angle);
                SpotComp->SetOuterConeAngle(40.f);
                SpotComp->SetInnerConeAngle(25.f);

            //SpotComp->SetInnerConeAngle(FMath::Max(Angle - 15.0f, 5.0f));
            LightComponent = SpotComp;
            break;
        }
        case ELightBrushType::Directional:
        {
            UDirectionalLightComponent* DirComp = NewObject<UDirectionalLightComponent>(this, TEXT("DirectionalLight"));
            DirComp->RegisterComponent();
            DirComp->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
            DirComp->SetVisibility(true);
            DirComp->Mobility = EComponentMobility::Movable;
            DirComp->bAffectsWorld = true;
            DirComp->SetIntensity(Intensity);
            DirComp->SetLightColor(LightColor);
                
            LightComponent = DirComp;
            break;
        }
    }

#if WITH_EDITORONLY_DATA
    SetupEditorSprite();
#endif
}

void ADynamicLightActor::SetupEditorSprite()
{
    if (SpriteComponent)
    {
        SpriteComponent->DestroyComponent();
        SpriteComponent = nullptr;
    }

    FString IconPath;
    switch (LightType)
    {
        case ELightBrushType::Point:
            IconPath = TEXT("/Engine/EditorResources/PointLightIcon"); break;
        case ELightBrushType::Spot:
            IconPath = TEXT("/Engine/EditorResources/SpotLightIcon"); break;
        case ELightBrushType::Directional:
            IconPath = TEXT("/Engine/EditorResources/DirectionalLightIcon"); break;
        default:
            IconPath = TEXT("/Engine/EditorResources/LightIcon"); break;
    }

    UTexture2D* Sprite = LoadObject<UTexture2D>(nullptr, *IconPath);
    if (!Sprite) return;

    SpriteComponent = NewObject<UBillboardComponent>(this, TEXT("EditorLightSprite"));
    SpriteComponent->SetSprite(Sprite);
    SpriteComponent->SetupAttachment(RootComponent);
    SpriteComponent->SetHiddenInGame(true);
    SpriteComponent->bIsScreenSizeScaled = true;
    SpriteComponent->bIsEditorOnly = true;
    SpriteComponent->RegisterComponent();
}
