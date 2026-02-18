// BrushPreviewActor.cpp

#include "BrushPreviewActor.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "DiggerEditorSettings.h"
#include "BrushPreviewLightComponent.h"
#include "VoxelConversion.h"
#include "Components/BillboardComponent.h"

// Static cache to avoid reloading every time
static UStaticMesh* CachedMeshSphere   = nullptr;
static UStaticMesh* CachedMeshCube     = nullptr;
static UStaticMesh* CachedMeshCapsule  = nullptr;
static UStaticMesh* CachedMeshCylinder = nullptr;
static UStaticMesh* CachedMeshCone     = nullptr;
static UStaticMesh* CachedMeshTorus    = nullptr;

ABrushPreviewActor::ABrushPreviewActor()
{
    bIsEditorOnlyActor = true;
    SetActorEnableCollision(false);
    SetReplicates(false);

    // ---------------------------------------------------------
    // 1. Root mesh
    // ---------------------------------------------------------
    PreviewMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PreviewMesh"));
    RootComponent = PreviewMesh;

    PreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PreviewMesh->SetCastShadow(false);
    PreviewMesh->SetReceivesDecals(false);
    PreviewMesh->SetMobility(EComponentMobility::Movable);

    AActor::SetActorHiddenInGame(true);
    PreviewMesh->SetHiddenInGame(true);
    PreviewMesh->SetVisibility(true, true);

    // ---------------------------------------------------------
    // 2. Light Component
    // ---------------------------------------------------------
    PreviewLight = CreateDefaultSubobject<UBrushPreviewLightComponent>(TEXT("PreviewLight"));
    PreviewLight->SetupAttachment(RootComponent);
    PreviewLight->SetMobility(EComponentMobility::Movable);
    PreviewLight->SetCastShadows(false); 
    PreviewLight->SetLightColor(FLinearColor::White);
    PreviewLight->SetIntensity(1500.0f);
    PreviewLight->SetAttenuationRadius(1000.0f);
    
    // Ensure light starts hidden in game too
    PreviewLight->SetHiddenInGame(true);

    // ---------------------------------------------------------
    // 3. Mode indicator billboard
    // ---------------------------------------------------------
    ModeIndicator = CreateDefaultSubobject<UBillboardComponent>(TEXT("ModeIndicator"));
    ModeIndicator->SetupAttachment(RootComponent);

    ModeIndicator->SetHiddenInGame(true);   // always hidden in PIE
    ModeIndicator->SetVisibility(true);     // sculpt mode is set to visible by default
    ModeIndicator->SetRelativeScale3D(FVector(10.0f));
    ModeIndicator->bIsScreenSizeScaled = true;
    ModeIndicator->SetWorldRotation(FRotator(0.f, 0.f, 0.f));

    // ---------------------------------------------------------
    // Load icons from settings
    // ---------------------------------------------------------
    const UDiggerEditorSettings* Settings = UDiggerEditorSettings::Get();

    if (Settings)
    {
        SculptSprite = Settings->SculptIcon.LoadSynchronous();
        RotateSprite = Settings->RotateIcon.LoadSynchronous();
        OffsetSprite = Settings->OffsetIcon.LoadSynchronous();
        LoadingSprite = Settings->LoadingIcon.LoadSynchronous();
    }

    // Debug logs
    if (!SculptSprite) UE_LOG(LogTemp, Error, TEXT("Failed to load SculptSprite"));
    if (!RotateSprite) UE_LOG(LogTemp, Error, TEXT("Failed to load RotateSprite"));
    if (!OffsetSprite) UE_LOG(LogTemp, Error, TEXT("Failed to load OffsetSprite"));

    // ---------------------------------------------------------
    // Set default sculpt sprite *after* loading
    // ---------------------------------------------------------
    if (SculptSprite)
    {
        ModeIndicator->SetSprite(SculptSprite);
    }
}

void ABrushPreviewActor::SetVisible(bool bVisible)
{
    // Visibility in the editor viewport:
    PreviewMesh->SetVisibility(bVisible, true);
    
    // Sync Light visibility
    if (PreviewLight)
    {
        PreviewLight->SetVisibility(bVisible, true);
    }

    // Always hidden in PIE / game
    SetActorHiddenInGame(true);
    PreviewMesh->SetHiddenInGame(true);
    if (PreviewLight) PreviewLight->SetHiddenInGame(true);
}

void ABrushPreviewActor::UpdateModeIcon(EDiggerMainMode Mode)
{
    if (!ModeIndicator)
        return;

    // If busy, the EdMode will override this with ShowLoadingSprite()
    if (Mode == EDiggerMainMode::Busy)
    {
        ModeIndicator->SetVisibility(false);
        return;
    }

    switch (Mode)
    {
    case EDiggerMainMode::Rotate:
        ModeIndicator->SetSprite(RotateSprite);
        break;

    case EDiggerMainMode::Offset:
        ModeIndicator->SetSprite(OffsetSprite);
        break;

    default:
        ModeIndicator->SetSprite(SculptSprite);
        break;
    }

    const bool bShow =
        (Mode == EDiggerMainMode::Rotate ||
         Mode == EDiggerMainMode::Offset);

    ModeIndicator->SetVisibility(bShow);
    ModeIndicator->SetHiddenInGame(true);
}

void ABrushPreviewActor::Initialize(UStaticMesh* ShapeMesh, UMaterialInterface* BaseMat)
{
    // Preload meshes so shape switching is seamless
    EnsureMeshesLoaded();

    // If no material explicitly provided, use settings
    if (!BaseMat)
    {
        const UDiggerEditorSettings* Settings = UDiggerEditorSettings::Get();
        if (Settings && !Settings->BrushPreviewMaterial.IsNull())
        {
            BaseMat = Settings->BrushPreviewMaterial.LoadSynchronous();
        }
    }

    if (ShapeMesh)
    {
        PreviewMesh->SetStaticMesh(ShapeMesh);
    }

    if (BaseMat)
    {
        MID = UMaterialInstanceDynamic::Create(BaseMat, this);
        PreviewMesh->SetMaterial(0, MID);
    }
}

void ABrushPreviewActor::ShowLoadingSprite(float RotationDegrees)
{
    if (!ModeIndicator || !LoadingSprite)
        return;

    ModeIndicator->SetSprite(LoadingSprite);
    ModeIndicator->SetVisibility(true);

    // Billboard rotation is screen‑facing, so we rotate the component itself
    ModeIndicator->SetRelativeRotation(FRotator(0.f, 0.f, RotationDegrees));
}

static inline float SnapFloat(float V, float Cell)
{
    return Cell > 0.f ? FMath::GridSnap(V, Cell) : V;
}

// ... inside UpdatePreview implementation ...

void ABrushPreviewActor::UpdatePreview(
    const FVector& CenterWS,
    const FVector& RadiusXYZ,
    float Falloff,
    bool bAddMode,
    float CellSize,                 
    EBrushPreviewShape Shape,
    const FQuat& RotationWS,
    FLinearColor ColorDig,
    FLinearColor ColorAdd,
    FLinearColor ColorFalloff,
    float DepthFadeDistance,
    float OpacityAmount,
    FLinearColor CurrentLightColor,
    float LightIntensity,
    float LightRadius)
{
    const UDiggerEditorSettings* Settings = UDiggerEditorSettings::Get();

    // 1. Snapping Logic (Existing)
    const bool bSnapToGrid = Settings ? Settings->bSnapPreviewToGrid : true;
    const float VoxelSize = FVoxelConversion::LocalVoxelSize;
    const float Cell = FMath::Max(1.f, VoxelSize);

    FVector FinalCenter = CenterWS;
    if (bSnapToGrid)
    {
        auto Snap = [Cell](float v) { return FMath::GridSnap(v, Cell); };
        FinalCenter = FVector(Snap(CenterWS.X), Snap(CenterWS.Y), Snap(CenterWS.Z));
    }

    // 2. Apply Transform
    SetActorLocation(FinalCenter);
    SetActorRotation(RotationWS);

    // 3. Set Shape
    SetShape(Shape);

    // 4. Calculate Scale
    const FVector SafeR(
        FMath::Max(0.5f * Cell, RadiusXYZ.X),
        FMath::Max(0.5f * Cell, RadiusXYZ.Y),
        FMath::Max(0.5f * Cell, RadiusXYZ.Z));

    const FVector Scale(
        SafeR.X / MeshUnitRadius,
        SafeR.Y / MeshUnitRadius,
        SafeR.Z / MeshUnitRadius);

    PreviewMesh->SetWorldScale3D(Scale);
    
    if (ModeIndicator)
    {
        ModeIndicator->SetWorldRotation(FRotator(0.f, 0.f, 0.f));
    }

    // ---------------------------------------------------------
    // 5. DRIVE THE MATERIAL
    // ---------------------------------------------------------
    if (MID)
    {
        // Standard shape params
        MID->SetScalarParameterValue(TEXT("Falloff"), FMath::Clamp(Falloff, 0.f, 1.f));
        MID->SetScalarParameterValue(TEXT("CellSize"), Cell);
        MID->SetScalarParameterValue(TEXT("ShapeType"), static_cast<float>(Shape));
        MID->SetVectorParameterValue(TEXT("InvScale"), FLinearColor(
                Scale.X > 0.f ? 1.f / Scale.X : 0.f,
                Scale.Y > 0.f ? 1.f / Scale.Y : 0.f,
                Scale.Z > 0.f ? 1.f / Scale.Z : 0.f, 0.f));

        // --- NEW MATERIAL PARAMETERS ---
        
        // 1. Logic Switch (0.0 = Dig, 1.0 = Add)
        MID->SetScalarParameterValue(TEXT("IsAdd"), bAddMode ? 1.0f : 0.0f);
        
        // 2. Colors (The material will blend these based on IsAdd)
        MID->SetVectorParameterValue(TEXT("ColorDig"), ColorDig);
        MID->SetVectorParameterValue(TEXT("ColorAdd"), ColorAdd);
        MID->SetVectorParameterValue(TEXT("ColorFalloff"), ColorFalloff);
        
        // 3. Depth & Opacity
        MID->SetScalarParameterValue(TEXT("DepthFadeDistance"), DepthFadeDistance);
        MID->SetScalarParameterValue(TEXT("OpacityAmount"), OpacityAmount);
    }

    // ---------------------------------------------------------
    // 6. DRIVE THE LIGHT COMPONENT
    // ---------------------------------------------------------
    // The light component doesn't use the material logic, 
    // so we pass it the pre-calculated single color.
    if (PreviewLight)
    {
        PreviewLight->SetLightColor(CurrentLightColor);
        PreviewLight->SetIntensity(LightIntensity);
        PreviewLight->SetAttenuationRadius(LightRadius);
    }

    // ---------------------------------------------------------------------
    // 7. Position Mode Indicator
    // ---------------------------------------------------------------------
    if (ModeIndicator)
    {
        const float BrushHeight = SafeR.Z;
        const FVector IconPos = FinalCenter + FVector(0, 0, BrushHeight + 100.f);
        ModeIndicator->SetWorldLocation(IconPos);
        ModeIndicator->SetWorldRotation(FRotator(0.f, 0.f, 0.f));
    }
}

void ABrushPreviewActor::EnsureMeshesLoaded()
{
    if (CachedMeshSphere) return;

    const UDiggerEditorSettings* Settings = UDiggerEditorSettings::Get();
    if (!Settings) return;

    CachedMeshSphere   = Settings->SphereBrushMesh.LoadSynchronous();
    CachedMeshCube     = Settings->CubeBrushMesh.LoadSynchronous();
    CachedMeshCapsule  = Settings->CapsuleBrushMesh.LoadSynchronous();
    CachedMeshCylinder = Settings->CylinderBrushMesh.LoadSynchronous();
    CachedMeshCone     = Settings->ConeBrushMesh.LoadSynchronous();
    CachedMeshTorus    = Settings->TorusBrushMesh.LoadSynchronous();
}

void ABrushPreviewActor::SetShape(EBrushPreviewShape NewShape)
{
    if (CurrentShape == NewShape && PreviewMesh->GetStaticMesh() != nullptr) return;

    EnsureMeshesLoaded();

    UStaticMesh* NewMesh = CachedMeshSphere;

    switch (NewShape)
    {
    case EBrushPreviewShape::Sphere:     NewMesh = CachedMeshSphere;   break;
    case EBrushPreviewShape::Box:        NewMesh = CachedMeshCube;     break;
    case EBrushPreviewShape::Capsule:    NewMesh = CachedMeshCapsule;  break;
    case EBrushPreviewShape::Cylinder:   NewMesh = CachedMeshCylinder; break;
    case EBrushPreviewShape::Cone:       NewMesh = CachedMeshCone;     break;
    case EBrushPreviewShape::RoundBox:   NewMesh = CachedMeshCube;     break;
    case EBrushPreviewShape::Ellipsoid:  NewMesh = CachedMeshSphere;   break;
    case EBrushPreviewShape::Torus:      NewMesh = CachedMeshTorus;    break;
    default:                             NewMesh = CachedMeshSphere;   break;
    }

    if (NewMesh)
    {
        PreviewMesh->SetStaticMesh(NewMesh);
        if (MID) PreviewMesh->SetMaterial(0, MID);
        const FBoxSphereBounds B = NewMesh->GetBounds();
        MeshUnitRadius = FMath::Max(1.f, B.SphereRadius);
    }

    CurrentShape = NewShape;
}