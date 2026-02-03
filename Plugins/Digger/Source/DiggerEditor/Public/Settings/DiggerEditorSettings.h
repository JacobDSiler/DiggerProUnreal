#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "DiggerEditorSettings.generated.h"

class UHoleShapeLibrary;

/**
 * Editor‑only settings for the Digger plugin.
 *
 * These settings control how brush shapes and hole previews appear inside the
 * Unreal Editor viewport. They do NOT affect runtime hole spawning or gameplay.
 *
 * Location:
 *   Project Settings → Digger → Editor Preview Settings
 */


// -------------------------------------------------------------------------
// HUD SETTINGS
// -------------------------------------------------------------------------

UENUM(BlueprintType)
enum class EBrushHUDPosition : uint8
{
    UpperLeft,
    UpperRight,
    LowerLeft,
    LowerRight,
    Custom
};

UCLASS(Config=Editor, DefaultConfig, meta=(DisplayName="Editor Preview Settings"))
class DIGGEREDITOR_API UDiggerEditorSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UDiggerEditorSettings();


    // -------------------------------------------------------------------------
    // HUD SETTINGS
    // -------------------------------------------------------------------------

    // HUD Messages Positioning

    UPROPERTY(EditAnywhere, Config, Category="HUD")
    EBrushHUDPosition BrushHUDPosition = EBrushHUDPosition::UpperLeft;

    UPROPERTY(EditAnywhere, Config, Category="HUD", meta=(EditCondition="BrushHUDPosition == EBrushHUDPosition::Custom"))
    FVector2D CustomHUDOffset = FVector2D(50.f, 50.f);
    
    
    // -------------------------------------------------------------------------
    // BRUSH PREVIEW SETTINGS
    // -------------------------------------------------------------------------

    /** Multiplier applied to scroll-wheel impulses when adjusting brush settings. */
    UPROPERTY(EditAnywhere, config, Category="Brush Controls", meta=(ClampMin="0.1", ClampMax="500.0"))
    float ScrollSpeed = 50.0f;


    // -------------------------------------------------------------------------
    // BRUSH PREVIEW MESHES
    // -------------------------------------------------------------------------

    /**
     * Mesh used to preview a spherical brush in the editor viewport.
     * This is purely visual and does not affect runtime hole geometry.
     */
    UPROPERTY(EditAnywhere, Config, Category="Brush Preview",
        meta=(AllowedClasses="/Script/Engine.StaticMesh",
              ToolTip="Mesh used to preview a spherical brush in the editor viewport. Purely visual; does not affect runtime hole generation."))
    TSoftObjectPtr<UStaticMesh> SphereBrushMesh;

    /**
     * Mesh used to preview a cube brush in the editor viewport.
     * Only affects editor visualization, not runtime digging behavior.
     */
    UPROPERTY(EditAnywhere, Config, Category="Brush Preview",
        meta=(AllowedClasses="/Script/Engine.StaticMesh",
              ToolTip="Mesh used to preview a cube brush shape. Editor‑only visualization; does not affect runtime hole generation."))
    TSoftObjectPtr<UStaticMesh> CubeBrushMesh;

    /**
     * Mesh used to preview a cylindrical brush.
     * Useful for visualizing tunnel‑like or column‑shaped brush strokes.
     */
    UPROPERTY(EditAnywhere, Config, Category="Brush Preview",
        meta=(AllowedClasses="/Script/Engine.StaticMesh",
              ToolTip="Mesh used to preview a cylindrical brush. Helpful for tunnel‑shaped or column‑shaped digging previews."))
    TSoftObjectPtr<UStaticMesh> CylinderBrushMesh;

    /**
     * Mesh used to preview a capsule brush.
     * Ideal for smooth, rounded tunnel shapes or sweeping dig motions.
     */
    UPROPERTY(EditAnywhere, Config, Category="Brush Preview",
        meta=(AllowedClasses="/Script/Engine.StaticMesh",
              ToolTip="Mesh used to preview a capsule brush. Useful for smooth, rounded tunnel or sweep‑based brush previews."))
    TSoftObjectPtr<UStaticMesh> CapsuleBrushMesh;

    /**
     * Mesh used to preview a cone‑shaped brush.
     * Purely visual; does not affect runtime hole geometry.
     */
    UPROPERTY(EditAnywhere, Config, Category="Brush Preview",
        meta=(AllowedClasses="/Script/Engine.StaticMesh",
              ToolTip="Mesh used to preview a cone‑shaped brush. Editor‑only visualization; does not affect runtime hole geometry."))
    TSoftObjectPtr<UStaticMesh> ConeBrushMesh;

    /**
     * Mesh used to preview a torus (donut‑shaped) brush.
     * Useful for visualizing ring‑shaped or hollow brush effects.
     */
    UPROPERTY(EditAnywhere, Config, Category="Brush Preview",
        meta=(AllowedClasses="/Script/Engine.StaticMesh",
              ToolTip="Mesh used to preview a torus (donut‑shaped) brush. Ideal for ring‑shaped or hollow brush previews."))
    TSoftObjectPtr<UStaticMesh> TorusBrushMesh;

    // -------------------------------------------------------------------------
    // MATERIALS
    // -------------------------------------------------------------------------

    /**
     * Material applied to all brush preview meshes in the editor viewport.
     * A translucent or emissive material is recommended for clarity.
     */
    UPROPERTY(EditAnywhere, Config, Category="Brush Preview",
        meta=(AllowedClasses="/Script/Engine.MaterialInterface",
              ToolTip="Material applied to all brush preview meshes. A translucent or emissive material is recommended for clear visibility over terrain."))
    TSoftObjectPtr<UMaterialInterface> BrushPreviewMaterial;

    // -------------------------------------------------------------------------
    // DEFAULT RESOURCES
    // -------------------------------------------------------------------------

    /**
     * Default Hole Shape Library used by the editor when previewing hole shapes.
     * This does NOT affect runtime hole spawning — runtime uses UDiggerSettings.
     */
    UPROPERTY(EditAnywhere, Config, Category="Defaults",
        meta=(AllowedClasses="/Script/Digger.HoleShapeLibrary",
              ToolTip="Default Hole Shape Library used for editor previews. Does not affect runtime hole spawning; runtime uses UDiggerSettings."))
    TSoftObjectPtr<UHoleShapeLibrary> DefaultHoleLibrary;

public:
    static const UDiggerEditorSettings* Get();
};
