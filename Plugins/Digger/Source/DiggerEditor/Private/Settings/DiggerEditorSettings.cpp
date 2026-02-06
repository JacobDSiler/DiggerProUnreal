#include "DiggerEditorSettings.h"


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

	ScrollSpeed = 150.0f;

	// Meshes
	SphereBrushMesh     = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Digger/Digger/DynamicHoles/HoleMeshes/DH_Sphere.DH_Sphere")));
	CubeBrushMesh       = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Digger/Digger/DynamicHoles/HoleMeshes/DH_Cube.DH_Cube")));
	CylinderBrushMesh   = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Digger/Digger/DynamicHoles/HoleMeshes/DH_Cylinder.DH_Cylinder")));
	CapsuleBrushMesh    = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Digger/Digger/DynamicHoles/HoleMeshes/DH_Capsule.DH_Capsule")));
	ConeBrushMesh       = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Digger/Digger/DynamicHoles/HoleMeshes/DH_Cone.DH_Cone")));
	TorusBrushMesh      = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Digger/Digger/DynamicHoles/HoleMeshes/DH_Torus.DH_Torus")));

	// UI/Holes
	bShowDynamicHolesFolder = true; // default visible

	// Meshing
	WeldVertexThreshold = 0.02f;

	
	BrushPreviewMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Digger/Digger/DiggerEditor/M_DiggerBrushPreview.M_DiggerBrushPreview")));
    
	DefaultHoleLibrary   = TSoftObjectPtr<UHoleShapeLibrary>(FSoftObjectPath(TEXT("/Digger/Digger/BluePrints/HoleShapeLibrary.HoleShapeLibrary")));
}

const UDiggerEditorSettings* UDiggerEditorSettings::Get()
{
	return GetDefault<UDiggerEditorSettings>();
}