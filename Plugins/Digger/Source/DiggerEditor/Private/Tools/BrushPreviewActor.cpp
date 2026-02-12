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

    PreviewMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PreviewMesh"));
    RootComponent = PreviewMesh;

    PreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PreviewMesh->SetCastShadow(false);
    PreviewMesh->SetReceivesDecals(false);
    PreviewMesh->SetMobility(EComponentMobility::Movable);

    AActor::SetActorHiddenInGame(true);
    PreviewMesh->SetHiddenInGame(true);
    PreviewMesh->SetVisibility(true, true);

    ModeIndicator = CreateDefaultSubobject<UBillboardComponent>(TEXT("ModeIndicator"));
    ModeIndicator->SetupAttachment(RootComponent);
    ModeIndicator->SetHiddenInGame(true);
    ModeIndicator->SetVisibility(false);
    ModeIndicator->SetRelativeLocation(FVector(0, 0, 120));
    ModeIndicator->SetRelativeScale3D(FVector(10.0f));
    ModeIndicator->bIsScreenSizeScaled = true; // optional but recommended
    ModeIndicator->SetHiddenInGame(true);
    ModeIndicator->SetVisibility(false);
    ModeIndicator->SetRelativeLocation(FVector(0, 0, 50));
    ModeIndicator->SetRelativeScale3D(FVector(10.0f));
    ModeIndicator->bIsScreenSizeScaled = true; // optional but recommended


    // ---------------------------------------------------------------------
    // Correct plugin content paths for icons
    // ---------------------------------------------------------------------
    const UDiggerEditorSettings* Settings = UDiggerEditorSettings::Get();

    if (Settings)
    {
        SculptSprite  = Settings->SculptIcon.LoadSynchronous();
        RotateSprite  = Settings->RotateIcon.LoadSynchronous();
        OffsetSprite  = Settings->OffsetIcon.LoadSynchronous();
    }
    

    // Debug logs
    if (!SculptSprite) UE_LOG(LogTemp, Error, TEXT("Failed to load SculptSprite"));
    if (!RotateSprite) UE_LOG(LogTemp, Error, TEXT("Failed to load RotateSprite"));
    if (!OffsetSprite) UE_LOG(LogTemp, Error, TEXT("Failed to load OffsetSprite"));
}



void ABrushPreviewActor::SetVisible(bool bVisible)
{
    // Visibility in the editor viewport:
    PreviewMesh->SetVisibility(bVisible, true);

    // Always hidden in PIE / game
    SetActorHiddenInGame(true);
    PreviewMesh->SetHiddenInGame(true);
}

void ABrushPreviewActor::UpdateModeIcon(EDiggerMainMode Mode)
{
    if (!ModeIndicator)
        return;

    // Choose sprite
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

    // Visibility rules
    const bool bShow =
        (Mode == EDiggerMainMode::Rotate ||
         Mode == EDiggerMainMode::Offset);

    ModeIndicator->SetVisibility(bShow);
    ModeIndicator->SetHiddenInGame(true); // always hidden in PIE
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
    float CellSize,                 // ignored for snapping now
    EBrushPreviewShape Shape,
    const FQuat& RotationWS)
{
    const UDiggerEditorSettings* Settings = UDiggerEditorSettings::Get();

    // ---------------------------------------------------------
    // 1. Determine snapping behavior
    // ---------------------------------------------------------
    const bool bSnapToGrid =
        Settings ? Settings->bSnapPreviewToGrid : true;

    // Use voxel size directly from FVoxelConversion
    const float VoxelSize = FVoxelConversion::LocalVoxelSize;
    const float Cell = FMath::Max(1.f, VoxelSize);

    FVector FinalCenter = CenterWS;

    if (bSnapToGrid)
    {
        auto Snap = [Cell](float v) { return FMath::GridSnap(v, Cell); };

        FinalCenter = FVector(
            Snap(CenterWS.X),
            Snap(CenterWS.Y),
            Snap(CenterWS.Z));
    }

    // ---------------------------------------------------------
    // 2. Apply transform
    // ---------------------------------------------------------
    SetActorLocation(FinalCenter);
    SetActorRotation(RotationWS);

    // ---------------------------------------------------------
    // 3. Ensure correct mesh
    // ---------------------------------------------------------
    SetShape(Shape);

    // ---------------------------------------------------------
    // 4. Scale preview mesh
    // ---------------------------------------------------------
    const FVector SafeR(
        FMath::Max(0.5f * Cell, RadiusXYZ.X),
        FMath::Max(0.5f * Cell, RadiusXYZ.Y),
        FMath::Max(0.5f * Cell, RadiusXYZ.Z));

    const FVector Scale(
        SafeR.X / MeshUnitRadius,
        SafeR.Y / MeshUnitRadius,
        SafeR.Z / MeshUnitRadius);

    PreviewMesh->SetWorldScale3D(Scale);

    // ---------------------------------------------------------
    // 5. Update material parameters
    // ---------------------------------------------------------
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
