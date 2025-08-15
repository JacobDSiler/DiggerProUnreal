#pragma once

#include "CoreMinimal.h"

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
#if WITH_EDITOR
    if (!IsValid(Lib)) return;

    FString FolderPath = GHoleMeshesFolder;
    FolderPath.RemoveFromEnd(TEXT("/"));
    FName FolderName(*FolderPath);

    IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
    FARFilter Filter;
    Filter.PackagePaths.Add(FolderName);
    Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
    Filter.bRecursivePaths = true;

    TArray<FAssetData> Assets;
    AR.GetAssets(Filter, Assets);

    TMap<FString, UStaticMesh*> FoundMeshes;
    for (const FAssetData& AD : Assets)
    {
        FString AssetName = AD.AssetName.ToString();
        if (AssetName.StartsWith(TEXT("DH_")))
        {
            AssetName.RightChopInline(3);
        }

        if (UStaticMesh* Mesh = Cast<UStaticMesh>(AD.GetAsset()))
        {
            FoundMeshes.Add(AssetName, Mesh);
        }
    }

    UStaticMesh* FallbackSphereMesh = FoundMeshes.Contains(TEXT("Sphere")) ? FoundMeshes[TEXT("Sphere")] : nullptr;

    Lib->ShapeMeshMappings.Empty();

    UEnum* EnumPtr = StaticEnum<EHoleShapeType>();
    for (int32 EnumIndex = 0; EnumIndex < EnumPtr->NumEnums() - 1; ++EnumIndex)
    {
        EHoleShapeType ShapeType = static_cast<EHoleShapeType>(EnumIndex);
        FString ShapeName = EnumPtr->GetNameStringByIndex(EnumIndex);

        UStaticMesh* MeshForShape = nullptr;
        if (UStaticMesh** FoundMesh = FoundMeshes.Find(ShapeName))
        {
            MeshForShape = *FoundMesh;
        }
        else if (FallbackSphereMesh)
        {
            MeshForShape = FallbackSphereMesh;
        }

        FHoleMeshMapping Mapping;
        Mapping.ShapeType = ShapeType;
        Mapping.Mesh = MeshForShape;
        Lib->ShapeMeshMappings.Add(Mapping);
    }

    Lib->MarkPackageDirty();
    UE_LOG(LogTemp, Log, TEXT("Seeded %d hole shape mappings"), Lib->ShapeMeshMappings.Num());
#endif
}