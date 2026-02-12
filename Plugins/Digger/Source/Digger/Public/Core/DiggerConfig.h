// DiggerConfig.h
#pragma once

#include "CoreMinimal.h"
#include "DiggerConfig.generated.h"

// The Enum lives here so Runtime classes (Chunk, Mesher) can see it
UENUM(BlueprintType)
enum class EDiggerShadingMode : uint8
{
	// Best for clay, organic terrain. Hides grid steps. Seamless.
	OrganicGradient UMETA(DisplayName = "Organic (Gradient)"),
    
	// Best for hard edges. Sharper, but shows 'terracing'.
	SurfaceGeometry UMETA(DisplayName = "Surface (Geometry)"),
    
	// No smoothing. Distinct triangles. Stylized look.
	FlatLowPoly UMETA(DisplayName = "Flat (Low Poly)")
};

// A lightweight struct to pass settings into the mesher
USTRUCT(BlueprintType)
struct DIGGER_API FDiggerMeshConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EDiggerShadingMode ShadingMode = EDiggerShadingMode::OrganicGradient;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float SkirtRadius = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float ClipBias = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float IsoLevel = 0.0f;

	private:
		static FDiggerMeshConfig* EditorPreviewOverride;

	public:
		static void SetEditorPreviewOverride(const FDiggerMeshConfig& InConfig)
		{
#if WITH_EDITOR
			if (!EditorPreviewOverride)
			{
				EditorPreviewOverride = new FDiggerMeshConfig();
			}
			*EditorPreviewOverride = InConfig;
#endif
		}

		static FDiggerMeshConfig* GetEditorPreviewOverride()
		{
#if WITH_EDITOR
			return EditorPreviewOverride;
#else
			return nullptr;
#endif
		}
	};

