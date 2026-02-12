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

public:
    /**
     * Builds a merged mesh config for editor preview.
     * Uses editor shading mode if runtime bUseEditorShadingSettings is true.
     */
    // FDiggerMeshConfig MakeMergedMeshConfig(const class UDiggerSettings* Runtime) const;

    static const UDiggerEditorSettings* Get();
};
