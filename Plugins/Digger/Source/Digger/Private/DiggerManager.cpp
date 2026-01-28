// 1. The Matching Header MUST be first. 
// This ensures DiggerManager.h compiles on its own.
#include "DiggerManager.h"

// 2. Core Unreal Minimal (Essential)
#include "CoreMinimal.h"

// 3. System / Platform Specifics (MUST BE WRAPPED)
// This was causing your "No Target Architecture" crash.
// We wrap it so Unreal handles the Windows types correctly.
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <winnt.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

// 4. Engine Core Systems
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"

// 5. Mesh & Rendering
#include "ProceduralMeshComponent.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "Engine/StaticMesh.h"
#include "Rendering/PositionVertexBuffer.h"
#include "Rendering/StaticMeshVertexBuffer.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/LightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"

// 6. Landscape
#include "LandscapeProxy.h"
#include "LandscapeInfo.h"

// 7. Kismet / Utilities
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "UObject/SoftObjectPath.h"

// 8. Digger Runtime Internal Headers
#include "DiggerDebug.h"
#include "Digger/Utilities/FastDebugRenderer.h"
#include "SparseVoxelGrid.h"
#include "VoxelChunk.h"
#include "VoxelConversion.h"
#include "MarchingCubes.h"
#include "FCustomSDFBrush.h"
#include "IslandActor.h"
#include "DynamicLightActor.h"
#include "HoleShapeLibrary.h"
#include "FSpawnedHoleData.h"
#include "HoleBPHelpers.h"
#include "Materials/DiggerMaterialTypes.h"
#include "Materials/DiggerTextureSet.h"

// 9. Brush Shapes
#include "VoxelBrushShape.h"
#include "Voxel/BrushShapes/SphereBrushShape.h"
#include "Voxel/BrushShapes/CubeBrushShape.h"
#include "Voxel/BrushShapes/CylinderBrushShape.h"
#include "Voxel/BrushShapes/ConeBrushShape.h"
#include "Voxel/BrushShapes/CapsuleBrushShape.h"
#include "Voxel/BrushShapes/TorusBrushShape.h"
#include "Voxel/BrushShapes/PyramidBrushShape.h"
#include "Voxel/BrushShapes/IcosphereBrushShape.h"
#include "Voxel/BrushShapes/SmoothBrushShape.h"
#include "Voxel/BrushShapes/NoiseBrushShape.h"

// 10. EDITOR-ONLY HEADERS
// CRITICAL: These must be wrapped, or your game will fail to package.
// Since Digger is a Runtime module, it cannot link to Editor modules in a Shipping build.
#if WITH_EDITOR
    #include "Editor.h"
    #include "LandscapeEdit.h"
    #include "AssetToolsModule.h"
    #include "IAssetTools.h"
    #include "Factories/MaterialInstanceConstantFactoryNew.h"
    #include "AssetRegistry/AssetRegistryModule.h"
#endif

// Forward Declarations (Only needed if this is a Header, but valid in CPP)
class ADynamicLightActor;
class FDiggerEdModeToolkit;
class FDiggerEdMode;
class MeshDescriptors;
class StaticMeshAttributes;




static UHoleShapeLibrary* LoadDefaultHoleLibrary()
{
    if (!GDefaultHoleLibraryPath || !*GDefaultHoleLibraryPath) return nullptr;
    UObject* Obj = StaticLoadObject(UHoleShapeLibrary::StaticClass(), nullptr, GDefaultHoleLibraryPath);
    return Cast<UHoleShapeLibrary>(Obj);
}

static TSubclassOf<AActor> LoadDefaultHoleBPClass()
{
    UClass* Cls = StaticLoadClass(AActor::StaticClass(), nullptr, GDefaultHoleBPPath);
    return Cls;
}

static UHoleShapeLibrary* CreateTransientHoleLibrary()
{
    // Create a transient instance so editor mode never crashes when no asset exists.
    UHoleShapeLibrary* Lib = NewObject<UHoleShapeLibrary>(GetTransientPackage(), UHoleShapeLibrary::StaticClass(), FName(TEXT("TransientHoleShapeLibrary")));
    if (Lib)
    {
        // Optionally seed defaults if your code expects entries:
        // Lib->AddDefaultSphere();
        // Lib->AddDefaultCube();
    }
    return Lib;
}


// Digger Material Management
void ADiggerManager::SetTargetRenderComponent(UPrimitiveComponent* InComponent, int32 InMaterialElementIndex)
{
    TargetRenderComponent = InComponent;
    MaterialElementIndex = InMaterialElementIndex;
}

void ADiggerManager::ApplyMaterialProfile(UDiggerMaterialProfile* Profile)
{
    if (!TargetRenderComponent.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("[Digger] ApplyMaterialProfile: No TargetRenderComponent set."));
        return;
    }

    ApplyMaterialProfileToComponent(Profile, TargetRenderComponent.Get(), MaterialElementIndex);
}

ALandscapeProxy* ADiggerManager::GetLandscapeProxyAt(const FVector& WorldPos) const
{
    if (!HeightCacheSystem)
    {
        if (DiggerDebug::Landscape())
        {
            UE_LOG(LogTemp, Warning, TEXT("GetLandscapeProxyAt: HeightCacheSystem is null"));
        }
        return nullptr;
    }

    // Use the cache's proxy lookup
    return HeightCacheSystem->FindProxy(WorldPos);
}


void ADiggerManager::ApplyMaterialProfileToComponent(UDiggerMaterialProfile* Profile, UPrimitiveComponent* TargetComponent, int32 ElementIndex)
{
    if (!Profile || !TargetComponent)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Digger] ApplyMaterialProfileToComponent: Invalid args (Profile:%p, Target:%p)"), Profile, TargetComponent);
        return;
    }

    UMaterialInstanceDynamic* MID = GetOrCreateMID(TargetComponent, ElementIndex);
    if (!MID)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Digger] ApplyMaterialProfileToComponent: Failed to create/find MID."));
        return;
    }

    PushProfileParamsToMID(Profile, MID, /*MaxLayers*/ 12);
}

/** Helper: load/cached master, create or fetch MID bound to TargetComponent + ElementIndex. */
UMaterialInstanceDynamic* ADiggerManager::GetOrCreateMID(UPrimitiveComponent* TargetComponent, int32 ElementIndex)
{
    if (!TargetComponent) return nullptr;

    // If we already have a MID cached for this component, reuse it.
    if (TObjectPtr<UMaterialInstanceDynamic>* Found = MIDCache.Find(TargetComponent))
    {
        return Found->Get(); // returns UMaterialInstanceDynamic*
    }

    // Resolve master material
    UMaterialInterface* Master =
        MasterMaterial.IsValid() ? MasterMaterial.Get()
                                 : (MasterMaterial.IsNull() ? nullptr : MasterMaterial.LoadSynchronous());

    if (!Master)
    {
        // Fallback to the component’s current material
        Master = TargetComponent->GetMaterial(ElementIndex);
    }
    if (!Master) return nullptr;

    // Create & assign MID
    UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Master, this);
    if (!MID) return nullptr;

    TargetComponent->SetMaterial(ElementIndex, MID);
    MIDCache.Add(TargetComponent, MID);
    return MID;
}


/** Push all parameters from the profile into the MID, clamping extra layers off. */
void ADiggerManager::PushProfileParamsToMID(UDiggerMaterialProfile* Profile, UMaterialInstanceDynamic* MID, int32 MaxLayers)
{
    if (!Profile || !MID) return;

    // ── Global toggles (only if your master material actually has these params)
    // MID->SetScalarParameterValue(FName(TEXT("UseTriplanar")),     Profile->bUseTriplanar     ? 1.0f : 0.0f);
    // MID->SetScalarParameterValue(FName(TEXT("UseVirtualTexture")), Profile->bUseVirtualTexture ? 1.0f : 0.0f);

    const int32 Used = FMath::Clamp(Profile->Layers.Num(), 0, MaxLayers);

    for (int32 i = 0; i < Used; ++i)
    {
        const int32 L1 = i + 1;
        const FDiggerLayerParams& L = Profile->Layers[i];

        // Enable/disable layer i
        MID->SetScalarParameterValue(LayerParam(L1, TEXT("Enabled")), L.bEnabled ? 1.0f : 0.0f);

        // Resolve texture set (loads the data asset if not already loaded)
        if (UDiggerTextureSet* Set = L.TextureSet.LoadSynchronous())
        {
            if (UTexture2D* BC  = Set->BaseColor.LoadSynchronous())
            {
                MID->SetTextureParameterValue(LayerParam(L1, TEXT("BaseColor")), BC);
            }
            if (UTexture2D* N   = Set->Normal.LoadSynchronous())
            {
                MID->SetTextureParameterValue(LayerParam(L1, TEXT("Normal")), N);
            }
            if (UTexture2D* ORM = Set->ORM.LoadSynchronous())
            {
                MID->SetTextureParameterValue(LayerParam(L1, TEXT("ORM")), ORM);
            }
        }

        // Scalar parameters expected by the master material
        MID->SetScalarParameterValue(LayerParam(L1, TEXT("Tiling")),        L.Tiling);
        MID->SetScalarParameterValue(LayerParam(L1, TEXT("MinHeight")),     L.MinHeight);
        MID->SetScalarParameterValue(LayerParam(L1, TEXT("MaxHeight")),     L.MaxHeight);
        MID->SetScalarParameterValue(LayerParam(L1, TEXT("NoiseAmp")),      L.NoiseAmplitude);
        MID->SetScalarParameterValue(LayerParam(L1, TEXT("NoiseFreq")),     L.NoiseFrequency);
        MID->SetScalarParameterValue(LayerParam(L1, TEXT("EdgeSharpness")), L.EdgeSharpness);
        MID->SetScalarParameterValue(LayerParam(L1, TEXT("Seed")),          L.Seed);
    }

    // Disable any remaining layers to prevent stale params from previous profiles
    for (int32 i = Used; i < MaxLayers; ++i)
    {
        const int32 L1 = i + 1;
        MID->SetScalarParameterValue(LayerParam(L1, TEXT("Enabled")), 0.0f);

        // (Optional) also clear textures to null if your material samples them unguarded by "Enabled"
        // MID->SetTextureParameterValue(LayerParam(L1, TEXT("BaseColor")), nullptr);
        // MID->SetTextureParameterValue(LayerParam(L1, TEXT("Normal")),    nullptr);
        // MID->SetTextureParameterValue(LayerParam(L1, TEXT("ORM")),       nullptr);
    }
}


// --------------------------------------------------------
// LayerParam: build names like "Layer3_BaseColor"
// --------------------------------------------------------
FName ADiggerManager::LayerParam(int32 Index1Based, const TCHAR* Suffix)
{
    return FName(*FString::Printf(TEXT("Layer%d_%s"), Index1Based, Suffix));
}

// --------------------------------------------------------
// ValidateMasterMaterial
// --------------------------------------------------------
bool ADiggerManager::ValidateMasterMaterial(UMaterialInterface* Master, int32 MaxLayers, FText& OutReport) const
{
    if (!Master)
    {
        OutReport = FText::FromString(TEXT("❌ No master material provided (null Master pointer)."));
        return false;
    }

    // Gather parameter infos
    TArray<FMaterialParameterInfo> ScalarInfos, VectorInfos, TextureInfos, RVTInfos, FontInfos;
    TArray<FGuid> ScalarIds, VectorIds, TextureIds, RVTIds, FontIds;

    Master->GetAllScalarParameterInfo(ScalarInfos, ScalarIds);
    Master->GetAllVectorParameterInfo(VectorInfos, VectorIds);
    Master->GetAllTextureParameterInfo(TextureInfos, TextureIds);
    Master->GetAllRuntimeVirtualTextureParameterInfo(RVTInfos, RVTIds);
    Master->GetAllFontParameterInfo(FontInfos, FontIds);

    // Build lookup sets
    TSet<FName> ScalarNames, TextureNames, VectorNames;
    for ( auto& Info : ScalarInfos)  ScalarNames.Add(Info.Name);
    for ( auto& Info : VectorInfos)  VectorNames.Add(Info.Name);
    for ( auto& Info : TextureInfos) TextureNames.Add(Info.Name);

    // Expected suffixes
    TArray<FString> ScalarSuffixes = {
        TEXT("Enabled"), TEXT("Tiling"), TEXT("MinHeight"), TEXT("MaxHeight"),
        TEXT("NoiseAmp"), TEXT("NoiseFreq"), TEXT("EdgeSharpness"), TEXT("Seed")
    };
    TArray<FString> TextureSuffixes = {
        TEXT("BaseColor"), TEXT("Normal"), TEXT("ORM")
    };

    // Collect problems
    TArray<FString> MissingScalars, MissingTextures, Warnings;

    for (int32 i = 1; i <= MaxLayers; ++i)
    {
        for (const FString& Suffix : ScalarSuffixes)
        {
            const FName N = LayerParam(i, *Suffix);
            if (!ScalarNames.Contains(N))
            {
                MissingScalars.Add(FString::Printf(TEXT("  - %s (Scalar)"), *N.ToString()));
            }
        }
        for (const FString& Suffix : TextureSuffixes)
        {
            const FName N = LayerParam(i, *Suffix);
            if (!TextureNames.Contains(N))
            {
                MissingTextures.Add(FString::Printf(TEXT("  - %s (Texture)"), *N.ToString()));
            }
        }
    }

    // Optional globals
    if (!ScalarNames.Contains(FName(TEXT("UseTriplanar"))))
    {
        Warnings.Add(TEXT("  - Global scalar 'UseTriplanar' not found (optional)."));
    }
    if (!ScalarNames.Contains(FName(TEXT("UseVirtualTexture"))))
    {
        Warnings.Add(TEXT("  - Global scalar 'UseVirtualTexture' not found (optional)."));
    }

    // Build report string
    FString Report;
    Report += FString::Printf(TEXT("Master: %s\n"), *Master->GetPathName());
    Report += FString::Printf(TEXT("Scalars found: %d  Textures found: %d  Vectors found: %d\n"),
                              ScalarNames.Num(), TextureNames.Num(), VectorNames.Num());

    if (MissingScalars.Num() == 0 && MissingTextures.Num() == 0)
    {
        Report += TEXT("\nAll required LayerN_* parameters are present.\n");
    }
    else
    {
        if (MissingScalars.Num() > 0)
        {
            Report += TEXT("\nMissing scalar parameters:\n");
            for (const FString& S : MissingScalars) Report += S + TEXT("\n");
        }
        if (MissingTextures.Num() > 0)
        {
            Report += TEXT("\nMissing texture parameters:\n");
            for (const FString& S : MissingTextures) Report += S + TEXT("\n");
        }
    }

    if (Warnings.Num() > 0)
    {
        Report += TEXT("\nWarnings:\n");
        for (const FString& W : Warnings) Report += W + TEXT("\n");
    }

#if WITH_EDITOR
    if (UMaterial* AsMat = Master->GetMaterial())
    {
        AsMat->ForceRecompileForRendering();
    }
#endif

    if (MissingScalars.Num() == 0 && MissingTextures.Num() == 0)
    {
        Report += TEXT("\n✅ All required LayerN_* parameters are present.\n");
    }
    else
    {
        if (MissingScalars.Num() > 0)
        {
            Report += TEXT("\n❌ Missing scalar parameters:\n");
            for (const FString& S : MissingScalars) Report += S + TEXT("\n");
        }
        if (MissingTextures.Num() > 0)
        {
            Report += TEXT("\n❌ Missing texture parameters:\n");
            for (const FString& S : MissingTextures) Report += S + TEXT("\n");
        }
    }

    if (Warnings.Num() > 0)
    {
        Report += TEXT("\n⚠️ Warnings:\n");
        for (const FString& W : Warnings) Report += W + TEXT("\n");
    }

    OutReport = FText::FromString(Report);

    return (MissingScalars.Num() == 0 && MissingTextures.Num() == 0);
}


#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstance.h"
#include "Materials/Material.h"

static FMaterialParameterInfo LInfo(const TCHAR* Name, int32 LayerIndex)
{
    return FMaterialParameterInfo(FName(Name), EMaterialParameterAssociation::LayerParameter, LayerIndex);
}


bool ADiggerManager::ValidateLayeredMaster(UMaterialInterface* Master, int32 ExpectedLayers, FText& OutReport) const
{
    if (!Master)
    {
        OutReport = FText::FromString(TEXT("No master material provided."));
        return false;
    }

    // Collect all parameter infos
    TArray<FMaterialParameterInfo> ScalarInfos, VectorInfos, TextureInfos, RVTInfos, FontInfos;
    TArray<FGuid> ScalarIds, VectorIds, TextureIds, RVTIds, FontIds;
    Master->GetAllScalarParameterInfo(ScalarInfos, ScalarIds);
    Master->GetAllVectorParameterInfo(VectorInfos, VectorIds);
    Master->GetAllTextureParameterInfo(TextureInfos, TextureIds);
    Master->GetAllRuntimeVirtualTextureParameterInfo(RVTInfos, RVTIds);
    Master->GetAllFontParameterInfo(FontInfos, FontIds);

    // Build a set for quick lookups
    TSet<FMaterialParameterInfo> ScalarSet, TextureSet;
    for (const auto& I : ScalarInfos)  ScalarSet.Add(I);
    for (const auto& I : TextureInfos) TextureSet.Add(I);

    // Names expected on the LAYER (your ML_SedimentLayer)
    static const TCHAR* LayerTextureParams[] = { TEXT("BaseColorTex"), TEXT("NormalTex"), TEXT("ORMTex") };
    static const TCHAR* LayerScalarParams[]  = { TEXT("Tiling") };

    // Names expected on the BLEND (your MLB_HeightBlend)
    static const TCHAR* BlendScalarParams[] = {
        TEXT("Enabled"), TEXT("MinHeight"), TEXT("MaxHeight"),
        TEXT("NoiseAmp"), TEXT("NoiseFreq"), TEXT("EdgeSharpness"), TEXT("Seed")
    };

    TArray<FString> Missing, Notes;

    // Check the first N entries (0..ExpectedLayers-1)
    for (int32 LayerIdx = 0; LayerIdx < ExpectedLayers; ++LayerIdx)
    {
        // Layer params
        for (const TCHAR* P : LayerTextureParams)
        {
            if (!TextureSet.Contains(LInfo(P, LayerIdx)))
            {
                Missing.Add(FString::Printf(TEXT("Layer %d: texture param '%s' not found"), LayerIdx, P));
            }
        }
        for (const TCHAR* P : LayerScalarParams)
        {
            if (!ScalarSet.Contains(LInfo(P, LayerIdx)))
            {
                Missing.Add(FString::Printf(TEXT("Layer %d: scalar param '%s' not found"), LayerIdx, P));
            }
        }

        // Blend params (for layer > 0 there is a blend over previous; for LayerIdx==0 some setups omit blends)
        if (LayerIdx > 0)
        {
            for (const TCHAR* P : BlendScalarParams)
            {
                if (!ScalarSet.Contains(LInfo(P, LayerIdx)))
                {
                    Missing.Add(FString::Printf(TEXT("Blend for Layer %d: scalar param '%s' not found"), LayerIdx, P));
                }
            }
        }
    }

    FString Report;
    Report += FString::Printf(TEXT("Master: %s\n"), *Master->GetPathName());
    Report += FString::Printf(TEXT("Expected layers checked: %d\n"), ExpectedLayers);
    Report += FString::Printf(TEXT("Found: %d scalars, %d textures\n"), ScalarInfos.Num(), TextureInfos.Num());

    if (Missing.Num() == 0)
    {
        Report += TEXT("\nOK: All expected layer/blend parameters are present.\n");
        OutReport = FText::FromString(Report);
        return true;
    }
    else
    {
        Report += TEXT("\nMissing parameters:\n");
        for (const FString& S : Missing) Report += TEXT("  - ") + S + TEXT("\n");
        OutReport = FText::FromString(Report);
        return false;
    }
}





UMaterialInstanceConstant* ADiggerManager::BuildMaterialInstanceFromProfile(UDiggerMaterialProfile* Profile, const FString& TargetFolder, const FString& BaseAssetName, UMaterialInterface* Parent)
{
#if WITH_EDITOR
    if (!Profile || !Parent) return nullptr;

    // Ensure folder exists & unique name
    FString OutPkgName, OutAssetName;
    {
        const FString BasePath = TargetFolder / BaseAssetName;
        FAssetToolsModule& ATM = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
        ATM.Get().CreateUniqueAssetName(BasePath, TEXT(""), OutPkgName, OutAssetName);
    }

    // Create new or reuse existing
    UMaterialInstanceConstant* MIC = nullptr;
    const FString PkgPath = FPackageName::GetLongPackagePath(OutPkgName);
    const FString ExistingObjPath = PkgPath / OutAssetName + TEXT(".") + OutAssetName;

    MIC = FindObject<UMaterialInstanceConstant>(nullptr, *ExistingObjPath);
    if (!MIC)
    {
        UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
        Factory->InitialParent = Parent;
        FAssetToolsModule& ATM = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
        UObject* NewObj = ATM.Get().CreateAsset(OutAssetName, PkgPath, UMaterialInstanceConstant::StaticClass(), Factory);
        MIC = Cast<UMaterialInstanceConstant>(NewObj);
    }

    if (!MIC) return nullptr;

    MIC->Modify();
    MIC->SetParentEditorOnly(Parent);

    // Push parameters (editor-only setters)
    const int32 MaxLayers = 32; // or your shader’s max
    const int32 Used = FMath::Clamp(Profile->Layers.Num(), 0, MaxLayers);

    // Scalars
    for (int32 i = 0; i < Used; ++i)
    {
        const int32 L1 = i + 1;
        const FDiggerLayerParams& L = Profile->Layers[i];

        MIC->SetScalarParameterValueEditorOnly(LayerParam(L1, TEXT("Enabled")),        L.bEnabled ? 1.0f : 0.0f);
        MIC->SetScalarParameterValueEditorOnly(LayerParam(L1, TEXT("Tiling")),         L.Tiling);
        MIC->SetScalarParameterValueEditorOnly(LayerParam(L1, TEXT("MinHeight")),      L.MinHeight);
        MIC->SetScalarParameterValueEditorOnly(LayerParam(L1, TEXT("MaxHeight")),      L.MaxHeight);
        MIC->SetScalarParameterValueEditorOnly(LayerParam(L1, TEXT("NoiseAmp")),       L.NoiseAmplitude);
        MIC->SetScalarParameterValueEditorOnly(LayerParam(L1, TEXT("NoiseFreq")),      L.NoiseFrequency);
        MIC->SetScalarParameterValueEditorOnly(LayerParam(L1, TEXT("EdgeSharpness")),  L.EdgeSharpness);
        MIC->SetScalarParameterValueEditorOnly(LayerParam(L1, TEXT("Seed")),           L.Seed);

        if (UDiggerTextureSet* Set = Profile->Layers[i].TextureSet.LoadSynchronous())
        {
            if (UTexture* BC  = Set->BaseColor.LoadSynchronous())
                MIC->SetTextureParameterValueEditorOnly(LayerParam(L1, TEXT("BaseColor")), BC);
            if (UTexture* N   = Set->Normal.LoadSynchronous())
                MIC->SetTextureParameterValueEditorOnly(LayerParam(L1, TEXT("Normal")), N);
            if (UTexture* ORM = Set->ORM.LoadSynchronous())
                MIC->SetTextureParameterValueEditorOnly(LayerParam(L1, TEXT("ORM")), ORM);
        }
    }

    // Disable remaining layers
    for (int32 i = Used; i < MaxLayers; ++i)
    {
        const int32 L1 = i + 1;
        MIC->SetScalarParameterValueEditorOnly(LayerParam(L1, TEXT("Enabled")), 0.0f);
    }

    MIC->PostEditChange();
    if (UPackage* Pkg = MIC->GetOutermost())
    {
        Pkg->MarkPackageDirty();
    }
    return MIC;
#else
    return nullptr;
#endif
}


ADiggerManager* ADiggerManager::FindDiggerManager(const UObject* WorldContextObject)
{
    // 1. Safety Checks
    if (!WorldContextObject) return nullptr;

    UWorld* World = WorldContextObject->GetWorld();
    if (!World) return nullptr;

    // 2. The Lookup
    // GetActorOfClass is reliable. It iterates the actor list for *that specific world*.
    // This makes it safe for PIE (which has its own world) vs Editor.
    AActor* FoundActor = UGameplayStatics::GetActorOfClass(World, ADiggerManager::StaticClass());

    return Cast<ADiggerManager>(FoundActor);
}

void ADiggerManager::BuildAndApplyProfileMaterial(UDiggerMaterialProfile* Profile)
{
#if WITH_EDITOR
    if (!Profile) return;

    // Choose parent master material (override or default)
    UMaterialInterface* Parent = nullptr;
    if (Profile->MasterMaterialOverride.IsValid())
    {
        Parent = Profile->MasterMaterialOverride.Get();
    }
    if (!Parent)
    {
        // Fallback: hardcode your default master path or store it in settings
        Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Digger/Content/Digger/Materials/M_SedimentMaster_Inst.M_SedimentMaster_Inst"));
    }
    if (!Parent) return;

    // Build or update a MIC asset
    const FString TargetFolder = TEXT("/Digger/Digger/Materials/Generated");
    const FString BaseName     = TEXT("MI_Digger_Sediment");
    UMaterialInstanceConstant* MIC = BuildMaterialInstanceFromProfile(Profile, TargetFolder, BaseName, Parent);
    if (!MIC) return;

    // Assign to your mesh components (adjust to your component layout)
    TArray<UMeshComponent*> Meshes;
    GetComponents<UMeshComponent>(Meshes);
    for (UMeshComponent* MC : Meshes)
    {
        if (!MC) continue;
        // If your mesh has multiple slots, choose which slot you want
        MC->SetMaterial(0, MIC);
        MC->MarkRenderStateDirty();
    }
#endif
}



//New Multi Save Files System
// Updated ADiggerManager methods for multiple save file support

FString ADiggerManager::GetSaveFileDirectory(const FString& SaveFileName) const
{
    // Create a subdirectory for each save file
    return FPaths::ProjectContentDir() / VOXEL_DATA_DIRECTORY / SaveFileName;
}

FString ADiggerManager::GetChunkFilePath(const FIntVector& ChunkCoords, const FString& SaveFileName) const
{
    FString FileName = FString::Printf(TEXT("Chunk_%d_%d_%d%s"), 
        ChunkCoords.X, ChunkCoords.Y, ChunkCoords.Z, *CHUNK_FILE_EXTENSION);
    
    return GetSaveFileDirectory(SaveFileName) / FileName;
}

// Overload for backward compatibility - uses "Default" save file
FString ADiggerManager::GetChunkFilePath(const FIntVector& ChunkCoords) const
{
    return GetChunkFilePath(ChunkCoords, TEXT("Default"));
}

bool ADiggerManager::DoesSaveFileExist(const FString& SaveFileName) const
{
    FString SaveDir = GetSaveFileDirectory(SaveFileName);
    return FPaths::DirectoryExists(SaveDir);
}

bool ADiggerManager::DoesChunkFileExist(const FIntVector& ChunkCoords, const FString& SaveFileName) const
{
    FString FilePath = GetChunkFilePath(ChunkCoords, SaveFileName);
    return FPaths::FileExists(FilePath);
}

// Overload for backward compatibility
bool ADiggerManager::DoesChunkFileExist(const FIntVector& ChunkCoords) const
{
    return DoesChunkFileExist(ChunkCoords, TEXT("Default"));
}

void ADiggerManager::EnsureSaveFileDirectoryExists(const FString& SaveFileName) const
{
    FString SaveDir = GetSaveFileDirectory(SaveFileName);
    
    // Create the main voxel data directory first
    FString MainDir = FPaths::ProjectContentDir() / VOXEL_DATA_DIRECTORY;
    if (!FPaths::DirectoryExists(MainDir))
    {
        FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*MainDir);
        UE_LOG(LogTemp, Log, TEXT("Created main voxel data directory: %s"), *MainDir);
    }
    
    // Create the save file specific directory
    if (!FPaths::DirectoryExists(SaveDir))
    {
        FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*SaveDir);
        UE_LOG(LogTemp, Log, TEXT("Created save file directory: %s"), *SaveDir);
    }
}

bool ADiggerManager::SaveChunk(const FIntVector& ChunkCoords, const FString& SaveFileName)
{
    EnsureSaveFileDirectoryExists(SaveFileName);
    
    UVoxelChunk** ChunkPtr = ChunkMap.Find(ChunkCoords);
    if (!ChunkPtr || !*ChunkPtr)
    {
        UE_LOG(LogTemp, Warning, TEXT("Cannot save chunk at %s to save file '%s' - chunk not found in memory"), 
            *ChunkCoords.ToString(), *SaveFileName);
        return false;
    }
    
    UVoxelChunk* Chunk = *ChunkPtr;
    FString FilePath = GetChunkFilePath(ChunkCoords, SaveFileName);
    
    bool bSaveSuccess = Chunk->SaveChunkData(FilePath);
    
    if (bSaveSuccess)
    {
        UE_LOG(LogTemp, Log, TEXT("Successfully saved chunk %s to save file '%s'"), 
            *ChunkCoords.ToString(), *SaveFileName);
        
        // Invalidate cache so it gets refreshed
        InvalidateSavedChunkCache(SaveFileName);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to save chunk %s to save file '%s'"), 
            *ChunkCoords.ToString(), *SaveFileName);
    }
    
    return bSaveSuccess;
}

void ADiggerManager::RestoreHolesInEditor()
{
    // Only allow this in Editor World, not PIE
    if (GetWorld()->IsGameWorld()) return;

    for (auto& Pair : ChunkMap)
    {
        if (UVoxelChunk* Chunk = Pair.Value)
        {
            Chunk->RegenerateHolesFromData();
        }
    }
}

// Overload for backward compatibility
bool ADiggerManager::SaveChunk(const FIntVector& ChunkCoords)
{
    return SaveChunk(ChunkCoords, TEXT("Default"));
}

bool ADiggerManager::LoadChunk(const FIntVector& ChunkCoords, const FString& SaveFileName)
{
    FString FilePath = GetChunkFilePath(ChunkCoords, SaveFileName);
    
    if (!FPaths::FileExists(FilePath))
    {
        UE_LOG(LogTemp, Warning, TEXT("Cannot load chunk at %s from save file '%s' - file does not exist"), 
            *ChunkCoords.ToString(), *SaveFileName);
        return false;
    }
    
    // Get or create the chunk
    UVoxelChunk* Chunk = GetOrCreateChunkAtChunk(ChunkCoords);
    if (!Chunk)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to get or create chunk at %s for save file '%s'"), 
            *ChunkCoords.ToString(), *SaveFileName);
        return false;
    }
    
    // Load the chunk data
    bool bLoadSuccess = Chunk->LoadChunkData(FilePath);
    
    if (bLoadSuccess)
    {
        UE_LOG(LogTemp, Log, TEXT("Successfully loaded chunk %s from save file '%s'"), 
            *ChunkCoords.ToString(), *SaveFileName);
        
        // IMPORTANT: Force mesh regeneration after loading
        Chunk->ForceUpdate();
        
        UE_LOG(LogTemp, Log, TEXT("Triggered mesh regeneration for loaded chunk %s from save file '%s'"), 
            *ChunkCoords.ToString(), *SaveFileName);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load chunk %s from save file '%s'"), 
            *ChunkCoords.ToString(), *SaveFileName);
    }
    
    return bLoadSuccess;
}

// Overload for backward compatibility
bool ADiggerManager::LoadChunk(const FIntVector& ChunkCoords)
{
    return LoadChunk(ChunkCoords, TEXT("Default"));
}

bool ADiggerManager::SaveAllChunks(const FString& SaveFileName)
{
    EnsureSaveFileDirectoryExists(SaveFileName);

    // 1. Distribute Lights to Chunks (Spatial Partitioning)
    for (AActor* LightActor : SpawnedLights)
    {
        if (!IsValid(LightActor)) continue;

        FIntVector ChunkCoords = FVoxelConversion::WorldToChunk(LightActor->GetActorLocation());
        
        UVoxelChunk* Chunk = GetOrCreateChunkAtChunk(ChunkCoords);
        if (Chunk)
        {
            // You need to add this method to UVoxelChunk
            Chunk->CaptureLightForSave(LightActor); 
        }
    }
    
    if (ChunkMap.Num() == 0)
    {
        UE_LOG(LogTemp, Log, TEXT("No chunks to save to save file '%s' - ChunkMap is empty"), *SaveFileName);
        return true;
    }
    
    int32 SavedCount = 0;
    int32 FailedCount = 0;
    
    UE_LOG(LogTemp, Log, TEXT("Starting to save %d chunks to save file '%s'..."), ChunkMap.Num(), *SaveFileName);
    
    for (const auto& ChunkPair : ChunkMap)
    {
        const FIntVector& ChunkCoords = ChunkPair.Key;
        
        if (SaveChunk(ChunkCoords, SaveFileName))
        {
            SavedCount++;
        }
        else
        {
            FailedCount++;
        }
    }
    
    UE_LOG(LogTemp, Log, TEXT("Finished saving chunks to save file '%s': %d successful, %d failed"), 
        *SaveFileName, SavedCount, FailedCount);

    // 3. IMPORTANT: Clear the temporary light data from chunks so we don't duplicate on next save
    for (auto& Pair : ChunkMap)
    {
        Pair.Value->ClearSavedLights();
    }
    
    // Invalidate cache after batch save
    InvalidateSavedChunkCache(SaveFileName);
    
    
    return FailedCount == 0;
}

// Overload for backward compatibility
bool ADiggerManager::SaveAllChunks()
{
    return SaveAllChunks(TEXT("Default"));
}

bool ADiggerManager::LoadAllChunks(const FString& SaveFileName)
{
    TArray<FIntVector> SavedChunkCoords = GetAllSavedChunkCoordinates(SaveFileName, true);
    
    if (SavedChunkCoords.Num() == 0)
    {
        return true;
    }
    
    // 1. Setup the Queue
    PendingChunksToLoad = SavedChunkCoords;
    
    // 2. STORE THE FILENAME (This is the fix)
    AsyncLoadingFileName = SaveFileName;

    // 3. Clear old timer if exists
    GetWorldTimerManager().ClearTimer(LoadQueueTimerHandle);

    UE_LOG(LogTemp, Log, TEXT("Queued %d chunks for async loading from '%s'..."), PendingChunksToLoad.Num(), *AsyncLoadingFileName);

    // 4. Start Timer
    // Notice we do NOT pass arguments here. The function will look at 'AsyncLoadingFileName'.
    GetWorldTimerManager().SetTimer(LoadQueueTimerHandle, this, &ADiggerManager::ProcessChunkLoadQueue, 0.01f, true);
    
    return true;
}

void ADiggerManager::ProcessChunkLoadQueue()
{
    // Safety check
    if (PendingChunksToLoad.Num() == 0)
    {
        GetWorldTimerManager().ClearTimer(LoadQueueTimerHandle);
        // We can now reference AsyncLoadingFileName
        UE_LOG(LogTemp, Log, TEXT("Finished Async Chunk Loading from '%s'."), *AsyncLoadingFileName);
        return;
    }

    int32 ProcessedThisFrame = 0;
    
    while (PendingChunksToLoad.Num() > 0 && ProcessedThisFrame < ChunksPerBatch)
    {
        FIntVector ChunkCoords = PendingChunksToLoad.Pop(false);

        // USE THE MEMBER VARIABLE HERE
        LoadChunk(ChunkCoords, AsyncLoadingFileName); 

        ProcessedThisFrame++;
    }
    
    if (PendingChunksToLoad.Num() == 0)
    {
        PendingChunksToLoad.Empty();
    }
}

// Overload for backward compatibility
bool ADiggerManager::LoadAllChunks()
{
    return LoadAllChunks(TEXT("Default"));
}

TArray<FIntVector> ADiggerManager::GetAllSavedChunkCoordinates(const FString& SaveFileName, bool bForceRefresh)
{
    // Use per-save-file caching
    FString CacheKey = SaveFileName;
    
    if (!bForceRefresh && SavedChunkCache.Contains(CacheKey))
    {
        return SavedChunkCache[CacheKey];
    }
    
    TArray<FIntVector> SavedChunks;
    FString SaveDir = GetSaveFileDirectory(SaveFileName);
    
    if (!FPaths::DirectoryExists(SaveDir))
    {
        // Directory doesn't exist, so no saved chunks
        SavedChunkCache.Add(CacheKey, SavedChunks);
        return SavedChunks;
    }
    
    TArray<FString> FoundFiles;
    FString SearchPattern = SaveDir / FString::Printf(TEXT("*%s"), *CHUNK_FILE_EXTENSION);
    IFileManager::Get().FindFiles(FoundFiles, *SearchPattern, true, false);
    
    for (const FString& FileName : FoundFiles)
    {
        // Parse filename to extract coordinates
        FString BaseName = FPaths::GetBaseFilename(FileName);
        
        // Expected format: Chunk_X_Y_Z
        TArray<FString> Parts;
        BaseName.ParseIntoArray(Parts, TEXT("_"), true);
        
        if (Parts.Num() >= 4 && Parts[0] == TEXT("Chunk"))
        {
            int32 X = FCString::Atoi(*Parts[1]);
            int32 Y = FCString::Atoi(*Parts[2]);
            int32 Z = FCString::Atoi(*Parts[3]);
            
            SavedChunks.Add(FIntVector(X, Y, Z));
        }
    }
    
    // Cache the results
    SavedChunkCache.Add(CacheKey, SavedChunks);
    
    return SavedChunks;
}

// Overload for backward compatibility
TArray<FIntVector> ADiggerManager::GetAllSavedChunkCoordinates(bool bForceRefresh)
{
    return GetAllSavedChunkCoordinates(TEXT("Default"), bForceRefresh);
}

TArray<FString> ADiggerManager::GetAllSaveFileNames() const
{
    TArray<FString> SaveFileNames;
    FString MainDir = FPaths::ProjectContentDir() / VOXEL_DATA_DIRECTORY;
    
    UE_LOG(LogTemp, Log, TEXT("GetAllSaveFileNames: Searching in directory: %s"), *MainDir);
    
    if (!FPaths::DirectoryExists(MainDir))
    {
        UE_LOG(LogTemp, Log, TEXT("GetAllSaveFileNames: Main directory does not exist"));
        return SaveFileNames;
    }
    
    // Get all entries in the main directory
    TArray<FString> DirectoryContents;
    IFileManager& FileManager = IFileManager::Get();
    FileManager.FindFiles(DirectoryContents, *(MainDir / TEXT("*")), false, true);
    
    UE_LOG(LogTemp, Log, TEXT("Found %d potential directories"), DirectoryContents.Num());
    
    // Check each directory to see if it contains chunk files
    for (const FString& DirName : DirectoryContents)
    {
        FString FullDirPath = MainDir / DirName;
        
        UE_LOG(LogTemp, Log, TEXT("Checking directory: %s"), *DirName);
        
        // Skip any hidden directories or files
        if (DirName.StartsWith(TEXT(".")))
        {
            UE_LOG(LogTemp, Log, TEXT("Skipping hidden directory: %s"), *DirName);
            continue;
        }
        
        // Verify it's actually a directory
        if (!FPaths::DirectoryExists(FullDirPath))
        {
            UE_LOG(LogTemp, Log, TEXT("Not a directory: %s"), *DirName);
            continue;
        }
        
        // Check if this directory contains chunk files
        TArray<FString> ChunkFiles;
        FString ChunkSearchPattern = FullDirPath / FString::Printf(TEXT("*%s"), *CHUNK_FILE_EXTENSION);
        FileManager.FindFiles(ChunkFiles, *ChunkSearchPattern, true, false);
        
        UE_LOG(LogTemp, Log, TEXT("Directory '%s' contains %d chunk files"), *DirName, ChunkFiles.Num());
        
        if (ChunkFiles.Num() > 0)
        {
            SaveFileNames.Add(DirName);
            UE_LOG(LogTemp, Log, TEXT("Added '%s' to save files list"), *DirName);
        }
        else
        {
            UE_LOG(LogTemp, Log, TEXT("Skipping '%s' - no chunk files found"), *DirName);
        }
    }
    
    UE_LOG(LogTemp, Log, TEXT("GetAllSaveFileNames: Final result - %d save files: [%s]"), 
        SaveFileNames.Num(), *FString::Join(SaveFileNames, TEXT(", ")));
    
    return SaveFileNames;
}

bool ADiggerManager::DeleteSaveFile(const FString& SaveFileName)
{
    FString SaveDir = GetSaveFileDirectory(SaveFileName);
    
    if (!FPaths::DirectoryExists(SaveDir))
    {
        UE_LOG(LogTemp, Warning, TEXT("Cannot delete save file '%s' - directory does not exist"), *SaveFileName);
        return false;
    }
    
    // Delete all files in the directory first
    TArray<FString> FilesToDelete;
    IFileManager::Get().FindFilesRecursive(FilesToDelete, *SaveDir, TEXT("*"), true, false);
    
    bool bAllFilesDeleted = true;
    for (const FString& FileToDelete : FilesToDelete)
    {
        if (!FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*FileToDelete))
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to delete file: %s"), *FileToDelete);
            bAllFilesDeleted = false;
        }
    }
    
    // Delete the directory itself
    bool bDirectoryDeleted = FPlatformFileManager::Get().GetPlatformFile().DeleteDirectory(*SaveDir);
    
    if (bDirectoryDeleted && bAllFilesDeleted)
    {
        UE_LOG(LogTemp, Log, TEXT("Successfully deleted save file '%s'"), *SaveFileName);
        
        // Remove from cache
        InvalidateSavedChunkCache(SaveFileName);
        
        return true;
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to completely delete save file '%s'"), *SaveFileName);
        return false;
    }
}

void ADiggerManager::InvalidateSavedChunkCache(const FString& SaveFileName)
{
    if (SaveFileName.IsEmpty())
    {
        // Clear all cache entries
        SavedChunkCache.Empty();
    }
    else
    {
        // Clear specific save file cache
        SavedChunkCache.Remove(SaveFileName);
    }
}


//End New Multi Save Files System.


void ADiggerManager::ApplyBrushInEditor(bool bDig)
{
    if (DiggerDebug::Brush())
    {
        UE_LOG(LogTemp, Error, TEXT("ApplyBrushInEditor Called!"));
    }

    FBrushStroke BrushStroke;
    BrushStroke.BrushPosition            = EditorBrushPosition + EditorBrushOffset;
    BrushStroke.BrushRadius              = EditorBrushRadius;
    BrushStroke.BrushRotation            = EditorBrushRotation;
    BrushStroke.bHiddenSeam              = EditorBrushHiddenSeam;
    BrushStroke.BrushType                = EditorBrushType;
    BrushStroke.bDig                     = bDig;
    BrushStroke.BrushLength              = EditorBrushLength;
    BrushStroke.bIsFilled                = EditorBrushIsFilled;
    BrushStroke.BrushAngle               = EditorBrushAngle;
    BrushStroke.HoleShape                = EditorBrushHoleShape;
    BrushStroke.bUseAdvancedCubeBrush    = EditorbUseAdvancedCubeBrush;
    BrushStroke.AdvancedCubeHalfExtentX  = EditorCubeHalfExtentX;
    BrushStroke.AdvancedCubeHalfExtentY  = EditorCubeHalfExtentY;
    BrushStroke.AdvancedCubeHalfExtentZ  = EditorCubeHalfExtentZ;
    BrushStroke.BrushStrength            = EditorBrushStrength;
    BrushStroke.LightColor               = EditorBrushLightColor; // default

    if (EditorBrushType == EVoxelBrushType::Light)
    {
        // Reasonable defaults for non-editor (or if toolkit missing)
        BrushStroke.LightType  = EditorBrushLightType;
        BrushStroke.LightColor = EditorBrushLightColor;
        

        if (DiggerDebug::Lights() || DiggerDebug::Brush())
        {
            UE_LOG(LogTemp, Warning, TEXT("Light brush setup: LightType=%d Color=%s"),
                (int32)BrushStroke.LightType,
                *BrushStroke.LightColor.ToString());
        }

        ApplyLightBrushInEditor(BrushStroke);
        return;
    }

    // Non-light brushes
    if (DiggerDebug::Brush())
    {
        UE_LOG(LogTemp, Warning, TEXT("DiggerManager.cpp: Extent X: %.2f  Y: %.2f  Z: %.2f"),
            BrushStroke.AdvancedCubeHalfExtentX,
            BrushStroke.AdvancedCubeHalfExtentY,
            BrushStroke.AdvancedCubeHalfExtentZ);
    }

    if (DiggerDebug::Brush() || DiggerDebug::Casts())
    {
        UE_LOG(LogTemp, Warning, TEXT("[ApplyBrushInEditor] EditorBrushPosition (World): %s"),
            *EditorBrushPosition.ToString());
    }

    ApplyBrushToAllChunks(BrushStroke);

#if WITH_EDITOR
    if (GEditor)
    {
        GEditor->RedrawAllViewports();
    }
#endif

    TArray<FIslandData> DetectedIslands = DetectUnifiedIslands();
}


void ADiggerManager::RemoveIslandAtPosition(const FVector& IslandCenter, const FIntVector& ReferenceVoxel)
{
    if (DiggerDebug::Islands())
    UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] RemoveIslandAtPosition called at %s"), *IslandCenter.ToString());

    // Guard 1: Reference voxel must be valid
    if (!ensure(!ReferenceVoxel.IsZero()))
    {
        if (DiggerDebug::Islands())
        UE_LOG(LogTemp, Error, TEXT("[DiggerPro] No reference voxel provided."));
        return;
    }

    FIntVector ChunkCoords, LocalVoxel;
    FVoxelConversion::GlobalVoxelToChunkAndLocal(ReferenceVoxel, ChunkCoords, LocalVoxel);

    // Guard 2: Chunk must exist and be valid
    UVoxelChunk** ChunkPtr = ChunkMap.Find(ChunkCoords);
    if (!ChunkPtr || !IsValid(*ChunkPtr))
    {
        if (DiggerDebug::Islands() || DiggerDebug::Chunks())
        UE_LOG(LogTemp, Error, TEXT("[DiggerPro] Invalid or missing chunk at coords %s"), *ChunkCoords.ToString());
        return;
    }

    UVoxelChunk* Chunk = *ChunkPtr;

    // Guard 3: Grid must exist and be valid
    USparseVoxelGrid* Grid = Chunk->GetSparseVoxelGrid();
    if (!IsValid(Grid))
    {
        if (DiggerDebug::Islands() || DiggerDebug::Voxels())
        UE_LOG(LogTemp, Error, TEXT("[DiggerPro] Invalid SparseVoxelGrid in chunk."));
        return;
    }

    // Guard 4: Attempt island extraction
    USparseVoxelGrid* ExtractedIsland = nullptr;
    TArray<FIntVector> ExtractedVoxels;

    if (!Grid->ExtractIslandByVoxel(LocalVoxel, ExtractedIsland, ExtractedVoxels) || !IsValid(ExtractedIsland) || ExtractedVoxels.IsEmpty())
    {
        if (DiggerDebug::Islands() || DiggerDebug::Voxels())
        UE_LOG(LogTemp, Error, TEXT("[DiggerPro] Failed to extract island at voxel %s"), *LocalVoxel.ToString());
        return;
    }

    // Safe voxel removal (encapsulated)
    Grid->RemoveVoxels(ExtractedVoxels);
    Chunk->MarkDirty();

    if (DiggerDebug::Islands() || DiggerDebug::Voxels())
    UE_LOG(LogTemp, Display, TEXT("[DiggerPro] Successfully removed %d voxels at %s."), ExtractedVoxels.Num(), *IslandCenter.ToString());
}




void ADiggerManager::ApplyLightBrushInEditor(const FBrushStroke& BrushStroke)
{
    if (DiggerDebug::Lights() || DiggerDebug::Brush()) {
        UE_LOG(LogTemp, Warning, TEXT("ApplyLightBrushInEditor called at position: %s"), *BrushStroke.BrushPosition.ToString());
        UE_LOG(LogTemp, Warning, TEXT("Light stroke - Type: %d, Radius: %f, Strength: %f, Rotation: %s"), 
               (int32)BrushStroke.LightType, 
               BrushStroke.BrushRadius, 
               BrushStroke.BrushStrength,
               *BrushStroke.BrushRotation.ToString());
    }

    // Now you have access to all brush stroke properties:
    // - BrushStroke.BrushPosition
    // - BrushStroke.BrushRadius  
    // - BrushStroke.BrushStrength
    // - BrushStroke.BrushRotation (for future light rotation)
    // - BrushStroke.LightType
    // - Any other properties you add later

    UE_LOG(LogTemp, Warning, TEXT("Spawning light of type: %i"), BrushStroke.LightType);
    SpawnLight(BrushStroke);

    // Redraw viewports to show the new light
    if (GEditor)
    {
        GEditor->RedrawAllViewports();
    }
}




void ADiggerManager::OnConstruction(const FTransform& Transform)
{
        Super::OnConstruction(Transform);
#if WITH_EDITOR
        this->SetFolderPath(FName(TEXT("Digger"))); // top-level folder for the manager
#endif

    // Always ensure we have a library pointer (cheap)
    EnsureHoleShapeLibrary();

    // Always make sure the BP is at the origin:
    EnforceZeroLocation();

#if WITH_EDITOR
    if (!IsRuntimeLike())
    {
        InitEditorLightweight(); // cheap preview only
        return;
    }
#endif
    
}



void ADiggerManager::InitHoleShapeLibrary()
{
    if (!HoleShapeLibrary)
    {
        // Load class dynamically
        const FSoftClassPath LibraryClassRef(TEXT("/Game/Blueprints/MyHoleShapeLibrary.MyHoleShapeLibrary_C"));
        UClass* LoadedClass = LibraryClassRef.TryLoadClass<UHoleShapeLibrary>();

        if (LoadedClass)
        {
            HoleShapeLibrary = NewObject<UHoleShapeLibrary>(this, LoadedClass);
            UE_LOG(LogTemp, Warning, TEXT("HoleShapeLibrary created at runtime successfully."));
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to load MyHoleShapeLibrary class dynamically."));
        }
    }
}



ADiggerManager::ADiggerManager()
{
#if WITH_EDITOR  
    this->SetFlags(RF_Transactional); // Enable undo/redo support in editor
#endif

    if(RootComponent)
    {
        RootComponent->SetMobility(EComponentMobility::Static); // Optional implication
        
        // This is the strongest UI lock:
        RootComponent->bVisualizeComponent = true; 
    }
    
    // Initialize the ProceduralMeshComponent
    ProceduralMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("GeneratedMesh"));
    RootComponent = ProceduralMesh;

    // Initialize the voxel grid and marching cubes
    SparseVoxelGrid = CreateDefaultSubobject<USparseVoxelGrid>(TEXT("SparseVoxelGrid"));
    MarchingCubes = CreateDefaultSubobject<UMarchingCubes>(TEXT("MarchingCubes"));

    // Create the subsystem
    HeightCacheSystem = CreateDefaultSubobject<UDiggerLandscapeCache>(TEXT("HeightCacheSystem"));

    if (!HoleBP)
    {
        HoleBP = LoadDefaultHoleBPClass();
    }

    // Load the material instance in the constructor
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Digger/Digger/Materials/M_SedimentMaster_Inst.M_SedimentMaster_Inst"));
    if (Material.Succeeded())
    {
        TerrainMaterial = Material.Object;
    }
    else
    {
        if (DiggerDebug::UserConv())
        {
            UE_LOG(LogTemp, Error, TEXT("Material M_SedimentMaster_Inst not found in /Game/Digger/Materials/. Please ensure it exists and is a Material Instance Constant."));
        }
    }
}


void ADiggerManager::SpawnLight(const FBrushStroke& BrushStroke)
{
    if (!GetWorld()) return;

    FVector FinalPosition = BrushStroke.BrushPosition + BrushStroke.BrushOffset;
    FRotator Rotation = BrushStroke.BrushRotation;

    // 1. Spawn
    ADynamicLightActor* Light = GetWorld()->SpawnActor<ADynamicLightActor>(
        ADynamicLightActor::StaticClass(), FinalPosition, Rotation);

    if (Light)
    {
        // 2. Initialize (This handles Type, Intensity, Color, and Component Creation all in one)
        // We do NOT need to call InitLight() separately.
        Light->InitializeFromBrush(BrushStroke);

        // 3. Track it
        SpawnedLights.Add(Light);

        // 4. Editor Organization (Wrapped safely)
#if WITH_EDITOR
        Light->SetFolderPath(FName("Digger/DynamicLights"));
#endif

        if (DiggerDebug::Lights())
        {
            UE_LOG(LogTemp, Warning, TEXT("Spawned Light Type: %s"), *GetLightTypeName(BrushStroke.LightType));
        }
    }
}


void ADiggerManager::PostInitProperties()
{
    Super::PostInitProperties();
    if (!HasAnyFlags(RF_ClassDefaultObject))
    {
        InitializeBrushShapes();
    }
}


// In ADiggerManager.cpp
void ADiggerManager::InitializeBrushShapes()
{
    // Clear existing brushes
    BrushShapeMap.Empty();

    // Initialize all available brush shapes
    BrushShapeMap.Add(EVoxelBrushType::Sphere, NewObject<USphereBrushShape>(this));
    BrushShapeMap.Add(EVoxelBrushType::Cube, NewObject<UCubeBrushShape>(this));
    BrushShapeMap.Add(EVoxelBrushType::Cone, NewObject<UConeBrushShape>(this));
    BrushShapeMap.Add(EVoxelBrushType::Cylinder, NewObject<UCylinderBrushShape>(this));
    BrushShapeMap.Add(EVoxelBrushType::Capsule, NewObject<UCapsuleBrushShape>(this));
    BrushShapeMap.Add(EVoxelBrushType::Torus, NewObject<UTorusBrushShape>(this));
    BrushShapeMap.Add(EVoxelBrushType::Pyramid, NewObject<UPyramidBrushShape>(this));
    BrushShapeMap.Add(EVoxelBrushType::Icosphere, NewObject<UIcosphereBrushShape>(this));
    BrushShapeMap.Add(EVoxelBrushType::Smooth, NewObject<USmoothBrushShape>(this));
    
    // Log initialization for debugging
    /*UE_LOG(LogTemp, Warning, TEXT("Initializing brush shapes..."));
    for (const auto& Pair : BrushShapeMap)
    {
        UE_LOG(LogTemp, Warning, TEXT("Initialized %s for brush type %d"), 
            *Pair.Value->GetClass()->GetName(), 
            (int32)Pair.Key);
    }*/

    // If there's an ActiveBrush member, we should probably handle it differently
    // Can you show me the ActiveBrush related code so we can fix that?
}

UVoxelBrushShape* ADiggerManager::GetActiveBrushShape(EVoxelBrushType BrushType) const
{
    if (UVoxelBrushShape* const* Found = BrushShapeMap.Find(BrushType))
    {
        return *Found;
    }
    return nullptr;
}

UVoxelBrushShape* ADiggerManager::GetBrushShapeForType(EVoxelBrushType BrushType)
{
    // Lazy init
    if (BrushShapeMap.Num() == 0)
    {
        InitializeBrushShapes();
    }

    if (UVoxelBrushShape* const* Found = BrushShapeMap.Find(BrushType))
    {
        return *Found;
    }

    // Fallback to Sphere if available
    if (UVoxelBrushShape* const* Sphere = BrushShapeMap.Find(EVoxelBrushType::Sphere))
    {
        return *Sphere;
    }

    return nullptr;
}



void ADiggerManager::ApplyBrushToAllChunksPIE(FBrushStroke& BrushStroke)
{
    // Get the hit location from camera first
    FHitResult HitResult;
    if (!ActiveBrush->GetCameraHitLocation(HitResult))
    {
        if (DiggerDebug::Brush())
        {
            UE_LOG(LogTemp, Warning, TEXT("No valid hit found"));
        }
        return;
    }

    // Create brush stroke with the hit location and brush settings
    BrushStroke = ActiveBrush->CreateBrushStroke(HitResult, ActiveBrush->GetDig());

    if (DiggerDebug::UserConv())
    {
        UE_LOG(LogTemp, Warning, TEXT("PIE Brush applied at: %s"), *BrushStroke.BrushPosition.ToString());
    }
    
    // Apply the brush
    ApplyBrushToAllChunks(BrushStroke);
}

void ADiggerManager::ApplyBrushToAllChunks(FBrushStroke& BrushStroke, bool ForceUpdate)
{
    ApplyBrushToAllChunks(BrushStroke);
}

void ADiggerManager::ApplyBrushToAllChunks(FBrushStroke& BrushStroke)
{
    const UVoxelBrushShape* ActiveBrushShape = GetActiveBrushShape(BrushStroke.BrushType);
    if (!ActiveBrushShape)
    {
        if (DiggerDebug::Brush())
        {
            UE_LOG(LogTemp, Error, TEXT("ActiveBrushShape is null for brush type %d"), (int32)BrushStroke.BrushType);
        }
        return;
    }

    // If Brush and Verbose Debug Flags are both on, give the full brush details.
    if (DiggerDebug::Brush() && DiggerDebug::Verbose())
    {
        UE_LOG(LogTemp, Warning, TEXT("ApplyBrushToAllChunks: === BRUSH STROKE DEBUG ==="));
        UE_LOG(LogTemp, Warning, TEXT("ApplyBrushToAllChunks: Position: %s"), *BrushStroke.BrushPosition.ToString());
        UE_LOG(LogTemp, Warning, TEXT("ApplyBrushToAllChunks: Radius: %f"), BrushStroke.BrushRadius);
        UE_LOG(LogTemp, Warning, TEXT("ApplyBrushToAllChunks: Falloff: %f"), BrushStroke.BrushFalloff);
        UE_LOG(LogTemp, Warning, TEXT("ApplyBrushToAllChunks: Strength: %f"), BrushStroke.BrushStrength);
        UE_LOG(LogTemp, Warning, TEXT("ApplyBrushToAllChunks: bDig: %s"), BrushStroke.bDig ? TEXT("true") : TEXT("false"));
    }

    
    if (FVoxelConversion::LocalVoxelSize <= 0.0f)
    {
        FVoxelConversion::InitFromConfig(8, 4, 100.0f, FVector::ZeroVector);
    }

    // Handle hole spawn ONCE per brush stroke, before processing chunks
    if (BrushStroke.bDig)
    {
        // 1. Get Height
        float TerrainHeight = GetLandscapeHeightAt(BrushStroke.BrushPosition);

        // 2. Check for Sentinel (Cache Miss)
        bool bHeightValid = (TerrainHeight > (UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT + 1.0f));

        if (!bHeightValid)
        {
            // CASE A: Cache Missed.
            // We assume we are digging on a valid surface that just hasn't cached yet.
            // Force the spawn attempt. HandleHoleSpawn has its own internal checks if needed.
            if (DiggerDebug::Holes())
            {
                UE_LOG(LogTemp, Warning, TEXT("ApplyBrushToAllChunks: Height Cache Miss at %s. Attempting Hole Spawn anyway."), 
                    *BrushStroke.BrushPosition.ToString());
            }
            HandleHoleSpawn(BrushStroke);
        }
        else
        {
            // CASE B: Valid Height.
            // Check if the bottom of the brush penetrates the surface.
            // Formula: BrushBottom (Center - Radius + Falloff) <= SurfaceHeight
            float BrushBottom = BrushStroke.BrushPosition.Z - BrushStroke.BrushRadius + BrushStroke.BrushFalloff;
            
            if (BrushBottom <= TerrainHeight)
            {
                HandleHoleSpawn(BrushStroke);
            }
            else
            {
                // We are digging in the air above the terrain? Don't spawn a black cap.
                if (DiggerDebug::Holes())
                {
                    // Optional verbose log
                     UE_LOG(LogTemp, Warning, TEXT("Skipped Hole Spawn: Brush is above terrain."));
                }
            }
        }
    }

    float BrushEffectRadius = BrushStroke.BrushRadius + BrushStroke.BrushFalloff;
    float ChunkWorldSize = FVoxelConversion::ChunkSize * FVoxelConversion::LocalVoxelSize;
    float ChunkDiagonal = ChunkWorldSize * 1.732f; // sqrt(3) for 3D diagonal
    float SafetyPadding = BrushEffectRadius + ChunkDiagonal;

    FVector Min = BrushStroke.BrushPosition - FVector(SafetyPadding);
    FVector Max = BrushStroke.BrushPosition + FVector(SafetyPadding);

    FIntVector MinChunk = FVoxelConversion::WorldToChunk(Min);
    FIntVector MaxChunk = FVoxelConversion::WorldToChunk(Max);

    for (int32 X = MinChunk.X; X <= MaxChunk.X; ++X)
    {
        for (int32 Y = MinChunk.Y; Y <= MaxChunk.Y; ++Y)
        {
            for (int32 Z = MinChunk.Z; Z <= MaxChunk.Z; ++Z)
            {
                FIntVector ChunkCoords(X, Y, Z);
                
                // Add this logging
                if (DiggerDebug::Chunks())
                {
                    UE_LOG(LogTemp, Warning, TEXT("Trying to get/create chunk at: %s"), *ChunkCoords.ToString());
                }
                
                if (UVoxelChunk* Chunk = GetOrCreateChunkAtChunk(ChunkCoords))
                {
                    if (DiggerDebug::Chunks())
                    {
                        UE_LOG(LogTemp, Warning, TEXT("Successfully got chunk at: %s"), *ChunkCoords.ToString());
                    }
                    
                    ENetMode NetMode = GetWorld()->GetNetMode();
                    if (NetMode == NM_Standalone)
                    {
                        // Single player: call the local method directly
                        Chunk->ApplyBrushStroke(BrushStroke);
                    }
                    else
                    {
                        // Multiplayer: call the multicast function (from the server)
                        Chunk->MulticastApplyBrushStroke(BrushStroke);
                    }
                }
                else
                {
                    if (DiggerDebug::Chunks())
                    {
                        UE_LOG(LogTemp, Error, TEXT("Failed to get/create chunk at: %s"), *ChunkCoords.ToString());
                    }
                }
            }
        }
    }
}

void ADiggerManager::SetVoxelAtWorldPosition(const FVector& WorldPos, float Value)
{
    // Safety checks
    if (TerrainGridSize <= 0.0f || Subdivisions <= 0)
    {
        if (DiggerDebug::Space() || DiggerDebug::Chunks())
        {
            UE_LOG(LogTemp, Error, TEXT("SetVoxelAtWorldPosition: Invalid TerrainGridSize or Subdivisions!"));
        }
        return;
    }

    // Calculate chunk and voxel index
    FIntVector ChunkCoords = FVoxelConversion::WorldToChunk(WorldPos);
    FIntVector LocalVoxelIndex = FVoxelConversion::WorldToMinCornerVoxel(WorldPos);

    // Try to get or create the chunk
    if (UVoxelChunk* Chunk = GetOrCreateChunkAtChunk(ChunkCoords))
    {
        //Chunk->GetSparseVoxelGrid()->SetVoxel(LocalVoxelIndex, Value, true);

        if (DiggerDebug::Space() || DiggerDebug::Chunks())
        {
            UE_LOG(LogTemp, Display, TEXT("[SetVoxelAtWorldPosition] WorldPos: %s → Chunk: %s, Voxel: %s, Value: %.2f"),
                   *WorldPos.ToString(), *ChunkCoords.ToString(), *LocalVoxelIndex.ToString(), Value);
        }
    }
    else
    {
        if (DiggerDebug::Space() || DiggerDebug::Chunks())
        {
            UE_LOG(LogTemp, Warning, TEXT("SetVoxelAtWorldPosition: Failed to get or create chunk at %s"), *ChunkCoords.ToString());
        }
    }
}


// Updated ADiggerManager::DebugBrushPlacement()
void ADiggerManager::DebugBrushPlacement(const FVector& ClickPosition)
{
    if (!IsInGameThread())
    {
        if (DiggerDebug::Brush() || DiggerDebug::Threads())
        {
            UE_LOG(LogTemp, Error, TEXT("DebugBrushPlacement: Not called from game thread!"));
        }
        return;
    }

    UWorld* CurrentWorld = GetSafeWorld();
    if (!CurrentWorld)
    {
        if (DiggerDebug::Context())
        {
            UE_LOG(LogTemp, Error, TEXT("DebugBrushPlacement: GetSafeWorld() returned null!"));
        }
        return;
    }

    // Ensure parameters are valid
    if (TerrainGridSize <= 0.0f || Subdivisions <= 0)
    {
        if (DiggerDebug::Brush() || DiggerDebug::Space())
        {
            UE_LOG(LogTemp, Error, TEXT("DebugBrushPlacement: Invalid TerrainGridSize or Subdivisions!"));
        }
        return;
    }

    VoxelSize = TerrainGridSize / Subdivisions;
    float ChunkWorldSize = ChunkSize * TerrainGridSize;

    if (DiggerDebug::Space())
    {
        UE_LOG(LogTemp, Display, TEXT("ChunkSize=%d, Subdivisions=%d, VoxelSize=%.2f, ChunkWorldSize=%.2f"),
               ChunkSize, Subdivisions, VoxelSize, ChunkWorldSize);
    }

    // Compute chunk coordinates from world position
    FIntVector ChunkCoords = FVoxelConversion::WorldToChunk(ClickPosition);

    if (DiggerDebug::Chunks() || DiggerDebug::Space() || DiggerDebug::Brush())
    {
        UE_LOG(LogTemp, Display, TEXT("ClickPos: %s → ChunkCoords: %s"),
               *ClickPosition.ToString(), *ChunkCoords.ToString());
    }

    // Fetch or create the relevant chunk
    if (UVoxelChunk* Chunk = GetOrCreateChunkAtChunk(ChunkCoords))
    {
        Chunk->GetSparseVoxelGrid()->RenderVoxels();
        Chunk->GetSparseVoxelGrid()->LogVoxelData();
    }
    else
    {
        if (DiggerDebug::Chunks() || DiggerDebug::Space())
        {
            UE_LOG(LogTemp, Warning, TEXT("Failed to get or create chunk at %s"), *ChunkCoords.ToString());
        }
        return;
    }

    // Compute chunk origin and extent
    FVector ChunkCenter = FVector(ChunkCoords) * ChunkWorldSize;
    FVector ChunkExtent = FVector(ChunkWorldSize * 0.5f);
    FVector ChunkOrigin = ChunkCenter - ChunkExtent;

    FVector LocalInChunk = ClickPosition - ChunkOrigin;
    FIntVector LocalVoxel = FVoxelConversion::WorldToLocalVoxel(ClickPosition);
    FVector LocalVoxelToWorld = FVoxelConversion::LocalVoxelToWorld(LocalVoxel);

    if (DiggerDebug::Space() || DiggerDebug::Voxels() || DiggerDebug::Chunks())
    {
        UE_LOG(LogTemp, Error, TEXT("LocalInChunk: %s, LocalVoxel: %s, WorldPosition: %s"),
               *LocalInChunk.ToString(), *LocalVoxel.ToString(), *LocalVoxelToWorld.ToString());
    }

    // World-space center of the voxel
    FVector VoxelCenter = ChunkOrigin + FVector(LocalVoxel) * VoxelSize + FVector(VoxelSize * 0.5f);

    if (DiggerDebug::Voxels())
    {
        UE_LOG(LogTemp, Display, TEXT("VoxelCenter: %s"), *VoxelCenter.ToString());
    }
    
    try
    {
        // Get the fast debug subsystem
        if (auto* FastDebug = UFastDebugSubsystem::Get(this))
        {
            // Draw the chunk bounding box (red)
            FastDebug->DrawBox(ChunkCenter, ChunkExtent, FRotator::ZeroRotator, 
                              FFastDebugConfig(FLinearColor::Red, 30.0f, 2.0f));

            // Draw chunk center (yellow sphere)
            FastDebug->DrawSphere(ChunkCenter, 30.0f, 
                                 FFastDebugConfig(FLinearColor::Yellow, 30.0f, 2.0f));

            // Draw chunk origin (orange sphere)
            FastDebug->DrawSphere(ChunkOrigin, 20.0f, 
                                 FFastDebugConfig(FLinearColor(1.0f, 0.5f, 0.0f), 30.0f, 2.0f));

            const int32 ChunkVoxels = ChunkSize * Subdivisions;
            FVector SelectedVoxelCenter = FVector::ZeroVector;

            // Collect edge voxel positions for batch rendering
            TArray<FVector> EdgeVoxelPositions;
            EdgeVoxelPositions.Reserve(ChunkVoxels * ChunkVoxels * 6); // Rough estimate

            for (int32 X = 0; X < ChunkVoxels; X++)
            {
                for (int32 Y = 0; Y < ChunkVoxels; Y++)
                {
                    for (int32 Z = 0; Z < ChunkVoxels; Z++)
                    {
                        FIntVector VoxelCoord(X, Y, Z);
                        FVector CurrentVoxelCenter = ChunkOrigin + FVector(VoxelCoord) * VoxelSize + FVector(VoxelSize * 0.5f);

                        // Collect edge voxels
                        if (X == 0 || X == ChunkVoxels - 1 ||
                            Y == 0 || Y == ChunkVoxels - 1 ||
                            Z == 0 || Z == ChunkVoxels - 1)
                        {
                            EdgeVoxelPositions.Add(CurrentVoxelCenter);
                        }

                        // Track the selected voxel
                        if (VoxelCoord == LocalVoxel)
                        {
                            SelectedVoxelCenter = CurrentVoxelCenter;
                        }
                    }
                }
            }

            // Batch draw edge voxels as points (cyan)
            if (EdgeVoxelPositions.Num() > 0)
            {
                FastDebug->DrawPoints(EdgeVoxelPositions, 5.0f, 
                                     FFastDebugConfig(FColor::Cyan, 30.0f, 2.0f));
            }

            // Draw the selected voxel (magenta box)
            if (SelectedVoxelCenter != FVector::ZeroVector)
            {
                FastDebug->DrawBox(SelectedVoxelCenter, FVector(VoxelSize * 0.5f), FRotator::ZeroRotator,
                                  FFastDebugConfig(FLinearColor(1.0f, 0.0f, 1.0f), 30.0f, 2.0f));
            }

            // Draw interaction line
            FastDebug->DrawLine(ClickPosition, SelectedVoxelCenter, 
                               FFastDebugConfig(FLinearColor::Green, 30.0f, 3.0f));

            // Draw interaction points
            FastDebug->DrawSphere(ClickPosition, 20.0f, 
                                 FFastDebugConfig(FLinearColor::Blue, 30.0f, 2.0f));
            FastDebug->DrawSphere(SelectedVoxelCenter, 20.0f, 
                                 FFastDebugConfig(FLinearColor(1.0f, 0.0f, 1.0f), 30.0f, 2.0f));
        }

        if (DiggerDebug::Brush())
        {
            UE_LOG(LogTemp, Warning, TEXT("DebugBrushPlacement: Debug visuals drawn successfully"));
        }
    }
    catch (...)
    {
        UE_LOG(LogTemp, Error, TEXT("DebugBrushPlacement: Exception drawing debug visuals"));
    }

    // Diagonal marker boxes
    DrawDiagonalDebugVoxelsFast(ChunkCoords);
    // Individual voxels right nearest the click position
    DebugDrawVoxelAtWorldPositionFast(ClickPosition, FLinearColor::White, 25.0f, 2.0f);

    if (DiggerDebug::Brush())
    {
        UE_LOG(LogTemp, Warning, TEXT("DebugBrushPlacement: completed"));
    }
}


// Updated ADiggerManager::DebugDrawVoxelAtWorldPosition()
void ADiggerManager::DebugDrawVoxelAtWorldPositionFast(const FVector& WorldPosition, const FLinearColor& BoxColor, float Duration, float Thickness)
{
    FIntVector ChunkCoords;
    FIntVector VoxelIndex;
    FVoxelConversion::WorldToChunkAndVoxel(WorldPosition, ChunkCoords, VoxelIndex);

    if (!FVoxelConversion::IsValidVoxelIndex(VoxelIndex))
    {
        UE_LOG(LogTemp, Warning, TEXT("Invalid voxel index at world position %s: Chunk %s, VoxelIndex %s"),
            *WorldPosition.ToString(), *ChunkCoords.ToString(), *VoxelIndex.ToString());
        return;
    }

    // Get the voxel center in world space
    FVector VoxelCenter = FVoxelConversion::MinCornerVoxelToWorld(ChunkCoords, VoxelIndex);

    // Draw using fast debug system
    if (auto* FastDebug = UFastDebugSubsystem::Get(this))
    {
        FastDebug->DrawBox(VoxelCenter, FVector(FVoxelConversion::LocalVoxelSize * 0.5f), FRotator::ZeroRotator,
                          FFastDebugConfig(BoxColor, Duration, Thickness));
    }

    if (DiggerDebug::Chunks() || DiggerDebug::Voxels())
    {
        UE_LOG(LogTemp, Log, TEXT("Drew voxel at world position %s: Chunk %s, VoxelIndex %s, Center %s"),
            *WorldPosition.ToString(), *ChunkCoords.ToString(), *VoxelIndex.ToString(), *VoxelCenter.ToString());
    }
}

bool ADiggerManager::PerformPlayerDig()
{
    // 1. Safety Checks
    if (!IsValid(ActiveBrush))
    {
        // Auto-create brush if missing (lazy initialization)
        ActiveBrush = NewObject<UVoxelBrushShape>(this, UVoxelBrushShape::StaticClass());
        ActiveBrush->InitializeBrush(EVoxelBrushType::Sphere, 150.0f, FVector::ZeroVector, this);
    }

    // 2. Get the Hit (Delegated to Brush Logic)
    FHitResult HitResult;
    // Ensure GetCameraHitLocation uses APlayerController, NOT GEditor
    if (!ActiveBrush->GetCameraHitLocation(HitResult)) 
    {
        if (DiggerDebug::Brush())
        {
            UE_LOG(LogTemp, Warning, TEXT("PerformPlayerDig: No valid hit found"));
        }
        return false; // Tell BP we failed
    }

    // 3. Construct the Stroke
    // Note: ensure GetDig() returns the correct boolean state for the current tool
    FBrushStroke BrushStroke = ActiveBrush->CreateBrushStroke(HitResult, ActiveBrush->GetDig());

    if (DiggerDebug::UserConv())
    {
        UE_LOG(LogTemp, Warning, TEXT("Player Dig applied at: %s"), *BrushStroke.BrushPosition.ToString());
    }
    
    // 4. Execute
    ApplyBrushToAllChunks(BrushStroke);
    
    return true; // Tell BP we succeeded
}

//Wrapper that is of type float from the TOptional worker method.
float ADiggerManager::GetLandscapeHeightAt(const FVector& Location)
{
    TOptional<float> HeightOpt = GetLandscapeHeightAt_Internal(Location);
    return HeightOpt.IsSet() ? HeightOpt.GetValue() : UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT;
}


TOptional<float> ADiggerManager::GetLandscapeHeightAt_Internal(const FVector& WorldPos) const
{
    // TEMP: bypass cache to restore old behavior and verify
    ALandscapeProxy* Landscape = GetLandscapeProxyAt(WorldPos);
    if (!Landscape || !IsValid(Landscape))
    {
        if (DiggerDebug::Landscape())
        {
            UE_LOG(LogTemp, Warning, TEXT("GetLandscapeHeightAt: No proxy at %s"), *WorldPos.ToString());
        }
        return TOptional<float>();
    }

    TOptional<float> Sampled = Landscape->GetHeightAtLocation(WorldPos);
    if (!Sampled.IsSet() && DiggerDebug::Landscape())
    {
        UE_LOG(LogTemp, Warning, TEXT("GetLandscapeHeightAt: Failed sampling at %s"), *WorldPos.ToString());
    }
    return Sampled;
}


/*TOptional<float> ADiggerManager::GetLandscapeHeightAt(const FVector& WorldPos) const
{
    if (!HeightCacheSystem)
    {
        return TOptional<float>();
    }

    // Route through precise sampler
    return HeightCacheSystem->SampleLandscapeHeightPrecise(WorldPos);
}*/

TOptional<float> ADiggerManager::SampleLandscapeHeight(ALandscapeProxy* Proxy, const FVector& WorldPos, bool bForcePrecise)
{
    if (!HeightCacheSystem)
    {
        return TOptional<float>();
    }

    // Route everything through the precise sampler
    return HeightCacheSystem->SampleLandscapeHeightPrecise(WorldPos);
}


bool ADiggerManager::PerformDig(FVector StartLocation, FVector Direction, float TraceRange, float Radius, float Falloff, EVoxelBrushType Shape, bool bIsDigging)
{
    // 1. Calculate Trace End
    FVector TraceEnd = StartLocation + (Direction * TraceRange);

    // 2. Safety: Ensure ActiveBrush exists for the calculation logic
    if (!IsValid(ActiveBrush))
    {
        ActiveBrush = NewObject<UVoxelBrushShape>(this, UVoxelBrushShape::StaticClass());
        // Init with dummy values, we will override them in the stroke anyway
        ActiveBrush->InitializeBrush(Shape, Radius, FVector::ZeroVector, this);
    }

    // 3. Perform Smart Trace (Ignores existing holes, hits terrain)
    FHitResult Hit = ActiveBrush->SmartTrace(StartLocation, TraceEnd);

    if (Hit.bBlockingHit)
    {
        // 4. Construct the Stroke manually
        FBrushStroke Stroke;
        Stroke.BrushPosition = Hit.ImpactPoint;
        Stroke.BrushRadius = Radius;
        Stroke.BrushFalloff = Falloff;
        Stroke.BrushType = Shape;
        Stroke.BrushStrength = 1.0f; // Default strength
        Stroke.bDig = bIsDigging;
        
        // Handle Advanced settings if needed (Rotation, etc.)
        Stroke.BrushRotation = FRotator::ZeroRotator; 

        // 5. Execute
        ApplyBrushToAllChunks(Stroke);

        if (DiggerDebug::Brush())
        {
            UE_LOG(LogTemp, Log, TEXT("PerformDig: Hit at %s, Applied %s"), *Hit.ImpactPoint.ToString(), bIsDigging ? TEXT("Dig") : TEXT("Add"));
        }

        return true;
    }

    return false;
}

// Updated ADiggerManager::DrawDiagonalDebugVoxels()
void ADiggerManager::DrawDiagonalDebugVoxelsFast(FIntVector ChunkCoords)
{
    // Get the fast debug subsystem
    auto* FastDebug = UFastDebugSubsystem::Get(this);
    if (!FastDebug) return;

    // Calculate chunk dimensions
    const int32 VoxelsPerChunk = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
    const float DebugDuration = 30.0f;
    
    // Get the chunk center using the conversion method
    FVector ChunkCenter = FVoxelConversion::ChunkToWorld(ChunkCoords);
    FVector ChunkExtent = FVector(FVoxelConversion::ChunkSize * FVoxelConversion::TerrainGridSize / 2.0f);
    
    // Calculate the minimum corner based on the center and extent
    FVector ChunkMinCorner = ChunkCenter - ChunkExtent;
    
    UE_LOG(LogTemp, Warning, TEXT("DrawDiagonalDebugVoxelsFast - ChunkCoords: %s"), *ChunkCoords.ToString());
    UE_LOG(LogTemp, Warning, TEXT("ChunkCenter: %s, ChunkMinCorner: %s"), 
           *ChunkCenter.ToString(), *ChunkMinCorner.ToString());
    UE_LOG(LogTemp, Warning, TEXT("VoxelsPerChunk: %d, LocalVoxelSize: %f"),
           VoxelsPerChunk, FVoxelConversion::LocalVoxelSize);
    
    // Collect diagonal voxel positions for batch rendering
    TArray<FVector> DiagonalVoxelPositions;
    DiagonalVoxelPositions.Reserve(VoxelsPerChunk);
    
    // Collect diagonal voxels from minimum corner to maximum corner
    for (int32 i = 0; i < VoxelsPerChunk; i++)
    {
        FVector VoxelCenter = ChunkMinCorner + 
                             FVector(i + 0.5f, i + 0.5f, i + 0.5f) * FVoxelConversion::LocalVoxelSize;
        DiagonalVoxelPositions.Add(VoxelCenter);
    }
    
    // Batch draw diagonal voxels (green)
    if (DiagonalVoxelPositions.Num() > 0)
    {
        FastDebug->DrawBoxes(DiagonalVoxelPositions, 
                           FVector(FVoxelConversion::LocalVoxelSize * 0.45f),
                           FFastDebugConfig(FLinearColor::Green, DebugDuration, 1.5f));
    }
    
    // Collect overflow voxel positions
    TArray<FVector> OverflowVoxelPositions;
    FVector overflowOffsets[] = {
        FVector(-1, -1, -1),  // Corner overflow
        FVector(-1, 0, 0),    // X-axis overflow
        FVector(0, -1, 0),    // Y-axis overflow
        FVector(0, 0, -1)     // Z-axis overflow
    };
    
    for (const FVector& offset : overflowOffsets)
    {
        FVector VoxelCenter = ChunkMinCorner + 
                             (offset + FVector(0.5f)) * FVoxelConversion::LocalVoxelSize;
        OverflowVoxelPositions.Add(VoxelCenter);
        
        UE_LOG(LogTemp, Warning, TEXT("Overflow voxel at local position %s, world position %s"),
               *offset.ToString(), *VoxelCenter.ToString());
    }
    
    // Batch draw overflow voxels (purple)
    if (OverflowVoxelPositions.Num() > 0)
    {
        FastDebug->DrawBoxes(OverflowVoxelPositions, 
                           FVector(FVoxelConversion::LocalVoxelSize * 0.45f),
                           FFastDebugConfig(FLinearColor(0.5f, 0.0f, 1.0f), DebugDuration, 2.0f));
    }
    
    // Draw reference boxes
    FastDebug->DrawBox(ChunkCenter, ChunkExtent, FRotator::ZeroRotator,
                      FFastDebugConfig(FLinearColor::Red, DebugDuration, 2.5f));
    
    FastDebug->DrawBox(ChunkMinCorner, FVector(FVoxelConversion::LocalVoxelSize * 0.75f), FRotator::ZeroRotator,
                      FFastDebugConfig(FLinearColor::Yellow, DebugDuration, 3.0f));
    
    FVector ChunkMaxCorner = ChunkCenter + ChunkExtent;
    FastDebug->DrawBox(ChunkMaxCorner, FVector(FVoxelConversion::LocalVoxelSize * 0.75f), FRotator::ZeroRotator,
                      FFastDebugConfig(FLinearColor::Blue, DebugDuration, 3.0f));
    
    FastDebug->DrawBox(FVector::ZeroVector, FVector(FVoxelConversion::LocalVoxelSize * 1.5f), FRotator::ZeroRotator,
                      FFastDebugConfig(FLinearColor(1.0f, 0.0f, 1.0f), DebugDuration, 3.0f));
    
    // Draw overflow regions on the minimum sides
    const float ChunkWorldSize = FVoxelConversion::ChunkSize * FVoxelConversion::TerrainGridSize;
    
    // X-axis overflow region
    FVector XOverflowCenter = ChunkMinCorner + FVector(-FVoxelConversion::LocalVoxelSize * 0.5f, 
                                                       ChunkWorldSize * 0.5f, 
                                                       ChunkWorldSize * 0.5f);
    FVector XOverflowExtent = FVector(FVoxelConversion::LocalVoxelSize * 0.5f, ChunkExtent.Y, ChunkExtent.Z);
    
    FastDebug->DrawBox(XOverflowCenter, XOverflowExtent, FRotator::ZeroRotator,
                      FFastDebugConfig(FLinearColor(1.0f, 0.5f, 1.0f, 0.25f), DebugDuration, 1.0f));
    
    // Y-axis overflow region
    FVector YOverflowCenter = ChunkMinCorner + FVector(ChunkWorldSize * 0.5f, 
                                                       -FVoxelConversion::LocalVoxelSize * 0.5f, 
                                                       ChunkWorldSize * 0.5f);
    FVector YOverflowExtent = FVector(ChunkExtent.X, FVoxelConversion::LocalVoxelSize * 0.5f, ChunkExtent.Z);
    
    FastDebug->DrawBox(YOverflowCenter, YOverflowExtent, FRotator::ZeroRotator,
                      FFastDebugConfig(FLinearColor(1.0f, 0.5f, 1.0f, 0.25f), DebugDuration, 1.0f));
    
    // Z-axis overflow region  
    FVector ZOverflowCenter = ChunkMinCorner + FVector(ChunkWorldSize * 0.5f, 
                                                       ChunkWorldSize * 0.5f, 
                                                       -FVoxelConversion::LocalVoxelSize * 0.5f);
    FVector ZOverflowExtent = FVector(ChunkExtent.X, ChunkExtent.Y, FVoxelConversion::LocalVoxelSize * 0.5f);
    
    FastDebug->DrawBox(ZOverflowCenter, ZOverflowExtent, FRotator::ZeroRotator,
                      FFastDebugConfig(FLinearColor(1.0f, 0.5f, 1.0f, 0.25f), DebugDuration, 1.0f));
}


UStaticMesh* ADiggerManager::ConvertIslandToStaticMesh(const FIslandData& Island, bool bWorldOrigin, FString AssetName)
{
    if (DiggerDebug::Islands())
        UE_LOG(LogTemp, Warning, TEXT("Converting island %d to static mesh..."));

    // 1. Generate mesh data for the island using Marching Cubes
    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FColor> Colors;
    TArray<FProcMeshTangent> Tangents;

/*
            // Single Island Mesh Generation in DiggerManager
            // You may need to create a temporary SparseVoxelGrid for just this island
            USparseVoxelGrid* TempGrid = NewObject<USparseVoxelGrid>();
            
            //TempGrid->AddVoxels(IslandVoxels);

            // Generate mesh for just this island
            MarchingCubes->GenerateMeshForIsland(TempGrid, Vertices, Triangles, Normals, UVs, Colors, Tangents);

            // 4. Remove island voxels from the main grid
            //TempGrid->RemoveVoxels(IslandVoxels);*/
    

    // For demonstration, we'll log and return nullptr if not implemented
    if (Vertices.Num() == 0 || Triangles.Num() == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("No mesh data generated for island!"));
        return nullptr;
    }

    // 2. Create a temporary ProceduralMeshComponent
    UProceduralMeshComponent* TempProcMesh = NewObject<UProceduralMeshComponent>(this);
    TempProcMesh->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, true);

    // 3. Convert ProceduralMeshComponent to StaticMesh
    FString PackageName = FString::Printf(TEXT("/Game/Islands/%s"), *AssetName);
    UPackage* MeshPackage = CreatePackage(*PackageName);

    UStaticMesh* NewStaticMesh = NewObject<UStaticMesh>(MeshPackage, *AssetName, RF_Public | RF_Standalone);
    if (!NewStaticMesh)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create UStaticMesh object!"));
        return nullptr;
    }

    // Use the built-in conversion utility (UE 4.23+)
    bool bSuccess = false;//TempProcMesh->CreateStaticMesh(NewStaticMesh, MeshPackage, AssetName, true);
    if (!bSuccess)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to convert procedural mesh to static mesh!"));
        return nullptr;
    }

    // 4. Register and save the asset
    FAssetRegistryModule::AssetCreated(NewStaticMesh);
    NewStaticMesh->MarkPackageDirty();

#if WITH_EDITOR
    // Notify the content browser
    TArray<UObject*> ObjectsToSync;
    ObjectsToSync.Add(NewStaticMesh);
    GEditor->SyncBrowserToObjects(ObjectsToSync);
#endif

    UE_LOG(LogTemp, Warning, TEXT("Static mesh asset created: %s"), *PackageName);

    return NewStaticMesh;
}

void ADiggerManager::RefreshLandscapeCache()
{
    HeightCacheSystem->RefreshLandscapeCache();
}


void ADiggerManager::ProcessDirtyChunksLoop()
{
    if (DiggerDebug::Islands())
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Running ADM::ProcessDirtyChunksLoop..."));
    }

    for (const auto& ChunkPair : ChunkMap)
    {
        UVoxelChunk* Chunk = ChunkPair.Value;
        if (Chunk)
        {
            Chunk->UpdateIfDirty(); // Only updates if marked dirty
        }
    }

    if (DiggerDebug::Islands())
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Finished updating dirty chunks."));
    }
}

void ADiggerManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // Cleanup
    if (HeightCacheSystem)
    {
        HeightCacheSystem->Clear();
    }
    
    // If the game is ending (EndPIE) or the Level is unloading, 
    // we must wipe the slate clean so the Editor doesn't inherit garbage data.
    if (EndPlayReason == EEndPlayReason::EndPlayInEditor || 
        EndPlayReason == EEndPlayReason::LevelTransition || 
        EndPlayReason == EEndPlayReason::Destroyed)
    {
        ClearAllVoxelData();
    }

    // Call the parent class (Standard practice)
    Super::EndPlay(EndPlayReason);
}

void ADiggerManager::ClearAllVoxelData()
{
#if WITH_EDITOR
    if (GIsEditor && !GetWorld()->IsGameWorld())
    {
        Modify(); 
    }
#endif
    
    UE_LOG(LogTemp, Warning, TEXT("DiggerManager: Clearing all voxel data..."));

    // --- 1. CLEAR LIGHTS ---
    // Iterate backwards or just loop and check validity
    for (AActor* Light : SpawnedLights)
    {
        if (IsValid(Light))
        {
            Light->Destroy();
        }
    }
    SpawnedLights.Empty();

    // --- 2. CLEAR CHUNKS ---
    if (ChunkMap.Num() > 0)
    {
        for (auto& Pair : ChunkMap)
        {
            UVoxelChunk* Chunk = Pair.Value;

            if (IsValid(Chunk))
            {
                // A. Destroy physical holes
                Chunk->ClearSpawnedHoles();

                // B. Clear the Data Grid explicitly
                // Check IsValid to be safe
                if (USparseVoxelGrid* Grid = Chunk->GetSparseVoxelGrid())
                {
                    // Assuming you added Clear() to SparseVoxelGrid as discussed previously.
                    // If not, use: Grid->VoxelData.Empty();
                    Grid->Clear(); 
                }
                
                // C. Mark the chunk object for GC
                Chunk->MarkAsGarbage(); 
            }
        }
    }

    // --- 3. CLEAN UP CONTAINERS ---
    ChunkMap.Empty();
    
    // --- 4. CLEAR PROCEDURAL MESHES ---
    ClearProceduralMeshes();

    UE_LOG(LogTemp, Warning, TEXT("DiggerManager: Cleanup Complete."));
}


FIslandMeshData ADiggerManager::ExtractAndGenerateIslandMesh(const FVector& IslandCenter)
{
    UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Starting ExtractAndGenerateIslandMesh at IslandCenter: %s"), *IslandCenter.ToString());

    FIslandMeshData Result;

    // Step 1: Find or create the voxel chunk at the given center
    UVoxelChunk* Chunk = FindOrCreateNearestChunk(IslandCenter);
    if (!Chunk)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] No chunk found or created near IslandCenter."));
        return Result;
    }

    FVector ChunkOrigin = FVoxelConversion::ChunkToWorld(Chunk->GetChunkCoordinates());
    FVector LocalPosition = IslandCenter - ChunkOrigin;

    FIntVector VoxelCoords = FIntVector(
        FMath::FloorToInt(LocalPosition.X / VoxelSize),
        FMath::FloorToInt(LocalPosition.Y / VoxelSize),
        FMath::FloorToInt(LocalPosition.Z / VoxelSize)
    );

    FVector VoxelWorldCenter = ChunkOrigin + FVector(VoxelCoords) * VoxelSize + FVector(FVoxelConversion::LocalVoxelSize * 0.5f);

    UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Chunk Origin: %s | LocalPosition: %s | VoxelCoords: %s | VoxelWorldCenter: %s"),
        *ChunkOrigin.ToString(),
        *LocalPosition.ToString(),
        *VoxelCoords.ToString(),
        *VoxelWorldCenter.ToString()
    );

    // Visual debugging
    if (true)
    {
        // Draw debug string above search origin
        DrawDebugString(
            GetSafeWorld(),
            VoxelWorldCenter + FVector(0, 0, VoxelSize),
            TEXT("Search Origin"),
            nullptr,
            FColor::White,
            60.0f,
            false
        );

        // RED: Voxel being searched
        DrawDebugBox(
            GetSafeWorld(),
            VoxelWorldCenter,
            FVector(VoxelSize * 0.5f),
            FColor::Red,
            false,
            60.0f,
            0,
            3.0f
        );

        // GREEN: 3x3x3 area around it
        for (int32 dx = -1; dx <= 1; dx++)
        {
            for (int32 dy = -1; dy <= 1; dy++)
            {
                for (int32 dz = -1; dz <= 1; dz++)
                {
                    FIntVector Offset = VoxelCoords + FIntVector(dx, dy, dz);
                    FVector OffsetWorldCenter = ChunkOrigin + FVector(Offset) * VoxelSize + FVector(VoxelSize * 0.5f);

                    DrawDebugBox(
                        GetSafeWorld(),
                        OffsetWorldCenter,
                        FVector(VoxelSize * 0.5f),
                        FColor::Green,
                        false,
                        60.0f,
                        0,
                        1.5f
                    );
                }
            }
        }
    }

    // Step 2: Get voxel grid
    USparseVoxelGrid* VoxelGrid = Chunk->GetSparseVoxelGrid();
    if (!VoxelGrid)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] No SparseVoxelGrid found on chunk."));
        return Result;
    }
    
    if (true)
    {
        for (const auto& Pair : VoxelGrid->VoxelData)
        {
            FVector WorldPos = FVoxelConversion::LocalVoxelToWorld(Pair.Key);
            DrawDebugBox(GetSafeWorld(), WorldPos, FVector(VoxelSize * 0.5f), FColor::Blue, false, 60.0f, 0, 1.5f);
        }
    }

    // Step 3: Find the nearest set voxel and extract the island
    USparseVoxelGrid* ExtractedGrid = nullptr;
    TArray<FIntVector> IslandVoxels;

    FIntVector StartVoxel;
    if (!VoxelGrid->FindNearestSetVoxel(VoxelCoords, StartVoxel))
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] No set voxel found near the provided position."));
        return Result;
    }

    // Make a backup of the original grid before extraction
    USparseVoxelGrid* OriginalGridBackup = nullptr;
   /* if (bMakeBackupBeforeExtraction)
    {
        OriginalGridBackup = DuplicateObject<USparseVoxelGrid>(VoxelGrid, this);
    }*/

    // Run extraction to get surface voxels
    bool bExtractionSuccess = VoxelGrid->ExtractIslandAtPosition(
        FVoxelConversion::LocalVoxelToWorld(StartVoxel), 
        ExtractedGrid, 
        IslandVoxels
    );

    if (!bExtractionSuccess || !ExtractedGrid || IslandVoxels.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Failed to extract island or ExtractedGrid is null."));
        
        // Restore from backup if needed
        /*if (bMakeBackupBeforeExtraction && OriginalGridBackup)
        {
            UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Restoring grid from backup."));
            Chunk->SetSparseVoxelGrid(OriginalGridBackup);
        }*/
        
        return Result;
    }

    // Ensure the extracted grid has the necessary references
    //ExtractedGrid->ParentChunk = nullptr; // Don't want to affect the original chunk
    //ExtractedGrid->DiggerManager = this;

    // Step 4: Generate mesh
    UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Starting mesh generation using Marching Cubes."));

    int32 N = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
    int32 SampleSize = N + 1; // <--- FIX
    TArray<float> HeightMap;
    HeightMap.SetNumUninitialized(SampleSize * SampleSize); // <--- FIX

    for (int32 x = 0; x < SampleSize; ++x) { // <--- FIX LOOP LIMIT
        for (int32 y = 0; y < SampleSize; ++y) { // <--- FIX LOOP LIMIT
            FVector Pos = ChunkOrigin + FVector(x * VoxelSize, y * VoxelSize, 0);
            HeightMap[y * SampleSize + x] = GetLandscapeHeightAt(Pos); // <--- FIX INDEX
        }
    }

    // Generate mesh using marching cubes
    MarchingCubes->GenerateMeshFromGrid(
        ExtractedGrid, ChunkOrigin, VoxelSize,
        HeightMap, // <--- Replaces TODO
        Result.Vertices, Result.Triangles, Result.Normals
    );
    
    
    // Step 5: Finalize result
    Result.MeshOrigin = ChunkOrigin;
    Result.bValid = (Result.Vertices.Num() > 0 && Result.Triangles.Num() > 0);

    if (Result.Vertices.Num() > 0 && Result.Triangles.Num() > 0 && Result.Normals.Num() > 0) {
        // Call mesh creation on game thread
        AsyncTask(ENamedThreads::GameThread, [=]()
        {
            int IslandId=0;
            MarchingCubes->CreateIslandProceduralMesh(Result.Vertices, Result.Triangles, Result.Normals, ChunkOrigin, IslandId);
        });
    } else {
        UE_LOG(LogTemp, Warning, TEXT("Island mesh generation returned empty data"));
    }

    // Debug visualization of the extracted island
    //if (IsDebugging && Result.bValid)
    //{
        for (const FIntVector& Voxel : IslandVoxels)
        {
            FVector WorldPos = ChunkOrigin + FVector(Voxel) * VoxelSize + FVector(VoxelSize * 0.5f);
            DrawDebugBox(GetSafeWorld(), WorldPos, FVector(VoxelSize * 0.4f), FColor::Yellow, false, 60.0f, 0, 2.0f);
        }
   //}

    UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Mesh generation complete. Valid: %s, Vertices: %d, Triangles: %d"),
        Result.bValid ? TEXT("true") : TEXT("false"),
        Result.Vertices.Num(),
        Result.Triangles.Num() / 3
    );

    // Mark the chunk as dirty to trigger a rebuild
    Chunk->MarkDirty();

    return Result;
}

// In ADiggerManager.cpp
void ADiggerManager::RemoveIslandVoxels(const FIslandData& Island)
{
    if (DiggerDebug::Islands())
    {
        UE_LOG(LogTemp, Warning, TEXT("Running ADM::RemoveIslandVoxels!"));
    }

    struct FChunkModification
    {
        TWeakObjectPtr<UVoxelChunk> Chunk;
        TArray<FIntVector> LocalVoxels;
    };

    TMap<FIntVector, FChunkModification> AffectedChunks;
    TSet<FIntVector> DeletedGlobalVoxels;

    {
        FScopeLock Lock(&IslandRemovalMutex);

        for (const FIntVector& GlobalVoxel : Island.Voxels)
        {
            FIntVector ChunkCoords, LocalVoxel;
            FVoxelConversion::GlobalVoxelToChunkAndLocal(GlobalVoxel, ChunkCoords, LocalVoxel);

            UVoxelChunk** ChunkPtr = ChunkMap.Find(ChunkCoords);
            if (!ChunkPtr || !*ChunkPtr) continue;

            USparseVoxelGrid* Grid = (*ChunkPtr)->GetSparseVoxelGrid();
            if (!Grid) continue;

            if (Grid->RemoveVoxel(LocalVoxel))
            {
                AffectedChunks.FindOrAdd(ChunkCoords).Chunk = *ChunkPtr;
                AffectedChunks[ChunkCoords].LocalVoxels.Add(LocalVoxel);
                DeletedGlobalVoxels.Add(GlobalVoxel);
            }
        }
    }

    auto RefreshChunks = [DeletedGlobalVoxels, AffectedChunks, this]()
    {
        int32 TotalRemoved = DeletedGlobalVoxels.Num();

        for (const auto& Pair : AffectedChunks)
        {
            const TWeakObjectPtr<UVoxelChunk>& ChunkPtr = Pair.Value.Chunk;
            if (!ChunkPtr.IsValid()) continue;

            USparseVoxelGrid* Grid = ChunkPtr->GetSparseVoxelGrid();
            if (!Grid) continue;

            // Access one of the deleted voxels to trigger mesh rebuild
            for (const FIntVector& LocalVoxel : Pair.Value.LocalVoxels)
            {
                FVoxelData Dummy;
                Grid->GetVoxel(LocalVoxel); // Ghost ping for mesh invalidation
                break; // Only need one
            }
            ChunkPtr->RefreshSectionMesh();
            ChunkPtr->MarkDirty();
            ChunkPtr->ForceUpdate(); // Single rebuild per chunk
        }

        if (DiggerDebug::Islands())
        {
            UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Island cleanup: Removed %d voxels across %d chunks"),
                   TotalRemoved, AffectedChunks.Num());
        }
        ProcessDirtyChunksLoop();
    };

    if (!IsInGameThread())
    {
        AsyncTask(ENamedThreads::GameThread, MoveTemp(RefreshChunks));
        ProcessDirtyChunksLoop();
    }
    else
    {
        RefreshChunks();
        ProcessDirtyChunksLoop();
    }
}



void ADiggerManager::SaveIslandData(AIslandActor* IslandActor, const FIslandMeshData& MeshData)
{
    if (!IslandActor)
        return;
        
    FIslandSaveData SaveData;
    SaveData.MeshOrigin = MeshData.MeshOrigin;
    SaveData.Vertices = MeshData.Vertices;
    SaveData.Triangles = MeshData.Triangles;
    SaveData.Normals = MeshData.Normals;
    SaveData.bEnablePhysics = IslandActor->ProcMesh->IsSimulatingPhysics();
    
    SavedIslands.Add(SaveData);
}


AIslandActor* ADiggerManager::SpawnIslandActorWithMeshData(
    const FVector& SpawnLocation,
    const FIslandMeshData& MeshData,
    bool bEnablePhysics
)
{
    if (!MeshData.bValid) return nullptr;
    

    AIslandActor* IslandActor = World->SpawnActor<AIslandActor>(AIslandActor::StaticClass(), SpawnLocation, FRotator::ZeroRotator);
    if (IslandActor && IslandActor->ProcMesh)
    {
                // Feed the mesh vertices as-is. We attempted to shift the vertices
                // relative to the spawn location, but this prevented island detection
                // from working correctly when converting islands to static or physics
                // actors. Using the original world-space vertices keeps conversion
                // functionality intact.
                // Use vertices relative to the spawn location so the mesh is correctly
                // positioned when the actor is spawned at SpawnLocation
                TArray<FVector> LocalVertices = MeshData.Vertices;
        for (FVector& Vertex : LocalVertices)
        {
            Vertex -= SpawnLocation;
        }

                IslandActor->ProcMesh->CreateMeshSection_LinearColor(
                    0, LocalVertices, MeshData.Triangles, MeshData.Normals, {}, {}, {}, true
                );
        if (bEnablePhysics)
            IslandActor->ApplyPhysics();
        else
            IslandActor->RemovePhysics();
    }
    return IslandActor;
}




void ADiggerManager::ConvertIslandAtPositionToStaticMesh(const FVector& IslandCenter)
{
    UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] ConvertIslandAtPositionToStaticMesh called at %s"), *IslandCenter.ToString());
    FIslandMeshData MeshData = ExtractAndGenerateIslandMesh(IslandCenter);
    if (MeshData.bValid)
    {
        FString AssetName = FString::Printf(TEXT("Island_%s_StaticMesh"), *IslandCenter.ToString());
        SaveIslandMeshAsStaticMesh(AssetName, MeshData);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] ConvertIslandAtPositionToStaticMesh called with invalid static mesh data at %s"), *IslandCenter.ToString());
    }
}

void ADiggerManager::ConvertIslandAtPositionToActor(const FVector& IslandCenter, bool bEnablePhysics, FIntVector ReferenceVoxel)
{
    UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] ConvertIslandAtPositionToActor called at %s"), *IslandCenter.ToString());

    // Debug visualization
    DrawDebugSphere(GetSafeWorld(), IslandCenter, 50.0f, 16, FColor::Red, false, 10.0f, 0, 2.0f);
    DrawDebugString(GetSafeWorld(), IslandCenter + FVector(0, 0, 60.0f), TEXT("Island Search Point"), nullptr, FColor::White, 10.0f);

    if (ReferenceVoxel != FIntVector::ZeroValue)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Using reference voxel: %s"), *ReferenceVoxel.ToString());

        // Convert global voxel index to chunk + local voxel
        FIntVector ChunkCoords, LocalVoxel;
        FVoxelConversion::GlobalVoxelToChunkAndLocal(ReferenceVoxel, ChunkCoords, LocalVoxel);

        // Find the chunk containing this voxel
        UVoxelChunk* Chunk = ChunkMap.FindRef(ChunkCoords);
        if (!Chunk)
        {
            UE_LOG(LogTemp, Error, TEXT("[DiggerPro] No chunk found for chunk coords: %s"), *ChunkCoords.ToString());
            return;
        }

        // Verify the voxel exists in the grid before extraction
        USparseVoxelGrid* Grid = Chunk->GetSparseVoxelGrid();
        if (!Grid || !Grid->IsVoxelSolid(LocalVoxel))
        {
            UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Reference voxel not found or not solid at local coords: %s"), *LocalVoxel.ToString());
            
            // Fallback to center-based extraction
            FIslandMeshData MeshData = ExtractIslandByCenter(IslandCenter, false, bEnablePhysics);
            if (MeshData.bValid)
            {
                AIslandActor* IslandActor = SpawnIslandActorWithMeshData(IslandCenter, MeshData, bEnablePhysics);
                if (IslandActor)
                {
                    UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Successfully created island actor using center-based extraction"));
                }
            }
            return;
        }

        // Assuming you have an FIslandData from your island detection system
        FIslandData MyIsland; // populated elsewhere
        FIslandMeshData MeshData = ExtractAndGenerateIslandMeshFromData(Chunk, MyIsland);

        if (MeshData.bValid)
        {
            // Use the generated mesh data
            UE_LOG(LogTemp, Log, TEXT("Successfully generated mesh for island at %s"), 
                   *MyIsland.Location.ToString());
        }
    }
    else
    {
        // Fallback to center-based extraction
        FIslandMeshData MeshData = ExtractIslandByCenter(IslandCenter, false, bEnablePhysics);
        if (MeshData.bValid)
        {
            AIslandActor* IslandActor = SpawnIslandActorWithMeshData(IslandCenter, MeshData, bEnablePhysics);
            if (IslandActor)
            {
                UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Successfully created island actor using center-based extraction"));
            }
        }
    }
}


// New function to extract island mesh from a specific voxel
FIslandMeshData ADiggerManager::ExtractIslandByCenter(const FVector& IslandCenter, bool bRemoveAfter, bool bEnablePhysics)
{
    FIslandMeshData Result;
    Result.bValid = false;

    // Step 1: Get all islands
    TArray<FIslandData> AllIslands = DetectUnifiedIslands();
    if (AllIslands.Num() == 0) return Result;

    // Step 2: Find closest island
    FIslandData* ClosestIsland = nullptr;
    float MinDist = FLT_MAX;

    for (FIslandData& Island : AllIslands)
    {
        float Dist = FVector::DistSquared(Island.Location, IslandCenter);
        if (Dist < MinDist)
        {
            MinDist = Dist;
            ClosestIsland = &Island;
        }
    }

    if (!ClosestIsland) return Result;

    // Step 3: Normalize Coordinates (World -> Local)
    // We need to shift the voxels so they start at 0,0,0 relative to a local origin,
    // otherwise the Marching Cubes loop (which runs 0..N) won't find them.
    
    FIntVector MinBound = FIntVector(2147483647, 2147483647, 2147483647);
    for (const FIntVector& Global : ClosestIsland->Voxels)
    {
        MinBound.X = FMath::Min(MinBound.X, Global.X);
        MinBound.Y = FMath::Min(MinBound.Y, Global.Y);
        MinBound.Z = FMath::Min(MinBound.Z, Global.Z);
    }
    
    // Add a small padding of 1 voxel to prevent mesh clipping at the edge
    MinBound -= FIntVector(1, 1, 1);

    // Build the grid with RE-CENTERED keys
    USparseVoxelGrid* TempGrid = NewObject<USparseVoxelGrid>();
    TMap<FIntVector, FVoxelData> IslandVoxelMap;

    for (const FIntVector& Global : ClosestIsland->Voxels)
    {
        // Find the original data
        FIntVector ChunkCoords, LocalVoxel;
        FVoxelConversion::GlobalVoxelToChunkAndLocal(Global, ChunkCoords, LocalVoxel);

        if (UVoxelChunk** ChunkPtr = ChunkMap.Find(ChunkCoords))
        {
            if (USparseVoxelGrid* Grid = (*ChunkPtr)->GetSparseVoxelGrid())
            {
                if (const FVoxelData* Data = Grid->GetVoxelData(LocalVoxel))
                {
                    // Shift the key: Global 100 -> Local 1 (if MinBound is 99)
                    FIntVector LocalKey = Global - MinBound;
                    IslandVoxelMap.Add(LocalKey, *Data);
                }
            }
        }
    }

    TempGrid->SetVoxelData(IslandVoxelMap);
    TempGrid->Initialize(nullptr);

    // Step 4: Calculate Origins & Heights
    VoxelSize = FVoxelConversion::LocalVoxelSize;
    
    // The World Origin for this mesh generation is the MinBound position
    FVector GridOrigin = FVoxelConversion::GlobalVoxelToWorld(MinBound);

    // Prepare Height Map aligned to the GridOrigin
    int32 N = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
    int32 SampleSize = N + 1; // <--- FIX
    TArray<float> HeightMap;
    HeightMap.SetNumUninitialized(SampleSize * SampleSize); // <--- FIX

    for (int32 x = 0; x < SampleSize; ++x) { // <--- FIX LOOP LIMIT
        for (int32 y = 0; y < SampleSize; ++y) { // <--- FIX LOOP LIMIT
            FVector Pos = GridOrigin + FVector(x * VoxelSize, y * VoxelSize, 0);
            HeightMap[y * SampleSize + x] = GetLandscapeHeightAt(Pos); // <--- FIX INDEX
        }
    }

    // Generate mesh using the TempGrid (now containing local keys) and the GridOrigin
    MarchingCubes->GenerateMeshFromGrid(
        TempGrid,      // <--- Corrected variable name
        GridOrigin,    // <--- Corrected Origin (not Zero)
        VoxelSize,
        HeightMap,
        Result.Vertices, Result.Triangles, Result.Normals
    );

    Result.MeshOrigin = GridOrigin;
    Result.bValid = Result.Vertices.Num() > 0;

    // Step 5: Remove original voxels
    if (bRemoveAfter && Result.bValid)
    {
        RemoveIslandVoxels(*ClosestIsland);
    }

    return Result;
}



// New function to extract island mesh from a specific voxel
FIslandMeshData ADiggerManager::ExtractAndGenerateIslandMeshFromData(UVoxelChunk* Chunk, const FIslandData& IslandData)
{
    FIslandMeshData Result;
    
    if (!Chunk)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] No chunk provided for extraction."));
        return Result;
    }
    
    USparseVoxelGrid* VoxelGrid = Chunk->GetSparseVoxelGrid();
    if (!VoxelGrid)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] No SparseVoxelGrid found on chunk."));
        return Result;
    }
    
    if (IslandData.Voxels.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Island data contains no voxels."));
        return Result;
    }
    
    // Validate that VoxelCount matches actual array size
    if (IslandData.VoxelCount != IslandData.Voxels.Num())
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Island VoxelCount (%d) doesn't match Voxels array size (%d)"), 
               IslandData.VoxelCount, IslandData.Voxels.Num());
    }
    
    FVector ChunkOrigin = FVoxelConversion::ChunkToWorld(Chunk->GetChunkCoordinates());
    
    // Create a temporary grid for the island
    USparseVoxelGrid* ExtractedGrid = NewObject<USparseVoxelGrid>();
    
    UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Extracting island at location %s with %d voxels (reference: %s)"), 
           *IslandData.Location.ToString(), 
           IslandData.Voxels.Num(),
           *IslandData.ReferenceVoxel.ToString());
    
    // Copy voxel data directly from the island data
    TMap<FIntVector, FVoxelData> ExtractedVoxelData;
    int32 ValidVoxels = 0;
    
    for (const FIntVector& Pos : IslandData.Voxels)
    {
        FVoxelData* Data = VoxelGrid->GetVoxelData(Pos);
        if (Data)
        {
            ExtractedVoxelData.Add(Pos, *Data);
            ValidVoxels++;
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Island voxel at %s not found in grid"), *Pos.ToString());
        }
    }
    
    if (ValidVoxels == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] No valid voxel data found for island."));
        return Result;
    }
    
    // Convert Voxels array to set for faster lookups when checking boundaries
    TSet<FIntVector> IslandVoxelSet(IslandData.Voxels);
    
    // Add boundary voxels for proper mesh generation
    TSet<FIntVector> BoundaryVoxels;
    for (const FIntVector& SolidVoxel : IslandData.Voxels)
    {
        for (int32 i = 0; i < 6; i++)
        {
            FIntVector Neighbor = SolidVoxel + FVoxelConversion::GetDirectionVector(i);
            
            // Only add boundary if it's not part of the island and not already added
            if (!IslandVoxelSet.Contains(Neighbor) && !BoundaryVoxels.Contains(Neighbor))
            {
                BoundaryVoxels.Add(Neighbor);
                
                // Check if this neighbor exists in the original grid
                FVoxelData* NeighborData = VoxelGrid->GetVoxelData(Neighbor);
                if (NeighborData)
                {
                    ExtractedVoxelData.Add(Neighbor, *NeighborData);
                }
                // Missing neighbors are implicitly AIR in sparse grid
            }
        }
    }
    
    // Set all the voxel data at once
    ExtractedGrid->SetVoxelData(ExtractedVoxelData);
    ExtractedGrid->Initialize(nullptr);
    
    UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Extracted grid contains %d voxels (%d solid + %d boundary)"), 
           ExtractedVoxelData.Num(), ValidVoxels, BoundaryVoxels.Num());
    
    // 1. Prepare Height Map
    int32 N = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
    int32 SampleSize = N + 1; // <--- FIX
    
    TArray<float> HeightMap;
    HeightMap.SetNumUninitialized(SampleSize * SampleSize); // <--- FIX

    for (int32 x = 0; x < SampleSize; ++x) { // <--- FIX LOOP LIMIT
        for (int32 y = 0; y < SampleSize; ++y) { // <--- FIX LOOP LIMIT
            FVector Pos = ChunkOrigin + FVector(x * VoxelSize, y * VoxelSize, 0);
            HeightMap[y * SampleSize + x] = GetLandscapeHeightAt(Pos); // <--- FIX INDEX
        }
    }

    // 2. Call Generation
    MarchingCubes->GenerateMeshFromGrid(
        ExtractedGrid, ChunkOrigin, VoxelSize,
        HeightMap, // <--- Replaces TODO
        Result.Vertices, Result.Triangles, Result.Normals
    );
    
    Result.MeshOrigin = ChunkOrigin;
    Result.bValid = (Result.Vertices.Num() > 0 && Result.Triangles.Num() > 0);
    
    UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Island mesh generation complete. Valid Voxels: %d, Vertices: %d, Triangles: %d"),
        ValidVoxels,
        Result.Vertices.Num(),
        Result.Triangles.Num() / 3
    );
    
    return Result;
}

void ADiggerManager::ClearAllIslandActors()
{
    for (AIslandActor* Actor : IslandActors)
    {
        if (Actor && IsValid(Actor))
        {
            Actor->Destroy();
        }
    }
    IslandActors.Empty();
    SavedIslands.Empty();
}

void ADiggerManager::DestroyIslandActor(AIslandActor* IslandActor)
{
    if (IslandActor && IsValid(IslandActor))
    {
        IslandActors.Remove(IslandActor);
        
        // Find and remove from saved islands
        for (int32 i = SavedIslands.Num() - 1; i >= 0; --i)
        {
            // This is a simple check - you might need a more sophisticated way to match
            if (FVector::DistSquared(SavedIslands[i].MeshOrigin, IslandActor->GetActorLocation()) < 1.0f)
            {
                SavedIslands.RemoveAt(i);
                break;
            }
        }
        
        IslandActor->Destroy();
    }
}



void ADiggerManager::SaveIslandMeshAsStaticMesh(
    const FString& AssetName,
    const FIslandMeshData& MeshData
)
{
    if (!MeshData.bValid) return;

    TArray<FVector3f> FloatVerts = ConvertArray<FVector, FVector3f>(MeshData.Vertices);
    TArray<FVector3f> FloatNormals = ConvertArray<FVector, FVector3f>(MeshData.Normals);
    
    CreateStaticMeshFromRawData(
        GetTransientPackage(),
        AssetName,
        FloatVerts,
        MeshData.Triangles,
        FloatNormals,
        {}, {}, {}
    );
}



bool ADiggerManager::EnsureWorldReference()
{
    if (World)
    {
        return true;
    }

    World = GetSafeWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("World not found in DiggerManager."));
        return false; // Exit if World is not found
    }
    return true;
}

void ADiggerManager::RecreateIslandFromSaveData(const FIslandSaveData& SaveData)
{
    // Create a temporary FIslandMeshData
    FIslandMeshData MeshData;
    MeshData.MeshOrigin = SaveData.MeshOrigin;
    MeshData.Vertices = SaveData.Vertices;
    MeshData.Triangles = SaveData.Triangles;
    MeshData.Normals = SaveData.Normals;
    MeshData.bValid = (MeshData.Vertices.Num() > 0 && MeshData.Triangles.Num() > 0);
    
    // Spawn the island actor
    SpawnIslandActorWithMeshData(SaveData.MeshOrigin, MeshData, SaveData.bEnablePhysics);
}


void ADiggerManager::BeginPlay()
{
    Super::BeginPlay();

    UE_LOG(LogTemp, Warning, TEXT("=== PIE STARTED - CHUNK STATUS ==="));
    
    // 1. Validate World
    if (!EnsureWorldReference())
    {
        UE_LOG(LogTemp, Error, TEXT("World is null in DiggerManager BeginPlay!"));
        return;
    }

    // 2. Initialize the New Height Cache System
    // This prepares the subsystem but does NOT scan the world yet (Lazy!)
    if (HeightCacheSystem)
    {
        HeightCacheSystem->Initialize(GetSafeWorld());
    }

    // 3. Update Configurations
    UpdateVoxelSize();
    
    // 4. Initialize Brushes
    if (IsValid(ActiveBrush))
    {
        ActiveBrush->InitializeBrush(ActiveBrush->GetBrushType(), ActiveBrush->GetBrushSize(), ActiveBrush->GetBrushLocation(), this);
    }
    InitializeBrushShapes();

    // 5. Clean Slate (Critical for PIE)
    // Remove any ghost holes or meshes left over from Editor Mode
    DestroyAllHoleBPs();
    ClearAllVoxelData(); 

    // 6. Setup Materials
    if (TerrainMaterial && ProceduralMesh)
    {
        if (ProceduralMesh->GetMaterial(0) != TerrainMaterial)
        {
            ProceduralMesh->SetMaterial(0, TerrainMaterial);
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("Material M_ProcGrid missing or ProceduralMesh invalid."));
    }

    // 7. DEFERRED LOADING (The Performance Fix)
    // We use a Lambda here to avoid needing a separate "StartHeightCaching" function declaration.
    // This waits 0.1s to let the engine finish rendering the first frame, then starts loading.
    FTimerHandle InitHandle;
    GetWorld()->GetTimerManager().SetTimer(InitHandle, [this]()
    {
        if (!IsValid(this)) return;

        UE_LOG(LogTemp, Log, TEXT("DiggerManager: Starting Deferred Load..."));

        // A. Load Chunks
        // This puts chunks into the load queue. 
        // As they process, they will call GetLandscapeHeightAt(), which triggers Lazy Caching.
        LoadAllChunks("Default"); 

        // B. Restore Islands
        for (const FIslandSaveData& SavedIsland : SavedIslands)
        {
            RecreateIslandFromSaveData(SavedIsland);
        }
        
        // Ensure the chunk refresh loop is running in Game
        GetWorld()->GetTimerManager().SetTimer(ChunkUpdateTimerHandle, this, &ADiggerManager::ProcessDirtyChunksLoop, 0.1f, true);
        
    }, 0.5f, false);
}



void ADiggerManager::InitEditorLightweight()
{
    // Keep this near zero cost. No voxel allocation, no terrain scan.
    // e.g., set default preview radius/type so the EdMode can draw gizmos.
    // EditorBrushRadius = FMath::Max(EditorBrushRadius, 50.f);

#if WITH_EDITOR
    // Ensure the loop is running in Editor
    if (GEditor && !GetWorld()->IsGameWorld())
    {
        if (!GetWorld()->GetTimerManager().IsTimerActive(ChunkUpdateTimerHandle))
        {
            GetWorld()->GetTimerManager().SetTimer(ChunkUpdateTimerHandle, this, &ADiggerManager::ProcessDirtyChunksLoop, 0.1f, true);
        }
    }
#endif
}

void ADiggerManager::EditorDeferredInit()
{
#if WITH_EDITOR
    if (IsRuntimeLike()) return;        // safety
    if (bEditorInitDone)  return;

    bEditorInitDone = true;
#endif
}

void ADiggerManager::EnsureHoleShapeLibrary()
{
    if (IsValid(HoleShapeLibrary)) return;

    // Prefer your project asset if you have one (leave null to force transient)
    if (UHoleShapeLibrary* Loaded = LoadDefaultHoleLibrary())
    {
        HoleShapeLibrary = Loaded;
        SeedHoleShapesFromFolder(HoleShapeLibrary);
        return;
    }

    // Otherwise create a transient library so editor mode works immediately
    UHoleShapeLibrary* Transient = NewObject<UHoleShapeLibrary>(
        GetTransientPackage(),
        UHoleShapeLibrary::StaticClass(),
        FName(TEXT("TransientHoleShapeLibrary"))
    );
    HoleShapeLibrary = Transient;

    SeedHoleShapesFromFolder(HoleShapeLibrary);
}


void ADiggerManager::DestroyAllHoleBPs()
{
    if (!HoleBP) return; // Ensure the reference is valid

    World = GetSafeWorld();
    if (!World) return;

    for (TActorIterator<AActor> It(World, HoleBP); It; ++It)
    {
        AActor* HoleActor = *It;
        if (HoleActor && IsValid(HoleActor))
        {
            HoleActor->Destroy();
        }
    }
}



void ADiggerManager::ClearHolesFromChunkMap()
{
    UE_LOG(LogTemp, Warning, TEXT("Clearing Spawned Holes From Chunk Grid:"));

    if (ChunkMap.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("No chunks in ChunkMap!"));
        return;
    }

    // Assuming you have a map or array of chunks
    for (auto& ChunkPair : ChunkMap)
    {
        UVoxelChunk* Chunk = ChunkPair.Value;
        if (Chunk)
        {
            Chunk->ClearSpawnedHoles();
        }
    }
}



UStaticMesh* ADiggerManager::CreateStaticMeshFromRawData(
    UObject* Outer,
    const FString& AssetName,
    const TArray<FVector3f>& Vertices,
    const TArray<int32>& Triangles,
    const TArray<FVector3f>& Normals,
    const TArray<FVector2d>& UVs,
    const TArray<FColor>& Colors,
    const TArray<FProcMeshTangent>& Tangents)
{
    // Validate input
    if (Vertices.Num() == 0 || Triangles.Num() == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("Invalid mesh data"));
        return nullptr;
    }

    // Create package
    FString PackageName = FString::Printf(TEXT("/Game/Islands/%s"), *AssetName);
    UPackage* MeshPackage = CreatePackage(*PackageName);
    
    // Create static mesh
    UStaticMesh* StaticMesh = NewObject<UStaticMesh>(MeshPackage, *AssetName, RF_Public | RF_Standalone);
    if (!StaticMesh) return nullptr;

    // Setup mesh description
    FMeshDescription MeshDescription;
    FStaticMeshAttributes Attributes(MeshDescription);
    Attributes.Register();

    // Create polygon group
    FPolygonGroupID PolyGroupID = MeshDescription.CreatePolygonGroup();

    // Add vertices
    TMap<int32, FVertexID> IndexToVertexID;
    for (int32 i = 0; i < Vertices.Num(); ++i)
    {
        FVertexID VertexID = MeshDescription.CreateVertex();
        Attributes.GetVertexPositions()[VertexID] = Vertices[i];
        IndexToVertexID.Add(i, VertexID);
    }

    // Add triangles with attributes
    for (int32 i = 0; i < Triangles.Num(); i += 3)
    {
        TArray<FVertexInstanceID> VertexInstanceIDs;
        
        for (int32 j = 0; j < 3; ++j)
        {
            int32 VertexIndex = Triangles[i + j];
            if (!IndexToVertexID.Contains(VertexIndex)) continue;

            FVertexInstanceID InstanceID = MeshDescription.CreateVertexInstance(IndexToVertexID[VertexIndex]);

            // Set all attributes
            if (Normals.IsValidIndex(VertexIndex))
                Attributes.GetVertexInstanceNormals()[InstanceID] = Normals[VertexIndex];

            if (UVs.IsValidIndex(VertexIndex))
                Attributes.GetVertexInstanceUVs().Set(InstanceID, 0, FVector2f(UVs[VertexIndex]));

            if (Colors.IsValidIndex(VertexIndex))
                Attributes.GetVertexInstanceColors()[InstanceID] = FVector4f(Colors[VertexIndex]);

            if (Tangents.IsValidIndex(VertexIndex))
            {
                Attributes.GetVertexInstanceTangents()[InstanceID] = (FVector3f)Tangents[VertexIndex].TangentX;
                Attributes.GetVertexInstanceBinormalSigns()[InstanceID] = Tangents[VertexIndex].bFlipTangentY ? -1.0f : 1.0f;
            }

            VertexInstanceIDs.Add(InstanceID);
        }

        if (VertexInstanceIDs.Num() == 3)
            MeshDescription.CreatePolygon(PolyGroupID, VertexInstanceIDs);
    }

#if WITH_EDITORONLY_DATA
    // Configure build settings
    FStaticMeshSourceModel& SourceModel = StaticMesh->GetSourceModel(0);
    SourceModel.BuildSettings.bRecomputeNormals = false;
    SourceModel.BuildSettings.bRecomputeTangents = false;

    // Add default material if needed
    if (StaticMesh->GetStaticMaterials().Num() == 0)
    {
        StaticMesh->GetStaticMaterials().Add(FStaticMaterial());
    }

    // Build mesh
    StaticMesh->BuildFromMeshDescriptions({ &MeshDescription });
#endif

    // Finalize mesh
    StaticMesh->CommitMeshDescription(0);
    StaticMesh->Build(false);
    StaticMesh->PostEditChange();

    // Notify asset system
    FAssetRegistryModule::AssetCreated(StaticMesh);
    MeshPackage->MarkPackageDirty();

    return StaticMesh;
}


void ADiggerManager::ClearProceduralMeshes()
{
        if (ProceduralMesh)
        {
            ProceduralMesh->ClearAllMeshSections();
            //ProceduralMesh->MarkRenderStateDirty(); // Optional: forces update to visual
            UE_LOG(LogTemp, Warning, TEXT("Procedural mesh cleared."));
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("No ProceduralMesh found to clear."));
        }
}


void ADiggerManager::BakeToStaticMesh(bool bEnableCollision, bool bEnableNanite, float DetailReduction)
{
    UE_LOG(LogTemp, Warning, TEXT("BakeToStaticMesh called with:\n  Collision: %s\n  Nanite: %s\n  DetailReduction: %.2f"),
        bEnableCollision ? TEXT("True") : TEXT("False"),
        bEnableNanite ? TEXT("True") : TEXT("False"),
        DetailReduction);

/*#if WITH_EDITOR
    FString AssetName = FString::Printf(TEXT("SM_Baked_%s_%s_%.2f"),
        bEnableCollision ? TEXT("Col") : TEXT("NoCol"),
        bEnableNanite ? TEXT("Nanite") : TEXT("NoNanite"),
        DetailReduction);

    UStaticMesh* NewMesh = CreateStaticMeshFromProceduralMesh(
        GetTransientPackage(), // Or another suitable Outer
        AssetName,
        MyProceduralMeshComponent // <-- Replace with your actual component
    );

    if (NewMesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("Static mesh created: %s"), *AssetName);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create static mesh!"));
    }
#endif*/
}




void ADiggerManager::UpdateVoxelSize()
{
    if (Subdivisions != 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("Initializing FVoxelConversion in PIE BeginPlay"));
        FVoxelConversion::InitFromConfig(ChunkSize, Subdivisions, TerrainGridSize, FVector::ZeroVector);
        VoxelSize = TerrainGridSize / Subdivisions;
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("Subdivisions value is zero. VoxelSize will not be updated to avoid division by zero."));
        VoxelSize = 1.0f; // Default fallback value
    }
}

void ADiggerManager::InitializeChunks()
{
    for (const auto& ChunkPair : ChunkMap)
    {
        const FIntVector& Coordinates = ChunkPair.Key;
        UVoxelChunk* NewChunk = NewObject<UVoxelChunk>(this);
        if (!NewChunk)
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to create a new chunk for coordinates: %s"), *Coordinates.ToString());
            continue;
        }

        UProceduralMeshComponent* NewMeshComponent = NewObject<UProceduralMeshComponent>(this);
        if (NewMeshComponent)
        {
            NewMeshComponent->RegisterComponent();
            NewMeshComponent->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
            NewChunk->InitializeMeshComponent(NewMeshComponent);
            ChunkMap.Add(Coordinates, NewChunk);
            ProceduralMeshComponents.Add(NewMeshComponent);
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to create a new ProceduralMeshComponent for coordinates: %s"), *Coordinates.ToString());
        }
    }
}

void ADiggerManager::InitializeSingleChunk(UVoxelChunk* Chunk)
{
    if (!Chunk)
    {
        UE_LOG(LogTemp, Error, TEXT("InitializeSingleChunk called with a null chunk."));
        return;
    }

    FIntVector Coordinates = Chunk->GetChunkCoordinates();
    UProceduralMeshComponent* NewMeshComponent = NewObject<UProceduralMeshComponent>(this);
    if (NewMeshComponent)
    {
        NewMeshComponent->RegisterComponent();
        NewMeshComponent->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
        Chunk->InitializeMeshComponent(NewMeshComponent);
        ChunkMap.Add(Coordinates, Chunk);
        ProceduralMeshComponents.Add(NewMeshComponent);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create a ProceduralMeshComponent for chunk at coordinates: %s"), *Coordinates.ToString());
    }
}


void ADiggerManager::EnforceZeroLocation()
{
    if (GetActorLocation() != FVector::ZeroVector)
    {
        SetActorLocation(FVector::ZeroVector);
        
        // Optional: Reset Rotation and Scale too if needed
        SetActorRotation(FRotator::ZeroRotator);
        SetActorScale3D(FVector::OneVector);
    }
}


// float ADiggerManager::GetLandscapeHeightAt(const FVector& Location)
// {
//     if (HeightCacheSystem)
//     {
//         return HeightCacheSystem->GetHeight(Location);
//     }
//     return Location.Z;
// }


FVector ADiggerManager::GetLandscapeNormalAt(const FVector& WorldPosition)
{
    // Get all landscape actors in the level
    TArray<AActor*> FoundLandscapes;
    UGameplayStatics::GetAllActorsOfClass(GetSafeWorld(), ALandscapeProxy::StaticClass(), FoundLandscapes);

    if (FoundLandscapes.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("No landscape found in level"));
        return FVector::UpVector;
    }

    ALandscapeProxy* LandscapeProxy = Cast<ALandscapeProxy>(FoundLandscapes[0]);
    if (!LandscapeProxy)
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to cast landscape actor"));
        return FVector::UpVector;
    }

    // Use a small offset for sampling (in cm)
    const float Delta = 5.0f;

    FVector PosX = WorldPosition + FVector(Delta, 0, 0);
    FVector NegX = WorldPosition - FVector(Delta, 0, 0);
    FVector PosY = WorldPosition + FVector(0, Delta, 0);
    FVector NegY = WorldPosition - FVector(0, Delta, 0);

    float HeightX1 = GetLandscapeHeightAt(WorldPosition);
    float HeightX0 = GetLandscapeHeightAt(WorldPosition);
    float HeightY1 = GetLandscapeHeightAt(WorldPosition);
    float HeightY0 = GetLandscapeHeightAt(WorldPosition);

    // Calculate gradient
    FVector Gradient;
    Gradient.X = (HeightX1 - HeightX0) / (2 * Delta);
    Gradient.Y = (HeightY1 - HeightY0) / (2 * Delta);
    Gradient.Z = 1.0f;

    // The normal is the inverse of the gradient in X and Y, with Z up
    FVector Normal = FVector(-Gradient.X, -Gradient.Y, 1.0f).GetSafeNormal();
    return Normal;
}


UWorld* ADiggerManager::GetSafeWorld() const
{
#if WITH_EDITOR
    if (GIsEditor && GEditor)
    {
        return GEditor->GetEditorWorldContext().World();
    }
#else
    return GetWorld();
#endif
    return GetWorld();
}

#if WITH_EDITOR
void ADiggerManager::EnsureDefaultHoleBP()
{
    if (!HoleBP)
    {
        FSoftObjectPath AssetPath(GDefaultHoleBPPath);
        UObject* LoadedObj = AssetPath.TryLoad();

        if (UClass* LoadedClass = Cast<UClass>(LoadedObj))
        {
            HoleBP = LoadedClass;
            UE_LOG(LogTemp, Log, TEXT("Loaded Default HoleBP from %s"), GDefaultHoleBPPath);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Failed to load HoleBP from %s"), GDefaultHoleBPPath);
        }
    }
}
#endif

void ADiggerManager::HandleHoleSpawn(const FBrushStroke& Stroke)
{
    // ---------------------------------------------------------
    // 1. VALIDATION
    // ---------------------------------------------------------
    if (!HoleBP)
    {
        EnsureDefaultHoleBP();
        if (!HoleBP)
        {
            UE_LOG(LogTemp, Error, TEXT("HandleHoleSpawn: HoleBP is NULL!"));
            return;
        }
    }

    if (!ActiveBrush)
    {
#if WITH_EDITOR
        UE_LOG(LogTemp, Error, TEXT("HandleHoleSpawn: ActiveBrush is NULL in Editor!"));
        return;
#else
        ActiveBrush = NewObject<UVoxelBrushShape>(this);
        if (ActiveBrush)
        {
            ActiveBrush->InitializeBrush(Stroke.BrushType, Stroke.BrushRadius, Stroke.BrushPosition, this);
            InitializeBrushShapes();
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("HandleHoleSpawn: Failed to create ActiveBrush at Runtime!"));
            return;
        }
#endif
    }

    // ---------------------------------------------------------
    // 2. INITIALIZATION
    // ---------------------------------------------------------
    FVector SpawnLocation = Stroke.BrushPosition;
    FRotator SpawnRotation = Stroke.BrushRotation;
    bool bFoundLandscape = false;

    const float TerrainZ = GetLandscapeHeightAt(SpawnLocation);
    const bool bValidTerrainZ = TerrainZ > UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT;

    // ---------------------------------------------------------
    // 3. STRATEGY A: CAMERA HIT
    // ---------------------------------------------------------
    FHitResult Hit;
    if (ActiveBrush->GetCameraHitLocation(Hit))
    {
        AActor* HitActor = Hit.GetActor();
        if (ActiveBrush->IsLandscape(HitActor))
        {
            SpawnLocation = Hit.Location;

            const FVector N = Hit.ImpactNormal.GetSafeNormal();
            SpawnRotation = N.IsNearlyZero()
                ? FRotator::ZeroRotator
                : FRotationMatrix::MakeFromZ(N).Rotator();

            bFoundLandscape = true;
        }
        else if (bValidTerrainZ && FMath::Abs(Hit.Location.Z - TerrainZ) <= Stroke.BrushRadius * 0.6f)
        {
            // Hit something else, but we're near terrain — allow it
            SpawnLocation.Z = TerrainZ;
            SpawnRotation = FRotator::ZeroRotator;
            bFoundLandscape = true;
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("HandleHoleSpawn: Camera hit %s, not landscape. Skipping."),
                HitActor ? *HitActor->GetName() : TEXT("None"));
            return;
        }
    }

    // ---------------------------------------------------------
    // 4. STRATEGY B: FALLBACK TRACE
    // ---------------------------------------------------------
    if (!bFoundLandscape)
    {
        const float TraceDist = Stroke.BrushRadius * 2.f;
        const FVector Start = SpawnLocation + FVector(0, 0, TraceDist);
        const FVector End   = SpawnLocation - FVector(0, 0, TraceDist);

        FCollisionQueryParams Params(SCENE_QUERY_STAT(HoleFallback), false, this);
        TArray<FHitResult> Hits;

        if (GetWorld()->LineTraceMultiByChannel(Hits, Start, End, ECC_Visibility, Params))
        {
            for (const FHitResult& H : Hits)
            {
                if (H.GetActor() && H.GetActor()->IsA(ALandscapeProxy::StaticClass()))
                {
                    SpawnLocation = H.Location;

                    const FVector N = H.ImpactNormal.GetSafeNormal();
                    SpawnRotation = N.IsNearlyZero()
                        ? FRotator::ZeroRotator
                        : FRotationMatrix::MakeFromZ(N).Rotator();

                    bFoundLandscape = true;
                    break;
                }
            }
        }
    }

    // ---------------------------------------------------------
    // 5. STRATEGY C: PROXIMITY CHECK
    // ---------------------------------------------------------
    if (!bFoundLandscape && bValidTerrainZ)
    {
        const float BrushZ = SpawnLocation.Z;
        const float MaxDistance = Stroke.BrushRadius * 0.6f;

        if (FMath::Abs(BrushZ - TerrainZ) <= MaxDistance)
        {
            SpawnLocation.Z = TerrainZ;
            SpawnRotation = FRotator::ZeroRotator;
            bFoundLandscape = true;
        }
    }

    // ---------------------------------------------------------
    // 6. FINAL VALIDATION
    // ---------------------------------------------------------
    if (!bFoundLandscape)
    {
        UE_LOG(LogTemp, Warning, TEXT("HandleHoleSpawn: No landscape found near brush."));
        return;
    }

    // ---------------------------------------------------------
    // 7. SCALE
    // ---------------------------------------------------------
    const float ScaleDivisor = 47.f;
    const FVector SpawnScale(Stroke.BrushRadius / ScaleDivisor);

    // ---------------------------------------------------------
    // 8. CHUNK RESOLUTION
    // ---------------------------------------------------------
    UVoxelChunk* Chunk = GetOrCreateChunkAtWorld(SpawnLocation);
    if (!Chunk)
    {
        // Retry with slight offset
        const FVector OffsetLoc = SpawnLocation + FVector(5.f, 5.f, 0.f);
        Chunk = GetOrCreateChunkAtWorld(OffsetLoc);
    }

    if (!Chunk)
    {
        UE_LOG(LogTemp, Error, TEXT("HandleHoleSpawn: No chunk found near %s"), *SpawnLocation.ToString());
        return;
    }

    // ---------------------------------------------------------
    // 9. SPAWN
    // ---------------------------------------------------------
    FHoleShape FinalShape = Stroke.HoleShape;
    FinalShape.ShapeType =
        (Stroke.BrushType == EVoxelBrushType::Cube) ? EHoleShapeType::Cube : EHoleShapeType::Sphere;

    FSpawnedHoleData Data(SpawnLocation, SpawnRotation, SpawnScale, FinalShape);
    Chunk->SpawnHoleFromData(Data);

    UE_LOG(LogTemp, Log, TEXT("HandleHoleSpawn: Spawned at %s (Chunk %s)"),
        *SpawnLocation.ToString(), *Chunk->GetChunkCoordinates().ToString());

    // Optional debug
    // DrawDebugSphere(GetWorld(), SpawnLocation, Stroke.BrushRadius, 16, FColor::Green, false, 5.f);
}




void ADiggerManager::DuplicateLandscape(ALandscapeProxy* Landscape)
{
    if (!Landscape) return;

    ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
    if (!LandscapeInfo) return;

    // Get landscape extent
    int32 LandscapeMinX, LandscapeMinY, LandscapeMaxX, LandscapeMaxY;
    LandscapeInfo->GetLandscapeExtent(LandscapeMinX, LandscapeMinY, LandscapeMaxX, LandscapeMaxY);

    // Create data access interface
    FLandscapeEditDataInterface LandscapeData(LandscapeInfo);

    int32 ChunkVoxelCount = ChunkSize / VoxelSize;

    // Get landscape transform for proper world position calculation
    FTransform LandscapeTransform = Landscape->GetActorTransform();
    float LandscapeScale = LandscapeTransform.GetScale3D().Z;

    // Iterate over the landscape extent in chunks
    for (int32 Y = LandscapeMinY; Y <= LandscapeMaxY; Y += ChunkVoxelCount)
    {
        for (int32 X = LandscapeMinX; X <= LandscapeMaxX; X += ChunkVoxelCount)
        {
            FIntVector ChunkPosition(X, Y, 0);
            UVoxelChunk* Chunk = GetOrCreateChunkAtChunk(ChunkPosition);

            if (Chunk)
            {
                // Calculate chunk boundaries in landscape space
                int32 ChunkEndX = FMath::Min(X + ChunkVoxelCount, LandscapeMaxX);
                int32 ChunkEndY = FMath::Min(Y + ChunkVoxelCount, LandscapeMaxY);

                // Get height data for the entire chunk area
                TMap<FIntPoint, uint16> HeightData;
                int32 StartX = X;
                int32 StartY = Y;
                int32 EndX = ChunkEndX;
                int32 EndY = ChunkEndY;
                LandscapeData.GetHeightData(StartX, StartY, EndX, EndY, HeightData);

                for (int32 VoxelY = 0; VoxelY < ChunkVoxelCount; ++VoxelY)
                {
                    for (int32 VoxelX = 0; VoxelX < ChunkVoxelCount; ++VoxelX)
                    {
                        int32 LandscapeX = X + VoxelX;
                        int32 LandscapeY = Y + VoxelY;

                        // Skip if we're outside landscape bounds
                        if (LandscapeX > LandscapeMaxX || LandscapeY > LandscapeMaxY)
                            continue;

                        // Get height value from the height data map
                        FIntPoint Key(LandscapeX, LandscapeY);
                        float Height = 0.0f;

                        if (const uint16* HeightValue = HeightData.Find(Key))
                        {
                            // Convert from uint16 to world space height
                            // LandscapeDataAccess.h: Landscape uses USHRT_MAX as "32768" = 0.0f world space
                            const float ScaleFactor = LandscapeScale / 128.0f; // Landscape units to world units
                            Height = ((float)*HeightValue - 32768.0f) * ScaleFactor;
                            Height += LandscapeTransform.GetLocation().Z; // Add landscape base height
                        }

                        for (int32 VoxelZ = 0; VoxelZ < ChunkVoxelCount; ++VoxelZ)
                        {
                            // Convert voxel coordinates to world space
                            FVector WorldPosition = FVoxelConversion::LocalVoxelToWorld(FIntVector(VoxelX, VoxelY, VoxelZ));

                            // Calculate SDF value (distance from surface)
                            // Positive values are above the surface, negative values are below
                            float SDFValue = WorldPosition.Z - Height;

                            // Optional: Normalize SDF value by voxel size
                            SDFValue /= VoxelSize;

                            // Set the voxel value
                            bool bDig = false;
                            Chunk->SetVoxel(VoxelX, VoxelY, VoxelZ, SDFValue, bDig);
                        }
                    }
                }

                // Mark the chunk for update
                Chunk->MarkDirty();
            }
        }
    }
}


void ADiggerManager::DebugLogVoxelChunkGrid() const
{
    UE_LOG(LogTemp, Warning, TEXT("Logging Voxel Chunk Grid:"));

    if (ChunkMap.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("No chunks in ChunkMap!"));
        return;
    }

    // Assuming you have a map or array of chunks
    for (auto& ChunkPair : ChunkMap)
    {
        UVoxelChunk* Chunk = ChunkPair.Value;
        if (Chunk)
        {
            Chunk->DebugDrawChunk();
        }
    }
}

void ADiggerManager::DebugVoxels()
{
    if (!ChunkMap.IsEmpty())
    {
        DebugDrawChunkSectionIDs();
        DebugLogVoxelChunkGrid();
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("No chunks available for rendering."));
    }
}




int32 ADiggerManager::GetHitSectionIndex(const FHitResult& HitResult)
{
    if (!HitResult.GetComponent()) return -1;

    UProceduralMeshComponent* HitProceduralMesh = Cast<UProceduralMeshComponent>(HitResult.GetComponent());
    if (!HitProceduralMesh) return -1;

    const FVector HitLocation = HitResult.ImpactPoint;

    for (int32 SectionIndex = 0; SectionIndex < HitProceduralMesh->GetNumSections(); ++SectionIndex)
    {
        const FProcMeshSection* Section = HitProceduralMesh->GetProcMeshSection(SectionIndex);
        if (!Section || Section->ProcVertexBuffer.Num() == 0) continue;

        FBox SectionBox(Section->ProcVertexBuffer[0].Position, Section->ProcVertexBuffer[0].Position);
        for (const FProcMeshVertex& Vertex : Section->ProcVertexBuffer)
        {
            SectionBox += Vertex.Position;
        }

        if (SectionBox.IsInside(HitLocation))
        {
            return SectionIndex;
        }
    }

    return -1; // Section not found
}

UVoxelChunk* ADiggerManager::GetChunkBySectionIndex(int32 SectionIndex)
{
    for (auto& Entry : ChunkMap)
    {
        if (Entry.Value->GetSectionIndex() == SectionIndex)
        {
            return Entry.Value;
        }
    }

    return nullptr; // Chunk not found
}

void ADiggerManager::UpdateChunkFromSectionIndex(const FHitResult& HitResult)
{
    int32 SectionIndex = GetHitSectionIndex(HitResult);
    if (SectionIndex == -1) return;

    UVoxelChunk* HitChunk = GetChunkBySectionIndex(SectionIndex);
    if (HitChunk)
    {
        HitChunk->MarkDirty();
    }
}

void ADiggerManager::DebugDrawChunkSectionIDs()
{
    for (auto& Entry : ChunkMap)
    {
        UVoxelChunk* Chunk = Entry.Value;
        if (Chunk)
        {
            Chunk->GetSparseVoxelGrid()->RenderVoxels();
            const FVector ChunkPosition = FVoxelConversion::ChunkToWorld(Chunk->GetChunkCoordinates());
            const FString SectionIDText = FString::Printf(TEXT("ID: %d"), Chunk->GetSectionIndex());
            DrawDebugString(GetSafeWorld(), ChunkPosition, SectionIDText, nullptr, FColor::Green, 5.0f, true);
        }
    }
}

void ADiggerManager::ApplyBrush()
{
    if (!ActiveBrush)
    {
        UE_LOG(LogTemp, Error, TEXT("ActiveBrush is null. Cannot apply brush."));
        return;
    }

    FVector BrushPosition = ActiveBrush->GetBrushLocation();
    float BrushRadius = ActiveBrush->GetBrushSize();
    FBrushStroke BrushStroke;
    BrushStroke.BrushPosition = BrushPosition;
    BrushStroke.BrushRadius = BrushRadius;
    BrushStroke.bDig = ActiveBrush->GetDig();
    BrushStroke.BrushType = ActiveBrush->GetBrushType();
    

    ApplyBrushToAllChunks(BrushStroke);


    if (BrushStroke.bDig)
    {
        MarkNearbyChunksDirty(BrushPosition, BrushRadius);
    }
}

UVoxelChunk* ADiggerManager::FindOrCreateNearestChunk(const FVector& Position)
{
    FIntVector ChunkCoords = FVoxelConversion::WorldToChunk(Position);
    UVoxelChunk** ExistingChunk = ChunkMap.Find(ChunkCoords);
    if (ExistingChunk && *ExistingChunk)
    {
        UE_LOG(LogTemp, Warning, TEXT("Chunk found at %s"), *ChunkCoords.ToString());
        return *ExistingChunk;
    }

    
    // Find nearest chunk if no exact match found
    UVoxelChunk* NearestChunk = nullptr;
    float MinDistance = FLT_MAX;

    for (auto& Entry : ChunkMap)
    {
        UVoxelChunk* Chunk = Entry.Value;
        if (!Chunk) continue;

        FVector WorldPos = FVoxelConversion::ChunkToWorld(Chunk->GetChunkCoordinates());
        float Distance = FVector::Dist(Position, WorldPos);

        if (Distance < MinDistance)
        {
            MinDistance = Distance;
            NearestChunk = Chunk;
        }
    }


    // If no nearby chunk is found, create a new one
    if (!NearestChunk)
    {
        UE_LOG(LogTemp, Warning, TEXT("[FindOrCreateNearestChunk] Input Position (World): %s"), *Position.ToString());
        ChunkCoords = FVoxelConversion::WorldToChunk(Position);
        UE_LOG(LogTemp, Warning, TEXT("[FindOrCreateNearestChunk] ChunkCoords: %s"), *ChunkCoords.ToString());

        
        UVoxelChunk* NewChunk = GetOrCreateChunkAtCoordinates(ChunkCoords.X, ChunkCoords.Y, ChunkCoords.Z);
        if (NewChunk)
        {
            NewChunk->InitializeChunk(ChunkCoords, this);
            ChunkMap.Add(ChunkCoords, NewChunk);
            return NewChunk;
        }
    }

    return NearestChunk;
}

UVoxelChunk* ADiggerManager::FindNearestChunk(const FVector& Position)
{
    FIntVector ChunkCoords = FVoxelConversion::WorldToChunk(Position);
    UVoxelChunk** ExistingChunk = ChunkMap.Find(ChunkCoords);
    if (ExistingChunk && *ExistingChunk)
    {
        return *ExistingChunk;
    }

    // Find nearest chunk if no exact match found
    UVoxelChunk* NearestChunk = nullptr;
    float MinDistance = FLT_MAX;
    for (auto& Entry : ChunkMap)
    {
        FVector WorldPos = FVoxelConversion::ChunkToWorld(Entry.Value->GetChunkCoordinates());
        float Distance = FVector::Dist(Position, WorldPos);
        if (Distance < MinDistance)
        {
            MinDistance = Distance;
            NearestChunk = Entry.Value;
        }
    }


    return NearestChunk;
}


void ADiggerManager::MarkNearbyChunksDirty(const FVector& CenterPosition, float Radius)
{
    int32 Reach = FMath::CeilToInt(Radius / (ChunkSize * TerrainGridSize));

    UE_LOG(LogTemp, Warning, TEXT("MarkNearbyChunksDirty: CenterPosition=%s, Radius=%f"), *CenterPosition.ToString(), Radius);
    UE_LOG(LogTemp, Warning, TEXT("FVoxelConversion: ChunkSize=%d, TerrainGridSize=%f, Origin=%s"), FVoxelConversion::ChunkSize, FVoxelConversion::TerrainGridSize, *FVoxelConversion::Origin.ToString());
    FIntVector CenterChunkCoords = FVoxelConversion::WorldToChunk(CenterPosition);
    UE_LOG(LogTemp, Warning, TEXT("WorldToChunk: CenterPosition=%s -> CenterChunkCoords=%s"), *CenterPosition.ToString(), *CenterChunkCoords.ToString());


    for (int32 X = -Reach; X <= Reach; ++X)
    {
        for (int32 Y = -Reach; Y <= Reach; ++Y)
        {
            for (int32 Z = -Reach; Z <= Reach; ++Z)
            {
                FIntVector NearbyChunkCoords = CenterChunkCoords + FIntVector(X, Y, Z);
                UVoxelChunk** NearbyChunk = ChunkMap.Find(NearbyChunkCoords);
                if (NearbyChunk && *NearbyChunk)
                {
                    (*NearbyChunk)->MarkDirty();
                    UE_LOG(LogTemp, Log, TEXT("Marked nearby chunk dirty at position: %s"), *NearbyChunkCoords.ToString());
                }
            }
        }
    }
}

TArray<FIslandData> ADiggerManager::DetectUnifiedIslands()
{
    // Step 1: Build NON-DEDUPLICATED voxel map with ALL physical instances
    TMap<FIntVector, FVoxelData> UnifiedVoxelData; // This will be deduplicated for island detection
    TArray<FVoxelInstance> AllPhysicalVoxelInstances; // This preserves ALL instances including overflow
    
    for (const auto& Pair : ChunkMap)
    {
        UVoxelChunk* Chunk = Pair.Value;
        if (!Chunk || !Chunk->IsValidLowLevel()) continue;

        USparseVoxelGrid* Grid = Chunk->GetSparseVoxelGrid();
        if (!Grid || !Grid->IsValidLowLevel()) continue;

        FIntVector ChunkCoords = Chunk->GetChunkCoordinates();

        for (const auto& VoxelPair : Grid->GetAllVoxels())
        {
            const FIntVector& LocalIndex = VoxelPair.Key;
            const FVoxelData& Data = VoxelPair.Value;

            FIntVector GlobalIndex = FVoxelConversion::ChunkAndLocalToGlobalVoxel_MinCornerAligned(ChunkCoords, LocalIndex);
            
            // Store for deduplicated island detection
            UnifiedVoxelData.Add(GlobalIndex, Data);
            
            // Store ALL physical instances (including overflow duplicates)
            FVoxelInstance Instance(GlobalIndex, ChunkCoords, LocalIndex);
            AllPhysicalVoxelInstances.Add(Instance);
            
            // Special logging for negative overflow voxels
            if (LocalIndex.X == -1 || LocalIndex.Y == -1 || LocalIndex.Z == -1)
            {
                if (DiggerDebug::Islands())
                UE_LOG(LogTemp, Warning, 
                    TEXT("[DiggerPro] NEGATIVE OVERFLOW DETECTED: Global %s -> Chunk %s -> Local %s"),
                    *GlobalIndex.ToString(), *ChunkCoords.ToString(), *LocalIndex.ToString());
            }
        }
    }

    if (DiggerDebug::Islands())
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Total physical voxel instances collected: %d"), AllPhysicalVoxelInstances.Num());
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Deduplicated voxels for island detection: %d"), UnifiedVoxelData.Num());
    }
    
    // Step 2: Create temporary sparse voxel grid for island detection (using deduplicated data)
    USparseVoxelGrid* TempGrid = NewObject<USparseVoxelGrid>();
    TempGrid->SetVoxelData(UnifiedVoxelData);

    // Step 3: Detect islands globally (this gives us deduplicated island boundaries)
    TArray<FIslandData> DeduplicatedIslands = TempGrid->DetectIslands(0.0f);

    // Step 4: For each deduplicated island, collect ALL physical instances that belong to it
    TArray<FIslandData> FinalIslands;
    
    for (const FIslandData& DeduplicatedIsland : DeduplicatedIslands)
    {
        FIslandData EnhancedIsland;
        
        // Copy the deduplicated island info for UI reporting
        EnhancedIsland.Location = DeduplicatedIsland.Location;
        EnhancedIsland.VoxelCount = DeduplicatedIsland.VoxelCount; // This is the deduplicated count for UI
        EnhancedIsland.Voxels = DeduplicatedIsland.Voxels; // Deduplicated global voxels for UI
        EnhancedIsland.ReferenceVoxel = DeduplicatedIsland.ReferenceVoxel;
        
        // Now find ALL physical instances that belong to this island
        TSet<FIntVector> IslandGlobalVoxels(DeduplicatedIsland.Voxels);
        
        for (const FVoxelInstance& Instance : AllPhysicalVoxelInstances)
        {
            // If this physical instance belongs to the current island, include it
            if (IslandGlobalVoxels.Contains(Instance.GlobalVoxel))
            {
                EnhancedIsland.VoxelInstances.Add(Instance);
                
                // Special logging for negative overflow voxels being added to islands
                if (Instance.LocalVoxel.X == -1 || Instance.LocalVoxel.Y == -1 || Instance.LocalVoxel.Z == -1)
                {
                    if (DiggerDebug::Islands())
                    UE_LOG(LogTemp, Warning, 
                        TEXT("[DiggerPro] NEGATIVE OVERFLOW ADDED TO ISLAND: Global %s -> Chunk %s -> Local %s"),
                        *Instance.GlobalVoxel.ToString(), *Instance.ChunkCoords.ToString(), *Instance.LocalVoxel.ToString());
                }
            }
            else if (Instance.LocalVoxel.X == -1 || Instance.LocalVoxel.Y == -1 || Instance.LocalVoxel.Z == -1)
            {
                // Log negative overflow voxels that DON'T belong to any island
                if (DiggerDebug::Islands())
                UE_LOG(LogTemp, Error, 
                    TEXT("[DiggerPro] ORPHANED NEGATIVE OVERFLOW: Global %s -> Chunk %s -> Local %s (not in any island)"),
                    *Instance.GlobalVoxel.ToString(), *Instance.ChunkCoords.ToString(), *Instance.LocalVoxel.ToString());
            }
        }
        
        FinalIslands.Add(EnhancedIsland);

        if (DiggerDebug::Islands())
        UE_LOG(LogTemp, Warning, 
            TEXT("[DiggerPro] Island complete: %d unique voxels (UI) -> %d total physical instances (removal)"),
            EnhancedIsland.VoxelCount, EnhancedIsland.VoxelInstances.Num());
    }

    // Step 5: Broadcast ONLY the deduplicated islands to UI (clean reporting)
    if (OnIslandsDetectionStarted.IsBound())  // or check .ExecuteIfBound with guards in the toolkit
    OnIslandsDetectionStarted.Broadcast();

    for (const FIslandData& Island : FinalIslands)
    {
        // Create a clean version for broadcasting (no VoxelInstances array)
        FIslandData BroadcastIsland;
        BroadcastIsland.Location = Island.Location;
        BroadcastIsland.VoxelCount = Island.VoxelCount; // Deduplicated count
        BroadcastIsland.Voxels = Island.Voxels; // Deduplicated voxels
        BroadcastIsland.ReferenceVoxel = Island.ReferenceVoxel;

        if (!OnIslandDetected.IsBound())
        {
            if (DiggerDebug::Islands())
                UE_LOG(LogTemp, Error, TEXT("OnIslandDetected not Bound."));
            continue;  // or check .ExecuteIfBound with guards in the toolkit
        }
        OnIslandDetected.Broadcast(BroadcastIsland);
        if (DiggerDebug::Islands())
        UE_LOG(LogTemp, Warning, TEXT("Unified Island Broadcast at %s with %d voxels"),
            *BroadcastIsland.Location.ToString(), BroadcastIsland.VoxelCount);
    }

    // Step 6: Return enhanced islands with ALL physical instances for removal
    return FinalIslands;
}


// Main function: always works in chunk coordinates
UVoxelChunk* ADiggerManager::GetOrCreateChunkAtChunk(const FIntVector& ChunkCoords)
{
    //Run Init on the static FVoxelCoversion struct so all of our conversions work properly even if the settings have changed!
    FVoxelConversion::InitFromConfig(ChunkSize,Subdivisions,TerrainGridSize, GetActorLocation());

    if (DiggerDebug::Chunks())
    UE_LOG(LogTemp, Warning, TEXT("➡️ Attempting chunk at coords: %s"), *ChunkCoords.ToString());
    
    if (UVoxelChunk** ExistingChunk = ChunkMap.Find(ChunkCoords))
    {
        if (DiggerDebug::Chunks())
        UE_LOG(LogTemp, Log, TEXT("Found existing chunk at position: %s"), *ChunkCoords.ToString());
        return *ExistingChunk;
    }

    UVoxelChunk* NewChunk = NewObject<UVoxelChunk>(this);
    if (NewChunk)
    {
        NewChunk->InitializeChunk(ChunkCoords, this);
        NewChunk->InitializeDiggerManager(this);
        ChunkMap.Add(ChunkCoords, NewChunk);

        if (DiggerDebug::Chunks())
        UE_LOG(LogTemp, Log, TEXT("Created a new chunk at position: %s"), *ChunkCoords.ToString());
        return NewChunk;
    }
    else
    {
        if (DiggerDebug::Chunks() || DiggerDebug::Error())
        UE_LOG(LogTemp, Error, TEXT("Failed to create a new chunk at position: %s"), *ChunkCoords.ToString());
        return nullptr;
    }
}


bool ADiggerManager::IsGameWorld() const
{
    UWorld* W = GetWorld();
    return W && (W->WorldType == EWorldType::Game || W->WorldType == EWorldType::PIE);
}






UVoxelChunk* ADiggerManager::GetOrCreateChunkAtCoordinates(const float& ProposedChunkX, const float& ProposedChunkY, const float& ProposedChunkZ)
{
    return GetOrCreateChunkAtWorld(FVector(ProposedChunkX, ProposedChunkY, ProposedChunkZ));
}

// Thin wrapper: converts world position to chunk coordinates
UVoxelChunk* ADiggerManager::GetOrCreateChunkAtWorld(const FVector& WorldPosition)
{
    FIntVector ChunkCoords = FVoxelConversion::WorldToChunk(WorldPosition);
    return GetOrCreateChunkAtChunk(ChunkCoords);
}


void ADiggerManager::RemoveUnifiedIslandVoxels(const FIslandData& Island)
{
    int32 TotalRemoved = 0;
    TSet<UVoxelChunk*> ChunksToUpdate;

    if (DiggerDebug::Islands() || DiggerDebug::Voxels() || DiggerDebug::Chunks())
    UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Starting unified island removal with %d pre-computed voxel instances"), Island.VoxelInstances.Num());
    
    // Group voxel instances by their storage chunks for efficient batch removal
    TMap<UVoxelChunk*, TArray<FIntVector>> VoxelsByChunk;
    
    // Simply iterate through the pre-computed voxel instances
    for (const FVoxelInstance& Instance : Island.VoxelInstances)
    {
        UVoxelChunk* Chunk = ChunkMap.FindRef(Instance.ChunkCoords);
        if (!Chunk || !Chunk->IsValidLowLevel()) 
        {
            if (DiggerDebug::Chunks() || DiggerDebug::Error())
            UE_LOG(LogTemp, Error, TEXT("[DiggerPro] Chunk %s not found or invalid"), *Instance.ChunkCoords.ToString());
            continue;
        }
        
        USparseVoxelGrid* Grid = Chunk->GetSparseVoxelGrid();
        if (!Grid || !Grid->IsValidLowLevel()) 
        {
            if (DiggerDebug::Islands() || DiggerDebug::Voxels() || DiggerDebug::Chunks())
            UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Grid for chunk %s not found or invalid"), *Instance.ChunkCoords.ToString());
            continue;
        }
        
        // Verify the voxel still exists before adding to removal list
        if (Grid->HasVoxelAt(Instance.LocalVoxel))
        {
            VoxelsByChunk.FindOrAdd(Chunk).Add(Instance.LocalVoxel);
            ChunksToUpdate.Add(Chunk);
            
            // Special logging for negative overflow voxels being queued for removal
            if (Instance.LocalVoxel.X == -1 || Instance.LocalVoxel.Y == -1 || Instance.LocalVoxel.Z == -1)
            {
                if (DiggerDebug::Islands() || DiggerDebug::Voxels() || DiggerDebug::Chunks())
                UE_LOG(LogTemp, Warning, 
                    TEXT("[DiggerPro] NEGATIVE OVERFLOW QUEUED FOR REMOVAL: Global %s -> Chunk %s -> Local %s"),
                    *Instance.GlobalVoxel.ToString(), *Instance.ChunkCoords.ToString(), *Instance.LocalVoxel.ToString());
            }
        }
        else
        {
            if (DiggerDebug::Islands() || DiggerDebug::Voxels() || DiggerDebug::Chunks())
            UE_LOG(LogTemp, Warning, 
                TEXT("[DiggerPro] Voxel not found at Local %s in chunk %s (Global %s)"),
                *Instance.LocalVoxel.ToString(), *Instance.ChunkCoords.ToString(), *Instance.GlobalVoxel.ToString());
        }
    }
    
    // Perform batch removal on each affected chunk
    for (auto& ChunkVoxelPair : VoxelsByChunk)
    {
        UVoxelChunk* Chunk = ChunkVoxelPair.Key;
        TArray<FIntVector>& LocalVoxels = ChunkVoxelPair.Value;
        TArray<FIntVector>& NonOverflowVoxels = ChunkVoxelPair.Value;
        
        if (Chunk && Chunk->IsValidLowLevel())
        {
            USparseVoxelGrid* Grid = Chunk->GetSparseVoxelGrid();
            if (Grid && Grid->IsValidLowLevel())
            {
                if (DiggerDebug::Islands() || DiggerDebug::Voxels() || DiggerDebug::Chunks())
                UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Removing %d voxel instances from chunk at %s"), 
                    LocalVoxels.Num(), *Chunk->GetChunkCoordinates().ToString());
                
                // Count negative overflow voxels being removed
                int32 NegativeOverflowCount = 0;
                for (const FIntVector& LocalVoxel : LocalVoxels)
                {
                    if (LocalVoxel.X == -1 || LocalVoxel.Y == -1 || LocalVoxel.Z == -1)
                    {
                        NegativeOverflowCount++;
                        if (DiggerDebug::Islands() || DiggerDebug::Voxels() || DiggerDebug::Chunks())
                        UE_LOG(LogTemp, Warning, 
                            TEXT("[DiggerPro] REMOVING NEGATIVE OVERFLOW: Local %s from chunk %s"),
                            *LocalVoxel.ToString(), *Chunk->GetChunkCoordinates().ToString());
                    }
                }
                
                if (NegativeOverflowCount > 0)
                {
                    if (DiggerDebug::Islands() || DiggerDebug::Voxels() || DiggerDebug::Chunks())
                    UE_LOG(LogTemp, Warning, 
                        TEXT("[DiggerPro] About to remove %d negative overflow voxels from chunk %s"),
                        NegativeOverflowCount, *Chunk->GetChunkCoordinates().ToString());
                }
                    
                Grid->RemoveSpecifiedVoxels(LocalVoxels);
                TotalRemoved += LocalVoxels.Num();
                if (DiggerDebug::Islands() || DiggerDebug::Voxels() || DiggerDebug::Chunks())
                UE_LOG(LogTemp, Warning, 
                    TEXT("[DiggerPro] Successfully removed %d voxels from chunk %s"),
                    LocalVoxels.Num(), *Chunk->GetChunkCoordinates().ToString());
            }
        }
    }
    
    // Update all affected chunks to regenerate meshes
    for (UVoxelChunk* Chunk : ChunksToUpdate)
    {
        if (Chunk && Chunk->IsValidLowLevel())
        {
            Chunk->ForceUpdate();
        }
    }
    if (DiggerDebug::Islands() || DiggerDebug::Chunks() || DiggerDebug::Voxels())
    UE_LOG(LogTemp, Warning,
        TEXT("[DiggerPro] Unified island removal complete: %d voxel instances removed across %d chunks"),
        TotalRemoved, ChunksToUpdate.Num());
}

// New helper method to perform flood fill across chunk boundaries
TSet<FIntVector> ADiggerManager::PerformCrossChunkFloodFill(const FIntVector& StartGlobalVoxel)
{
    TSet<FIntVector> VisitedVoxels;
    TQueue<FIntVector> VoxelsToCheck;
    
    // Start the flood fill
    VoxelsToCheck.Enqueue(StartGlobalVoxel);
    
    while (!VoxelsToCheck.IsEmpty())
    {
        FIntVector CurrentGlobal;
        VoxelsToCheck.Dequeue(CurrentGlobal);
        
        if (VisitedVoxels.Contains(CurrentGlobal))
            continue;
            
        // Check if this global voxel is solid in any storage chunk
        bool bFoundSolidVoxel = false;
        TArray<FIntVector> StorageChunkCoords = GetAllPhysicalStorageChunks(CurrentGlobal);
        
        for (const FIntVector& ChunkCoord : StorageChunkCoords)
        {
            UVoxelChunk* Chunk = ChunkMap.FindRef(ChunkCoord);
            if (!Chunk || !Chunk->IsValidLowLevel()) continue;
            
            USparseVoxelGrid* Grid = Chunk->GetSparseVoxelGrid();
            if (!Grid || !Grid->IsValidLowLevel()) continue;
            
            // Convert global to local coordinates for this candidate chunk
            FIntVector LocalVoxel, OutChunkCoord;
            FVoxelConversion::GlobalVoxelToChunkAndLocal(
                CurrentGlobal,
                OutChunkCoord,
                LocalVoxel
            );
            
            if (Grid->HasVoxelAt(LocalVoxel) && Grid->IsVoxelSolid(LocalVoxel))
            {
                bFoundSolidVoxel = true;
                break;
            }
        }
        
        if (!bFoundSolidVoxel)
            continue;
            
        // Mark as visited and add neighbors
        VisitedVoxels.Add(CurrentGlobal);
        
        // Check all 6 neighboring voxels
        static const FIntVector Neighbors[6] = {
            FIntVector(1, 0, 0), FIntVector(-1, 0, 0),
            FIntVector(0, 1, 0), FIntVector(0, -1, 0),
            FIntVector(0, 0, 1), FIntVector(0, 0, -1)
        };
        
        for (const FIntVector& Offset : Neighbors)
        {
            FIntVector NeighborGlobal = CurrentGlobal + Offset;
            if (!VisitedVoxels.Contains(NeighborGlobal))
            {
                VoxelsToCheck.Enqueue(NeighborGlobal);
            }
        }
    }
    
    return VisitedVoxels;
}

// Helper method to find ALL chunks that actually contain this voxel
// This includes the canonical owner AND adjacent chunks with overflow slabs
TArray<FIntVector> ADiggerManager::GetAllPhysicalStorageChunks(const FIntVector& GlobalVoxel)
{
    TArray<FIntVector> StorageChunks;
    
    // Start with the canonical owning chunk
    FIntVector CanonicalChunk, LocalVoxelOut;
    FVoxelConversion::GlobalVoxelToChunkAndLocal(GlobalVoxel, CanonicalChunk, LocalVoxelOut);
    
    // Check all 27 possible chunks (canonical + 26 neighbors) to see which ones actually store this voxel
    for (int32 dx = -1; dx <= 1; dx++)
    {
        for (int32 dy = -1; dy <= 1; dy++)
        {
            for (int32 dz = -1; dz <= 1; dz++)
            {
                FIntVector CandidateChunk = CanonicalChunk + FIntVector(dx, dy, dz);
                
                // Check if this chunk exists and actually contains the voxel
                UVoxelChunk* Chunk = ChunkMap.FindRef(CandidateChunk);
                if (!Chunk || !Chunk->IsValidLowLevel()) continue;
                
                USparseVoxelGrid* Grid = Chunk->GetSparseVoxelGrid();
                if (!Grid || !Grid->IsValidLowLevel()) continue;
                
                // Convert global to local coordinates for this candidate chunk
                FIntVector LocalVoxel;
                FVoxelConversion::GlobalVoxelToChunkAndLocal(
                    GlobalVoxel,
                    CandidateChunk,
                    LocalVoxel
                );
                
                // If this chunk actually has the voxel, add it to storage list
                if (Grid->HasVoxelAt(LocalVoxel))
                {
                    StorageChunks.AddUnique(CandidateChunk);

                    if (DiggerDebug::Chunks() || DiggerDebug::Voxels())
                    UE_LOG(LogTemp, Warning, 
                        TEXT("[DiggerPro] Global voxel %s found in chunk %s at local %s"),
                        *GlobalVoxel.ToString(), *CandidateChunk.ToString(), *LocalVoxel.ToString());
                }
                else
                {
                    if (DiggerDebug::Chunks() || DiggerDebug::Voxels())
                    // Debug: Log when we don't find expected voxels
                    UE_LOG(LogTemp, VeryVerbose, 
                        TEXT("[DiggerPro] Global voxel %s NOT found in chunk %s (would be local %s)"),
                        *GlobalVoxel.ToString(), *CandidateChunk.ToString(), *LocalVoxel.ToString());
                }
            }
        }
    }
    
    // If we didn't find the voxel anywhere, this is a problem!
    if (StorageChunks.Num() == 0)
    {
        if (DiggerDebug::Chunks() || DiggerDebug::Voxels())
        UE_LOG(LogTemp, Error, 
            TEXT("[DiggerPro] CRITICAL: Global voxel %s was not found in any chunk! Canonical chunk: %s"),
            *GlobalVoxel.ToString(), *CanonicalChunk.ToString());
    }
    
    return StorageChunks;
}

TArray<FIntVector> ADiggerManager::GetPossibleOwningChunks(const FIntVector& GlobalIndex)
{
    TArray<FIntVector> PossibleChunks;

    // Scan all 8 neighboring chunks where this voxel might overflow into
    for (int32 dx = 0; dx <= 1; dx++)
        for (int32 dy = 0; dy <= 1; dy++)
            for (int32 dz = 0; dz <= 1; dz++)
            {
                FIntVector Offset(dx, dy, dz);
                FIntVector AdjustedGlobal = GlobalIndex - Offset;

                FIntVector CandidateChunkCoords, LocalVoxel;
                FVoxelConversion::GlobalVoxelToChunkAndLocal(AdjustedGlobal, CandidateChunkCoords, LocalVoxel);

                if (ChunkMap.Contains(CandidateChunkCoords))
                {
                    PossibleChunks.Add(CandidateChunkCoords);
                }
            }

    return PossibleChunks;
}


#if WITH_EDITOR
void ADiggerManager::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    // Sync updated settings to FVoxelConversion using the actor's position as origin
    FVoxelConversion::InitFromConfig(ChunkSize,Subdivisions, TerrainGridSize, GetActorLocation());

    if (!IsGameWorld())
    {
        // avoid full rebuilds in editor
        bEditorInitDone = false;
        return;
    }

    // Enforce the 0,0,0 position for the DiggerManager
    EnforceZeroLocation();
}


void ADiggerManager::PostEditMove(bool bFinished)
{
    Super::PostEditMove(bFinished);

    // Update FVoxelConversion origin if the actor is moved in the editor
    FVoxelConversion::InitFromConfig(ChunkSize,Subdivisions, TerrainGridSize, GetActorLocation());

    if (bFinished && !IsRuntimeLike())
    {
        EditorDeferredInit();
    }
    
    // Enforce the 0,0,0 position for the DiggerManager
    EnforceZeroLocation();
}

void ADiggerManager::PostEditUndo()
{
    Super::PostEditUndo();

    // Update FVoxelConversion in case undo/redo changed position or settings
    FVoxelConversion::InitFromConfig(ChunkSize, Subdivisions, TerrainGridSize, GetActorLocation());
    
}

void ADiggerManager::EditorRebuildAllChunks()
{
    // Optionally clear and rebuild all chunks
    InitializeChunks();
}


#endif // WITH_EDITOR

// Static constants
const FString ADiggerManager::VOXEL_DATA_DIRECTORY = TEXT("VoxelData");
const FString ADiggerManager::CHUNK_FILE_EXTENSION = TEXT(".VoxelData");

void ADiggerManager::EnsureVoxelDataDirectoryExists() const
{
    FString VoxelDataPath = FPaths::ProjectContentDir() / VOXEL_DATA_DIRECTORY;
    
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlatformFile.DirectoryExists(*VoxelDataPath))
    {
        if (PlatformFile.CreateDirectoryTree(*VoxelDataPath))
        {
            if (DiggerDebug::IO())
            UE_LOG(LogTemp, Log, TEXT("Created VoxelData directory at: %s"), *VoxelDataPath);
        }
        else
        {
            if (DiggerDebug::IO())
            UE_LOG(LogTemp, Error, TEXT("Failed to create VoxelData directory at: %s"), *VoxelDataPath);
        }
    }
}

void ADiggerManager::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // Process background loading
    if (HeightCacheSystem)
    {
        HeightCacheSystem->TickProcessQueue();
    }

    // 2. THE MISSING LINK: Process Dirty Chunks
    // This iterates the map and triggers generation for any chunk marked dirty.
    ProcessDirtyChunks();
    
    // Update cache refresh timer
    SavedChunkCacheRefreshTimer += DeltaTime;
    if (SavedChunkCacheRefreshTimer >= CACHE_REFRESH_INTERVAL)
    {
        SavedChunkCacheRefreshTimer = 0.0f;
        if (!bSavedChunkCacheValid)
        {
            RefreshSavedChunkCache();
        }
    }
    
}

void ADiggerManager::ProcessDirtyChunks()
{
    //1. Process chunks
    // UE_LOG(LogTemp, Log, TEXT("ProcessDirtyChunks running...")); // Optional Debug
    ProcessDirtyChunksLoop();

    // 2. Process Height Cache Queue (Background Work)
    if (HeightCacheSystem)
    {
        HeightCacheSystem->TickProcessQueue();
    }
}

void ADiggerManager::QueueBrushPoint(const FVector& HitPointWS, float Radius, float Strength, float Hardness, uint8 Shape, uint8 Op)
{
    const float Spacing = (BrushResampleSpacing > 0.f) ? BrushResampleSpacing : (Radius * 0.5f);
    if (!bHasLastSample || FVector::DistSquared(LastSamplePos, HitPointWS) >= Spacing*Spacing)
    {
        PendingStroke.Add({ HitPointWS, Radius, Strength, Hardness, Shape, Op });
        LastSamplePos = HitPointWS;
        bHasLastSample = true;
    }
}



void ADiggerManager::ApplyPendingBrushSamples()
{
    if (PendingStroke.Num() == 0) return;

    // TODO: replace this loop with your chunk/SDF application logic
    for (const FBrushSample& S : PendingStroke)
    {
        // Currently you'd call your existing ApplyBrush(S.WorldPos, S.Radius, ...) here
    }

    PendingStroke.Reset();
    bHasLastSample = false;
}


void ADiggerManager::CreateHoleAt(FVector WorldPosition, FRotator Rotation, FVector Scale, TSubclassOf<AActor> HoleBPClass)
{
    FIntVector ChunkCoords = FVoxelConversion::WorldToChunk(WorldPosition);
    if (UVoxelChunk* Chunk = GetOrCreateChunkAtChunk(ChunkCoords))
    {
        //ToDo: set shape type based on brush used!
        Chunk->SpawnHole(HoleBPClass, WorldPosition, Rotation, Scale, EHoleShapeType::Sphere);
    }
}

bool ADiggerManager::RemoveHoleNear(FVector WorldPosition, float MaxDistance)
{
    FIntVector ChunkCoords = FVoxelConversion::WorldToChunk(WorldPosition);
    if (UVoxelChunk* Chunk = GetOrCreateChunkAtChunk(ChunkCoords))
    {
        return Chunk->RemoveNearestHole(WorldPosition, MaxDistance);
    }
    return false;
}

bool ADiggerManager::ShouldTickIfViewportsOnly() const
{
    // 2. Return true to tick in the editor viewport
    return true;
}

TArray<FIntVector> ADiggerManager::GetAllSavedChunkCoordinates(bool bForceRefresh) const
{
    // Use cached data if valid and not forcing refresh
    if (!bForceRefresh && bSavedChunkCacheValid)
    {
        return CachedSavedChunkCoordinates;
    }
    
    // If we need to refresh, do it on the mutable cache
    const_cast<ADiggerManager*>(this)->RefreshSavedChunkCache();
    return CachedSavedChunkCoordinates;
}

void ADiggerManager::RefreshSavedChunkCache()
{
    CachedSavedChunkCoordinates.Empty();
    
    FString VoxelDataPath = FPaths::ProjectContentDir() / VOXEL_DATA_DIRECTORY;
    
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    
    if (!PlatformFile.DirectoryExists(*VoxelDataPath))
    {
        bSavedChunkCacheValid = true;
        return;
    }
    
    // Find all .VoxelData files
    TArray<FString> FoundFiles;
    PlatformFile.FindFiles(FoundFiles, *VoxelDataPath, *CHUNK_FILE_EXTENSION);
    
    for (const FString& FileName : FoundFiles)
    {
        // Parse filename to extract coordinates
        FString BaseName = FPaths::GetBaseFilename(FileName);
        
        if (BaseName.StartsWith(TEXT("Chunk")))
        {
            FString CoordString = BaseName.RightChop(5); // Remove "Chunk" prefix
            
            TArray<FString> CoordParts;
            CoordString.ParseIntoArray(CoordParts, TEXT("_"), true);
            
            if (CoordParts.Num() == 3)
            {
                int32 X = FCString::Atoi(*CoordParts[0]);
                int32 Y = FCString::Atoi(*CoordParts[1]);
                int32 Z = FCString::Atoi(*CoordParts[2]);
                
                CachedSavedChunkCoordinates.Add(FIntVector(X, Y, Z));
            }
        }
    }
    
    bSavedChunkCacheValid = true;
    
    // Only log when actually refreshing to avoid spam
    static int32 LastCachedCount = -1;
    if (CachedSavedChunkCoordinates.Num() != LastCachedCount)
    {
        if (DiggerDebug::Chunks())
        {
            UE_LOG(LogTemp, Warning, TEXT("Refreshed saved chunk cache: found %d files"), 
                CachedSavedChunkCoordinates.Num());
        }
        LastCachedCount = CachedSavedChunkCoordinates.Num();
    }
}



bool ADiggerManager::DeleteChunkFile(const FIntVector& ChunkCoords)
{
    FString FilePath = GetChunkFilePath(ChunkCoords);
    
    if (!FPaths::FileExists(FilePath))
    {
        if (DiggerDebug::IO())
        {
            UE_LOG(LogTemp, Warning, TEXT("Cannot delete chunk file - file does not exist: %s"), *FilePath);
        }
        return false;
    }
    
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    bool bDeleteSuccess = PlatformFile.DeleteFile(*FilePath);
    
    if (bDeleteSuccess)
    {
        if (DiggerDebug::IO())
        {
            UE_LOG(LogTemp, Log, TEXT("Successfully deleted chunk file for chunk %s"), *ChunkCoords.ToString());
        }
        
        // Invalidate cache after deletion
        InvalidateSavedChunkCache(FilePath);
    }
    else
    {
        if (DiggerDebug::IO())
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to delete chunk file for chunk %s"), *ChunkCoords.ToString());
        }
    }
    
    
    return bDeleteSuccess;
}