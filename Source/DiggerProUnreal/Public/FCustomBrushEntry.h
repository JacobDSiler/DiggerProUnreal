// FCustomBrushEntry.h

#pragma once

#include "CoreMinimal.h"
#include "Engine/StaticMesh.h"

struct FCustomBrushEntry
{
    TSoftObjectPtr<UStaticMesh> Mesh;      // The original mesh, if any
    FString SDFBrushFilePath;              // Path to the SDF brush file, if converted
    TSharedPtr<FAssetThumbnail> Thumbnail; // For UI display

    FCustomBrushEntry() {}

    bool IsSDF() const { return !SDFBrushFilePath.IsEmpty(); }
    bool IsMesh() const { return Mesh.IsValid(); }
};

