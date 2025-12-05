// ABrushPreviewActor.cpp
#include "BrushPreviewActor.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

// Cached custom meshes used for preview shapes not provided by Engine basics
static UStaticMesh* MeshRoundBox  = nullptr;
static UStaticMesh* MeshEllipsoid = nullptr;
static UStaticMesh* MeshTorus     = nullptr;

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

    // Show in editor
    SetActorHiddenInGame(true);          // hidden in PIE
    PreviewMesh->SetHiddenInGame(true);  // hidden in PIE
    PreviewMesh->SetVisibility(true, true); // visible in editor viewport
}

void ABrushPreviewActor::SetVisible(bool bVisible)
{
    // Visibility in the editor viewport:
    PreviewMesh->SetVisibility(bVisible, true);

    // Keep it hidden in PIE / game so it never leaks into runtime
    SetActorHiddenInGame(true);
    PreviewMesh->SetHiddenInGame(true);
}


void ABrushPreviewActor::Initialize(UStaticMesh* ShapeMesh, UMaterialInterface* BaseMat)
{
    // Preload all meshes up front so switching shapes later is seamless.
    EnsureMeshesLoaded();

    if (ShapeMesh)
        PreviewMesh->SetStaticMesh(ShapeMesh);

    if (BaseMat)
    {
        MID = UMaterialInstanceDynamic::Create(BaseMat, this);
        PreviewMesh->SetMaterial(0, MID);
    }
}


static inline float Snap(float V, float Cell){ return Cell > 0 ? FMath::GridSnap(V, Cell) : V; }

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
    auto Snap = [Cell](float v){ return FMath::GridSnap(v, Cell); };
    const FVector Snapped(Snap(CenterWS.X), Snap(CenterWS.Y), Snap(CenterWS.Z));
    SetActorLocation(Snapped);
    SetActorRotation(RotationWS);

    // Ensure the right mesh is displayed
    SetShape(Shape);

    // Scale by desired world radii vs mesh’s native radius
    const FVector SafeR(
        FMath::Max(0.5f*Cell, RadiusXYZ.X),
        FMath::Max(0.5f*Cell, RadiusXYZ.Y),
        FMath::Max(0.5f*Cell, RadiusXYZ.Z)
    );
    const FVector Scale(SafeR.X / MeshUnitRadius,
                        SafeR.Y / MeshUnitRadius,
                        SafeR.Z / MeshUnitRadius);
    PreviewMesh->SetWorldScale3D(Scale);

    if (MID)
    {
        MID->SetScalarParameterValue(TEXT("Falloff"), FMath::Clamp(Falloff, 0.f, 1.f));
        MID->SetScalarParameterValue(TEXT("CellSize"), Cell);
        MID->SetScalarParameterValue(TEXT("IsAdd"), bAddMode ? 1.f : 0.f);
        MID->SetScalarParameterValue(TEXT("ShapeType"), (float)Shape);
        MID->SetVectorParameterValue(TEXT("InvScale"),
            FLinearColor(Scale.X>0?1.f/Scale.X:0.f, Scale.Y>0?1.f/Scale.Y:0.f, Scale.Z>0?1.f/Scale.Z:0.f, 0.f));
    }
}

void ABrushPreviewActor::EnsureMeshesLoaded()
{
    auto Load = [](UStaticMesh*& Out, const TCHAR* Path)
    {
        if (!Out) Out = LoadObject<UStaticMesh>(nullptr, Path);
    };
    Load(MeshSphere,   TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    Load(MeshCube,     TEXT("/Engine/BasicShapes/Cube.Cube"));
    Load(MeshCapsule,  TEXT("/Engine/BasicShapes/Capsule.Capsule"));
    Load(MeshCylinder, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    Load(MeshCone,     TEXT("/Engine/BasicShapes/Cone.Cone"));
    Load(MeshRoundBox,  TEXT("/Game/DynamicHoles/HoleMeshes/DH_Cub.DH_Cub"));
    Load(MeshEllipsoid, TEXT("/Game/DynamicHoles/HoleMeshes/DH_Sphere.DH_Sphere"));
    Load(MeshTorus,     TEXT("/Game/DynamicHoles/HoleMeshes/DH_Torus.DH_Torus"));
}

void ABrushPreviewActor::SetShape(EBrushPreviewShape NewShape)
{
    if (CurrentShape == NewShape) return;
    EnsureMeshesLoaded();

    UStaticMesh* NewMesh = MeshSphere; // default
    switch (NewShape)
    {
    case EBrushPreviewShape::Sphere:     NewMesh = MeshSphere;     break;
    case EBrushPreviewShape::Box:        NewMesh = MeshCube;       break;
    case EBrushPreviewShape::Capsule:    NewMesh = MeshCapsule;    break;
    case EBrushPreviewShape::Cylinder:   NewMesh = MeshCylinder;   break;
    case EBrushPreviewShape::Cone:       NewMesh = MeshCone;       break;
    case EBrushPreviewShape::RoundBox:   NewMesh = MeshRoundBox;  break;
    case EBrushPreviewShape::Ellipsoid:  NewMesh = MeshEllipsoid; break; // will non-uniform scale
    case EBrushPreviewShape::Torus:      NewMesh = MeshTorus;     break;
    }

    if (NewMesh)
    {
        PreviewMesh->SetStaticMesh(NewMesh);
        if (MID)
        {
            // Ensure our preview material stays applied when swapping meshes.
            PreviewMesh->SetMaterial(0, MID);
        }

        // Update cached unit radius from the new mesh
        const FBoxSphereBounds B = NewMesh->GetBounds();
        MeshUnitRadius = FMath::Max(1.f, B.SphereRadius);
    }

    CurrentShape = NewShape;
}