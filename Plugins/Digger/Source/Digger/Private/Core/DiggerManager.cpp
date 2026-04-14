// 1. The Matching Header MUST be first. 
// This ensures DiggerManager.h compiles on its own.
#include "DiggerManager.h"
#include "DiggerIslandRuntimeSubsystem.h"

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
#include "Components/BoxComponent.h"
#include "GameFramework/PlayerStart.h"
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
#include "Components/RuntimeVirtualTextureComponent.h"
#include "Materials/MaterialExpressionMakeMaterialAttributes.h"

// 6. Landscape
#include "LandscapeProxy.h"
#include "LandscapeInfo.h"

// 7. Kismet / Utilities
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "UObject/SoftObjectPath.h"

// 8. Digger Runtime Internal Headers
#include "DiggerDebug.h"
#include "Utils/FastDebugRenderer.h"
#include "SparseVoxelGrid.h"
#include "DiggerHistory.h"
#include "VoxelChunk.h"
#include "VoxelConversion.h"
#include "MarchingCubes.h"
#include "FCustomSDFBrush.h"
#include "IslandActor.h"
#include "DynamicLightActor.h"
#include "HoleShapeLibrary.h"
#include "FSpawnedHoleData.h"
#include "DynamicHole.h"
#include "DynamicHolesHelpers.h"
#include "Materials/DiggerMaterialTypes.h"
#include "Materials/DiggerTextureSet.h"

// 9. Niagara
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"

// 10. Brush Shapes
#include "FileHelpers.h"
#include "VoxelBrushShape.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Materials/MaterialAttributeDefinitionMap.h"
#include "Materials/MaterialExpressionMakeMaterialAttributes.h"
#include "Materials/MaterialExpressionRuntimeVirtualTextureSampleParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Shapes/SphereBrushShape.h"
#include "Shapes/CubeBrushShape.h"
#include "Shapes/CylinderBrushShape.h"
#include "Shapes/ConeBrushShape.h"
#include "Shapes/CapsuleBrushShape.h"
#include "Shapes/TorusBrushShape.h"
#include "Shapes/PyramidBrushShape.h"
#include "Shapes/IcosphereBrushShape.h"
#include "Shapes/SmoothBrushShape.h"
#include "Shapes/NoiseBrushShape.h"
#include "VT/RuntimeVirtualTextureVolume.h"
#include "Widgets/Notifications/SNotificationList.h"

// 10. EDITOR-ONLY HEADERS
// CRITICAL: These must be wrapped, or your game will fail to package.
// Since Digger is a Runtime module, it cannot link to Editor modules in a Shipping build.
#if WITH_EDITOR
    #include "Editor.h"
    #include "UObject/ObjectSaveContext.h"  // FObjectPreSaveContext
    #include "LandscapeEdit.h"
    #include "AssetToolsModule.h"
    #include "IAssetTools.h"
    #include "Factories/MaterialInstanceConstantFactoryNew.h"
    #include "AssetRegistry/AssetRegistryModule.h"
    #include "Components/RuntimeVirtualTextureComponent.h"
    #include "Landscape.h"
    #include "LandscapeComponent.h"
    #include "Factories/MaterialInstanceConstantFactoryNew.h"
    #include "Materials/MaterialInstanceConstant.h"
    #include "Materials/MaterialExpressionRuntimeVirtualTextureSample.h"
    #include "Materials/MaterialExpressionSetMaterialAttributes.h"
    #include "Materials/MaterialExpressionGetMaterialAttributes.h"
    #include "Materials/MaterialExpressionMultiply.h"
    #include "Materials/MaterialExpressionOneMinus.h"
    #include "AssetToolsModule.h"
    #include "AssetRegistry/AssetRegistryModule.h"
    #include "Framework/Notifications/NotificationManager.h"
    #include "Widgets/Notifications/SNotificationList.h"
    #include "FileHelpers.h"          // FEditorFileUtils
    #include "Components/RuntimeVirtualTextureComponent.h"
    #include "VT/RuntimeVirtualTexture.h"
    #include "LandscapeComponent.h"
    #include "Materials/MaterialExpressionConstant.h"
    #include "Materials/MaterialExpressionClamp.h"
    #include "Materials/MaterialFunctionInstance.h"
#endif

// Forward Declarations (Only needed if this is a Header, but valid in CPP)
class ADynamicLightActor;
class FDiggerEdModeToolkit;
class FDiggerEdMode;
class MeshDescriptors;
class StaticMeshAttributes;




// --- New helpers using UDiggerSettings ---
static UHoleShapeLibrary* LoadDefaultHoleLibraryFromSettings()
{
    const UDiggerSettings* Settings = UDiggerSettings::Get();
    if (!Settings)
    {
        UE_LOG(LogTemp, Warning,
               TEXT("DiggerManager: UDiggerSettings not found when loading default HoleShapeLibrary"));
        return nullptr;
    }
    UHoleShapeLibrary* Library = Settings->DefaultHoleLibrary.LoadSynchronous();
    if (!Library)
    {
        UE_LOG(LogTemp, Warning,
               TEXT("DiggerManager: DefaultHoleLibrary is not set or failed to load in Digger Settings"));
    }
    return Library;
}

static TSubclassOf<AActor> LoadDefaultDynamicHoleClassFromSettings()
{
    const UDiggerSettings* Settings = UDiggerSettings::Get();
    if (!Settings)
    {
        UE_LOG(LogTemp, Warning,
               TEXT("DiggerManager: UDiggerSettings not found when loading default Hole Actor Class"));
        return nullptr;
    }
    TSubclassOf<AActor> HoleClass = Settings->DefaultHoleActorClass.LoadSynchronous();
    if (!HoleClass)
    {
        UE_LOG(LogTemp, Warning,
               TEXT("DiggerManager: DefaultHoleActorClass is not set or failed to load in Digger Settings"));
    }
    return HoleClass;
}

// --- Public/Member “Ensure” functions ---
void ADiggerManager::EnsureHoleShapeLibrary()
{
    if (HoleShapeLibrary) { return; }
    HoleShapeLibrary = LoadDefaultHoleLibraryFromSettings();
}

void ADiggerManager::EnsureDefaultHoleBP()
{
    if (DynamicHoleClass) { return; }
    DynamicHoleClass = LoadDefaultDynamicHoleClassFromSettings();
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

// ─────────────────────────────────────────────────────────────────────────────
// LANDSCAPE SETUP — Implementation moved to DiggerLandscapeSetup.cpp
// ─────────────────────────────────────────────────────────────────────────────
// Functions now in DiggerLandscapeSetup.cpp:
//   RunSetupPreflight, BackupMaterial, ResolveDiggerRVT,
//   ResolveDiggerRVTForPosition, EnsureDiggerRVTAsset,
//   EnsureRVTVolumeForBounds, EnsureLandscapeMaterialHasOpacityMask,
//   InjectOpacityMaskNodes, InjectOpacityMaskNodes_MaterialAttributes,
//   InjectDiggerRVTParameter, AutoSetupLandscapeForDynamicHoles
// ─────────────────────────────────────────────────────────────────────────────


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


float ADiggerManager::GetWorldSDF(const FVector& WorldPos) const
{
    // 1. Which chunk owns this world position?
    FIntVector OwningChunkCoords = FVoxelConversion::WorldToChunk(WorldPos);

    // 2. Is that chunk loaded?
    const UVoxelChunk* const* FoundChunk = ChunkMap.Find(OwningChunkCoords);
    if (!FoundChunk || !(*FoundChunk))
    {
        // Chunk not loaded — fall back to landscape baseline so the ghost
        // value is still physically meaningful rather than 0 or garbage.
        float TerrainHeight = const_cast<ADiggerManager*>(this)->GetLandscapeHeightAt(
            FVector(WorldPos.X, WorldPos.Y, 0.f));
        return (WorldPos.Z - TerrainHeight) / FVoxelConversion::LocalVoxelSize;
    }

    const UVoxelChunk* Chunk = *FoundChunk;
    USparseVoxelGrid* Grid = Chunk->GetSparseVoxelGrid();
    if (!Grid)
    {
        float TerrainHeight = const_cast<ADiggerManager*>(this)->GetLandscapeHeightAt(
            FVector(WorldPos.X, WorldPos.Y, 0.f));
        return (WorldPos.Z - TerrainHeight) / FVoxelConversion::LocalVoxelSize;
    }

    // 3. Convert world position to local voxel coords within that chunk.
    //    WorldToLocalVoxel gives us the integer voxel index within the owning chunk.
    FIntVector LocalCoord = FVoxelConversion::WorldToLocalVoxel(WorldPos);

    // 4. If the neighbor has a modified voxel here, use it.
    if (Grid->HasVoxelAt(LocalCoord.X, LocalCoord.Y, LocalCoord.Z))
    {
        return Grid->GetVoxel(LocalCoord.X, LocalCoord.Y, LocalCoord.Z);
    }

    // 5. Unmodified neighbor voxel — derive from landscape exactly as
    //    ApplyBrushStroke does for unset interior voxels.
    float TerrainHeight = const_cast<ADiggerManager*>(this)->GetLandscapeHeightAt(
        FVector(WorldPos.X, WorldPos.Y, 0.f));
    return (WorldPos.Z - TerrainHeight) / FVoxelConversion::LocalVoxelSize;
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



bool ADiggerManager::IsInsideHole(const FVector& WorldPos) const
{
    for (const TPair<FIntVector, UVoxelChunk*>& Pair : ChunkMap)
    {
        const UVoxelChunk* Chunk = Pair.Value;
        if (!Chunk) continue;

        const TArray<TWeakObjectPtr<ADynamicHole>>& Holes = Chunk->GetSpawnedHoles();
        for (const TWeakObjectPtr<ADynamicHole>& HolePtr : Holes)
        {
            ADynamicHole* Hole = HolePtr.Get();
            if (!Hole) continue;

            // Pool actors are hidden — they are not active holes and must never
            // influence IsInsideHole.  Without this check, returned pool actors
            // sitting at their last world position would cause SmartTrace to
            // incorrectly skip landscape hits at stale locations.
            if (Hole->IsHidden()) continue;

            if (Hole->ContainsPoint(WorldPos))
                return true;
        }
    }

    return false;
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
    UVoxelChunk* Chunk = GetOrCreateChunkAtCoords(ChunkCoords);
    if (!Chunk)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to get or create chunk at %s for save file '%s'"), 
            *ChunkCoords.ToString(), *SaveFileName);
        return false;
    }
    
    bool bLoadSuccess = Chunk->LoadChunkData(FilePath);

    if (bLoadSuccess)
    {
        // Use async mesh generation — GenerateMesh() dispatches MC to a
        // background thread and returns immediately, avoiding game thread stalls.
        // ForceUpdate() was synchronous and caused large spikes on direct loads.
        Chunk->GenerateMesh();

        // Queue collision for the idle cook.
        Chunk->RequestImmediateCollisionRebuild();
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
        
        UVoxelChunk* Chunk = GetOrCreateChunkAtCoords(ChunkCoords);
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
        return true;

    AsyncLoadingFileName = SaveFileName;

    // ── Determine sort origin from player / PlayerStart ──────────────────────
    FVector SortOrigin = FVector::ZeroVector;
    if (UWorld* W = GetWorld())
    {
        if (APlayerController* PC = W->GetFirstPlayerController())
        {
            if (APawn* P = PC->GetPawn())
                SortOrigin = P->GetActorLocation();
        }

        if (SortOrigin.IsNearlyZero())
        {
            for (TActorIterator<APlayerStart> It(W); It; ++It)
            {
                SortOrigin = It->GetActorLocation();
                break;
            }
        }
    }

    // ── Priority-radius sort ─────────────────────────────────────────────────
    // Chunks within DiggerLoadRadiusPriority are processed before any chunk
    // outside it.  Within each group, sort by distance ascending.
    const float PriorityRadiusSq = FMath::Square(DiggerLoadRadiusPriority);
    const float HalfChunkWorld   = ChunkSize * TerrainGridSize * 0.5f;

    SavedChunkCoords.Sort([&](const FIntVector& A, const FIntVector& B)
    {
        const FVector WA = FVoxelConversion::ChunkToWorld(A) + FVector(HalfChunkWorld);
        const FVector WB = FVoxelConversion::ChunkToWorld(B) + FVector(HalfChunkWorld);
        const float DA = FVector::DistSquared(WA, SortOrigin);
        const float DB = FVector::DistSquared(WB, SortOrigin);
        const bool  InA = DA <= PriorityRadiusSq;
        const bool  InB = DB <= PriorityRadiusSq;
        if (InA != InB) return InA; // priority-radius chunks first
        return DA < DB;
    });

    // ── Set up 3-phase pipeline ──────────────────────────────────────────────
    // Phase 1: Landscape holes (pre-spawn hole actors for RVT opacity).
    // Phase 2: Voxel rehydration (full data load, holes skipped).
    // Phase 3: Mesh generation (async marching cubes + collision).
    PendingHoleLoad       = SavedChunkCoords;
    PendingChunksToLoad.Empty();
    PendingMeshBuild.Empty();
    PendingCollisionBuild.Empty();
    TotalChunksToLoad     = SavedChunkCoords.Num();
    CurrentLoadPhase      = EDiggerLoadPhase::LandscapeHoles;
    LoadStartTime         = FPlatformTime::Seconds();
    LoadErrorCount        = 0;

    // Block brush input while loading.
    bLoadingBrushDisabled = true;
    OnModifierBlocked.Broadcast(true);

    GetWorldTimerManager().ClearTimer(LoadQueueTimerHandle);
    GetWorldTimerManager().SetTimer(LoadQueueTimerHandle, this,
        &ADiggerManager::ProcessChunkLoadQueue, 0.016f, true);

    UE_LOG(LogTemp, Log, TEXT("[Digger] Streaming %d chunks from '%s' (origin %s, budget %.1fms)"),
        TotalChunksToLoad, *AsyncLoadingFileName, *SortOrigin.ToString(), DiggerLoadBudgetMs);

#if WITH_EDITOR
    if (GIsEditor)
    {
        FDiggerLoadProgress P;
        P.Phase       = EDiggerLoadPhase::LandscapeHoles;
        P.Pct         = 0.f;
        P.TotalChunks = TotalChunksToLoad;
        OnLoadProgressUpdated.Broadcast(P);
    }
#endif

    return true;
}

void ADiggerManager::ProcessChunkLoadQueue()
{
    const double TickStart  = FPlatformTime::Seconds();
    const double BudgetSec  = DiggerLoadBudgetMs / 1000.0;
    int32 ChunksThisTick    = 0;

    auto BudgetExceeded = [&]()
    {
        return (FPlatformTime::Seconds() - TickStart) >= BudgetSec
            || ChunksThisTick >= DiggerLoadChunksPerTick;
    };

    // ══════════════════════════════════════════════════════════════════════════
    // PHASE 1 — LANDSCAPE HOLES
    // Pre-spawn hole actors so RVT opacity masks update immediately.
    // This is cheap (pool acquire + reposition) and should complete in 1-2 ticks.
    // ══════════════════════════════════════════════════════════════════════════
    if (PendingHoleLoad.Num() > 0)
    {
        CurrentLoadPhase = EDiggerLoadPhase::LandscapeHoles;

        while (PendingHoleLoad.Num() > 0 && !BudgetExceeded())
        {
            const FIntVector Coords = PendingHoleLoad[0];
            PendingHoleLoad.RemoveAt(0, 1, false);

            const FString FilePath = GetChunkFilePath(Coords, AsyncLoadingFileName);
            if (!FPaths::FileExists(FilePath)) continue;

            UVoxelChunk* Chunk = GetOrCreateChunkAtCoords(Coords);
            if (!Chunk) continue;

            if (Chunk->LoadChunkHolesOnly(FilePath))
            {
                // Enqueue for Phase 2 (voxel rehydration).
                PendingChunksToLoad.Add(Coords);
            }
            else
            {
                ++LoadErrorCount;
            }
            ++ChunksThisTick;
        }

        // If Phase 1 just completed, transition label for next tick.
        if (PendingHoleLoad.IsEmpty() && PendingChunksToLoad.Num() > 0)
            CurrentLoadPhase = EDiggerLoadPhase::VoxelRehydration;
    }

    // ══════════════════════════════════════════════════════════════════════════
    // PHASE 2+3 — VOXEL REHYDRATION + MESH GENERATION (pipelined)
    // Deserialize chunk voxel data from disk.  Holes already spawned in Phase 1
    // so LoadChunkData skips hole spawning (bHolesPreLoaded flag on the chunk).
    // Immediately after a chunk's data is loaded, dispatch GenerateMesh() on it.
    // GenerateMesh() is async (returns immediately) so it costs near-zero game
    // thread time — we never budget-gate it separately.
    // ══════════════════════════════════════════════════════════════════════════
    if (PendingHoleLoad.IsEmpty() && PendingChunksToLoad.Num() > 0)
    {
        CurrentLoadPhase = EDiggerLoadPhase::VoxelRehydration;

        while (PendingChunksToLoad.Num() > 0 && !BudgetExceeded())
        {
            const FIntVector Coords = PendingChunksToLoad[0];
            PendingChunksToLoad.RemoveAt(0, 1, false);

            const FString FilePath = GetChunkFilePath(Coords, AsyncLoadingFileName);
            if (!FPaths::FileExists(FilePath)) continue;

            UVoxelChunk* Chunk = GetOrCreateChunkAtCoords(Coords);
            if (!Chunk) continue;

            if (Chunk->LoadChunkData(FilePath))
            {
                // Pipeline: dispatch async mesh gen immediately after data load.
                // GenerateMesh() returns instantly — the heavy work runs on the
                // thread pool.  This means mesh starts building while the next
                // chunk's data is still loading.
                Chunk->GenerateMesh();
                PendingCollisionBuild.Add(Coords);
            }
            else
            {
                ++LoadErrorCount;
            }
            ++ChunksThisTick;
        }

        // Once all data is loaded, switch to MeshGeneration label for remaining
        // collision work.
        if (PendingChunksToLoad.IsEmpty())
            CurrentLoadPhase = EDiggerLoadPhase::MeshGeneration;
    }

    // ── Collision cook (proximity-based) ─────────────────────────────────────
    if (PendingCollisionBuild.Num() > 0)
    {
        FVector PlayerPos = FVector::ZeroVector;
        if (UWorld* W = GetWorld())
        {
            if (APlayerController* PC = W->GetFirstPlayerController())
                if (APawn* P = PC->GetPawn())
                    PlayerPos = P->GetActorLocation();
        }

        while (PendingCollisionBuild.Num() > 0 && !BudgetExceeded())
        {
            const FIntVector Coords = PendingCollisionBuild[0];
            PendingCollisionBuild.RemoveAt(0, 1, false);

            if (UVoxelChunk* Chunk = GetChunkAtCoords(Coords))
            {
                const FVector ChunkCenter = FVoxelConversion::ChunkToWorld(Coords)
                    + FVector(ChunkSize * TerrainGridSize * 0.5f);
                const float DistSq = FVector::DistSquared(ChunkCenter, PlayerPos);

                Chunk->RequestImmediateCollisionRebuild();
                if (DistSq <= FMath::Square(ImmediateCollisionRadius))
                    Chunk->RebuildCollisionIfNeeded(/*bIdleFullRebuild=*/true);
            }
            ++ChunksThisTick;
        }
    }

    // ── Broadcast progress ───────────────────────────────────────────────────
#if WITH_EDITOR
    if (GIsEditor && TotalChunksToLoad > 0)
    {
        // Weight: Phase 1 = 10%, Phase 2+3 = 60%, Collision = 30%
        const int32 HoleDone    = TotalChunksToLoad - PendingHoleLoad.Num();
        const int32 DataDone    = TotalChunksToLoad - PendingHoleLoad.Num() - PendingChunksToLoad.Num();
        const int32 CollDone    = DataDone - PendingCollisionBuild.Num();
        const float Pct = FMath::Clamp(
            (HoleDone * 0.1f + DataDone * 0.6f + CollDone * 0.3f) / (float)TotalChunksToLoad,
            0.f, 0.999f);

        FDiggerLoadProgress P;
        P.Phase          = CurrentLoadPhase;
        P.Pct            = Pct;
        P.ChunksLoaded   = FMath::Min(DataDone, TotalChunksToLoad);
        P.TotalChunks    = TotalChunksToLoad;
        P.ErrorCount     = LoadErrorCount;
        P.ElapsedSeconds = (float)(FPlatformTime::Seconds() - LoadStartTime);
        OnLoadProgressUpdated.Broadcast(P);
    }
#endif

    // ── Done? ─────────────────────────────────────────────────────────────────
    if (PendingHoleLoad.IsEmpty() &&
        PendingChunksToLoad.IsEmpty() &&
        PendingCollisionBuild.IsEmpty())
    {
        GetWorldTimerManager().ClearTimer(LoadQueueTimerHandle);

        CurrentLoadPhase = (LoadErrorCount > 0)
            ? EDiggerLoadPhase::Failed
            : EDiggerLoadPhase::Complete;

        const float TotalTime = (float)(FPlatformTime::Seconds() - LoadStartTime);
        UE_LOG(LogTemp, Log, TEXT("[Digger] Chunk streaming complete from '%s' — %d chunks in %.2fs (%d errors)."),
            *AsyncLoadingFileName, TotalChunksToLoad, TotalTime, LoadErrorCount);

        // Re-enable brush input.
        bLoadingBrushDisabled = false;
        OnModifierBlocked.Broadcast(false);

#if WITH_EDITOR
        if (GIsEditor)
        {
            FDiggerLoadProgress P;
            P.Phase          = CurrentLoadPhase;
            P.Pct            = 1.f;
            P.ChunksLoaded   = TotalChunksToLoad;
            P.TotalChunks    = TotalChunksToLoad;
            P.ErrorCount     = LoadErrorCount;
            P.ElapsedSeconds = TotalTime;
            OnLoadProgressUpdated.Broadcast(P);
            TotalChunksToLoad = 0;
        }
#endif

        CurrentLoadPhase = EDiggerLoadPhase::Idle;

        // Deferred bake: wait for async mesh gen to finish, then merge all
        // hole actors into a single PMC to keep draw-call count low.
        GetWorldTimerManager().ClearTimer(HoleBakeTimerHandle);
        GetWorldTimerManager().SetTimer(HoleBakeTimerHandle, this,
            &ADiggerManager::BakeStableHoles, 2.0f, false);
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
        UE_LOG(LogTemp, Verbose, TEXT("ApplyBrushInEditor Called!"));
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
    // Only self-commit when called standalone. During a continuous editor
    // stroke, BeginStroke() is already open — EdMode commits on mouse-up.
    if (!bHasPendingAction)
        CommitPendingAction();

    // Fire dig FX (sound + Niagara) if a profile is assigned.
    {
        EDiggerFXOperation FXOp;
        if (BrushStroke.BrushType == EVoxelBrushType::Smooth)
            FXOp = EDiggerFXOperation::Smooth;
        else if (BrushStroke.bDig)
            FXOp = EDiggerFXOperation::Dig;
        else
            FXOp = EDiggerFXOperation::Fill;
        PlayDigFX(FXOp, BrushStroke.BrushPosition, BrushStroke.BrushRadius);
    }

#if WITH_EDITOR
    if (GEditor)
    {
        GEditor->RedrawAllViewports();
    }
#endif

    // Island detection dispatches to the runtime subsystem which owns the full
    // pipeline: fragment cleanup, grounding checks, float policy, and broadcasting.
#if WITH_EDITOR
    if (bEnableIslandDetection)
    {
        if (UWorld* W = GetWorld())
        {
            if (auto* IslandSys = W->GetSubsystem<UDiggerIslandRuntimeSubsystem>())
            {
                IslandSys->OnBrushStrokeCompleted(this, /*bIsUndoRedo=*/false);
            }
        }
    }
#endif
}


// =============================================================================
// PlayDigFX — fire sound + Niagara for a brush operation
// =============================================================================

void ADiggerManager::PlayDigFX(EDiggerFXOperation Operation, const FVector& Location, float BrushRadius)
{
    if (!FXProfile) return;

    // Throttle: respect MinInterval
    const double Now = FPlatformTime::Seconds();
    if (Now - LastFXTime < FXProfile->MinInterval) return;
    LastFXTime = Now;

    const FDiggerFXEntry& Entry = FXProfile->GetEntry(Operation);
    UWorld* World = GetWorld();
    if (!World) return;

    // Audio
    if (bUse3DSound)
    {
        if (Entry.Sound3D)
        {
            UGameplayStatics::SpawnSoundAtLocation(
                World, Entry.Sound3D, Location,
                FRotator::ZeroRotator,
                Entry.VolumeMultiplier);
        }
    }
    else
    {
        if (Entry.Sound2D)
        {
            UGameplayStatics::PlaySound2D(
                World, Entry.Sound2D,
                Entry.VolumeMultiplier);
        }
    }

    // Niagara
    if (Entry.NiagaraEffect)
    {
        const FVector Scale(Entry.NiagaraScale * (BrushRadius / 100.f));
        UNiagaraFunctionLibrary::SpawnSystemAtLocation(
            World, Entry.NiagaraEffect, Location,
            FRotator::ZeroRotator, Scale,
            /*bAutoDestroy=*/true);
    }
}


void ADiggerManager::RemoveIslandAtPosition(const FVector& IslandCenter, const FIntVector& ReferenceVoxel)
{
    if (DiggerDebug::Islands())
    UE_LOG(LogTemp, Warning, TEXT("[Digger] RemoveIslandAtPosition called at %s"), *IslandCenter.ToString());

    // Guard 1: Reference voxel must be valid
    if (ReferenceVoxel.IsZero())
    {
        if (DiggerDebug::Islands())
        UE_LOG(LogTemp, Warning, TEXT("[Digger] RemoveIslandAtPosition: No reference voxel provided."));
        return;
    }

    FIntVector ChunkCoords, LocalVoxel;
    FVoxelConversion::GlobalVoxelToChunkAndLocal(ReferenceVoxel, ChunkCoords, LocalVoxel);

    // Guard 2: Chunk must exist and be valid
    UVoxelChunk** ChunkPtr = ChunkMap.Find(ChunkCoords);
    if (!ChunkPtr || !IsValid(*ChunkPtr))
    {
        if (DiggerDebug::Islands() || DiggerDebug::Chunks())
        UE_LOG(LogTemp, Error, TEXT("[Digger] Invalid or missing chunk at coords %s"), *ChunkCoords.ToString());
        return;
    }

    UVoxelChunk* Chunk = *ChunkPtr;

    // Guard 3: Grid must exist and be valid
    USparseVoxelGrid* Grid = Chunk->GetSparseVoxelGrid();
    if (!IsValid(Grid))
    {
        if (DiggerDebug::Islands() || DiggerDebug::Voxels())
        UE_LOG(LogTemp, Error, TEXT("[Digger] Invalid SparseVoxelGrid in chunk."));
        return;
    }

    // Guard 4: Attempt island extraction
    USparseVoxelGrid* ExtractedIsland = nullptr;
    TArray<FIntVector> ExtractedVoxels;

    if (!Grid->ExtractIslandByVoxel(LocalVoxel, ExtractedIsland, ExtractedVoxels) || !IsValid(ExtractedIsland) || ExtractedVoxels.IsEmpty())
    {
        if (DiggerDebug::Islands() || DiggerDebug::Voxels())
        UE_LOG(LogTemp, Error, TEXT("[Digger] Failed to extract island at voxel %s"), *LocalVoxel.ToString());
        return;
    }

    // Safe voxel removal (encapsulated)
    Grid->RemoveVoxels(ExtractedVoxels);
    Chunk->MarkDirty();

    if (DiggerDebug::Islands() || DiggerDebug::Voxels())
    UE_LOG(LogTemp, Display, TEXT("[Digger] Successfully removed %d voxels at %s."), ExtractedVoxels.Num(), *IslandCenter.ToString());
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
    

    if (!DynamicHoleClass)
    {
        const UDiggerSettings* Settings = UDiggerSettings::Get();
        if (Settings)
        {
            DynamicHoleClass = Settings->DefaultHoleActorClass.LoadSynchronous();
        }

        if (!DynamicHoleClass)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("DiggerManager: DefaultHoleActorClass is not set in Project Settings -> Digger"));
        }
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

    ADynamicLightActor* Light = GetWorld()->SpawnActor<ADynamicLightActor>(
        ADynamicLightActor::StaticClass(), FinalPosition, Rotation);

    if (Light)
    {
        // Assign UID and register for undo
        const int32 NewUID = AllocateActorUID();
        Light->ActorUID = NewUID;
        Light->SetDiggerManager(this);
        RegisterActorUID(NewUID, Light);

        // Assign owning chunk
        FIntVector ChunkCoords = FVoxelConversion::WorldToChunk(FinalPosition);
        if (UVoxelChunk* Chunk = GetOrCreateChunkAtCoords(ChunkCoords))
            Light->SetOwningChunk(Chunk);

        Light->InitializeFromBrush(BrushStroke);

        SpawnedLights.Add(Light);

#if WITH_EDITOR
        Light->SetFolderPath(FName("Digger/DynamicLights"));
#endif

        if (DiggerDebug::Lights())
            UE_LOG(LogTemp, Warning, TEXT("Spawned Light Type: %s UID=%d"), *GetLightTypeName(BrushStroke.LightType), NewUID);
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

// Dirty Chunk handling
void ADiggerManager::BuildFinalMesh()
{
    if (!ProceduralMesh)
        return;

    // Nothing to build? Do NOT clear the existing mesh.
    if (GlobalVertices.Num() == 0 || GlobalTriangles.Num() == 0)
    {
        if( DiggerDebug::Mesh() )
            UE_LOG(LogTemp, Warning, TEXT("BuildFinalMesh: Global mesh is empty, skipping rebuild"));
        return;
    }

    // Normalize normals
    for (FVector& N : GlobalNormals)
        N.Normalize();

    // Replace the global section
    ProceduralMesh->ClearAllMeshSections();

    ProceduralMesh->CreateMeshSection(
        0,
        GlobalVertices,
        GlobalTriangles,
        GlobalNormals,
        TArray<FVector2D>(),
        TArray<FColor>(),
        TArray<FProcMeshTangent>(),
        true
    );

    ProceduralMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    ProceduralMesh->bUseComplexAsSimpleCollision = true;
}



void ADiggerManager::RegisterDirtyChunk(const FIntVector& Coord)
{
    DirtyChunkCoords.Add(Coord);
}

// Call this once per “batch” before you kick off mesh generation for all dirty chunks.
// You can expose it or call it from wherever you trigger updates.
void ADiggerManager::BeginMeshUpdateBatch()
{
    GlobalVertexCache.Empty();
    GlobalVertices.Empty();
    GlobalNormals.Empty();
    GlobalTriangles.Empty();

    // Quantization: 1% of voxel size
    GlobalVertexQuant = 1.0f / (FVoxelConversion::LocalVoxelSize * 0.01f);

    PendingMeshUpdates = DirtyChunkCoords.Num();
}


void ADiggerManager::NotifyChunkMeshComplete(const FIntVector& Coord)
{
    DirtyChunkCoords.Remove(Coord);
    PendingMeshUpdates--;

    if (PendingMeshUpdates <= 0)
    {
        BuildFinalMesh();
        InvalidateLandscapeRVTForDirtyBounds();

        // Schedule a deferred hole bake — after the user stops sculpting for
        // 3 seconds, merge all hole actors into a single mesh to keep draw
        // calls low.  The timer resets on each new mesh completion, so rapid
        // painting never triggers a premature bake.
        if (UWorld* W = GetWorld())
        {
            W->GetTimerManager().ClearTimer(HoleBakeTimerHandle);
            W->GetTimerManager().SetTimer(HoleBakeTimerHandle, this,
                &ADiggerManager::BakeStableHoles, 3.0f, false);
        }
    }
}

void ADiggerManager::InvalidateLandscapeRVTForDirtyBounds()
{
    UWorld* W = GetSafeWorld();
    if (!W) return;

    // Collect world-space bounds of every hole across all chunks.
    // This gives us the minimal region to invalidate rather than the whole landscape.
    FBox HoleBounds(ForceInit);

    for (auto& Pair : ChunkMap)
    {
        UVoxelChunk* Chunk = Pair.Value;
        if (!Chunk) continue;

        for (const TWeakObjectPtr<ADynamicHole>& HolePtr : Chunk->GetSpawnedHoles())
        {
            if (ADynamicHole* Hole = HolePtr.Get())
            {
                // Use the hole actor's component bounds — already in world space.
                FVector Origin, Extent;
                Hole->GetActorBounds(false, Origin, Extent);
                // Expand slightly so shadow pages at the boundary are caught.
                HoleBounds += FBox(Origin - Extent * 1.5f, Origin + Extent * 1.5f);
            }
        }
    }

    if (!HoleBounds.IsValid)
    {
        // No holes found — nothing to invalidate.
        return;
    }

    const FBoxSphereBounds InvalidationBounds(HoleBounds);

    // RVT components live on ARuntimeVirtualTextureVolume, not landscape proxies
    for (TActorIterator<ARuntimeVirtualTextureVolume> It(W); It; ++It)
    {
        ARuntimeVirtualTextureVolume* Vol = *It;
        if (!Vol) continue;
        URuntimeVirtualTextureComponent* RVTComp =
            Vol->FindComponentByClass<URuntimeVirtualTextureComponent>();
        if (!RVTComp) continue;
        if (RVTComp->Bounds.GetBox().Intersect(HoleBounds))
            RVTComp->Invalidate(InvalidationBounds);
    }

    for (TActorIterator<ALandscapeProxy> It(W); It; ++It)
    {
        ALandscapeProxy* Proxy = *It;
        if (!Proxy) continue;

        TArray<ULandscapeComponent*> LandscapeComponents;
        Proxy->GetComponents<ULandscapeComponent>(LandscapeComponents);

        for (ULandscapeComponent* LC : LandscapeComponents)
        {
            if (!LC) continue;
            if (LC->Bounds.GetBox().Intersect(HoleBounds))
                LC->MarkRenderStateDirty();
        }
    }

#if WITH_EDITOR
    if (GEditor)
        GEditor->RedrawLevelEditingViewports();
#endif
}


void ADiggerManager::StitchNormalsAcrossSections()
{
    if (!ProceduralMesh)
        return;

    struct FVertRef
    {
        int32 Section;
        int32 Index;
    };

    // Try a slightly generous epsilon so border verts actually land together
    const float Epsilon = FVoxelConversion::LocalVoxelSize * 0.5f;
    const float Inv     = 1.0f / Epsilon;

    TMap<FIntVector, TArray<FVertRef>> Buckets;

    struct FSectionData
    {
        TArray<FVector>          Verts;
        TArray<FVector>          Normals;
        TArray<FVector2D>        UVs;
        TArray<FColor>           Colors;
        TArray<FProcMeshTangent> Tangents;
    };

    TMap<int32, FSectionData> Sections;

    const int32 NumSections = ProceduralMesh->GetNumSections();

    // 1. Gather all vertices/normals from all sections
    for (int32 S = 0; S < NumSections; ++S)
    {
        FProcMeshSection* Sec = ProceduralMesh->GetProcMeshSection(S);
        if (!Sec)
            continue;

        const int32 VertCount = Sec->ProcVertexBuffer.Num();
        if (VertCount == 0)
            continue;

        FSectionData Data;
        Data.Verts.Reserve(VertCount);
        Data.Normals.Reserve(VertCount);
        Data.UVs.Reserve(VertCount);
        Data.Colors.Reserve(VertCount);
        Data.Tangents.Reserve(VertCount);

        for (int32 i = 0; i < VertCount; ++i)
        {
            const FProcMeshVertex& V = Sec->ProcVertexBuffer[i];

            Data.Verts.Add(V.Position);
            Data.Normals.Add(V.Normal);
            Data.UVs.Add(V.UV0);
            Data.Colors.Add(V.Color);
            Data.Tangents.Add(V.Tangent);

            const FVector& P = V.Position;
            const FIntVector Key(
                FMath::RoundToInt(P.X * Inv),
                FMath::RoundToInt(P.Y * Inv),
                FMath::RoundToInt(P.Z * Inv)
            );

            Buckets.FindOrAdd(Key).Add({ S, i });
        }

        Sections.Add(S, MoveTemp(Data));
    }

    // 2. For each bucket with >1 vertex, average *positions* and normals
    for (auto& Pair : Buckets)
    {
        const TArray<FVertRef>& Refs = Pair.Value;
        if (Refs.Num() < 2)
            continue;

        FVector PosAccum = FVector::ZeroVector;
        FVector NrmAccum = FVector::ZeroVector;

        for (const FVertRef& R : Refs)
        {
            FSectionData& SecData = Sections[R.Section];
            PosAccum += SecData.Verts[R.Index];
            NrmAccum += SecData.Normals[R.Index];
        }

        const FVector AvgPos = PosAccum / float(Refs.Num());
        FVector AvgNrm = NrmAccum.GetSafeNormal();
        if (AvgNrm.IsNearlyZero())
            AvgNrm = FVector::UpVector;

        for (const FVertRef& R : Refs)
        {
            FSectionData& SecData = Sections[R.Section];
            SecData.Verts[R.Index]   = AvgPos;
            SecData.Normals[R.Index] = AvgNrm;
        }
    }

    // 3. Push updated verts + normals back into the mesh
    for (auto& SecPair : Sections)
    {
        const int32 S = SecPair.Key;
        FSectionData& D = SecPair.Value;

        ProceduralMesh->UpdateMeshSection(
            S,
            D.Verts,
            D.Normals,
            D.UVs,
            D.Colors,
            D.Tangents
        );
    }
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
    
    // Apply the brush. This records deltas into PendingAction each tick.
    // DO NOT call CommitPendingAction() here — this function is called every
    // tick during a continuous drag.  The caller (Blueprint or PlayerController)
    // must call CommitPendingAction() on mouse-up / stroke-end so the whole
    // drag becomes a single undoable step rather than one per tick.
    ApplyBrushToAllChunks(BrushStroke);

    // Fire dig FX in PIE / runtime (3D sound by default in gameplay).
    {
        EDiggerFXOperation FXOp;
        if (BrushStroke.BrushType == EVoxelBrushType::Smooth)
            FXOp = EDiggerFXOperation::Smooth;
        else if (BrushStroke.bDig)
            FXOp = EDiggerFXOperation::Dig;
        else
            FXOp = EDiggerFXOperation::Fill;
        PlayDigFX(FXOp, BrushStroke.BrushPosition, BrushStroke.BrushRadius);
    }
}

void ADiggerManager::ApplyBrushToAllChunks(FBrushStroke& BrushStroke, bool ForceUpdate)
{
    ApplyBrushToAllChunks(BrushStroke);
}

void ADiggerManager::ApplyBrushToAllChunks(FBrushStroke& BrushStroke)
{
    // New holes coexist alongside the baked PMC — both write to the RVT.
    // The next scheduled bake absorbs the new holes.  No unbake needed.

    const UVoxelBrushShape* ActiveBrushShape = GetActiveBrushShape(BrushStroke.BrushType);
    if (!ActiveBrushShape) return;

    if (FVoxelConversion::LocalVoxelSize <= 0.0f)
    {
        FVoxelConversion::InitFromConfig(8, 4, 100.0f, FVector::ZeroVector);
    }

    // Handle Holes (Keep your existing logic here)
    // ...

    VoxelSize = FVoxelConversion::LocalVoxelSize;
    float BrushEffectRadius = BrushStroke.BrushRadius + BrushStroke.BrushFalloff;
    
    // PADDING: 1 voxel beyond the brush effect radius is sufficient to catch
    // any boundary voxel written by ApplyBrushStroke's ghost-redirect path.
    // The previous 3-voxel padding created and meshed chunks well outside the
    // visible dig region, producing the squared-off excess mesh in screenshots.
    float RoutingPadding = BrushEffectRadius + VoxelSize;

    FVector Min = BrushStroke.BrushPosition - FVector(RoutingPadding);
    FVector Max = BrushStroke.BrushPosition + FVector(RoutingPadding);

    FIntVector MinChunk = FVoxelConversion::WorldToChunk(Min);
    FIntVector MaxChunk = FVoxelConversion::WorldToChunk(Max);

    // Collect all chunks that will receive the stroke first, creating them as
    // needed.  We must not mesh any chunk until ALL of them have been stroked —
    // otherwise a newly-created chunk B meshes before chunk A's ghost-redirect
    // voxels are written, leaving B's pad empty and causing seam puckering on
    // the very first brush stroke.
    TArray<UVoxelChunk*> AffectedChunks;
    for (int32 X = MinChunk.X; X <= MaxChunk.X; ++X)
    for (int32 Y = MinChunk.Y; Y <= MaxChunk.Y; ++Y)
    for (int32 Z = MinChunk.Z; Z <= MaxChunk.Z; ++Z)
    {
        if (UVoxelChunk* Chunk = GetOrCreateChunkAtCoords(FIntVector(X, Y, Z)))
            AffectedChunks.Add(Chunk);
    }

    // Phase 1: write all voxels into all chunks (including ghost redirects).
    // Dirty marking is suppressed during this phase — see SuppressDirty flag.
    ENetMode NetMode = GetWorld()->GetNetMode();
    for (UVoxelChunk* Chunk : AffectedChunks)
    {
        Chunk->SetSuppressDirty(true);
        if (NetMode == NM_Standalone)
            Chunk->ApplyBrushStroke(BrushStroke);
        else
            Chunk->MulticastApplyBrushStroke(BrushStroke);
        Chunk->SetSuppressDirty(false);
    }

    // Phase 2: now that every chunk has its full authored data (including
    // boundary voxels written by neighbours via ghost redirect), mark all
    // dirty so they all remesh with complete neighbour data.
    for (UVoxelChunk* Chunk : AffectedChunks)
        Chunk->MarkDirty();
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
    if (UVoxelChunk* Chunk = GetOrCreateChunkAtCoords(ChunkCoords))
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
    if (UVoxelChunk* Chunk = GetOrCreateChunkAtCoords(ChunkCoords))
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
        UE_LOG(LogTemp, Verbose, TEXT("LocalInChunk: %s, LocalVoxel: %s, WorldPosition: %s"),
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
    // Only self-commit when called standalone (e.g. from runtime Blueprint).
    // During an editor stroke the bracket is already open.
    if (!bHasPendingAction)
        CommitPendingAction();
    
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
    // Direct landscape sampling — cache path disabled (HeightCacheSystem route
    // is available via SampleLandscapeHeight but not used here to keep grounding
    // checks simple and always-correct).
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
        // Only self-commit when called standalone.
        // During an editor stroke the bracket is already open.
        if (!bHasPendingAction)
            CommitPendingAction();

        // Fire dig FX (runtime gameplay path).
        PlayDigFX(bIsDigging ? EDiggerFXOperation::Dig : EDiggerFXOperation::Fill,
                  Stroke.BrushPosition, Stroke.BrushRadius);

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
        UE_LOG(LogTemp, Warning, TEXT("[Digger] Running ADM::ProcessDirtyChunksLoop..."));
    }

    // Pass idle state to the collision cook so it knows whether to do a
    // fast AABB-culled partial cook (while painting) or a full section cook
    // (when the user has stopped brushing and collision should be authoritative).
    const bool bIdle = !bIsEditorPainting;

    // Only process chunks that are actually dirty — avoids O(N) full map
    // scan every 0.1s when only a handful of chunks need updating.
    // DirtyChunkCoords is maintained by RegisterDirtyChunk (called from MarkDirty).
    // We snapshot it to allow safe modification during iteration.
    TArray<FIntVector, TInlineAllocator<8>> ToProcess(DirtyChunkCoords.Array());
    for (const FIntVector& Coords : ToProcess)
    {
        UVoxelChunk** ChunkPtr = ChunkMap.Find(Coords);
        if (!ChunkPtr || !*ChunkPtr)
        {
            DirtyChunkCoords.Remove(Coords);
            continue;
        }
        UVoxelChunk* Chunk = *ChunkPtr;
        Chunk->UpdateIfDirty();
        if (!Chunk->IsDirty())
            DirtyChunkCoords.Remove(Coords);
    }

    // Collision rebuild: only iterate chunks that have flagged themselves dirty.
    // CollisionDirtyCoords is populated by RegisterCollisionDirtyChunk (called
    // from RequestImmediateCollisionRebuild).  This is O(dirty) not O(all).
    if (CollisionDirtyCoords.Num() > 0)
    {
        if (bIdle)
        {
            // Idle: cook all dirty chunks at once.
            TSet<FIntVector> ToRebuild = CollisionDirtyCoords;
            for (const FIntVector& Coords : ToRebuild)
            {
                UVoxelChunk** ChunkPtr = ChunkMap.Find(Coords);
                if (!ChunkPtr || !*ChunkPtr) continue;
                UVoxelChunk* Chunk = *ChunkPtr;
                Chunk->RebuildCollisionIfNeeded(/*bIdleFullRebuild=*/true);
                if (!Chunk->bNeedsFullCollisionRebuild)
                    CollisionDirtyCoords.Remove(Coords);
            }
        }
        else
        {
            // Painting: cook ONE chunk per tick so the preview can snap down
            // for drill-through, without stalling the brush with many cooks.
            const FIntVector BrushChunk = FVoxelConversion::WorldToChunk(EditorBrushPosition);
            UVoxelChunk** ChunkPtr = ChunkMap.Find(BrushChunk);
            if (ChunkPtr && *ChunkPtr)
            {
                UVoxelChunk* Chunk = *ChunkPtr;
                Chunk->RebuildCollisionIfNeeded(/*bIdleFullRebuild=*/true);
                if (!Chunk->bNeedsFullCollisionRebuild)
                    CollisionDirtyCoords.Remove(BrushChunk);
            }
        }
    }

    if (DiggerDebug::Islands())
    {
        UE_LOG(LogTemp, Warning, TEXT("[Digger] Finished updating dirty chunks."));
    }
}

void ADiggerManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // Destroy all pooled hole actors — they are RF_Transient so the GC won't
    // find them through normal object tracing; we must explicitly destroy them.
    FlushHolePool();

    // Reset streaming-load state so a fresh session starts clean.
    bLoadingBrushDisabled = false;
    CurrentLoadPhase      = EDiggerLoadPhase::Idle;
    LoadErrorCount        = 0;
    PendingHoleLoad.Empty();

    // Clear baked holes and timer.
    GetWorldTimerManager().ClearTimer(HoleBakeTimerHandle);
    for (auto& Pair : BakedHoleISMs)
    {
        if (Pair.Value)
            Pair.Value->ClearInstances();
    }
    bHolesBaked = false;

    // Clear the pre-loaded holes flag on all chunks.
    for (auto& Pair : ChunkMap)
    {
        if (Pair.Value) Pair.Value->bHolesPreLoaded = false;
    }

#if WITH_EDITOR
    if (PreSaveWorldHandle.IsValid())
    {
        FEditorDelegates::PreSaveWorldWithContext.Remove(PreSaveWorldHandle);
        PreSaveWorldHandle.Reset();
    }
    // Broadcast cancelled state so the editor module can clean up its notification.
    {
        FDiggerLoadProgress CancelP;
        CancelP.Phase = EDiggerLoadPhase::Idle;
        CancelP.Pct   = -1.f;
        OnLoadProgressUpdated.Broadcast(CancelP);
    }
#endif

    // Restore the editor history preference unconditionally.
    // Any PIE undo state is discarded — it belongs to the play session.
    bHistoryEnabled   = bHistoryEnabledBeforePIE;
    UndoStack.Empty();
    RedoStack.Empty();
    PendingAction     = FDiggerHistoryAction();
    bHasPendingAction = false;

    // Cleanup
    if (HeightCacheSystem)
    {
        HeightCacheSystem->Clear();
    }
    
    // Only clear voxel data on true destruction — NOT on EndPlayInEditor.
    // EndPlayInEditor fires after every PIE session but the editor actor
    // persists; clearing here wipes the loaded voxel mesh and leaves only
    // the hole BPs, causing the "holey landscape with no underground mesh"
    // problem seen on project load.
    // LevelTransition and Destroyed are genuine teardown events that need
    // a clean slate.
    if (EndPlayReason == EEndPlayReason::LevelTransition ||
        EndPlayReason == EEndPlayReason::Destroyed)
    {
        ClearAllVoxelData();
    }
    else if (EndPlayReason == EEndPlayReason::EndPlayInEditor)
    {
        // PIE ended — the editor world is still live.
        // The ProceduralMeshComponent sections are cleared by UE during PIE
        // teardown (the PMC is a component on the editor actor — UE resets it).
        // We must force every loaded chunk to regenerate its mesh section.
        // We do NOT reload from disk — the SparseVoxelGrid data is still live
        // in memory (we didn't call ClearAllVoxelData).
#if WITH_EDITOR
        // Brief delay so UE finishes its own PIE teardown before we rebuild.
        if (UWorld* W = GetSafeWorld())
        {
            FTimerHandle PIERestoreHandle;
            W->GetTimerManager().SetTimer(PIERestoreHandle, [this]()
            {
                if (!IsValid(this)) return;

                // Rebuild mesh for every chunk that has authored voxels.
                for (auto& Pair : ChunkMap)
                {
                    UVoxelChunk* Chunk = Pair.Value;
                    if (Chunk && Chunk->GetSparseVoxelGrid() &&
                        Chunk->GetSparseVoxelGrid()->VoxelData.Num() > 0)
                    {
                        Chunk->MarkDirty();
                    }
                }

                // Restart the chunk update loop (timer is cleared by UE on PIE end).
                if (!GetWorld()->GetTimerManager().IsTimerActive(ChunkUpdateTimerHandle))
                {
                    GetWorld()->GetTimerManager().SetTimer(
                        ChunkUpdateTimerHandle, this,
                        &ADiggerManager::ProcessDirtyChunksLoop, 0.1f, true);
                }

                SpawnOrUpdateBedrockFloor();

            }, 0.3f, false);
        }
#endif
    }

    Super::EndPlay(EndPlayReason);
}

// =============================================================================
// BEDROCK FLOOR
// =============================================================================

void ADiggerManager::SpawnOrUpdateBedrockFloor()
{
    if (!bEnableBedrockFloor)
    {
        DestroyBedrockFloor();
        return;
    }

    UWorld* W = GetSafeWorld();
    if (!W) return;

    // Destroy stale actor if already spawned.
    if (IsValid(BedrockFloorActor))
        BedrockFloorActor->Destroy();
    BedrockFloorActor = nullptr;

    // Find the lowest landscape point so bedrock sits below it.
    float LowestLandscapeZ = 0.f;
    bool bFoundLandscape = false;
    for (TActorIterator<ALandscapeProxy> It(W); It; ++It)
    {
        const FBox Bounds = It->GetComponentsBoundingBox(true);
        if (!bFoundLandscape || Bounds.Min.Z < LowestLandscapeZ)
        {
            LowestLandscapeZ = Bounds.Min.Z;
            bFoundLandscape  = true;
        }
    }
    const float BedrockZ = LowestLandscapeZ - BedrockDepthBelowLandscape;

    // Spawn a plain AActor with a thin box collision component.
    // Invisible, ECC_Visibility, huge XY — enough for any landscape.
    FActorSpawnParameters P;
    P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    P.ObjectFlags = RF_Transient;
    P.bHideFromSceneOutliner = true;

    AActor* Floor = W->SpawnActor<AActor>(
        AActor::StaticClass(),
        FVector(0.f, 0.f, BedrockZ),
        FRotator::ZeroRotator, P);

    if (!Floor) return;

    Floor->SetActorLabel(TEXT("DiggerBedrockFloor"), /*bMarkDirty=*/false);

    // Box component: flat slab 10cm thick, huge XY.
    UBoxComponent* Box = NewObject<UBoxComponent>(Floor, TEXT("BedrockBox"));
    Box->SetBoxExtent(FVector(BedrockHalfExtentXY, BedrockHalfExtentXY, 5.f));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Box->SetCollisionObjectType(ECC_WorldStatic);
    Box->SetCollisionResponseToAllChannels(ECR_Ignore);
    Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    Box->SetVisibility(false);
    Box->SetHiddenInGame(true);
    Box->RegisterComponent();
    Box->AttachToComponent(Floor->GetRootComponent()
        ? Floor->GetRootComponent()
        : (USceneComponent*)Floor->AddComponentByClass(
              USceneComponent::StaticClass(), false, FTransform::Identity, false),
        FAttachmentTransformRules::KeepRelativeTransform);

    // Make it the root so the actor location drives the box.
    Floor->SetRootComponent(Cast<USceneComponent>(Box));
    BedrockFloorActor = Floor;

#if WITH_EDITOR
    Floor->SetFolderPath(FName("Digger/Internal"));
#endif

    if (DiggerDebug::Chunks())
        UE_LOG(LogTemp, Log,
            TEXT("[Digger] Bedrock floor spawned at Z=%.1f (extent=%.0fcm)"),
            BedrockZ, BedrockHalfExtentXY);
}

void ADiggerManager::DestroyBedrockFloor()
{
    if (IsValid(BedrockFloorActor))
    {
        BedrockFloorActor->Destroy();
        BedrockFloorActor = nullptr;
    }
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

    // Destroy bedrock floor — it will be recreated on next init.
    DestroyBedrockFloor();

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

    // --- 2b. FLUSH HOLE POOL ---
    // ClearSpawnedHoles returns holes to the pool (hidden, collision off).
    // Flush the pool now so those actors are fully destroyed and don't
    // linger in the world after a clear.
    FlushHolePool();

    // Belt-and-suspenders: destroy any stray ADynamicHole actors that
    // somehow weren't tracked by a chunk (e.g. orphaned from a crash).
    if (UWorld* W = GetSafeWorld())
    {
        TArray<AActor*> StrayHoles;
        UGameplayStatics::GetAllActorsOfClass(W, ADynamicHole::StaticClass(), StrayHoles);
        for (AActor* A : StrayHoles)
        {
            if (IsValid(A))
                A->Destroy();
        }
    }

    // --- 3. CLEAN UP CONTAINERS ---
    ChunkMap.Empty();
    DirtyChunkCoords.Empty();     // stale coords from before the clear must not fire
    CollisionDirtyCoords.Empty(); // same for collision rebuild set

    // Cancel any in-progress streaming load — the queue coords reference
    // chunks that no longer exist after the clear.
    if (GetWorld())
    {
        GetWorldTimerManager().ClearTimer(LoadQueueTimerHandle);
    }
    PendingChunksToLoad.Empty();
    PendingMeshBuild.Empty();
    PendingCollisionBuild.Empty();

    // --- 4. CLEAR PROCEDURAL MESHES ---
    ClearProceduralMeshes();

    // --- 5. RESET GLOBAL SECTION COUNTER ---
    // GlobalChunkID is a static counter in VoxelChunk.cpp.  After a clear the
    // PMC has no sections, so new chunks must start from section 0 again.
    // Without this reset new chunks get section indices like 50, 51... which
    // don't exist on the freshly-cleared PMC, causing null-section access and
    // freezing on the first dig after a clear.
    UVoxelChunk::ResetGlobalChunkID();

    // --- 6. ENSURE CHUNK UPDATE TIMER IS STILL RUNNING ---
    if (GetWorld() && !GetWorld()->GetTimerManager().IsTimerActive(ChunkUpdateTimerHandle))
    {
        GetWorld()->GetTimerManager().SetTimer(
            ChunkUpdateTimerHandle, this,
            &ADiggerManager::ProcessDirtyChunksLoop, 0.1f, true);
    }

    // Reset painting flag.
    bIsEditorPainting = false;

    // --- 8. PRE-WARM HEIGHT CACHE FOR BRUSH AREA ---
    // After a clear every MarchingCubesGenerator is brand new with an empty
    // height cache.  The first dig calls GenerateMesh which calls CacheHeightMap
    // synchronously on the game thread — (N+2*Pad+1)² landscape queries — before
    // going async.  With a 32-voxel grid that is 1369 queries, causing a
    // noticeable freeze on the first click.
    //
    // We pre-warm by scheduling a deferred task (next frame) that creates the
    // chunk at the current brush position and runs CacheHeightMap on it now,
    // while the user is not yet clicking.  Subsequent chunks hit the already-
    // warm cache and dispatch to the background thread immediately.
    {
        const FVector BrushPos = EditorBrushPosition;
        FTimerHandle PreWarmHandle;
        GetWorldTimerManager().SetTimer(PreWarmHandle, [this, BrushPos]()
        {
            if (!IsValid(this)) return;
            // Warm the height cache for the chunk the user will most likely
            // dig first (the one currently under the brush).  We use a
            // temporary MarchingCubes instance so no permanent chunk is
            // created — the cache is stored per-instance, so when the real
            // chunk is created on first dig its own MC will still need to
            // warm.  The real value here is spreading the landscape queries
            // across this deferred tick rather than the first mouse-click.
            //
            // If a chunk already exists at the brush position (e.g. partial
            // clear), warm that one directly so it is ready immediately.
            const FIntVector BrushChunk = FVoxelConversion::WorldToChunk(BrushPos);
            const FVector    Origin     = FVoxelConversion::ChunkToWorld(BrushChunk);
            const int32      N         = ChunkSize * Subdivisions;
            const float      VS        = FVoxelConversion::LocalVoxelSize;

            // If chunk already exists, warm it and its immediate neighbours.
            TArray<FIntVector> ToWarm = { BrushChunk,
                BrushChunk + FIntVector(1,0,0), BrushChunk + FIntVector(-1,0,0),
                BrushChunk + FIntVector(0,1,0), BrushChunk + FIntVector(0,-1,0) };

            for (const FIntVector& Coords : ToWarm)
            {
                UVoxelChunk* Chunk = GetChunkAtCoords(Coords); // non-creating lookup
                if (!Chunk) continue;
                if (UMarchingCubes* MC = Chunk->GetMarchingCubesGenerator())
                {
                    const FVector ChunkOrigin = FVoxelConversion::ChunkToWorld(Coords);
                    if (!MC->IsHeightCacheValid(ChunkOrigin, VS))
                        MC->CacheHeightMap(ChunkOrigin, VS, N);
                }
            }
        }, 0.05f, false); // one-shot, next frame
    }

    UE_LOG(LogTemp, Warning, TEXT("DiggerManager: Cleanup Complete."));
}


FIslandMeshData ADiggerManager::ExtractAndGenerateIslandMesh(const FVector& IslandCenter)
{
    UE_LOG(LogTemp, Log, TEXT("[Digger] Starting ExtractAndGenerateIslandMesh at IslandCenter: %s"), *IslandCenter.ToString());

    FIslandMeshData Result;

    // Step 1: Find or create the voxel chunk at the given center
    UVoxelChunk* Chunk = FindOrCreateNearestChunk(IslandCenter);
    if (!Chunk)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Digger] No chunk found or created near IslandCenter."));
        return Result;
    }

    FVector ChunkOrigin = FVoxelConversion::ChunkToWorld(Chunk->GetChunkCoords());
    FVector LocalPosition = IslandCenter - ChunkOrigin;

    FIntVector VoxelCoords = FIntVector(
        FMath::FloorToInt(LocalPosition.X / VoxelSize),
        FMath::FloorToInt(LocalPosition.Y / VoxelSize),
        FMath::FloorToInt(LocalPosition.Z / VoxelSize)
    );

    FVector VoxelWorldCenter = ChunkOrigin + FVector(VoxelCoords) * VoxelSize + FVector(FVoxelConversion::LocalVoxelSize * 0.5f);

    UE_LOG(LogTemp, Log, TEXT("[Digger] Chunk Origin: %s | LocalPosition: %s | VoxelCoords: %s | VoxelWorldCenter: %s"),
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
        UE_LOG(LogTemp, Warning, TEXT("[Digger] No SparseVoxelGrid found on chunk."));
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
        UE_LOG(LogTemp, Warning, TEXT("[Digger] No set voxel found near the provided position."));
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
        UE_LOG(LogTemp, Warning, TEXT("[Digger] Failed to extract island or ExtractedGrid is null."));
        
        // Restore from backup if needed
        /*if (bMakeBackupBeforeExtraction && OriginalGridBackup)
        {
            UE_LOG(LogTemp, Warning, TEXT("[Digger] Restoring grid from backup."));
            Chunk->SetSparseVoxelGrid(OriginalGridBackup);
        }*/
        
        return Result;
    }

    // Ensure the extracted grid has the necessary references
    //ExtractedGrid->ParentChunk = nullptr; // Don't want to affect the original chunk
    //ExtractedGrid->DiggerManager = this;

    // Step 4: Generate mesh
    UE_LOG(LogTemp, Log, TEXT("[Digger] Starting mesh generation using Marching Cubes."));

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

    UE_LOG(LogTemp, Log, TEXT("[Digger] Mesh generation complete. Valid: %s, Vertices: %d, Triangles: %d"),
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
            UE_LOG(LogTemp, Warning, TEXT("[Digger] Island cleanup: Removed %d voxels across %d chunks"),
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
    UE_LOG(LogTemp, Warning, TEXT("[Digger] ConvertIslandAtPositionToStaticMesh called at %s"), *IslandCenter.ToString());
    FIslandMeshData MeshData = ExtractAndGenerateIslandMesh(IslandCenter);
    if (MeshData.bValid)
    {
        FString AssetName = FString::Printf(TEXT("Island_%s_StaticMesh"), *IslandCenter.ToString());
        SaveIslandMeshAsStaticMesh(AssetName, MeshData);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[Digger] ConvertIslandAtPositionToStaticMesh called with invalid static mesh data at %s"), *IslandCenter.ToString());
    }
}

void ADiggerManager::ConvertIslandAtPositionToActor(const FVector& IslandCenter, bool bEnablePhysics, FIntVector ReferenceVoxel)
{
    UE_LOG(LogTemp, Warning, TEXT("[Digger] ConvertIslandAtPositionToActor called at %s"), *IslandCenter.ToString());

    // Debug visualization
    DrawDebugSphere(GetSafeWorld(), IslandCenter, 50.0f, 16, FColor::Red, false, 10.0f, 0, 2.0f);
    DrawDebugString(GetSafeWorld(), IslandCenter + FVector(0, 0, 60.0f), TEXT("Island Search Point"), nullptr, FColor::White, 10.0f);

    if (ReferenceVoxel != FIntVector::ZeroValue)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Digger] Using reference voxel: %s"), *ReferenceVoxel.ToString());

        // Convert global voxel index to chunk + local voxel
        FIntVector ChunkCoords, LocalVoxel;
        FVoxelConversion::GlobalVoxelToChunkAndLocal(ReferenceVoxel, ChunkCoords, LocalVoxel);

        // Find the chunk containing this voxel
        UVoxelChunk* Chunk = ChunkMap.FindRef(ChunkCoords);
        if (!Chunk)
        {
            UE_LOG(LogTemp, Error, TEXT("[Digger] No chunk found for chunk coords: %s"), *ChunkCoords.ToString());
            return;
        }

        // Verify the voxel exists in the grid before extraction
        USparseVoxelGrid* Grid = Chunk->GetSparseVoxelGrid();
        if (!Grid || !Grid->IsVoxelSolid(LocalVoxel))
        {
            UE_LOG(LogTemp, Warning, TEXT("[Digger] Reference voxel not found or not solid at local coords: %s"), *LocalVoxel.ToString());
            
            // Fallback to center-based extraction
            FIslandMeshData MeshData = ExtractIslandByCenter(IslandCenter, false, bEnablePhysics);
            if (MeshData.bValid)
            {
                AIslandActor* IslandActor = SpawnIslandActorWithMeshData(IslandCenter, MeshData, bEnablePhysics);
                if (IslandActor)
                {
                    UE_LOG(LogTemp, Warning, TEXT("[Digger] Successfully created island actor using center-based extraction"));
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
                UE_LOG(LogTemp, Warning, TEXT("[Digger] Successfully created island actor using center-based extraction"));
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
        UE_LOG(LogTemp, Warning, TEXT("[Digger] No chunk provided for extraction."));
        return Result;
    }
    
    USparseVoxelGrid* VoxelGrid = Chunk->GetSparseVoxelGrid();
    if (!VoxelGrid)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Digger] No SparseVoxelGrid found on chunk."));
        return Result;
    }
    
    if (IslandData.Voxels.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Digger] Island data contains no voxels."));
        return Result;
    }
    
    // Validate that VoxelCount matches actual array size
    if (IslandData.VoxelCount != IslandData.Voxels.Num())
    {
        UE_LOG(LogTemp, Warning, TEXT("[Digger] Island VoxelCount (%d) doesn't match Voxels array size (%d)"), 
               IslandData.VoxelCount, IslandData.Voxels.Num());
    }
    
    FVector ChunkOrigin = FVoxelConversion::ChunkToWorld(Chunk->GetChunkCoords());
    
    // Create a temporary grid for the island
    USparseVoxelGrid* ExtractedGrid = NewObject<USparseVoxelGrid>();
    
    UE_LOG(LogTemp, Log, TEXT("[Digger] Extracting island at location %s with %d voxels (reference: %s)"), 
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
            UE_LOG(LogTemp, Warning, TEXT("[Digger] Island voxel at %s not found in grid"), *Pos.ToString());
        }
    }
    
    if (ValidVoxels == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Digger] No valid voxel data found for island."));
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
    
    UE_LOG(LogTemp, Log, TEXT("[Digger] Extracted grid contains %d voxels (%d solid + %d boundary)"), 
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
    
    UE_LOG(LogTemp, Log, TEXT("[Digger] Island mesh generation complete. Valid Voxels: %d, Vertices: %d, Triangles: %d"),
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

    // Pre-warm the hole actor pool so the first brush strokes don't stall
    // waiting for SpawnActor.  Uses HolePoolSize (default 64).
    PrewarmHolePool();

    // Save the editor value and apply the PIE preference.
    // This means bHistoryEnabled drives everything at runtime;
    // the editor value is transparently restored on EndPlay.
    bHistoryEnabledBeforePIE = bHistoryEnabled;
    bHistoryEnabled = bHistoryEnabledInPIE;

    // Clear any stale editor undo state so PIE starts clean.
    // (Editor strokes recorded before PIE are not meaningful at runtime.)
    if (!bHistoryEnabled)
    {
        UndoStack.Empty();
        RedoStack.Empty();
        PendingAction     = FDiggerHistoryAction();
        bHasPendingAction = false;
    }

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
    DestroyAllDynamicHoles();
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

    // 7. DEFERRED LOADING
    // Wait one frame (0.05s) for the engine to finish initial rendering,
    // then start the async chunk load pipeline.
    FTimerHandle InitHandle;
    GetWorld()->GetTimerManager().SetTimer(InitHandle, [this]()
    {
        if (!IsValid(this)) return;

        UE_LOG(LogTemp, Log, TEXT("DiggerManager: Starting Deferred Load..."));

        // A. Load Chunks (async 3-phase pipeline)
        LoadAllChunks("Default");
        SpawnOrUpdateBedrockFloor();

        // B. Restore Islands
        for (const FIslandSaveData& SavedIsland : SavedIslands)
        {
            RecreateIslandFromSaveData(SavedIsland);
        }

        // Ensure the chunk refresh loop is running in Game
        GetWorld()->GetTimerManager().SetTimer(ChunkUpdateTimerHandle, this, &ADiggerManager::ProcessDirtyChunksLoop, 0.1f, true);

    }, 0.05f, false);
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
    if (IsRuntimeLike()) return;
    if (bEditorInitDone)  return;

    bEditorInitDone = true;

    // Ensure core systems are ready before loading.
    EnsureHoleShapeLibrary();
    EnsureDefaultHoleBP();

    if (!HeightCacheSystem)
        return; // not yet constructed — OnConstruction will retry

    HeightCacheSystem->Initialize(GetSafeWorld());
    UpdateVoxelSize();
    InitializeBrushShapes();
    PrewarmHolePool();

    // Load all saved chunks (voxels + holes) so they appear in the editor
    // immediately after a restart without requiring PIE.
    // Deferred 0.5s so the landscape and viewport are fully initialized
    // before height sampling begins.
    if (UWorld* W = GetSafeWorld())
    {
        FTimerHandle EditorLoadHandle;
        W->GetTimerManager().SetTimer(EditorLoadHandle, [this]()
        {
            if (!IsValid(this)) return;

            LoadAllChunks(TEXT("Default"));
            SpawnOrUpdateBedrockFloor();

            // Keep the chunk update loop running in editor.
            if (UWorld* W2 = GetSafeWorld())
            {
                if (!W2->GetTimerManager().IsTimerActive(ChunkUpdateTimerHandle))
                {
                    W2->GetTimerManager().SetTimer(
                        ChunkUpdateTimerHandle, this,
                        &ADiggerManager::ProcessDirtyChunksLoop, 0.1f, true);
                }
            }
        }, 0.5f, false);
    }
#endif
}



void ADiggerManager::DestroyAllDynamicHoles()
{
    if (!DynamicHoleClass) return; // Ensure the reference is valid

    World = GetSafeWorld();
    if (!World) return;

    for (TActorIterator<AActor> It(World, DynamicHoleClass); It; ++It)
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

    FIntVector Coordinates = Chunk->GetChunkCoords();
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


void ADiggerManager::HandleHoleSpawn(const FBrushStroke& Stroke)
{
    // -------------------------------------------------------------------------
    // 1. CLASS VALIDATION
    // -------------------------------------------------------------------------
    if (!DynamicHoleClass)
    {
        EnsureDefaultHoleBP();
        if (!DynamicHoleClass)
        {
            if (DiggerDebug::Holes())
                UE_LOG(LogTemp, Error, TEXT("HandleHoleSpawn: DynamicHoleClass is NULL!"));
            return;
        }
    }

    if (!ActiveBrush)
    {
#if WITH_EDITOR
        if (DiggerDebug::Brush())
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
            if (DiggerDebug::Holes())
                UE_LOG(LogTemp, Error, TEXT("HandleHoleSpawn: Failed to create ActiveBrush at Runtime!"));
            return;
        }
#endif
    }

    if (!ActiveBrush)
    {
        if (DiggerDebug::Holes())
            UE_LOG(LogTemp, Error, TEXT("HandleHoleSpawn: ActiveBrush is still NULL after initialization."));
        return;
    }

    // -------------------------------------------------------------------------
    // 2. AUTHORITATIVE CENTER / ROTATION FROM STROKE (PREVIEW-DRIVEN)
    // -------------------------------------------------------------------------
    FVector Center   = Stroke.BrushPosition;
    FVector Extents  = FVector(Stroke.BrushRadius);
    FQuat   Rotation = Stroke.BrushRotation.Quaternion();
    float   Falloff  = Stroke.BrushFalloff;
    EVoxelBrushType BrushType = Stroke.BrushType;

    ActiveBrush->GetPreviewData(Center, Extents, Rotation, Falloff, BrushType, Stroke);

    const FVector  SpawnLocation = Center;
    const FRotator SpawnRotation = Rotation.Rotator();
    const float    Radius        = Stroke.BrushRadius;

    // -------------------------------------------------------------------------
    // 3. MULTI-PATH LANDSCAPE DETECTION
    //    Path A: camera hit (original)
    //    Path B: landscape height cache + XY ring search
    //    Path C: vertical line trace (last resort)
    // -------------------------------------------------------------------------
    bool  bFoundLandscape = false;
    float LandscapeZ      = UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT;

    // --- Path A: camera hit ---
    {
        FHitResult CamHit;
        if (ActiveBrush->GetCameraHitLocation(CamHit))
        {
            if (ActiveBrush->IsLandscape(CamHit.GetActor()))
            {
                bFoundLandscape = true;
                // Use the hit Z as a reference height if cache is unavailable
                if (LandscapeZ <= UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT)
                    LandscapeZ = CamHit.ImpactPoint.Z;
            }
        }
    }

    // --- Path B: height cache with XY ring fallback ---
    {
        float CachedZ = GetLandscapeHeightAt(SpawnLocation);
        if (CachedZ > UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT)
        {
            bFoundLandscape = true;
            LandscapeZ      = CachedZ;
        }
        else
        {
            // Ring search: 8 directions at 33%, 66%, and 100% of brush radius.
            // Ensures we detect landscape anywhere within the brush volume.
            const float RingPcts[] = { 0.33f, 0.66f, 1.0f };
            const float Angles[]   = { 0.f, 45.f, 90.f, 135.f, 180.f, 225.f, 270.f, 315.f };
            bool bRingFound = false;
            for (float Pct : RingPcts)
            {
                const float SearchR = Radius * Pct;
                for (float Ang : Angles)
                {
                    const float Rad = FMath::DegreesToRadians(Ang);
                    const FVector Off(SearchR * FMath::Cos(Rad), SearchR * FMath::Sin(Rad), 0.f);
                    CachedZ = GetLandscapeHeightAt(SpawnLocation + Off);
                    if (CachedZ > UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT)
                    {
                        bFoundLandscape = true;
                        LandscapeZ      = CachedZ;
                        bRingFound      = true;
                        break;
                    }
                }
                if (bRingFound) break;
            }
        }
    }

    // --- Path C: vertical line trace ---
    if (!bFoundLandscape)
    {
        if (UWorld* W = GetWorld())
        {
            const FVector Start(SpawnLocation.X, SpawnLocation.Y, SpawnLocation.Z + Radius * 2.f);
            const FVector End  (SpawnLocation.X, SpawnLocation.Y, SpawnLocation.Z - Radius * 4.f);
            FHitResult    VTrace;
            FCollisionQueryParams VParams(SCENE_QUERY_STAT(DiggerHandleHoleProbe), true);

            if (W->LineTraceSingleByChannel(VTrace, Start, End, ECC_Visibility, VParams))
            {
                if (VTrace.GetActor() && VTrace.GetActor()->IsA(ALandscapeProxy::StaticClass()))
                {
                    bFoundLandscape = true;
                    LandscapeZ      = VTrace.ImpactPoint.Z;
                }
            }
        }
    }

    if (!bFoundLandscape)
    {
        if (DiggerDebug::Holes())
            UE_LOG(LogTemp, Warning,
                TEXT("HandleHoleSpawn: No landscape detected near %s — hole skipped."),
                *SpawnLocation.ToString());
        return;
    }

    // -------------------------------------------------------------------------
    // 3b. UNIFIED DEPTH GATE
    //
    //  • Brush must intersect the landscape surface (|brushZ - landscapeZ| <= radius)
    //  • Top of brush must not be buried more than 60% of radius
    //  • Brush must not be more than one radius ABOVE the landscape (floating hole)
    // -------------------------------------------------------------------------
    const float VerticalDist  = FMath::Abs(SpawnLocation.Z - LandscapeZ);
    const float TopOfBrush    = SpawnLocation.Z + Radius;
    const float BurialDepth   = LandscapeZ - TopOfBrush;   // positive = buried below surface

    if (VerticalDist > Radius)
    {
        // Brush doesn't touch the landscape surface at all
        if (DiggerDebug::Holes())
            UE_LOG(LogTemp, Verbose,
                TEXT("HandleHoleSpawn: Brush doesn't reach surface (dist=%.1f, r=%.1f) — skipped."),
                VerticalDist, Radius);
        return;
    }

    const float MaxBurial = Radius * 0.60f;
    if (BurialDepth > MaxBurial)
    {
        if (DiggerDebug::Holes())
            UE_LOG(LogTemp, Verbose,
                TEXT("HandleHoleSpawn: Brush too deep (burial=%.1f, max=%.1f) — skipped."),
                BurialDepth, MaxBurial);
        return;
    }

    // -------------------------------------------------------------------------
    // 4. SCALE
    // -------------------------------------------------------------------------
    const float ScaleDivisor = UDiggerSettings::Get()->ScaleDivisor;
    const float BaseScale = Radius / ScaleDivisor;
    const FVector SpawnScale(BaseScale, BaseScale, BaseScale * 0.1f);

    // -------------------------------------------------------------------------
    // 5. CHUNK RESOLUTION
    // -------------------------------------------------------------------------
    UVoxelChunk* Chunk = GetOrCreateChunkAtWorld(SpawnLocation);
    if (!Chunk)
    {
        const FVector OffsetLoc = SpawnLocation + FVector(5.f, 5.f, 0.f);
        Chunk = GetOrCreateChunkAtWorld(OffsetLoc);
    }

    if (!Chunk)
    {
        if (DiggerDebug::Holes())
            UE_LOG(LogTemp, Error,
                TEXT("HandleHoleSpawn: No chunk found near %s"),
                *SpawnLocation.ToString());
        return;
    }

    // -------------------------------------------------------------------------
    // 6. PREPARE HOLE SHAPE (NO GRID SNAP)
    // -------------------------------------------------------------------------
    FHoleShape FinalShape = Stroke.HoleShape;
    FinalShape.ShapeType  =
        (BrushType == EVoxelBrushType::Cube || BrushType == EVoxelBrushType::AdvancedCube)
            ? EHoleShapeType::Cube
            : EHoleShapeType::Sphere;

    // -------------------------------------------------------------------------
    // 7. SPAWN HOLE ACTOR
    // -------------------------------------------------------------------------
    // Snap the hole's Z to the landscape surface so the RVT opacity mask
    // always cuts through the landscape crust — even when the brush center
    // is below the surface.  Without this, deep brush strokes produce holes
    // that sit below the landscape and don't mask it, leaving the landscape
    // visually solid while the voxel mesh underneath is hollow.
    FVector HoleLocation = SpawnLocation;
    HoleLocation.Z = LandscapeZ;

    // Allocate a stable UID before spawning so SpawnHoleFromData can assign it
    // to the actor and we can pass the same UID to the history system.
    const int32 NewUID = AllocateActorUID();

    FSpawnedHoleData Data(HoleLocation, SpawnRotation, SpawnScale, FinalShape);
    Data.HoleUID = NewUID;
    Chunk->SpawnHoleFromData(Data, NewUID);

    // Notify the history system — this records a FDiggerActorSpawnRecord keyed
    // by UID so undo can destroy the actor and redo can recreate it.
    NotifyHoleAddedForHistory(Chunk->GetChunkCoords(), Data, NewUID);

    if (DiggerDebug::Holes())
        UE_LOG(LogTemp, Log,
            TEXT("HandleHoleSpawn: Spawned at %s (Chunk %s) LandscapeZ=%.1f Burial=%.1f UID=%d"),
            *HoleLocation.ToString(),
            *Chunk->GetChunkCoords().ToString(),
            LandscapeZ,
            BurialDepth,
            NewUID);
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
            UVoxelChunk* Chunk = GetOrCreateChunkAtCoords(ChunkPosition);

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
            const FVector ChunkPosition = FVoxelConversion::ChunkToWorld(Chunk->GetChunkCoords());
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

        FVector WorldPos = FVoxelConversion::ChunkToWorld(Chunk->GetChunkCoords());
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

    // O(1) bounded neighbor search instead of O(N) full iteration.
    // Check the 26 surrounding chunk coordinates for the nearest occupied chunk.
    UVoxelChunk* NearestChunk = nullptr;
    float MinDistance = FLT_MAX;
    for (int32 dz = -1; dz <= 1; ++dz)
    {
        for (int32 dy = -1; dy <= 1; ++dy)
        {
            for (int32 dx = -1; dx <= 1; ++dx)
            {
                if (dx == 0 && dy == 0 && dz == 0) continue; // Already checked above
                FIntVector NeighborCoords(ChunkCoords.X + dx, ChunkCoords.Y + dy, ChunkCoords.Z + dz);
                UVoxelChunk** NeighborChunk = ChunkMap.Find(NeighborCoords);
                if (NeighborChunk && *NeighborChunk)
                {
                    FVector WorldPos = FVoxelConversion::ChunkToWorld(NeighborCoords);
                    float Distance = FVector::Dist(Position, WorldPos);
                    if (Distance < MinDistance)
                    {
                        MinDistance = Distance;
                        NearestChunk = *NeighborChunk;
                    }
                }
            }
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

        FIntVector ChunkCoords = Chunk->GetChunkCoords();

        for (const auto& VoxelPair : Grid->GetVoxelDataRef())
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
                    TEXT("[Digger] NEGATIVE OVERFLOW DETECTED: Global %s -> Chunk %s -> Local %s"),
                    *GlobalIndex.ToString(), *ChunkCoords.ToString(), *LocalIndex.ToString());
            }
        }
    }

    if (DiggerDebug::Islands())
    {
        UE_LOG(LogTemp, Warning, TEXT("[Digger] Total physical voxel instances collected: %d"), AllPhysicalVoxelInstances.Num());
        UE_LOG(LogTemp, Warning, TEXT("[Digger] Deduplicated voxels for island detection: %d"), UnifiedVoxelData.Num());
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
                        TEXT("[Digger] NEGATIVE OVERFLOW ADDED TO ISLAND: Global %s -> Chunk %s -> Local %s"),
                        *Instance.GlobalVoxel.ToString(), *Instance.ChunkCoords.ToString(), *Instance.LocalVoxel.ToString());
                }
            }
            else if (Instance.LocalVoxel.X == -1 || Instance.LocalVoxel.Y == -1 || Instance.LocalVoxel.Z == -1)
            {
                // Log negative overflow voxels that DON'T belong to any island
                if (DiggerDebug::Islands())
                UE_LOG(LogTemp, Error, 
                    TEXT("[Digger] ORPHANED NEGATIVE OVERFLOW: Global %s -> Chunk %s -> Local %s (not in any island)"),
                    *Instance.GlobalVoxel.ToString(), *Instance.ChunkCoords.ToString(), *Instance.LocalVoxel.ToString());
            }
        }
        
        FinalIslands.Add(EnhancedIsland);

        if (DiggerDebug::Islands())
        UE_LOG(LogTemp, Warning, 
            TEXT("[Digger] Island complete: %d unique voxels (UI) -> %d total physical instances (removal)"),
            EnhancedIsland.VoxelCount, EnhancedIsland.VoxelInstances.Num());
    }

    // All broadcasting is now handled by UDiggerIslandRuntimeSubsystem.
    // This function is pure data — it returns the island list without side effects.
    return FinalIslands;
}


// Main function: always works in chunk coordinates
UVoxelChunk* ADiggerManager::GetOrCreateChunkAtCoords(const FIntVector& ChunkCoords)
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

UVoxelChunk* ADiggerManager::GetChunkAtCoords(const FIntVector& ChunkCoords) const
{
    FVoxelConversion::InitFromConfig(ChunkSize, Subdivisions, TerrainGridSize, GetActorLocation());

    if (DiggerDebug::Chunks())
        UE_LOG(LogTemp, Warning, TEXT("➡️ Looking up chunk at coords: %s"), *ChunkCoords.ToString());

    if (UVoxelChunk* const* ExistingChunk = ChunkMap.Find(ChunkCoords))
    {
        if (DiggerDebug::Chunks())
            UE_LOG(LogTemp, Log, TEXT("Found existing chunk at position: %s"), *ChunkCoords.ToString());
        return *ExistingChunk;
    }

    // Chunk not found — dump available coords to help diagnose wrong target chunk in UI
    if (DiggerDebug::Chunks() || DiggerDebug::Error())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("GetChunkAtCoords: Chunk %s not found in ChunkMap. Available chunks (%d):"),
            *ChunkCoords.ToString(),
            ChunkMap.Num());

        for (const auto& Pair : ChunkMap)
        {
            UE_LOG(LogTemp, Warning, TEXT("  📦 %s (holes=%d)"),
                *Pair.Key.ToString(),
                Pair.Value ? Pair.Value->GetSpawnedHoleCount() : -1);
        }
    }

    return nullptr;
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
    return GetOrCreateChunkAtCoords(ChunkCoords);
}


void ADiggerManager::RemoveUnifiedIslandVoxels(const FIslandData& Island)
{
    int32 TotalRemoved = 0;
    TSet<UVoxelChunk*> ChunksToUpdate;

    if (DiggerDebug::Islands() || DiggerDebug::Voxels() || DiggerDebug::Chunks())
    UE_LOG(LogTemp, Warning, TEXT("[Digger] Starting unified island removal with %d pre-computed voxel instances"), Island.VoxelInstances.Num());
    
    // Group voxel instances by their storage chunks for efficient batch removal
    TMap<UVoxelChunk*, TArray<FIntVector>> VoxelsByChunk;
    
    // Simply iterate through the pre-computed voxel instances
    for (const FVoxelInstance& Instance : Island.VoxelInstances)
    {
        UVoxelChunk* Chunk = ChunkMap.FindRef(Instance.ChunkCoords);
        if (!Chunk || !Chunk->IsValidLowLevel()) 
        {
            if (DiggerDebug::Chunks() || DiggerDebug::Error())
            UE_LOG(LogTemp, Error, TEXT("[Digger] Chunk %s not found or invalid"), *Instance.ChunkCoords.ToString());
            continue;
        }
        
        USparseVoxelGrid* Grid = Chunk->GetSparseVoxelGrid();
        if (!Grid || !Grid->IsValidLowLevel()) 
        {
            if (DiggerDebug::Islands() || DiggerDebug::Voxels() || DiggerDebug::Chunks())
            UE_LOG(LogTemp, Warning, TEXT("[Digger] Grid for chunk %s not found or invalid"), *Instance.ChunkCoords.ToString());
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
                    TEXT("[Digger] NEGATIVE OVERFLOW QUEUED FOR REMOVAL: Global %s -> Chunk %s -> Local %s"),
                    *Instance.GlobalVoxel.ToString(), *Instance.ChunkCoords.ToString(), *Instance.LocalVoxel.ToString());
            }
        }
        else
        {
            if (DiggerDebug::Islands() || DiggerDebug::Voxels() || DiggerDebug::Chunks())
            UE_LOG(LogTemp, Warning, 
                TEXT("[Digger] Voxel not found at Local %s in chunk %s (Global %s)"),
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
                UE_LOG(LogTemp, Warning, TEXT("[Digger] Removing %d voxel instances from chunk at %s"), 
                    LocalVoxels.Num(), *Chunk->GetChunkCoords().ToString());
                
                // Count negative overflow voxels being removed
                int32 NegativeOverflowCount = 0;
                for (const FIntVector& LocalVoxel : LocalVoxels)
                {
                    if (LocalVoxel.X == -1 || LocalVoxel.Y == -1 || LocalVoxel.Z == -1)
                    {
                        NegativeOverflowCount++;
                        if (DiggerDebug::Islands() || DiggerDebug::Voxels() || DiggerDebug::Chunks())
                        UE_LOG(LogTemp, Warning, 
                            TEXT("[Digger] REMOVING NEGATIVE OVERFLOW: Local %s from chunk %s"),
                            *LocalVoxel.ToString(), *Chunk->GetChunkCoords().ToString());
                    }
                }
                
                if (NegativeOverflowCount > 0)
                {
                    if (DiggerDebug::Islands() || DiggerDebug::Voxels() || DiggerDebug::Chunks())
                    UE_LOG(LogTemp, Warning, 
                        TEXT("[Digger] About to remove %d negative overflow voxels from chunk %s"),
                        NegativeOverflowCount, *Chunk->GetChunkCoords().ToString());
                }
                    
                Grid->RemoveSpecifiedVoxels(LocalVoxels);
                TotalRemoved += LocalVoxels.Num();
                if (DiggerDebug::Islands() || DiggerDebug::Voxels() || DiggerDebug::Chunks())
                UE_LOG(LogTemp, Warning, 
                    TEXT("[Digger] Successfully removed %d voxels from chunk %s"),
                    LocalVoxels.Num(), *Chunk->GetChunkCoords().ToString());
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
        TEXT("[Digger] Unified island removal complete: %d voxel instances removed across %d chunks"),
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
                        TEXT("[Digger] Global voxel %s found in chunk %s at local %s"),
                        *GlobalVoxel.ToString(), *CandidateChunk.ToString(), *LocalVoxel.ToString());
                }
                else
                {
                    if (DiggerDebug::Chunks() || DiggerDebug::Voxels())
                    // Debug: Log when we don't find expected voxels
                    UE_LOG(LogTemp, VeryVerbose, 
                        TEXT("[Digger] Global voxel %s NOT found in chunk %s (would be local %s)"),
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
            TEXT("[Digger] CRITICAL: Global voxel %s was not found in any chunk! Canonical chunk: %s"),
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

#if WITH_EDITOR
void ADiggerManager::OnPreSaveWorld(UWorld* InWorld, FObjectPreSaveContext /*Context*/)
{
    // Only save when this manager belongs to the world being saved.
    if (InWorld != GetWorld()) return;

    // SaveAllChunks writes voxel data + hole data for every loaded chunk.
    // This ensures holes survive editor restarts and Ctrl+S captures everything.
    const int32 Saved = SaveAllChunks() ? 1 : 0;

    if (DiggerDebug::Chunks())
        UE_LOG(LogTemp, Log, TEXT("[Digger] OnPreSaveWorld: SaveAllChunks completed (%d)."), Saved);
}
#endif

void ADiggerManager::PostRegisterAllComponents()
{
    Super::PostRegisterAllComponents();
    FVoxelConversion::RefreshDiggerManager(GetWorld());

#if WITH_EDITOR
    // Bind the pre-save delegate once.
    if (GIsEditor && !PreSaveWorldHandle.IsValid())
    {
        PreSaveWorldHandle = FEditorDelegates::PreSaveWorldWithContext.AddWeakLambda(
            this, [this](UWorld* InWorld, FObjectPreSaveContext Context)
            {
                OnPreSaveWorld(InWorld, Context);
            });
    }

    // Trigger the editor load so holes and voxels appear after a restart.
    // bEditorInitDone guards against double-init across multiple
    // PostRegisterAllComponents calls on the same actor.
    if (GIsEditor && !IsRuntimeLike())
    {
        EditorDeferredInit();
    }
#endif
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

    // Drain bake queue — processes up to BakeChunksPerTick loaded chunks
    // per frame so BakeFullTerrain / EnqueueBakeAllLoadedChunks don't stall.
    DrainBakeQueue();
    
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
    // Process mesh-dirty chunks only — no collision cook here.
    // Collision is handled by the 0.1s ChunkUpdateTimerHandle which calls
    // ProcessDirtyChunksLoop directly (and passes the idle state correctly).
    // Running collision from Tick too would attempt cooks at 60fps.
    // Iterate a snapshot so we can safely remove during iteration.
    // Use a small inline array to avoid heap allocation on the hot Tick path
    // when only 1-3 chunks are dirty (the common case during painting).
    TArray<FIntVector, TInlineAllocator<8>> ToProcess(DirtyChunkCoords.Array());
    for (const FIntVector& Coords : ToProcess)
    {
        UVoxelChunk** ChunkPtr = ChunkMap.Find(Coords);
        if (!ChunkPtr || !*ChunkPtr)
        {
            DirtyChunkCoords.Remove(Coords); // stale entry
            continue;
        }
        UVoxelChunk* Chunk = *ChunkPtr;
        Chunk->UpdateIfDirty();
        if (!Chunk->IsDirty())
            DirtyChunkCoords.Remove(Coords);
    }

    // 2. Process Height Cache Queue (Background Work)
    if (HeightCacheSystem)
    {
        HeightCacheSystem->TickProcessQueue();
    }
}



void ADiggerManager::CreateHoleAt(FVector WorldPosition, FRotator Rotation, FVector Scale, TSubclassOf<AActor> InDynamicHoleClass)
{
    FIntVector ChunkCoords = FVoxelConversion::WorldToChunk(WorldPosition);
    if (UVoxelChunk* Chunk = GetOrCreateChunkAtCoords(ChunkCoords))
    {
        //ToDo: set shape type based on brush used!
        Chunk->SpawnHole(DynamicHoleClass, WorldPosition, Rotation, Scale, EHoleShapeType::Sphere);
    }
}

bool ADiggerManager::RemoveHoleNear(FVector WorldPosition, float MaxDistance)
{
    FIntVector ChunkCoords = FVoxelConversion::WorldToChunk(WorldPosition);
    if (UVoxelChunk* Chunk = GetOrCreateChunkAtCoords(ChunkCoords))
    {
        return Chunk->RemoveNearestHole(WorldPosition, MaxDistance);
    }
    return false;
}

void ADiggerManager::GetAllHoleActors(TArray<AActor*>& OutHoles) const
{
    OutHoles.Reset();

    for (const TPair<FIntVector, UVoxelChunk*>& Pair : ChunkMap)
    {
        UVoxelChunk* Chunk = Pair.Value;
        if (!Chunk) continue;

        const TArray<TWeakObjectPtr<ADynamicHole>>& Holes = Chunk->GetSpawnedHoles();
        for (const TWeakObjectPtr<ADynamicHole>& HolePtr : Holes)
        {
            if (HolePtr.IsValid())
            {
                OutHoles.Add(Cast<AActor>(HolePtr.Get()));
            }
        }
    }
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

// =============================================================================
// DIGGER HISTORY — UNDO / REDO / TIMELINE
// =============================================================================


// -----------------------------------------------------------------------------
// UID REGISTRY
// Every chunk-owned actor (hole, light, island) gets a unique ID that is stable
// for the lifetime of the editor session.  Undo looks actors up by UID, which
// is resilient to array reordering and out-of-band destruction.
// -----------------------------------------------------------------------------

int32 ADiggerManager::AllocateActorUID()
{
    return NextActorUID++;
}

void ADiggerManager::RegisterActorUID(int32 UID, AActor* Actor)
{
    if (UID == INDEX_NONE || !IsValid(Actor)) return;
    ActorRegistry.Add(UID, Actor);
}

void ADiggerManager::UnregisterActorUID(int32 UID)
{
    ActorRegistry.Remove(UID);
}

AActor* ADiggerManager::FindActorByUID(int32 UID) const
{
    const TWeakObjectPtr<AActor>* Found = ActorRegistry.Find(UID);
    if (!Found) return nullptr;
    return Found->Get();
}


// -----------------------------------------------------------------------------
// EnsurePendingAction
// All recording paths call this first.  Opens a pending action with the given
// label if one is not already open.  Safe to call any number of times.
// -----------------------------------------------------------------------------

void ADiggerManager::EnsurePendingAction(const FString& Label)
{
    if (bHasPendingAction) return;
    PendingAction     = FDiggerHistoryAction(Label, NextActionID++);
    bHasPendingAction = true;
}


// -----------------------------------------------------------------------------
// BeginStroke
// Called by EdMode::StartTracking (mouse-down) and by any tool that opens a
// discrete stroke bracket.  Equivalent to EnsurePendingAction but public.
// -----------------------------------------------------------------------------

void ADiggerManager::BeginStroke(const FString& Label)
{
    if (!bHistoryEnabled) return;
    EnsurePendingAction(Label);
}


// -----------------------------------------------------------------------------
// RecordChunkAction
// Called by UVoxelChunk::ApplyBrushStroke once per chunk per tick.
// Accumulates into PendingAction; committed by CommitPendingAction().
// -----------------------------------------------------------------------------

void ADiggerManager::RecordChunkAction(FDiggerChunkAction&& ChunkAction)
{
    if (!bHistoryEnabled) return;
    if (ChunkAction.Deltas.Num() == 0) return;

    FString Label;
    switch (ChunkAction.SourceStroke.BrushType)
    {
        case EVoxelBrushType::Smooth: Label = TEXT("Smooth"); break;
        case EVoxelBrushType::Noise:  Label = TEXT("Noise");  break;
        default: Label = ChunkAction.SourceStroke.bDig ? TEXT("Dig") : TEXT("Add"); break;
    }
    EnsurePendingAction(Label);

    FDiggerChunkAction& Dest = PendingAction.GetOrAddChunkAction(ChunkAction.ChunkCoords);
    Dest.SourceStroke = ChunkAction.SourceStroke;
    Dest.Deltas.Append(ChunkAction.Deltas);

    FDiggerHistory& History = ChunkHistories.FindOrAdd(ChunkAction.ChunkCoords);
    History.AppendAction(ChunkAction);

    if (bAutoCommitEveryTick)
        CommitPendingAction();
}


// -----------------------------------------------------------------------------
// NotifyHoleAddedForHistory
// Called by HandleHoleSpawn after a hole actor is spawned.  Records a spawn
// record keyed by UID so undo can destroy it by UID lookup.
// Signature changed: now takes HoleUID so the record is authoritative.
// -----------------------------------------------------------------------------

void ADiggerManager::NotifyHoleAddedForHistory(const FIntVector& ChunkCoords,
                                               const FSpawnedHoleData& HoleData,
                                               int32 HoleUID)
{
    if (!bHistoryEnabled) return;

    // If no stroke is open (e.g. hole placed with dedicated RMB click outside
    // a voxel stroke), open a hole-only action and commit it immediately so
    // it becomes a single discrete undo step.
    const bool bWasPending = bHasPendingAction;
    EnsurePendingAction(TEXT("Place Hole"));

    FDiggerActorSpawnRecord Rec;
    Rec.ActorType         = EDiggerActorType::Hole;
    Rec.ActorUID          = HoleUID;
    Rec.OwningChunkCoords = ChunkCoords;
    Rec.HoleData          = HoleData;
    if (AActor* Actor = FindActorByUID(HoleUID))
        Rec.SpawnTransform = Actor->GetActorTransform();
    PendingAction.SpawnedActors.Add(MoveTemp(Rec));

    // Also track the UID on the per-chunk action (used by audit replay).
    // Full FSpawnedHoleData is already on PendingAction.SpawnedActors above.
    FDiggerChunkAction& Dest = PendingAction.GetOrAddChunkAction(ChunkCoords);
    Dest.HoleUIDsAdded.Add(HoleUID);

    // If the hole was placed outside a voxel stroke, commit immediately so
    // placing a single hole is one Ctrl+Z step.
    if (!bWasPending)
        CommitPendingAction();
}


// -----------------------------------------------------------------------------
// @deprecated  SnapshotHoleCount
// Kept so existing call-sites compile without changes.  No-op in new code.
// -----------------------------------------------------------------------------

void ADiggerManager::SnapshotHoleCount(const FIntVector& /*ChunkCoords*/, int32 /*CountBefore*/)
{
    // No-op: replaced by UID-based spawn records.
}


// -----------------------------------------------------------------------------
// RecordActorMove
// Called from PostEditMove(true) on DynamicHole, DynamicLightActor, IslandActor.
// Each move is committed as its own single-step undo action so Ctrl+Z undoes
// exactly one actor relocation at a time.
// -----------------------------------------------------------------------------

void ADiggerManager::RecordActorMove(FDiggerActorMoveRecord&& Record)
{
    if (!bHistoryEnabled) return;

    // A move is always a standalone undo step — don't merge it into an open
    // voxel stroke.  If a stroke is already open, commit it first.
    if (bHasPendingAction)
        CommitPendingAction();

    EnsurePendingAction(TEXT("Move"));
    PendingAction.MovedActors.Add(MoveTemp(Record));
    CommitPendingAction();
}


// -----------------------------------------------------------------------------
// RecordActorDestroy
// Called when a chunk-owned actor is explicitly deleted.
// -----------------------------------------------------------------------------

void ADiggerManager::RecordActorDestroy(FDiggerActorDestroyRecord&& Record)
{
    if (!bHistoryEnabled) return;

    if (bHasPendingAction)
        CommitPendingAction();

    EnsurePendingAction(TEXT("Delete"));
    PendingAction.DestroyedActors.Add(MoveTemp(Record));
    CommitPendingAction();
}


// -----------------------------------------------------------------------------
// CommitPendingAction
// Seals the in-progress stroke as one undoable step on the undo stack.
// Call on mouse-up / stroke-end.
// -----------------------------------------------------------------------------

void ADiggerManager::CommitPendingAction()
{
    if (!bHasPendingAction || PendingAction.IsEmpty())
    {
        bHasPendingAction = false;
        return;
    }

    RedoStack.Empty();
    UndoStack.Push(MoveTemp(PendingAction));
    PendingAction     = FDiggerHistoryAction();
    bHasPendingAction = false;

    TrimUndoStack();
}


// -----------------------------------------------------------------------------
// Undo / Redo
// -----------------------------------------------------------------------------

void ADiggerManager::BakeFullTerrain()
{
    // BakeFullTerrain now delegates to the queue so the editor stays
    // responsive — chunks bake one-per-tick rather than all at once.
    EnqueueBakeAllLoadedChunks();
}

// =============================================================================
// HOLE ACTOR POOL
// =============================================================================
//
// Eliminates SpawnActor cost on every brush stroke by reusing a fixed set of
// pre-allocated ADynamicHole actors.  Actors are hidden (SetActorHiddenInGame)
// and have collision disabled when idle; AcquireHoleFromPool re-enables both.
//
// Pool grows automatically when empty so there is no hard cap on hole count —
// the cap is just how many actors we keep warm between strokes.

ADynamicHole* ADiggerManager::AcquireHoleFromPool(TSubclassOf<AActor> HoleClass)
{
    if (!HoleClass) return nullptr;

    World = GetSafeWorld();
    if (!World) return nullptr;

    // If the class changed (e.g. designer swapped BP), flush stale pool.
    if (PooledHoleClass != HoleClass)
    {
        FlushHolePool();
        PooledHoleClass = HoleClass;
    }

    // Grow pool if empty.
    if (HolePool.IsEmpty())
    {
        const int32 Grow = FMath::Max(1, HolePoolSize);
        HolePool.Reserve(HolePool.Num() + Grow);
        for (int32 i = 0; i < Grow; ++i)
        {
            FActorSpawnParameters P;
            P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            P.ObjectFlags = RF_Transient;
            P.bHideFromSceneOutliner = true;

            ADynamicHole* NewHole = World->SpawnActor<ADynamicHole>(
                HoleClass, FVector::ZeroVector, FRotator::ZeroRotator, P);
            if (NewHole)
            {
                NewHole->SetActorHiddenInGame(true);
                NewHole->SetActorEnableCollision(false);
                HolePool.Add(NewHole);
            }
        }
    }

    if (HolePool.IsEmpty()) return nullptr;

    ADynamicHole* Hole = HolePool.Pop(/*bAllowShrinking=*/false);
    if (!IsValid(Hole))
    {
        // Stale pointer — try again recursively (at most once since we just grew).
        return AcquireHoleFromPool(HoleClass);
    }

    Hole->SetActorHiddenInGame(false);
    Hole->SetActorEnableCollision(true);
    return Hole;
}

void ADiggerManager::ReturnHoleToPool(ADynamicHole* Hole)
{
    if (!IsValid(Hole)) return;

    Hole->SetActorHiddenInGame(true);
    Hole->SetActorEnableCollision(false);
    Hole->SetOwningChunk(nullptr);
    Hole->HoleUID = INDEX_NONE;

    // Move far out of the world so ContainsPoint / overlap queries can never
    // accidentally match this pooled actor against real world positions.
    // IsInsideHole also guards on IsHidden(), but defence in depth is cheap.
    Hole->SetActorLocation(FVector(0.f, 0.f, -9999999.f));

    HolePool.AddUnique(Hole);
}

void ADiggerManager::PrewarmHolePool(int32 Count)
{
    const int32 Target = (Count > 0) ? Count : HolePoolSize;
    const int32 ToSpawn = FMath::Max(0, Target - HolePool.Num());
    if (ToSpawn == 0) return;

    TSubclassOf<AActor> HoleClass = GetDynamicHoleClass();
    if (!HoleClass)
    {
        if (const UDiggerSettings* S = UDiggerSettings::Get())
            HoleClass = S->DefaultHoleActorClass.LoadSynchronous();
    }
    if (!HoleClass) return;

    // Trigger pool growth by acquiring then immediately returning.
    TArray<ADynamicHole*> Temp;
    Temp.Reserve(ToSpawn);
    for (int32 i = 0; i < ToSpawn; ++i)
        if (ADynamicHole* H = AcquireHoleFromPool(HoleClass))
            Temp.Add(H);
    for (ADynamicHole* H : Temp)
        ReturnHoleToPool(H);

    UE_LOG(LogTemp, Log, TEXT("Digger: Hole pool warmed to %d actors."), HolePool.Num());
}

void ADiggerManager::FlushHolePool()
{
    for (ADynamicHole* Hole : HolePool)
    {
        if (IsValid(Hole))
            Hole->Destroy();
    }
    HolePool.Empty();
    PooledHoleClass = nullptr;
}

// =============================================================================
// BAKED HOLES — INSTANCED STATIC MESH
// =============================================================================
//
// Replaces N individual DynamicHole actors with a few ISM components (one per
// shape type, max 7).  ISM extends UStaticMeshComponent — full RVT support.
// Each ISM instances all holes of that shape in 1-2 draw calls.
//
// No geometry extraction, no mesh building, no distance-field rebuilds.
// Just AddInstance(Transform) per hole from the existing HoleDataArray.
//
// New holes created after a bake coexist as individual DynamicHole actors.
// The next scheduled bake absorbs them.  No unbake-on-dig.
// =============================================================================

void ADiggerManager::BakeStableHoles()
{
    // 1. Resolve dependencies.
    EnsureHoleShapeLibrary();
    if (!HoleShapeLibrary)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Digger] BakeStableHoles: No HoleShapeLibrary — skipped."));
        return;
    }

    if (!HoleWriterMaterial)
    {
        HoleWriterMaterial = Cast<UMaterialInterface>(StaticLoadObject(
            UMaterialInterface::StaticClass(), nullptr,
            TEXT("/Digger/Digger/Materials/LandscapeMaterials/M_OpacityMask.M_OpacityMask")));
    }
    if (!HoleWriterMaterial)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Digger] BakeStableHoles: M_OpacityMask not found — skipped."));
        return;
    }

#if WITH_EDITORONLY_DATA
    URuntimeVirtualTexture* RVT = ResolveDiggerRVT();
    if (!RVT)
    {
        UE_LOG(LogTemp, Warning, TEXT("[Digger] BakeStableHoles: No RVT resolved — skipped."));
        return;
    }
#endif

    // 2. Clear all existing ISM instances (rebuild from scratch each bake).
    for (auto& Pair : BakedHoleISMs)
    {
        if (Pair.Value)
            Pair.Value->ClearInstances();
    }

    // 3. Iterate ALL hole data from all chunks and add ISM instances.
    int32 TotalBaked = 0;
    int32 ShapeMissing = 0;

    for (auto& ChunkPair : ChunkMap)
    {
        UVoxelChunk* Chunk = ChunkPair.Value;
        if (!Chunk) continue;

        for (const FSpawnedHoleData& Data : Chunk->HoleDataArray)
        {
            const EHoleShapeType ShapeType = Data.Shape.ShapeType;

            // Get or create ISM for this shape type.
            UInstancedStaticMeshComponent*& ISM = BakedHoleISMs.FindOrAdd(ShapeType);
            if (!ISM)
            {
                UStaticMesh* ShapeMesh = HoleShapeLibrary->GetMeshForShape(ShapeType);
                if (!ShapeMesh)
                {
                    ++ShapeMissing;
                    continue;
                }

                ISM = NewObject<UInstancedStaticMeshComponent>(this);
                ISM->SetupAttachment(RootComponent);
                ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                ISM->SetCastShadow(false);
                ISM->SetStaticMesh(ShapeMesh);
                ISM->SetMaterial(0, HoleWriterMaterial);
                ISM->NumCustomDataFloats = 0;

#if WITH_EDITORONLY_DATA
                ISM->RuntimeVirtualTextures.Empty();
                ISM->RuntimeVirtualTextures.Add(RVT);
                ISM->VirtualTextureRenderPassType =
                    ERuntimeVirtualTextureMainPassType::Never;
#endif

                ISM->RegisterComponent();
                ISM->MarkRenderStateDirty();

                UE_LOG(LogTemp, Log, TEXT("[Digger] Created ISM for shape %d with mesh '%s'."),
                    (int32)ShapeType, *ShapeMesh->GetName());
            }

            // Add instance at the hole's world transform.
            const FTransform HoleXForm(Data.Rotation, Data.Location, Data.Scale);
            ISM->AddInstance(HoleXForm, /*bWorldSpace=*/true);
            ++TotalBaked;
        }
    }

    if (TotalBaked == 0)
    {
        UE_LOG(LogTemp, Log, TEXT("[Digger] BakeStableHoles: 0 holes to bake."));
        bHolesBaked = false;
        return;
    }

    // 4. Return all individual hole actors to the pool.
    int32 ActorsPooled = 0;
    for (auto& ChunkPair : ChunkMap)
    {
        UVoxelChunk* Chunk = ChunkPair.Value;
        if (!Chunk) continue;

        for (const TWeakObjectPtr<ADynamicHole>& HolePtr : Chunk->GetSpawnedHoles())
        {
            ADynamicHole* Hole = HolePtr.Get();
            if (!Hole || Hole->IsHidden()) continue;

            if (Hole->HoleUID != INDEX_NONE)
                UnregisterActorUID(Hole->HoleUID);
            ReturnHoleToPool(Hole);
            ++ActorsPooled;
        }
        Chunk->GetSpawnedHolesArray().Empty();
    }

    bHolesBaked = true;

    UE_LOG(LogTemp, Log,
        TEXT("[Digger] Baked %d holes into %d ISM components, pooled %d actors.%s"),
        TotalBaked, BakedHoleISMs.Num(), ActorsPooled,
        ShapeMissing > 0 ? *FString::Printf(TEXT(" (%d shapes missing mesh)"), ShapeMissing) : TEXT(""));
}

void ADiggerManager::UnbakeHoles()
{
    if (!bHolesBaked) return;

    // Clear all ISM instances.
    for (auto& Pair : BakedHoleISMs)
    {
        if (Pair.Value)
            Pair.Value->ClearInstances();
    }

    // Re-spawn individual hole actors from each chunk's HoleDataArray.
    for (auto& Pair : ChunkMap)
    {
        UVoxelChunk* Chunk = Pair.Value;
        if (!Chunk) continue;
        Chunk->RegenerateHolesFromData();
    }

    bHolesBaked = false;
    UE_LOG(LogTemp, Log, TEXT("[Digger] Unbaked holes — individual actors restored."));
}

void ADiggerManager::EnqueueBakeAllLoadedChunks()
{
    // Only enqueue chunks that already exist in ChunkMap (lazy-loaded).
    // Unvisited terrain is left alone — no point baking land never sculpted.
    ChunkBakeQueue.Empty();
    for (const auto& Pair : ChunkMap)
    {
        if (Pair.Value) // null-guard
            ChunkBakeQueue.Add(Pair.Key);
    }
    UE_LOG(LogTemp, Log, TEXT("Digger: Enqueued %d loaded chunks for bake (%d per tick)."),
           ChunkBakeQueue.Num(), BakeChunksPerTick);
}

void ADiggerManager::DrainBakeQueue()
{
    if (ChunkBakeQueue.IsEmpty()) return;

    const bool bWasFull = bBakeFullChunkVolume;
    bBakeFullChunkVolume = true;

    const int32 DrainCount = FMath::Min(BakeChunksPerTick, ChunkBakeQueue.Num());
    for (int32 i = 0; i < DrainCount; ++i)
    {
        const FIntVector Coords = ChunkBakeQueue.Pop(/*bAllowShrinking=*/false);
        if (UVoxelChunk* Chunk = GetOrCreateChunkAtCoords(Coords))
            Chunk->MarkDirty();
    }

    bBakeFullChunkVolume = bWasFull;
}

void ADiggerManager::BakeTargetChunk()
{
    const bool bWasFull = bBakeFullChunkVolume;
    bBakeFullChunkVolume = true;

    if (UVoxelChunk* Chunk = GetOrCreateChunkAtCoords(BakeTargetCoords))
        Chunk->MarkDirty();

    bBakeFullChunkVolume = bWasFull;
}

void ADiggerManager::BakeChunkAtWorldPosition(FVector WorldPos)
{
    BakeTargetCoords = FVoxelConversion::WorldToChunk(WorldPos);
    BakeTargetChunk();
}

void ADiggerManager::Undo()
{
    if (!bHistoryEnabled || UndoStack.Num() == 0) return;

    // Commit anything in-flight first (shouldn't normally be open during Undo,
    // but guard against it).
    if (bHasPendingAction)
        CommitPendingAction();

    FDiggerHistoryAction Action = UndoStack.Pop();
    ApplyHistoryAction(Action, /*bApplyNew=*/false);
    RedoStack.Push(MoveTemp(Action));
}

void ADiggerManager::Redo()
{
    if (!bHistoryEnabled || RedoStack.Num() == 0) return;

    if (bHasPendingAction)
        CommitPendingAction();

    FDiggerHistoryAction Action = RedoStack.Pop();
    ApplyHistoryAction(Action, /*bApplyNew=*/true);
    UndoStack.Push(MoveTemp(Action));
}


// -----------------------------------------------------------------------------
// ApplyHistoryAction
// Applies voxel deltas AND actor records for one history action.
// bApplyNew=true → Redo, false → Undo.
// -----------------------------------------------------------------------------

void ADiggerManager::ApplyHistoryAction(const FDiggerHistoryAction& Action, bool bApplyNew)
{
    // ------------------------------------------------------------------
    // 1. VOXEL DELTAS (per chunk)
    // ------------------------------------------------------------------
    for (const FDiggerChunkAction& ChunkAction : Action.ChunkActions)
    {
        UVoxelChunk** ChunkPtr = ChunkMap.Find(ChunkAction.ChunkCoords);
        if (!ChunkPtr || !IsValid(*ChunkPtr))
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[History] Chunk %s not found during %s — skipping."),
                *ChunkAction.ChunkCoords.ToString(),
                bApplyNew ? TEXT("Redo") : TEXT("Undo"));
            continue;
        }
        (*ChunkPtr)->ApplyChunkAction(ChunkAction, bApplyNew, /*bRecordForUndo=*/false);
    }

    // ------------------------------------------------------------------
    // 2. ACTOR SPAWNS
    //    Undo → destroy the actor.
    //    Redo → respawn from serialised data.
    // ------------------------------------------------------------------
    for (const FDiggerActorSpawnRecord& Rec : Action.SpawnedActors)
    {
        if (!bApplyNew)
        {
            // UNDO spawn: destroy the actor
            AActor* Actor = FindActorByUID(Rec.ActorUID);
            if (IsValid(Actor))
            {
                UnregisterActorUID(Rec.ActorUID);
                Actor->Destroy();
            }

            // For holes: also clean up the chunk's HoleDataArray / SpawnedHoleInstances
            if (Rec.ActorType == EDiggerActorType::Hole)
            {
                UVoxelChunk** ChunkPtr = ChunkMap.Find(Rec.OwningChunkCoords);
                if (ChunkPtr && IsValid(*ChunkPtr))
                    (*ChunkPtr)->DestroyHoleByUID(Rec.ActorUID);
            }
        }
        else
        {
            // REDO spawn: recreate from data
            if (Rec.ActorType == EDiggerActorType::Hole)
            {
                UVoxelChunk** ChunkPtr = ChunkMap.Find(Rec.OwningChunkCoords);
                if (ChunkPtr && IsValid(*ChunkPtr))
                {
                    // SpawnHoleFromData allocates a new UID internally;
                    // we need to force the original UID back.
                    (*ChunkPtr)->SpawnHoleFromData(Rec.HoleData, Rec.ActorUID);
                }
            }
            else if (Rec.ActorType == EDiggerActorType::Light)
            {
                // Known limitation: light redo requires FBrushStroke serialisation
                // in FDiggerActorSpawnRecord, which is not yet implemented.
                // For now, light redo is a no-op.
                UE_LOG(LogTemp, Warning,
                    TEXT("[History] Redo of light spawn not yet implemented."));
            }
            // Islands: redo is a no-op (geometry must be re-extracted by user).
        }
    }

    // ------------------------------------------------------------------
    // 3. ACTOR MOVES
    //    Undo → apply PreTransform (and transfer chunk if needed).
    //    Redo → apply PostTransform.
    // ------------------------------------------------------------------
    for (const FDiggerActorMoveRecord& Rec : Action.MovedActors)
    {
        AActor* Actor = FindActorByUID(Rec.ActorUID);
        if (!IsValid(Actor)) continue;

        const FTransform& TargetTransform  = bApplyNew ? Rec.PostTransform : Rec.PreTransform;
        const FIntVector&  TargetChunk     = bApplyNew ? Rec.NewChunkCoords : Rec.OldChunkCoords;
        const FIntVector&  PrevChunk       = bApplyNew ? Rec.OldChunkCoords : Rec.NewChunkCoords;

        Actor->SetActorTransform(TargetTransform);

        // Re-assign chunk ownership if the move crossed a boundary
        if (TargetChunk != PrevChunk)
        {
            if (Rec.ActorType == EDiggerActorType::Hole)
            {
                ADynamicHole* Hole = Cast<ADynamicHole>(Actor);
                if (Hole)
                {
                    UVoxelChunk** OldChunkPtr = ChunkMap.Find(PrevChunk);
                    if (OldChunkPtr && IsValid(*OldChunkPtr))
                        (*OldChunkPtr)->RemoveHoleFromChunk(Hole);

                    UVoxelChunk** NewChunkPtr = ChunkMap.Find(TargetChunk);
                    if (NewChunkPtr && IsValid(*NewChunkPtr))
                    {
                        (*NewChunkPtr)->AddHoleToChunk(Hole);
                        Hole->SetOwningChunk(*NewChunkPtr);
                    }
                }
            }
            else if (Rec.ActorType == EDiggerActorType::Light)
            {
                ADynamicLightActor* Light = Cast<ADynamicLightActor>(Actor);
                if (Light)
                {
                    UVoxelChunk** NewChunkPtr = ChunkMap.Find(TargetChunk);
                    Light->SetOwningChunk(NewChunkPtr ? *NewChunkPtr : nullptr);
                }
            }
            else if (Rec.ActorType == EDiggerActorType::Island)
            {
                AIslandActor* Island = Cast<AIslandActor>(Actor);
                if (Island)
                {
                    UVoxelChunk** NewChunkPtr = ChunkMap.Find(TargetChunk);
                    Island->SetOwningChunk(NewChunkPtr ? *NewChunkPtr : nullptr);
                }
            }
        }
    }

    // ------------------------------------------------------------------
    // 4. ACTOR DESTROYS
    //    Undo → respawn.
    //    Redo → destroy again.
    // ------------------------------------------------------------------
    for (const FDiggerActorDestroyRecord& Rec : Action.DestroyedActors)
    {
        if (!bApplyNew)
        {
            // UNDO destroy: respawn
            if (Rec.ActorType == EDiggerActorType::Hole)
            {
                UVoxelChunk** ChunkPtr = ChunkMap.Find(Rec.OwningChunkCoords);
                if (ChunkPtr && IsValid(*ChunkPtr))
                    (*ChunkPtr)->SpawnHoleFromData(Rec.HoleData, Rec.ActorUID);
            }
        }
        else
        {
            // REDO destroy: destroy
            AActor* Actor = FindActorByUID(Rec.ActorUID);
            if (IsValid(Actor))
            {
                UnregisterActorUID(Rec.ActorUID);
                Actor->Destroy();
            }
        }
    }
}


// -----------------------------------------------------------------------------
// TrimUndoStack
// -----------------------------------------------------------------------------

void ADiggerManager::TrimUndoStack()
{
    if (MaxUndoHistory <= 0) return;
    while (UndoStack.Num() > MaxUndoHistory)
        UndoStack.RemoveAt(0); // remove oldest (bottom of stack)
}


// =============================================================================
// TIMELINE — ZBrush-style sculpt history replay
// =============================================================================

void ADiggerManager::ReplayChunkHistoryOnto(FIntVector SourceChunkCoords,
                                             FIntVector TargetChunkCoords,
                                             bool bRecordForUndo)
{
    if (SourceChunkCoords == TargetChunkCoords) return;

    const FDiggerHistory* SourceHistory = ChunkHistories.Find(SourceChunkCoords);
    if (!SourceHistory || SourceHistory->Actions.Num() == 0)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[History] ReplayChunkHistoryOnto: no history for source chunk %s."),
            *SourceChunkCoords.ToString());
        return;
    }

    UVoxelChunk** TargetPtr = ChunkMap.Find(TargetChunkCoords);
    if (!TargetPtr || !IsValid(*TargetPtr))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[History] ReplayChunkHistoryOnto: target chunk %s not found."),
            *TargetChunkCoords.ToString());
        return;
    }

    // Voxel-space offset between source and target.
    const int32 VoxelsPerChunk = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
    const FIntVector ChunkDelta  = TargetChunkCoords - SourceChunkCoords;
    const FIntVector VoxelOffset = FIntVector(
        ChunkDelta.X * VoxelsPerChunk,
        ChunkDelta.Y * VoxelsPerChunk,
        ChunkDelta.Z * VoxelsPerChunk);

    for (const FDiggerChunkAction& SrcAction : SourceHistory->Actions)
        ReplayActionOnChunk(SrcAction, TargetChunkCoords, VoxelOffset, bRecordForUndo);

    if (bRecordForUndo)
        CommitPendingAction();

    if (DiggerDebug::Voxels())
        UE_LOG(LogTemp, Log,
            TEXT("[History] Replayed %d actions from %s onto %s."),
            SourceHistory->Actions.Num(),
            *SourceChunkCoords.ToString(),
            *TargetChunkCoords.ToString());
}


void ADiggerManager::ReplayActionOnChunk(const FDiggerChunkAction& Action,
                                          const FIntVector& TargetChunkCoords,
                                          const FIntVector& VoxelOffset,
                                          bool bRecordForUndo)
{
    UVoxelChunk** TargetPtr = ChunkMap.Find(TargetChunkCoords);
    if (!TargetPtr || !IsValid(*TargetPtr)) return;

    const int32 VoxelsPerChunk = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;

    // Build a translated copy of the action for the target chunk.
    FDiggerChunkAction TranslatedAction(TargetChunkCoords);
    TranslatedAction.SourceStroke = Action.SourceStroke;

    // Translate the stroke world position so visual feedback (brush sphere) is correct.
    const FVector SourceWorld = FVoxelConversion::ChunkToWorld(Action.ChunkCoords);
    const FVector TargetWorld = FVoxelConversion::ChunkToWorld(TargetChunkCoords);
    TranslatedAction.SourceStroke.BrushPosition += (TargetWorld - SourceWorld);

    for (const FVoxelDelta& Delta : Action.Deltas)
    {
        const FIntVector TranslatedCoord = Delta.VoxelCoord + VoxelOffset;

        // Drop deltas that translate outside the target chunk's voxel range.
        if (TranslatedCoord.X < 0 || TranslatedCoord.X >= VoxelsPerChunk ||
            TranslatedCoord.Y < 0 || TranslatedCoord.Y >= VoxelsPerChunk ||
            TranslatedCoord.Z < 0 || TranslatedCoord.Z >= VoxelsPerChunk)
            continue;

        TranslatedAction.AddDelta(TranslatedCoord,
            Delta.OldSDF, Delta.NewSDF, Delta.bWasWritten);
    }

    if (TranslatedAction.Deltas.Num() == 0) return;

    // ApplyChunkAction with bRecordForUndo writes the deltas and, when
    // bRecordForUndo is true, calls RecordChunkAction so the replay ends up
    // on the undo stack via CommitPendingAction().
    (*TargetPtr)->ApplyChunkAction(TranslatedAction, /*bApplyNew=*/true, bRecordForUndo);
}