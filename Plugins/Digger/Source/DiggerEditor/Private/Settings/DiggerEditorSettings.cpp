#include "DiggerEditorSettings.h"
#include "DiggerSettings.h" // safe: editor → runtime
#include "Core/DiggerConfig.h"



UDiggerEditorSettings::UDiggerEditorSettings()
{
	CategoryName = TEXT("Digger");
	SectionName = TEXT("Editor Preview Settings");

	// Default Colors (Green for Add, Red for Dig)
	BrushColorAdd = FLinearColor(0.1f, 1.0f, 0.2f, 0.4f);
	BrushColorDig = FLinearColor(1.0f, 0.1f, 0.1f, 0.4f);

	// Light Defaults
	bMatchLightColorToBrush = true;
	BrushLightIntensity = 3000.0f; // Bright enough for caves
	BrushLightAttenuationRadius = 1500.0f;
	BrushLightColor = FLinearColor::White;

	// Brush Settings
	ForceMultiplier = 300.0f;
	ScrollSpeed = 150.0f;

	// Meshes
	SphereBrushMesh     = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Digger/Digger/DynamicHoles/HoleMeshes/DH_Sphere.DH_Sphere")));
	CubeBrushMesh       = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Digger/Digger/DynamicHoles/HoleMeshes/DH_Cube.DH_Cube")));
	CylinderBrushMesh   = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Digger/Digger/DynamicHoles/HoleMeshes/DH_Cylinder.DH_Cylinder")));
	CapsuleBrushMesh    = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Digger/Digger/DynamicHoles/HoleMeshes/DH_Capsule.DH_Capsule")));
	ConeBrushMesh       = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Digger/Digger/DynamicHoles/HoleMeshes/DH_Cone.DH_Cone")));
	TorusBrushMesh      = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Digger/Digger/DynamicHoles/HoleMeshes/DH_Torus.DH_Torus")));

	bSmoothBrushLandscapeAware = false;
	bCameraFollowsBrush = true; // follows brush by default; persisted via Config=Editor
	bFallbackToLandscapeWhenNoMeshHit = true;
	TunnelSuppressionAngleDeg         = 60.f;
	
	// UI/Holes
	bShowDynamicHolesFolder = false; // default visible

	// Meshing
	WeldVertexThreshold = 0.02f;


	SculptIcon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath("/Digger/Digger/Icons/SculptIcon.SculptIcon"));
	RotateIcon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath("/Digger/Digger/Icons/RotateIcon.RotateIcon"));
	OffsetIcon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath("/Digger/Digger/Icons/TransformIcon.TransformIcon"));
	
	
	BrushPreviewMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Digger/Digger/DiggerEditor/M_DiggerBrushPreview.M_DiggerBrushPreview")));
    
	DefaultHoleLibrary   = TSoftObjectPtr<UHoleShapeLibrary>(FSoftObjectPath(TEXT("/Digger/Digger/BluePrints/HoleShapeLibrary.HoleShapeLibrary")));
}



FDiggerMeshConfig UDiggerEditorSettings::MakeMergedMeshConfig(const UDiggerSettings* Runtime) const
{
	FDiggerMeshConfig Config;

	Config.SkirtRadius = Runtime->SkirtRadiusWorld;
	Config.ClipBias    = Runtime->SkirtClipBias;
	Config.IsoLevel    = 0.0f;

	if (Runtime->bUseEditorShadingSettings)
	{
		switch (ShadingMode)
		{
		case EDiggerEditorShadingMode::OrganicGradient:
			Config.ShadingMode = EDiggerShadingMode::OrganicGradient;
			break;

		case EDiggerEditorShadingMode::SurfaceGeometry:
			Config.ShadingMode = EDiggerShadingMode::SurfaceGeometry;
			break;

		case EDiggerEditorShadingMode::FlatLowPoly:
			Config.ShadingMode = EDiggerShadingMode::FlatLowPoly;
			break;
		}
	}
	else
	{
		Config.ShadingMode = Runtime->ShadingMode;
	}

	return Config;
}



const UDiggerEditorSettings* UDiggerEditorSettings::Get()
{
	return GetDefault<UDiggerEditorSettings>();
}