#pragma once

#include "CoreMinimal.h"
#include "Engine/StaticMesh.h"
#include "HoleShapeLibrary.h"
#include "FHoleShape.h"
#include "DiggerSettings.h" // Include your settings

// --- REMOVED HARDCODED CONSTANTS ---

// Ensures the given class reference is loaded from the settings if not set
inline void EnsureDefaultHoleBP(TSubclassOf<AActor> &HoleBPRef)
{
    // 1. If already set, do nothing
    if (HoleBPRef)
        return;

    // 2. Get Settings
    const UDiggerSettings *Settings = UDiggerSettings::Get();
    if (!Settings)
        return;

    // 3. Try to Load
    // Note: We use LoadSynchronous() for simplicity here, but in gameplay code,
    // you should ideally Async Load this before you need it.
    HoleBPRef = Settings->DefaultHoleActorClass.LoadSynchronous();

#if WITH_EDITOR
    if (!HoleBPRef)
    {
        UE_LOG(LogTemp, Warning, TEXT("Digger: Failed to load DefaultHoleActorClass. Check Project Settings -> Digger."));
    }
#endif
}

// 4. Wrap Seeding in Editor Only
// Scanning folders by string is not safe for Shipping builds.
#if WITH_EDITOR

inline void SeedHoleShapesFromFolder(UHoleShapeLibrary *Lib)
{
    if (!IsValid(Lib))
        return;

    // Keep this path local here, or move to DiggerEditorSettings if you want it configurable.
    // Since this is an Editor-Only tool helper, a string literal is less dangerous here than in Runtime code.
    const FString BasePath = TEXT("/Digger/Digger/DynamicHoles/HoleMeshes");

    Lib->ShapeMeshMappings.Empty();

    UEnum *EnumPtr = StaticEnum<EHoleShapeType>();
    if (!EnumPtr)
        return;

    // Load fallback
    const FString FallbackPath = FString::Printf(TEXT("%s/DH_Sphere.DH_Sphere"), *BasePath);
    UStaticMesh *FallbackSphereMesh = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), nullptr, *FallbackPath));

    for (int32 EnumIndex = 0; EnumIndex < EnumPtr->NumEnums(); ++EnumIndex)
    {
        EHoleShapeType ShapeType = static_cast<EHoleShapeType>(EnumIndex);
        const FString ShapeName = EnumPtr->GetNameStringByIndex(EnumIndex);

        if (ShapeName.Contains(TEXT("MAX")))
            continue;

        const FString AssetPath = FString::Printf(TEXT("%s/DH_%s.DH_%s"), *BasePath, *ShapeName, *ShapeName);
        UStaticMesh *MeshForShape = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), nullptr, *AssetPath));

        if (!MeshForShape)
            MeshForShape = FallbackSphereMesh;

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
#endif // WITH_EDITOR