#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "DiggerEditorSettings.generated.h"

class UHoleShapeLibrary;

/**
 * Configurable settings for the Digger Editor Plugin.
 * Exposed in Project Settings -> Editor -> Digger.
 */
UCLASS(Config=Editor, DefaultConfig, meta=(DisplayName="Digger"))
class DIGGEREDITOR_API UDiggerEditorSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UDiggerEditorSettings();

    // --- Preview Meshes ---
    // Used by ABrushPreviewActor to show the brush shape in the viewport.

    UPROPERTY(EditAnywhere, Config, Category = "Brush Preview", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
    TSoftObjectPtr<UStaticMesh> SphereBrushMesh;

    UPROPERTY(EditAnywhere, Config, Category = "Brush Preview", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
    TSoftObjectPtr<UStaticMesh> CubeBrushMesh;

    UPROPERTY(EditAnywhere, Config, Category = "Brush Preview", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
    TSoftObjectPtr<UStaticMesh> CylinderBrushMesh;

    UPROPERTY(EditAnywhere, Config, Category = "Brush Preview", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
    TSoftObjectPtr<UStaticMesh> CapsuleBrushMesh;

    UPROPERTY(EditAnywhere, Config, Category = "Brush Preview", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
    TSoftObjectPtr<UStaticMesh> ConeBrushMesh;

    UPROPERTY(EditAnywhere, Config, Category = "Brush Preview", meta = (AllowedClasses = "/Script/Engine.StaticMesh"))
    TSoftObjectPtr<UStaticMesh> TorusBrushMesh;

    // --- Materials ---

    UPROPERTY(EditAnywhere, Config, Category = "Brush Preview", meta = (AllowedClasses = "/Script/Engine.MaterialInterface"))
    TSoftObjectPtr<UMaterialInterface> BrushPreviewMaterial;

    // --- Default Resources ---

    UPROPERTY(EditAnywhere, Config, Category = "Defaults", meta = (AllowedClasses = "/Script/Digger.HoleShapeLibrary"))
    TSoftObjectPtr<UHoleShapeLibrary> DefaultHoleLibrary;

public:
    // Helper to get the settings object easily
    static const UDiggerEditorSettings* Get();
};