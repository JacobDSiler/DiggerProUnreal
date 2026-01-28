#include "DiggerEditorSettings.h"

UDiggerEditorSettings::UDiggerEditorSettings()
{
	// Initialize with the paths you were previously hardcoding.
	// This provides a fallback so existing setups don't break, 
	// but the strings are now isolated to this one file.

	SphereBrushMesh     = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Digger/Digger/DynamicHoles/HoleMeshes/DH_Sphere.DH_Sphere")));
	CubeBrushMesh       = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Digger/Digger/DynamicHoles/HoleMeshes/DH_Cube.DH_Cube")));
	CylinderBrushMesh   = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Digger/Digger/DynamicHoles/HoleMeshes/DH_Cylinder.DH_Cylinder")));
	CapsuleBrushMesh    = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Digger/Digger/DynamicHoles/HoleMeshes/DH_Capsule.DH_Capsule")));
	ConeBrushMesh       = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Digger/Digger/DynamicHoles/HoleMeshes/DH_Cone.DH_Cone")));
	TorusBrushMesh      = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Digger/Digger/DynamicHoles/HoleMeshes/DH_Torus.DH_Torus")));
    
	BrushPreviewMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Digger/Digger/DiggerEditor/M_DiggerBrushPreview.M_DiggerBrushPreview")));
    
	DefaultHoleLibrary   = TSoftObjectPtr<UHoleShapeLibrary>(FSoftObjectPath(TEXT("/Digger/Digger/BluePrints/HoleShapeLibrary.HoleShapeLibrary")));
}

const UDiggerEditorSettings* UDiggerEditorSettings::Get()
{
	return GetDefault<UDiggerEditorSettings>();
}