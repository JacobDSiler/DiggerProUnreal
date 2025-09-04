#pragma once

#include "CoreMinimal.h"
#include "StaticMeshResources.h"
#include "Engine/StaticMesh.h"
#include "FCustomSDFBrush.h" // Include your custom brush struct here

class FBrushAssetEditorUtils
{
public:
	static bool SaveSDFBrushToFile(const FCustomSDFBrush& Brush, const FString& FilePath);
	static bool LoadSDFBrushFromFile(const FString& FilePath, FCustomSDFBrush& OutBrush);
	static bool GenerateSDFBrushFromStaticMesh(UStaticMesh* Mesh, const FTransform& MeshTransform, float InVoxelSize, FCustomSDFBrush& OutBrush);
};

