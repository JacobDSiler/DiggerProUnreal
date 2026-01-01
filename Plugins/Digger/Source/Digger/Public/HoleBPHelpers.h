#pragma once

#include "CoreMinimal.h"
#include "Engine/StaticMesh.h"
#include "HoleShapeLibrary.h"
#include "FHoleShape.h"

// 1. Fixed Case Sensitivity ('Blueprints' -> 'BluePrints')
static const TCHAR* GDefaultHoleLibraryPath = TEXT("/Digger/Digger/BluePrints/HoleShapeLibrary.HoleShapeLibrary");

// 2. This was actually correct, but ensures we match the folder structure
static const TCHAR* GDefaultHoleBPPath = TEXT("/Digger/Digger/DynamicHoles/BP_MeshHole.BP_MeshHole_C");

// 3. REMOVED '/Content/' from the path
static const TCHAR* GHoleMeshesFolder = TEXT("/Digger/Digger/DynamicHoles/HoleMeshes");



// Ensures the given class reference is loaded from the default path if not set
inline void EnsureDefaultHoleBP(TSubclassOf<AActor>& HoleBPRef)
{
#if WITH_EDITOR
	if (!HoleBPRef)
	{
		FSoftObjectPath AssetPath(GDefaultHoleBPPath);
		UObject* LoadedObj = AssetPath.TryLoad();
		if (UClass* LoadedClass = Cast<UClass>(LoadedObj))
		{
			HoleBPRef = LoadedClass;
			UE_LOG(LogTemp, Log, TEXT("Loaded Default HoleBP from %s"), GDefaultHoleBPPath);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Failed to load HoleBP from %s"), GDefaultHoleBPPath);
		}
	}
#endif
}

inline void SeedHoleShapesFromFolder(UHoleShapeLibrary* Lib)
{
    if (!IsValid(Lib))
    {
        return;
    }

    // Base path where the hole meshes live. Each mesh is expected to be named
    // "DH_<ShapeName>" where <ShapeName> matches the EHoleShapeType value.
    const FString BasePath = GHoleMeshesFolder;

    Lib->ShapeMeshMappings.Empty();

    UEnum* EnumPtr = StaticEnum<EHoleShapeType>();
    if (!EnumPtr)
    {
        return;
    }

    // Load the sphere mesh first to use as a fallback for any missing shapes.
    const FString FallbackPath = FString::Printf(TEXT("%s/DH_Sphere.DH_Sphere"), *BasePath);
    UStaticMesh* FallbackSphereMesh = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), nullptr, *FallbackPath));

    for (int32 EnumIndex = 0; EnumIndex < EnumPtr->NumEnums(); ++EnumIndex)
    {
        EHoleShapeType ShapeType = static_cast<EHoleShapeType>(EnumIndex);
        const FString ShapeName = EnumPtr->GetNameStringByIndex(EnumIndex);

    	
    	// ADD THIS LINE to prevent reading past the valid data:
    	if (ShapeName.Contains(TEXT("MAX"))) { continue; }

        const FString AssetPath = FString::Printf(TEXT("%s/DH_%s.DH_%s"), *BasePath, *ShapeName, *ShapeName);
        UStaticMesh* MeshForShape = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), nullptr, *AssetPath));

        if (!MeshForShape)
        {
            MeshForShape = FallbackSphereMesh;
        }

    	// Only add mapping if we actually found something (or have a valid fallback)
    	if (MeshForShape)
    	{
    		FHoleMeshMapping Mapping;
    		Mapping.ShapeType = ShapeType;
    		Mapping.Mesh = MeshForShape;
    		Lib->ShapeMeshMappings.Add(Mapping);
    	}
    }

    UE_LOG(LogTemp, Log, TEXT("Seeded %d hole shape mappings"), Lib->ShapeMeshMappings.Num());
}