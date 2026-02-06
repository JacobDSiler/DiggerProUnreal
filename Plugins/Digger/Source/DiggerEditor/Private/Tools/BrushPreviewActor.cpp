// BrushPreviewActor.cpp

#include "BrushPreviewActor.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "DiggerEditorSettings.h"
#include "BrushPreviewLightComponent.h"
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

    PreviewMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PreviewMesh"));
    RootComponent = PreviewMesh;

    PreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PreviewMesh->SetCastShadow(false);
    PreviewMesh->SetReceivesDecals(false);
    PreviewMesh->SetMobility(EComponentMobility::Movable);

    // Show in editor only
    SetActorHiddenInGame(true);          // hidden in PIE
    PreviewMesh->SetHiddenInGame(true);  // hidden in PIE
    PreviewMesh->SetVisibility(true, true); // visible in editor viewport

    ModeIndicator = CreateDefaultSubobject<UBillboardComponent>(TEXT("ModeIndicator"));
    ModeIndicator->SetupAttachment(RootComponent);
    ModeIndicator->SetHiddenInGame(true);
    ModeIndicator->SetVisibility(false);
    ModeIndicator->SetRelativeLocation(FVector(0, 0, 120)); // above brush

    // Load textures (replace with your actual paths)
    SculptSprite = LoadObject<UTexture2D>(nullptr, TEXT("/Digger/Digger/Icons/SculptIcon.SculptIcon"));
    RotateSprite = LoadObject<UTexture2D>(nullptr, TEXT("/Digger/Digger/Icons/RotateIcon.RotateIcon"));
    OffsetSprite = LoadObject<UTexture2D>(nullptr, TEXT("/Digger/Digger/Icons/OffsetIcon.OffsetIcon"));
}


void ABrushPreviewActor::SetVisible(bool bVisible)
{
    // Visibility in the editor viewport:
    PreviewMesh->SetVisibility(bVisible, true);

    // Always hidden in PIE / game
    SetActorHiddenInGame(true);
    PreviewMesh->SetHiddenInGame(true);
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

static inline float SnapFloat(float V, float Cell)
{
    return Cell > 0.f ? FMath::GridSnap(V, Cell) : V;
}

void ABrushPreviewActor::UpdatePreview(
    const FVector& CenterWS,
    const FVector& RadiusXYZ,
    float Falloff,
    bool bAddMode,
    float CellSize,
    EBrushPreviewShape Shape,
    const FQuat& RotationWS)
{
    const float Cell = FMath::Max(1.f, CellSize);
    auto Snap = [Cell](float v) { return FMath::GridSnap(v, Cell); };

    const FVector Snapped(
        Snap(CenterWS.X),
        Snap(CenterWS.Y),
        Snap(CenterWS.Z));

    SetActorLocation(Snapped);
    SetActorRotation(RotationWS);

    // Ensure the right mesh is displayed
    SetShape(Shape);

    // Scale by desired world radii vs mesh’s native radius
    const FVector SafeR(
        FMath::Max(0.5f * Cell, RadiusXYZ.X),
        FMath::Max(0.5f * Cell, RadiusXYZ.Y),
        FMath::Max(0.5f * Cell, RadiusXYZ.Z));

    const FVector Scale(
        SafeR.X / MeshUnitRadius,
        SafeR.Y / MeshUnitRadius,
        SafeR.Z / MeshUnitRadius);

    PreviewMesh->SetWorldScale3D(Scale);

    if (MID)
    {
        MID->SetScalarParameterValue(TEXT("Falloff"), FMath::Clamp(Falloff, 0.f, 1.f));
        MID->SetScalarParameterValue(TEXT("CellSize"), Cell);
        MID->SetScalarParameterValue(TEXT("IsAdd"), bAddMode ? 1.f : 0.f);
        MID->SetScalarParameterValue(TEXT("ShapeType"), static_cast<float>(Shape));
        MID->SetVectorParameterValue(
            TEXT("InvScale"),
            FLinearColor(
                Scale.X > 0.f ? 1.f / Scale.X : 0.f,
                Scale.Y > 0.f ? 1.f / Scale.Y : 0.f,
                Scale.Z > 0.f ? 1.f / Scale.Z : 0.f,
                0.f));
    }
}

void ABrushPreviewActor::EnsureMeshesLoaded()
{
    // Only load once
    if (CachedMeshSphere)
    {
        return;
    }

    const UDiggerEditorSettings* Settings = UDiggerEditorSettings::Get();
    if (!Settings)
    {
        return;
    }

    // Editor-only, one-time synchronous loads are fine here
    CachedMeshSphere   = Settings->SphereBrushMesh.LoadSynchronous();
    CachedMeshCube     = Settings->CubeBrushMesh.LoadSynchronous();
    CachedMeshCapsule  = Settings->CapsuleBrushMesh.LoadSynchronous();
    CachedMeshCylinder = Settings->CylinderBrushMesh.LoadSynchronous();
    CachedMeshCone     = Settings->ConeBrushMesh.LoadSynchronous();
    CachedMeshTorus    = Settings->TorusBrushMesh.LoadSynchronous();
}

void ABrushPreviewActor::SetShape(EBrushPreviewShape NewShape)
{
    // If already set and mesh is valid, no need to change
    if (CurrentShape == NewShape && PreviewMesh->GetStaticMesh() != nullptr)
    {
        return;
    }

    EnsureMeshesLoaded();

    UStaticMesh* NewMesh = CachedMeshSphere; // default fallback

    switch (NewShape)
    {
    case EBrushPreviewShape::Sphere:     NewMesh = CachedMeshSphere;   break;
    case EBrushPreviewShape::Box:        NewMesh = CachedMeshCube;     break;
    case EBrushPreviewShape::Capsule:    NewMesh = CachedMeshCapsule;  break;
    case EBrushPreviewShape::Cylinder:   NewMesh = CachedMeshCylinder; break;
    case EBrushPreviewShape::Cone:       NewMesh = CachedMeshCone;     break;
    case EBrushPreviewShape::RoundBox:   NewMesh = CachedMeshCube;     break;   // round box uses cube mesh + scale
    case EBrushPreviewShape::Ellipsoid:  NewMesh = CachedMeshSphere;   break;   // ellipsoid uses sphere mesh + non-uniform scale
    case EBrushPreviewShape::Torus:      NewMesh = CachedMeshTorus;    break;
    default:                             NewMesh = CachedMeshSphere;   break;
    }

    if (NewMesh)
    {
        PreviewMesh->SetStaticMesh(NewMesh);

        if (MID)
        {
            // Keep preview material applied when swapping meshes
            PreviewMesh->SetMaterial(0, MID);
        }

        const FBoxSphereBounds B = NewMesh->GetBounds();
        MeshUnitRadius = FMath::Max(1.f, B.SphereRadius);
    }

    CurrentShape = NewShape;
}
