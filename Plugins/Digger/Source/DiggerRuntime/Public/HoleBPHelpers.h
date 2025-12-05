#pragma once

#include "CoreMinimal.h"
#include "Engine/StaticMesh.h"
#include "HoleShapeLibrary.h"
#include "FHoleShape.h"

static const TCHAR* GDefaultHoleLibraryPath = TEXT("/Game/Blueprints/HoleShapeLibrary.HoleShapeLibrary");
static const TCHAR* GDefaultHoleBPPath = TEXT("/Game/DynamicHoles/BP_MeshHole.BP_MeshHole_C");
static const TCHAR* GHoleMeshesFolder = TEXT("/Game/DynamicHoles/HoleMeshes");

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

        const FString AssetPath = FString::Printf(TEXT("%s/DH_%s.DH_%s"), *BasePath, *ShapeName, *ShapeName);
        UStaticMesh* MeshForShape = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), nullptr, *AssetPath));

        if (!MeshForShape)
        {
            MeshForShape = FallbackSphereMesh;
        }

        FHoleMeshMapping Mapping;
        Mapping.ShapeType = ShapeType;
        Mapping.Mesh = MeshForShape;
        Lib->ShapeMeshMappings.Add(Mapping);
    }

    UE_LOG(LogTemp, Log, TEXT("Seeded %d hole shape mappings"), Lib->ShapeMeshMappings.Num());
}