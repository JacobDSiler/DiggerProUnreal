#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "DiggerEditorSettings.generated.h"

class UHoleShapeLibrary;
struct FDiggerMeshConfig;     // from runtime
class UDiggerSettings;        // from runtime

/**
 * Editor-only settings for the Digger plugin.
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

// -------------------------------------------------------------------------
// VOXEL SHADING SETTINGS
// -------------------------------------------------------------------------

UENUM(BlueprintType)
enum class EDiggerEditorShadingMode : uint8
{
    // Best for clay, terrain, and organic shapes. Hides grid steps. Seamless.
    OrganicGradient UMETA(DisplayName = "Organic (Gradient)"),

    // Best for hard edges. Sharper, but may show 'terracing' artifacts.
    SurfaceGeometry UMETA(DisplayName = "Surface (Geometry)"),

    // No smoothing. Distinct triangles. Stylized look.
    FlatLowPoly UMETA(DisplayName = "Flat (Low Poly)")
};

UCLASS(Config=Editor, DefaultConfig, meta=(DisplayName="Editor Preview Settings"))
class DIGGEREDITOR_API UDiggerEditorSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UDiggerEditorSettings();
    

    // -------------------------------------------------------------------------
    // VISUAL FEEDBACK (Colors)
    // -------------------------------------------------------------------------

    /** Color of the brush when in 'Add' / 'Fill' mode. */
    UPROPERTY(EditAnywhere, Config, Category="Brush Visualization")
    FLinearColor BrushColorAdd = FLinearColor(0.2f, 1.0f, 0.2f, 0.5f); // Green

    /** Color of the brush when in 'Dig' / 'Remove' mode. */
    UPROPERTY(EditAnywhere, Config, Category="Brush Visualization")
    FLinearColor BrushColorDig = FLinearColor(1.0f, 0.1f, 0.1f, 0.5f); // Red

    /** Color to tint the edges/falloff of the brush. */
    UPROPERTY(EditAnywhere, Config, Category="Brush Visualization")
    FLinearColor BrushColorFalloff = FLinearColor(1.0f, 0.5f, 0.0f, 0.5f); // Orange

    /** How soft the intersection with the ground looks (in cm). */
    UPROPERTY(EditAnywhere, Config, Category="Brush Visualization")
    float BrushDepthFadeDistance = 50.0f;

    UPROPERTY(EditAnywhere, Config, Category="Brush Visualization")
    float BrushOpacity = 0.5f;
    
    /**
     * If true, the brush light will change color to match the BrushColor (Add/Dig).
     * If false, it uses the explicit BrushLightColor below.
     */
    UPROPERTY(EditAnywhere, Config, Category="Brush Light")
    bool bMatchLightColorToBrush = true;

    /** Explicit color for the light if not matching the brush state. */
    UPROPERTY(EditAnywhere, Config, Category="Brush Light", meta=(EditCondition="!bMatchLightColorToBrush"))
    FLinearColor BrushLightColor = FLinearColor::White;

    /** Intensity of the light attached to the brush. Set to 0 to disable. */
    UPROPERTY(EditAnywhere, Config, Category="Brush Light", meta=(ClampMin="0.0"))
    float BrushLightIntensity = 1500.0f;

    /** Radius of the brush light. */
    UPROPERTY(EditAnywhere, Config, Category="Brush Light")
    float BrushLightAttenuationRadius = 1000.0f;

    // -------------------------------------------------------------------------
    // BRUSH SETTINGS
    // -------------------------------------------------------------------------

    UPROPERTY(EditAnywhere, Config, Category="Brush Settings", meta=(ClampMin="0.1", ClampMax="500.0"))
    float ForceMultiplier = 100.0f;

    // -------------------------------------------------------------------------
    // HUD SETTINGS
    // -------------------------------------------------------------------------

    UPROPERTY(EditAnywhere, Config, Category="HUD")
    EBrushHUDPosition BrushHUDPosition = EBrushHUDPosition::UpperLeft;

    UPROPERTY(EditAnywhere, Config, Category="HUD", meta=(EditCondition="BrushHUDPosition == EBrushHUDPosition::Custom"))
    FVector2D CustomHUDOffset = FVector2D(50.f, 50.f);

    // -------------------------------------------------------------------------
    // BRUSH PREVIEW SETTINGS
    // -------------------------------------------------------------------------

    UPROPERTY(EditAnywhere, Config, Category="Brush Controls", meta=(ClampMin="0.1", ClampMax="500.0"))
    float ScrollSpeed = 50.0f;

    /** Whether the preview should snap to the voxel grid. */
    UPROPERTY(EditAnywhere, Config, Category="Digger|Preview")
    bool bSnapPreviewToGrid = true;
    

    // -------------------------------------------------------------------------
    // BRUSH PREVIEW ICON PATHS
    // -------------------------------------------------------------------------

    UPROPERTY(EditAnywhere, Config, Category="Digger|Icons")
    TSoftObjectPtr<UTexture2D> SculptIcon;

    UPROPERTY(EditAnywhere, Config, Category="Digger|Icons")
    TSoftObjectPtr<UTexture2D> RotateIcon;

    UPROPERTY(EditAnywhere, Config, Category="Digger|Icons")
    TSoftObjectPtr<UTexture2D> OffsetIcon;

    UPROPERTY(EditAnywhere, Config, Category="Digger|Icons")
    TSoftObjectPtr<UTexture2D> LoadingIcon;

    // -------------------------------------------------------------------------
    // BRUSH PREVIEW MESHES
    // -------------------------------------------------------------------------

    UPROPERTY(EditAnywhere, Config, Category="Brush Preview", meta=(AllowedClasses="/Script/Engine.StaticMesh"))
    TSoftObjectPtr<UStaticMesh> SphereBrushMesh;

    UPROPERTY(EditAnywhere, Config, Category="Brush Preview", meta=(AllowedClasses="/Script/Engine.StaticMesh"))
    TSoftObjectPtr<UStaticMesh> CubeBrushMesh;

    UPROPERTY(EditAnywhere, Config, Category="Brush Preview", meta=(AllowedClasses="/Script/Engine.StaticMesh"))
    TSoftObjectPtr<UStaticMesh> CylinderBrushMesh;

    UPROPERTY(EditAnywhere, Config, Category="Brush Preview", meta=(AllowedClasses="/Script/Engine.StaticMesh"))
    TSoftObjectPtr<UStaticMesh> CapsuleBrushMesh;

    UPROPERTY(EditAnywhere, Config, Category="Brush Preview", meta=(AllowedClasses="/Script/Engine.StaticMesh"))
    TSoftObjectPtr<UStaticMesh> ConeBrushMesh;

    UPROPERTY(EditAnywhere, Config, Category="Brush Preview", meta=(AllowedClasses="/Script/Engine.StaticMesh"))
    TSoftObjectPtr<UStaticMesh> TorusBrushMesh;

    // -------------------------------------------------------------------------
    // BRUSH SETTINGS
    // -------------------------------------------------------------------------
    
    UPROPERTY(EditAnywhere, config, Category="Smooth Brush")
    bool bSmoothBrushLandscapeAware = false;

    // -------------------------------------------------------------------------
    // MATERIALS
    // -------------------------------------------------------------------------

    UPROPERTY(EditAnywhere, Config, Category="Brush Preview", meta=(AllowedClasses="/Script/Engine.MaterialInterface"))
    TSoftObjectPtr<UMaterialInterface> BrushPreviewMaterial;

    // -------------------------------------------------------------------------
    // UI/HOLES
    // -------------------------------------------------------------------------

    UPROPERTY(EditAnywhere, Config, Category="Outliner")
    bool bShowDynamicHolesFolder = true;

    // -------------------------------------------------------------------------
    // MESHING PREFERENCES
    // -------------------------------------------------------------------------

    /** Distance within which vertices are welded after marching cubes. */
    UPROPERTY(EditAnywhere, Config, Category="Mesh Generation", meta=(ClampMin="0.0001", ClampMax="5.0"))
    float WeldVertexThreshold = 0.02f;

    FDiggerMeshConfig MakeMergedMeshConfig(const UDiggerSettings* Runtime) const;

    // -------------------------------------------------------------------------
    // DEFAULT RESOURCES
    // -------------------------------------------------------------------------

    UPROPERTY(EditAnywhere, Config, Category="Defaults", meta=(AllowedClasses="/Script/Digger.HoleShapeLibrary"))
    TSoftObjectPtr<UHoleShapeLibrary> DefaultHoleLibrary;

    // -------------------------------------------------------------------------
    // MESH SETTINGS
    // -------------------------------------------------------------------------

    UPROPERTY(EditAnywhere, Config, Category="Meshing")
    EDiggerEditorShadingMode ShadingMode = EDiggerEditorShadingMode::OrganicGradient;

    // -------------------------------------------------------------------------
    // VIEWPORT BEHAVIOUR
    // -------------------------------------------------------------------------

    /** When enabled the viewport camera tracks the brush during painting.
     *  Toggle with the  L  key while in Digger mode.
     *  Mirrors the "Camera Follows Brush" checkbox in the toolkit panel. */
    UPROPERTY(EditAnywhere, Config, Category="Viewport")
    bool bCameraFollowsBrush = true;

    // -------------------------------------------------------------------------
    // BRUSH PLACEMENT FALLBACK BEHAVIOUR
    // -------------------------------------------------------------------------

    /** When SmartTrace finds no voxel mesh hit, the fallback normally returns
     *  the landscape surface so the user can keep digging.
     *
     *  If this is TRUE (recommended for open-world games): the landscape hit
     *  is always accepted unless the ray is nearly horizontal AND the hit
     *  point is over a known air voxel (indicating a real tunnel opening).
     *  Sky holes caused by ungenerated mesh are invisible to this check so
     *  they correctly fall back to the landscape surface.
     *
     *  If FALSE: the old aggressive behaviour — any air voxel below the
     *  landscape suppresses the hit, which hides the brush over ungenerated
     *  areas. Only useful for fully pre-baked worlds where every underground
     *  region is guaranteed to have collision. */
    UPROPERTY(EditAnywhere, Config, Category="Brush Placement",
              meta=(DisplayName="Fallback to Landscape When No Mesh Hit"))
    bool bFallbackToLandscapeWhenNoMeshHit = true;

    /** Minimum angle between the camera ray and the DOWN vector required
     *  before the air-voxel suppression activates.
     *  At 0° the ray points straight down — very unlikely to be a tunnel.
     *  At 90° the ray is horizontal — very likely to be shooting through
     *  a tunnel into open sky.
     *
     *  Only used when bFallbackToLandscapeWhenNoMeshHit = true.
     *  Default 60°: rays more horizontal than this AND over an air voxel
     *  will suppress the landscape hit (brush disappears).
     *  Rays steeper than this always return the landscape hit. */
    UPROPERTY(EditAnywhere, Config, Category="Brush Placement",
              meta=(DisplayName="Tunnel Suppression Angle (degrees)",
                    ClampMin="0.0", ClampMax="89.0",
                    EditCondition="bFallbackToLandscapeWhenNoMeshHit"))
    float TunnelSuppressionAngleDeg = 60.f;

public:
    /**
     * Builds a merged mesh config for editor preview.
     * Uses editor shading mode if runtime bUseEditorShadingSettings is true.
     */
    // FDiggerMeshConfig MakeMergedMeshConfig(const class UDiggerSettings* Runtime) const;

    static const UDiggerEditorSettings* Get();
};