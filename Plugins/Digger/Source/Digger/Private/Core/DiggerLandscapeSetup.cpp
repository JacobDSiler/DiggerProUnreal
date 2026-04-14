// ============================================================================
// DiggerLandscapeSetup.cpp
// ============================================================================
// Landscape setup implementation for the Digger plugin.
// Handles RVT creation, volume placement, material injection, and preflight.
//
// Extracted from DiggerManager.cpp to keep the manager class focused on
// core voxel/mesh operations. All functions are still members of
// ADiggerManager but live in this translation unit.
// ============================================================================

#include "DiggerManager.h"
#include "CoreMinimal.h"

// Engine
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"

// Materials
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialAttributeDefinitionMap.h"
#include "Materials/MaterialExpressionMakeMaterialAttributes.h"
#include "Materials/MaterialExpressionRuntimeVirtualTextureSampleParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"

// Landscape
#include "LandscapeProxy.h"
#include "LandscapeInfo.h"

// RVT
#include "Components/RuntimeVirtualTextureComponent.h"
#include "VT/RuntimeVirtualTextureVolume.h"

// Digger
#include "DynamicHole.h"
#include "DynamicHolesHelpers.h"

// Notifications
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

// Editor-only
#if WITH_EDITOR
    #include "Editor.h"
    #include "LandscapeEdit.h"
    #include "Landscape.h"
    #include "LandscapeComponent.h"
    #include "AssetToolsModule.h"
    #include "IAssetTools.h"
    #include "AssetRegistry/AssetRegistryModule.h"
    #include "FileHelpers.h"
    #include "Components/RuntimeVirtualTextureComponent.h"
    #include "VT/RuntimeVirtualTexture.h"
    #include "Materials/MaterialExpressionRuntimeVirtualTextureSample.h"
    #include "Materials/MaterialExpressionSetMaterialAttributes.h"
    #include "Materials/MaterialExpressionGetMaterialAttributes.h"
    #include "Materials/MaterialExpressionMultiply.h"
    #include "Materials/MaterialExpressionOneMinus.h"
    #include "Materials/MaterialExpressionConstant.h"
    #include "Materials/MaterialExpressionClamp.h"
    #include "Materials/MaterialFunctionInstance.h"
#endif



// ─────────────────────────────────────────────────────────────────────────────
// AUTO SETUP — Pre-flight check items.
// ─────────────────────────────────────────────────────────────────────────────
#if WITH_EDITOR
ADiggerManager::FDiggerSetupPreflightResult ADiggerManager::RunSetupPreflight()
{
    FDiggerSetupPreflightResult Result;
    UWorld* W = GetSafeWorld();
    if (!W) return Result;

    // ── Landscape ────────────────────────────────────────────────────────────
    TArray<ALandscapeProxy*> Proxies;
    for (TActorIterator<ALandscapeProxy> It(W); It; ++It)
        if (*It) Proxies.Add(*It);

    Result.LandscapeProxyCount = Proxies.Num();
    Result.bHasLandscape       = Proxies.Num() > 0;

    if (!Result.bHasLandscape)
    {
        Result.Warnings.Add(
            TEXT("No landscape found. Add a landscape actor to the scene first."));
        return Result;
    }

    // ── Per-proxy checks ─────────────────────────────────────────────────────
    bool bAllMasked          = true;
    bool bAnyHasMFSetAttribs = false;

    auto IsDiggerInjected = [](UMaterialInterface* Mat) -> bool
    {
        if (!Mat) return false;
        TArray<FMaterialParameterInfo> ScalarInfos;
        TArray<FGuid> Guids;
        Mat->GetAllScalarParameterInfo(ScalarInfos, Guids);
        for (const FMaterialParameterInfo& Info : ScalarInfos)
            if (Info.Name == FName(TEXT("DiggerOpacityInjected")))
                return true;

        UMaterial* Base = Mat->GetMaterial();
        if (!Base) return false;
        for (UMaterialExpression* Expr :
             Base->GetEditorOnlyData()->ExpressionCollection.Expressions)
        {
            if (auto* FC = Cast<UMaterialExpressionMaterialFunctionCall>(Expr))
                if (FC->MaterialFunction && FC->MaterialFunction->GetName().Contains(TEXT("MF_SetAttribs")))
                    return true;
            if (auto* RVTParam = Cast<UMaterialExpressionRuntimeVirtualTextureSampleParameter>(Expr))
                if (RVTParam->ParameterName == FName(TEXT("DiggerOpacityRVT")))
                    return true;
            if (auto* SP = Cast<UMaterialExpressionScalarParameter>(Expr))
                if (SP->ParameterName == FName(TEXT("DiggerOpacityInjected")))
                    return true;
        }
        return false;
    };

    for (ALandscapeProxy* Proxy : Proxies)
    {
        if (!Proxy) continue;

        // ── RVT check ────────────────────────────────────────────────────
        // Check if this proxy has any RVT registered
        if (!Result.bHasRVT)
        {
            for (URuntimeVirtualTexture* RVT : Proxy->RuntimeVirtualTextures)
            {
                if (RVT)
                {
                    Result.bHasRVT = true;
                    break;
                }
            }
        }

        // ── Material checks ──────────────────────────────────────────────
        UMaterialInterface* AssignedMat = Proxy->GetLandscapeMaterial();
        if (!AssignedMat) continue;

        UMaterial* Base = AssignedMat->GetMaterial();
        if (!Base) continue;

        // Blend mode — accept Masked on the base material, or if Digger
        // has already injected nodes (injection also sets Masked)
        if (Base->GetBlendMode() != BLEND_Masked)
        {
            if (!IsDiggerInjected(AssignedMat))
                bAllMasked = false;
        }

        // Opacity injection — tag parameter, MF_SetAttribs, or RVT param node
        if (IsDiggerInjected(AssignedMat))
            bAnyHasMFSetAttribs = true;
    }

    // ── Fallback RVT check: look for RuntimeVirtualTextureVolume actors ──
    // With World Partition, proxies may not have the RVT directly in their
    // RuntimeVirtualTextures array — the RVT Volume exists as a separate actor.
    if (!Result.bHasRVT)
    {
        for (TActorIterator<ARuntimeVirtualTextureVolume> It(W); It; ++It)
        {
            ARuntimeVirtualTextureVolume* Vol = *It;
            if (!Vol) continue;
            URuntimeVirtualTextureComponent* RVTComp =
                Vol->FindComponentByClass<URuntimeVirtualTextureComponent>();
            if (RVTComp && RVTComp->GetVirtualTexture())
            {
                Result.bHasRVT = true;
                break;
            }
        }
    }

    Result.bAllMaterialsMasked   = bAllMasked;
    Result.bMFSetAttribsDetected = bAnyHasMFSetAttribs;

    return Result;
}

bool ADiggerManager::BackupMaterial(UMaterial* BaseMat)
{
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// ResolveDiggerRVT — scans landscape proxies for an existing Mask-channel RVT
// ─────────────────────────────────────────────────────────────────────────────
URuntimeVirtualTexture* ADiggerManager::ResolveDiggerRVT()
{
    if (IsValid(CachedDiggerRVT))
        return CachedDiggerRVT;

    UWorld* W = GetSafeWorld();
    if (!W) return nullptr;

    // First pass: prefer an RVT with "Digger" or "Opacity" in its name
    for (TActorIterator<ALandscapeProxy> It(W); It; ++It)
    {
        ALandscapeProxy* Proxy = *It;
        if (!Proxy) continue;
        for (URuntimeVirtualTexture* RVT : Proxy->RuntimeVirtualTextures)
        {
            if (!RVT) continue;
            FString Name = RVT->GetName();
            if (Name.Contains(TEXT("Digger")) || Name.Contains(TEXT("Opacity")))
            {
                CachedDiggerRVT = RVT;
                UE_LOG(LogTemp, Log,
                    TEXT("ResolveDiggerRVT: Found Digger RVT '%s' on landscape."),
                    *RVT->GetName());
                return CachedDiggerRVT;
            }
        }
    }

    // Second pass: prefer BaseColor_Normal_Specular type (the hole mask format)
    for (TActorIterator<ALandscapeProxy> It(W); It; ++It)
    {
        ALandscapeProxy* Proxy = *It;
        if (!Proxy) continue;
        for (URuntimeVirtualTexture* RVT : Proxy->RuntimeVirtualTextures)
        {
            if (!RVT) continue;
            if (RVT->GetMaterialType() ==
                ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_Mask_YCoCg)
            {
                CachedDiggerRVT = RVT;
                UE_LOG(LogTemp, Log,
                    TEXT("ResolveDiggerRVT: Found BaseColor_Normal_Specular RVT '%s'."),
                    *RVT->GetName());
                return CachedDiggerRVT;
            }
        }
    }

    // Third pass: accept any RVT on the landscape (legacy/manual setup)
    for (TActorIterator<ALandscapeProxy> It(W); It; ++It)
    {
        ALandscapeProxy* Proxy = *It;
        if (!Proxy) continue;
        for (URuntimeVirtualTexture* RVT : Proxy->RuntimeVirtualTextures)
        {
            if (!RVT) continue;
            CachedDiggerRVT = RVT;
            UE_LOG(LogTemp, Log,
                TEXT("ResolveDiggerRVT: Fallback to RVT '%s' (type %d)."),
                *RVT->GetName(), (int32)RVT->GetMaterialType());
            return CachedDiggerRVT;
        }
    }
    UE_LOG(LogTemp, Log,
        TEXT("ResolveDiggerRVT: No matching RVT found on any proxy."));
    return nullptr;
}

URuntimeVirtualTexture* ADiggerManager::ResolveDiggerRVTForPosition(const FVector& WorldPos)
{
    UWorld* W = GetSafeWorld();
    if (!W) return ResolveDiggerRVT();
    for (TActorIterator<ALandscapeProxy> It(W); It; ++It)
    {
        ALandscapeProxy* Proxy = *It;
        if (!Proxy) continue;
        FVector Origin, Extent;
        Proxy->GetActorBounds(false, Origin, Extent);
        FBox ProxyBox(Origin - Extent, Origin + Extent);
        ProxyBox.Min.Z -= 10000.f; ProxyBox.Max.Z += 10000.f;
        if (!ProxyBox.IsInside(WorldPos)) continue;
        // Prefer RVT named with "Digger" or "Opacity"
        URuntimeVirtualTexture* Fallback = nullptr;
        for (URuntimeVirtualTexture* RVT : Proxy->RuntimeVirtualTextures)
        {
            if (!RVT) continue;
            FString Name = RVT->GetName();
            if (Name.Contains(TEXT("Digger")) || Name.Contains(TEXT("Opacity")))
                return RVT;
            if (!Fallback) Fallback = RVT;
        }
        if (Fallback) return Fallback;
    }
    return ResolveDiggerRVT();
}


// ─────────────────────────────────────────────────────────────────────────────
// EnsureDiggerRVTAsset — creates RVT_DiggerOpacityMask if not present
// ─────────────────────────────────────────────────────────────────────────────
// Change: Ensure the RVT asset is fully saved and flushed before returning,
// so any material that references it won't encounter a transient/unsaved asset.

URuntimeVirtualTexture* ADiggerManager::EnsureDiggerRVTAsset()
{
    const FString AssetPath = TEXT("/Digger/Digger/RVT/RVT_DiggerOpacityMask");

    if (URuntimeVirtualTexture* Existing =
        LoadObject<URuntimeVirtualTexture>(nullptr, *AssetPath))
    {
        // Verify the existing asset has the correct type; fix if wrong
        if (Existing->GetMaterialType() != ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_Mask_YCoCg)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("EnsureDiggerRVTAsset: '%s' has wrong type (%d), fixing to BaseColor_Normal_Specular."),
                *AssetPath, (int32)Existing->GetMaterialType());
            if (FEnumProperty* P = CastField<FEnumProperty>(
                URuntimeVirtualTexture::StaticClass()->FindPropertyByName(FName(TEXT("MaterialType")))))
            {
                Existing->Modify();
                P->GetUnderlyingProperty()->SetIntPropertyValue(
                    P->ContainerPtrToValuePtr<void>(Existing),
                    (int64)ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_Mask_YCoCg);
                Existing->PostEditChange();
                Existing->MarkPackageDirty();

                // ◄◄ FIX: Save after fixing type
                UPackage* Pkg = Existing->GetOutermost();
                if (Pkg)
                {
                    TArray<UPackage*> PkgsToSave = { Pkg };
                    FEditorFileUtils::PromptForCheckoutAndSave(PkgsToSave, false, false);
                }
            }
        }
        CachedDiggerRVT = Existing;
        return Existing;
    }

    UPackage* Package = CreatePackage(*AssetPath);
    if (!Package) return nullptr;

    URuntimeVirtualTexture* RVT = NewObject<URuntimeVirtualTexture>(
        Package, URuntimeVirtualTexture::StaticClass(),
        FName(TEXT("RVT_DiggerOpacityMask")), RF_Public | RF_Standalone);
    if (!RVT) return nullptr;

    auto SetIntProp = [RVT](const TCHAR* Name, int32 Val)
    {
        if (FIntProperty* P = CastField<FIntProperty>(
            URuntimeVirtualTexture::StaticClass()->FindPropertyByName(FName(Name))))
            P->SetPropertyValue_InContainer(RVT, Val);
        else
            UE_LOG(LogTemp, Warning,
                TEXT("EnsureDiggerRVTAsset: int property '%s' not found."), Name);
    };
    auto SetBoolProp = [RVT](const TCHAR* Name, bool Val)
    {
        if (FBoolProperty* P = CastField<FBoolProperty>(
            URuntimeVirtualTexture::StaticClass()->FindPropertyByName(FName(Name))))
            P->SetPropertyValue_InContainer(RVT, Val);
    };
    auto SetEnumProp = [RVT](const TCHAR* Name, int64 Val)
    {
        if (FEnumProperty* P = CastField<FEnumProperty>(
            URuntimeVirtualTexture::StaticClass()->FindPropertyByName(FName(Name))))
            P->GetUnderlyingProperty()->SetIntPropertyValue(
                P->ContainerPtrToValuePtr<void>(RVT), Val);
        else
            UE_LOG(LogTemp, Warning,
                TEXT("EnsureDiggerRVTAsset: enum property '%s' not found."), Name);
    };

    SetEnumProp(TEXT("MaterialType"),
        (int64)ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_Mask_YCoCg);
    SetIntProp(TEXT("TileCount"),     10);
    SetIntProp(TEXT("TileSize"),       3);
    SetIntProp(TEXT("RemoveLowMips"), 0);
    SetBoolProp(TEXT("bCompressTextures"), true);

    RVT->PostEditChange();
    Package->MarkPackageDirty();
    FAssetRegistryModule::AssetCreated(RVT);

    // ◄◄ FIX: Save immediately and VERIFY success.
    // The RVT must be on disk before any material references it,
    // otherwise the material's PostLoad will crash with
    // TextureReferenceIndex == INDEX_NONE.
    TArray<UPackage*> ToSave = { Package };
    bool bSaved = (FEditorFileUtils::PromptForCheckoutAndSave(ToSave, false, false) == FEditorFileUtils::PR_Success);
    if (!bSaved)
    {
        UE_LOG(LogTemp, Error,
            TEXT("EnsureDiggerRVTAsset: FAILED to save RVT package '%s'. "
                 "Material injection may crash on next level load!"),
            *AssetPath);
    }

    CachedDiggerRVT = RVT;
    UE_LOG(LogTemp, Log,
        TEXT("EnsureDiggerRVTAsset: Created and saved '%s'."), *AssetPath);
    return RVT;
}


// ─────────────────────────────────────────────────────────────────────────────
// EnsureRVTVolumeForBounds — finds or spawns the RTVV and fires Set Bounds
// ─────────────────────────────────────────────────────────────────────────────
ARuntimeVirtualTextureVolume* ADiggerManager::EnsureRVTVolumeForBounds(
    const FBox& LandscapeUnionBox,
    URuntimeVirtualTexture* RVT,
    UWorld* W)
{
    if (!RVT || !W) return nullptr;

    // ── Find the ALandscape root actor (the "main" landscape, not a streaming proxy) ──
    // We prefer ALandscape over ALandscapeProxy because SetBoundsAlignActor
    // on the root will cause the engine's internal "Set Bounds" to consider
    // ALL streaming proxies. If no root ALandscape exists (pure proxy setup),
    // we fall back to manual transform.
    ALandscapeProxy* BoundsAlignTarget = nullptr;
    for (TActorIterator<ALandscapeProxy> It(W); It; ++It)
    {
        ALandscapeProxy* Proxy = *It;
        if (!Proxy) continue;
        
        // Prefer the root ALandscape actor (the non-streaming-proxy parent)
        if (Proxy->IsA<ALandscape>())
        {
            BoundsAlignTarget = Proxy;
            break;
        }
    }
    // If no ALandscape root found, just grab any proxy as fallback
    if (!BoundsAlignTarget)
    {
        for (TActorIterator<ALandscapeProxy> It(W); It; ++It)
        {
            if (*It) { BoundsAlignTarget = *It; break; }
        }
    }

    // ── Lambda: Apply bounds from the union box ──────────────────────────
    // This is the KEY FIX: we manually set the volume's transform from the
    // pre-computed union box encompassing ALL landscape proxies, rather than
    // relying solely on SetBoundsAlignActor (which only snaps to one proxy).
    auto ApplyUnionBounds = [&](ARuntimeVirtualTextureVolume* Vol)
    {
        if (!Vol) return;
        URuntimeVirtualTextureComponent* RVTComp =
            Vol->FindComponentByClass<URuntimeVirtualTextureComponent>();
        if (!RVTComp) return;

        // Step A: Try the engine's built-in "Set Bounds" with the align actor
        bool bEngineBoundsWorked = false;
        if (BoundsAlignTarget)
        {
            RVTComp->SetBoundsAlignActor(BoundsAlignTarget);

            if (FBoolProperty* SnapProp = CastField<FBoolProperty>(
                URuntimeVirtualTextureComponent::StaticClass()
                    ->FindPropertyByName(FName(TEXT("bSnapBoundsToLandscape")))))
            {
                SnapProp->SetPropertyValue_InContainer(RVTComp, true);
            }

            FProperty* SetBoundsProp =
                URuntimeVirtualTextureComponent::StaticClass()
                    ->FindPropertyByName(FName(TEXT("bSetBoundsButton")));
            if (SetBoundsProp)
            {
                FPropertyChangedEvent PropEvent(
                    SetBoundsProp, EPropertyChangeType::ValueSet);
                RVTComp->PostEditChangeProperty(PropEvent);
                bEngineBoundsWorked = true;
            }
        }

        // Step B: VERIFY the engine bounds actually cover the union box.
        // If not (or if Set Bounds wasn't available), apply manual transform.
        FBoxSphereBounds CurrentBounds = RVTComp->Bounds;
        FBox CurrentBox = CurrentBounds.GetBox();

        // Check if the current RVT volume bounds contain the entire union box
        // with a small tolerance (1% of extent)
        const FVector UnionExtent = LandscapeUnionBox.GetExtent();
        const float Tolerance = UnionExtent.GetMax() * 0.01f;
        const bool bCoversAll =
            CurrentBox.Min.X <= LandscapeUnionBox.Min.X + Tolerance &&
            CurrentBox.Min.Y <= LandscapeUnionBox.Min.Y + Tolerance &&
            CurrentBox.Max.X >= LandscapeUnionBox.Max.X - Tolerance &&
            CurrentBox.Max.Y >= LandscapeUnionBox.Max.Y - Tolerance;

        if (!bCoversAll || !bEngineBoundsWorked)
        {
            UE_LOG(LogTemp, Log,
                TEXT("EnsureRVTVolumeForBounds: Engine bounds %s union box — applying manual transform."),
                bEngineBoundsWorked ? TEXT("did not cover") : TEXT("unavailable, using"));

            // Manually set the volume's transform to cover the union box
            const FVector Center = LandscapeUnionBox.GetCenter();
            const FVector Extent = LandscapeUnionBox.GetExtent();

            // Add generous padding (5%) to prevent edge artifacts
            const FVector PaddedExtent = Extent * 1.05f;

            // Set the volume actor's transform
            Vol->SetActorLocation(Center);

            // The RVT volume uses its scale to define the covered area.
            // URuntimeVirtualTextureComponent interprets the owning actor's
            // scale as the half-extent of the volume in world units.
            // The default box is 2x2x2 centered at origin, so scale = extent.
            Vol->SetActorScale3D(FVector(
                PaddedExtent.X,
                PaddedExtent.Y,
                FMath::Max(PaddedExtent.Z, 50000.f)  // generous Z to cover terrain height range
            ));
        }
        else
        {
            UE_LOG(LogTemp, Log,
                TEXT("EnsureRVTVolumeForBounds: Engine 'Set Bounds' covers all %d proxies."),
                0); // Will log proxy count from caller
        }

        RVTComp->MarkRenderStateDirty();
        Vol->PostEditChange();
        Vol->MarkPackageDirty();
    };

    // ── Reuse existing RTVV assigned to our RVT ──────────────────────────
    for (TActorIterator<ARuntimeVirtualTextureVolume> It(W); It; ++It)
    {
        ARuntimeVirtualTextureVolume* Vol = *It;
        if (!Vol) continue;
        URuntimeVirtualTextureComponent* RVTComp =
            Vol->FindComponentByClass<URuntimeVirtualTextureComponent>();
        if (!RVTComp || RVTComp->GetVirtualTexture() != RVT) continue;

        ApplyUnionBounds(Vol);
        UE_LOG(LogTemp, Log,
            TEXT("EnsureRVTVolumeForBounds: Refreshed existing RTVV '%s'."),
            *Vol->GetName());
        return Vol;
    }

    // ── Spawn a new one ──────────────────────────────────────────────────
    FActorSpawnParameters SpawnParams;
    SpawnParams.bNoFail     = true;
    SpawnParams.ObjectFlags = RF_Transactional;

    ARuntimeVirtualTextureVolume* Vol =
        W->SpawnActor<ARuntimeVirtualTextureVolume>(
            ARuntimeVirtualTextureVolume::StaticClass(),
            LandscapeUnionBox.GetCenter(),
            FRotator::ZeroRotator,
            SpawnParams);
    if (!Vol) return nullptr;

    Vol->SetActorLabel(TEXT("Digger_RVT_Volume"));
    Vol->SetFolderPath(FName(TEXT("Digger/RVT")));

    if (URuntimeVirtualTextureComponent* RVTComp =
        Vol->FindComponentByClass<URuntimeVirtualTextureComponent>())
    {
        RVTComp->SetVirtualTexture(RVT);
    }

    ApplyUnionBounds(Vol);
    UE_LOG(LogTemp, Log,
        TEXT("EnsureRVTVolumeForBounds: Spawned new RTVV '%s'."),
        *Vol->GetName());
    return Vol;
}


// ---------------------------------------------------------------------------
// REPLACE: EnsureLandscapeMaterialHasOpacityMask  (DiggerManager.cpp ~606-682)
// ---------------------------------------------------------------------------
// Change: Detects Material Attributes mode and calls the correct injector.

void ADiggerManager::EnsureLandscapeMaterialHasOpacityMask(
    ALandscapeProxy* Landscape,
    URuntimeVirtualTexture* OpacityRVT)
{
    if (!Landscape || !OpacityRVT) return;

    UMaterialInterface* CurrentMat = Landscape->GetLandscapeMaterial();
    if (!CurrentMat)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("EnsureLandscapeMaterial: Proxy '%s' has no material -- skipping."),
            *Landscape->GetName());
        return;
    }

    UMaterial* BaseMat = CurrentMat->GetMaterial();
    if (!BaseMat) return;

    // ALWAYS update RVT references inside material functions (MF_SetAttribs etc.)
    InjectDiggerRVTParameter(BaseMat, OpacityRVT);

    // Idempotency -- check if already injected
    bool bAlreadyInjected = false;
    TArray<FMaterialParameterInfo> ScalarInfos;
    TArray<FGuid> ScalarGuids;
    CurrentMat->GetAllScalarParameterInfo(ScalarInfos, ScalarGuids);
    for (const FMaterialParameterInfo& Info : ScalarInfos)
    {
        if (Info.Name == FName(TEXT("DiggerOpacityInjected")))
        { bAlreadyInjected = true; break; }
    }
    if (!bAlreadyInjected)
    {
        for (UMaterialExpression* Expr :
             BaseMat->GetEditorOnlyData()->ExpressionCollection.Expressions)
        {
            if (auto* SP = Cast<UMaterialExpressionScalarParameter>(Expr))
                if (SP->ParameterName == FName(TEXT("DiggerOpacityInjected")))
                { bAlreadyInjected = true; break; }
        }
    }
    if (bAlreadyInjected)
    {
        BaseMat->UpdateCachedExpressionData();
        BaseMat->PostEditChange();
        UE_LOG(LogTemp, Log,
            TEXT("EnsureLandscapeMaterial: '%s' already injected -- refreshed RVT pointers."),
            *BaseMat->GetName());
        return;
    }

    // Auto-fix blend mode to Masked
    if (BaseMat->GetBlendMode() != BLEND_Masked)
    {
        BaseMat->Modify();
        if (FByteProperty* P = CastField<FByteProperty>(
            UMaterial::StaticClass()->FindPropertyByName(FName(TEXT("BlendMode")))))
            P->SetPropertyValue_InContainer(BaseMat, (uint8)BLEND_Masked);
        if (FFloatProperty* P = CastField<FFloatProperty>(
            UMaterial::StaticClass()->FindPropertyByName(FName(TEXT("OpacityMaskClipValue")))))
            P->SetPropertyValue_InContainer(BaseMat, 0.3333f);
        UE_LOG(LogTemp, Log,
            TEXT("EnsureLandscapeMaterial: Set BlendMode=Masked on '%s'."),
            *BaseMat->GetName());
    }

    // ======================================================================
    // DETECT: Material Attributes vs Pin-Based workflow
    // ======================================================================
    bool bUsesMaterialAttributes = false;

    // Check 1: The material's own flag
    if (FBoolProperty* MAProp = CastField<FBoolProperty>(
        UMaterial::StaticClass()->FindPropertyByName(
            FName(TEXT("bUseMaterialAttributes")))))
    {
        bUsesMaterialAttributes = MAProp->GetPropertyValue_InContainer(BaseMat);
    }

    // Check 2: If the flag isn't set, look for SetMaterialAttributes or
    // GetMaterialAttributes nodes in the expression graph -- some materials
    // use MA workflow through function calls without the top-level flag
    if (!bUsesMaterialAttributes)
    {
        for (UMaterialExpression* Expr :
             BaseMat->GetEditorOnlyData()->ExpressionCollection.Expressions)
        {
            if (Cast<UMaterialExpressionSetMaterialAttributes>(Expr) ||
                Cast<UMaterialExpressionGetMaterialAttributes>(Expr))
            {
                bUsesMaterialAttributes = true;
                break;
            }
            // Also check if any MF call uses SetAttribs/GetAttribs pattern
            if (auto* FuncCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expr))
            {
                if (FuncCall->MaterialFunction)
                {
                    FString FuncName = FuncCall->MaterialFunction->GetName();
                    if (FuncName.Contains(TEXT("SetAttrib")) ||
                        FuncName.Contains(TEXT("SetMaterialAttrib")))
                    {
                        bUsesMaterialAttributes = true;
                        break;
                    }
                }
            }
        }
    }

    UE_LOG(LogTemp, Log,
        TEXT("EnsureLandscapeMaterial: '%s' uses %s workflow."),
        *BaseMat->GetName(),
        bUsesMaterialAttributes ? TEXT("Material Attributes") : TEXT("Pin-Based"));

    // ======================================================================
    // INJECT: Call the correct path
    // ======================================================================
    if (bUsesMaterialAttributes)
    {
        InjectOpacityMaskNodes_MaterialAttributes(BaseMat, OpacityRVT);
    }
    else
    {
        InjectOpacityMaskNodes(BaseMat, OpacityRVT);
    }

    BaseMat->UpdateCachedExpressionData();
    BaseMat->PostEditChange();
    BaseMat->MarkPackageDirty();

    UE_LOG(LogTemp, Log,
        TEXT("EnsureLandscapeMaterial: Injected opacity nodes into '%s' (%s path)."),
        *BaseMat->GetName(),
        bUsesMaterialAttributes ? TEXT("MaterialAttributes") : TEXT("Pin-Based"));
}


// ─────────────────────────────────────────────────────────────────────────────
// InjectOpacityMaskNodes — adds RVT sample + hard-edge contrast to OpacityMask
// ─────────────────────────────────────────────────────────────────────────────
void ADiggerManager::InjectOpacityMaskNodes(
    UMaterial* BaseMat,
    URuntimeVirtualTexture* OpacityRVT)
{
    if (!BaseMat || !OpacityRVT) return;

    BaseMat->Modify();
    auto* Exprs = &BaseMat->GetEditorOnlyData()->ExpressionCollection.Expressions;

    // =======================================================================
    // READ SIDE
    // Convention: Landscape writes 1.0 (white) into RVT via VirtualTextureRenderPassType=Always
    //             DynamicHole writes 0.0 (black) via M_OpacityMask
    // Chain: RVTSample → 1-x → ×1000 → Clamp → 1-x → OpacityMask
    // =======================================================================

   // 1. RVT sample parameter
    UMaterialExpressionRuntimeVirtualTextureSampleParameter* RVTSample =
        NewObject<UMaterialExpressionRuntimeVirtualTextureSampleParameter>(BaseMat);
    RVTSample->ParameterName      = FName(TEXT("DiggerOpacityRVT"));
    RVTSample->VirtualTexture     = OpacityRVT;
    RVTSample->MaterialType       =
        ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_Mask_YCoCg;
    RVTSample->bHidePreviewWindow = true;
    RVTSample->MaterialExpressionEditorX = -1000;
    RVTSample->MaterialExpressionEditorY =  400;
    Exprs->Add(RVTSample);

    // DEBUG: Log all output names and indices
    const TArray<FExpressionOutput>& Outputs = RVTSample->GetOutputs();
    for (int32 i = 0; i < Outputs.Num(); i++)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("RVTSample Output[%d] = '%s'"),
            i, *Outputs[i].OutputName.ToString());
    }

    // 2. First 1-x
    UMaterialExpressionOneMinus* InvertSample =
        NewObject<UMaterialExpressionOneMinus>(BaseMat);
    InvertSample->MaterialExpressionEditorX = -680;
    InvertSample->MaterialExpressionEditorY =  400;
    InvertSample->Input.Expression  = RVTSample;
    InvertSample->Input.OutputIndex = 5;  // ◄◄ FIX: Mask output, not BaseColor (0)
    Exprs->Add(InvertSample);

    // 3. Multiply × 1000
    UMaterialExpressionConstant* ContrastConst =
        NewObject<UMaterialExpressionConstant>(BaseMat);
    ContrastConst->R = 1000.f;
    ContrastConst->MaterialExpressionEditorX = -680;
    ContrastConst->MaterialExpressionEditorY =  520;
    Exprs->Add(ContrastConst);

    UMaterialExpressionMultiply* ContrastMul =
        NewObject<UMaterialExpressionMultiply>(BaseMat);
    ContrastMul->MaterialExpressionEditorX = -460;
    ContrastMul->MaterialExpressionEditorY =  400;
    ContrastMul->A.Expression = InvertSample;
    ContrastMul->B.Expression = ContrastConst;
    Exprs->Add(ContrastMul);

    // 4. Clamp to [0,1] — ◄◄ FIX: explicitly set Min=0 Max=1
    UMaterialExpressionClamp* ContrastClamp =
        NewObject<UMaterialExpressionClamp>(BaseMat);
    ContrastClamp->MaterialExpressionEditorX = -240;
    ContrastClamp->MaterialExpressionEditorY =  400;
    ContrastClamp->Input.Expression = ContrastMul;
    ContrastClamp->MinDefault = 0.0f;  // ◄◄ FIX
    ContrastClamp->MaxDefault = 1.0f;  // ◄◄ FIX
    Exprs->Add(ContrastClamp);

    // 5. Second 1-x: flip back so hole=0 (invisible), no-hole=1 (visible)
    UMaterialExpressionOneMinus* InvertHole =
        NewObject<UMaterialExpressionOneMinus>(BaseMat);
    InvertHole->MaterialExpressionEditorX = -20;
    InvertHole->MaterialExpressionEditorY =  400;
    InvertHole->Input.Expression = ContrastClamp;
    Exprs->Add(InvertHole);

    // 6. Combine with any existing OpacityMask
    UMaterialExpression* ExistingOpacity =
        BaseMat->GetEditorOnlyData()->OpacityMask.Expression;

    UMaterialExpression* FinalOpacity = nullptr;
    if (ExistingOpacity)
    {
        UMaterialExpressionMultiply* Combine =
            NewObject<UMaterialExpressionMultiply>(BaseMat);
        Combine->MaterialExpressionEditorX = 180;
        Combine->MaterialExpressionEditorY = 400;
        Combine->A.Expression = ExistingOpacity;
        Combine->B.Expression = InvertHole;
        Exprs->Add(Combine);
        FinalOpacity = Combine;
    }
    else
    {
        FinalOpacity = InvertHole;
    }

    // 7. Wire to OpacityMask pin
    auto& OpacityPin         = BaseMat->GetEditorOnlyData()->OpacityMask;
    OpacityPin.Expression    = FinalOpacity;
    OpacityPin.OutputIndex   = 0;
    OpacityPin.Mask          = 1;
    OpacityPin.MaskR         = 1;
    OpacityPin.MaskG         = 0;
    OpacityPin.MaskB         = 0;
    OpacityPin.MaskA         = 0;

    // 8. Idempotency tag
    UMaterialExpressionScalarParameter* Tag =
        NewObject<UMaterialExpressionScalarParameter>(BaseMat);
    Tag->ParameterName             = FName(TEXT("DiggerOpacityInjected"));
    Tag->DefaultValue              = 1.f;
    Tag->bHidePreviewWindow        = true;
    Tag->MaterialExpressionEditorX = -900;
    Tag->MaterialExpressionEditorY =  620;
    Exprs->Add(Tag);

    BaseMat->UpdateCachedExpressionData();

    // ======================================================================
    // Post-injection: Force material recompile and save
    // ======================================================================
    
    // Nudge an expression to dirty the material graph — replicates the
    // "move a node slightly" that forces UE to recognize the graph changed
    if (Exprs->Num() > 0)
    {
        UMaterialExpression* AnyExpr = (*Exprs)[Exprs->Num() - 1]; // the tag node
        AnyExpr->MaterialExpressionEditorX += 1;
        AnyExpr->MaterialExpressionEditorX -= 1;
        BaseMat->Modify();
    }

    BaseMat->PreEditChange(nullptr);
    BaseMat->UpdateCachedExpressionData();
    BaseMat->PostEditChange();
    BaseMat->MarkPackageDirty();
    BaseMat->ForceRecompileForRendering();

    // Auto-save the material package
    UPackage* MatPkg = BaseMat->GetOutermost();
    if (MatPkg)
    {
        TArray<UPackage*> PkgsToSave = { MatPkg };
        FEditorFileUtils::PromptForCheckoutAndSave(PkgsToSave, false, false);
    }
    

    UE_LOG(LogTemp, Log,
        TEXT("InjectOpacityMaskNodes: Done on '%s' (%s existing opacity)."),
        *BaseMat->GetName(),
        ExistingOpacity ? TEXT("combined with") : TEXT("no"));
}



// ---------------------------------------------------------------------------
// NEW FUNCTION: InjectOpacityMaskNodes_MaterialAttributes
// ---------------------------------------------------------------------------
// Replicates the MF_SetAttribs logic directly in the material graph:
//
//   OpacityRVT (Mask output)
//       |
//       v
//   Multiply x1000  -->  1-x  -->  Multiply.B
//                                       |
//   [existing MA chain] --> GetMaterialAttributes
//       |                       |
//       |                  OpacityMask --> Multiply.A
//       |                                      |
//       +----> SetMaterialAttributes <---------+
//                  MaterialAttributes = passthrough from existing chain
//                  Opacity Mask = Multiply result
//                       |
//                       v
//                  [wired to material output]

void ADiggerManager::InjectOpacityMaskNodes_MaterialAttributes(
    UMaterial* BaseMat,
    URuntimeVirtualTexture* OpacityRVT)
{
    if (!BaseMat || !OpacityRVT) return;

    BaseMat->Modify();
    auto* Exprs = &BaseMat->GetEditorOnlyData()->ExpressionCollection.Expressions;

    // =======================================================================
    // Find the MakeMaterialAttributes node
    // =======================================================================
    UMaterialExpressionMakeMaterialAttributes* MakeMA = nullptr;
    for (UMaterialExpression* Expr : *Exprs)
    {
        MakeMA = Cast<UMaterialExpressionMakeMaterialAttributes>(Expr);
        if (MakeMA) break;
    }

    if (!MakeMA)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("InjectOpacityMaskNodes_MA: '%s' has no MakeMaterialAttributes "
                 "node. Falling back to pin-based."),
            *BaseMat->GetName());
        InjectOpacityMaskNodes(BaseMat, OpacityRVT);
        return;
    }

    // =======================================================================
    // 1. RVT Sample
    // =======================================================================
    UMaterialExpressionRuntimeVirtualTextureSampleParameter* RVTSample =
        NewObject<UMaterialExpressionRuntimeVirtualTextureSampleParameter>(BaseMat);
    RVTSample->ParameterName      = FName(TEXT("DiggerOpacityRVT"));
    RVTSample->VirtualTexture     = OpacityRVT;
    RVTSample->MaterialType       =
        ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_Mask_YCoCg;
    RVTSample->bHidePreviewWindow = true;
    RVTSample->MaterialExpressionEditorX = -900;
    RVTSample->MaterialExpressionEditorY =  500;
    Exprs->Add(RVTSample);

    // =======================================================================
    // 2. 1-x (first inversion)
    // =======================================================================
    UMaterialExpressionOneMinus* InvertSample =
        NewObject<UMaterialExpressionOneMinus>(BaseMat);
    InvertSample->MaterialExpressionEditorX = -680;
    InvertSample->MaterialExpressionEditorY =  500;
    InvertSample->Input.Expression  = RVTSample;
    InvertSample->Input.OutputIndex = 5;  // Mask output
    Exprs->Add(InvertSample);

    // =======================================================================
    // 3. Multiply x 1000
    // =======================================================================
    UMaterialExpressionConstant* ContrastConst =
        NewObject<UMaterialExpressionConstant>(BaseMat);
    ContrastConst->R = 1000.f;
    ContrastConst->MaterialExpressionEditorX = -680;
    ContrastConst->MaterialExpressionEditorY =  620;
    Exprs->Add(ContrastConst);

    UMaterialExpressionMultiply* ContrastMul =
        NewObject<UMaterialExpressionMultiply>(BaseMat);
    ContrastMul->MaterialExpressionEditorX = -460;
    ContrastMul->MaterialExpressionEditorY =  500;
    ContrastMul->A.Expression = InvertSample;
    ContrastMul->B.Expression = ContrastConst;
    Exprs->Add(ContrastMul);

    // =======================================================================
    // 4. Clamp
    // =======================================================================
    UMaterialExpressionClamp* ContrastClamp =
        NewObject<UMaterialExpressionClamp>(BaseMat);
    ContrastClamp->MaterialExpressionEditorX = -240;
    ContrastClamp->MaterialExpressionEditorY =  500;
    ContrastClamp->Input.Expression = ContrastMul;
    ContrastClamp->MinDefault = 0.0f;
    ContrastClamp->MaxDefault = 1.0f;
    Exprs->Add(ContrastClamp);

    // =======================================================================
    // 5. 1-x (second inversion)
    // =======================================================================
    UMaterialExpressionOneMinus* InvertHole =
        NewObject<UMaterialExpressionOneMinus>(BaseMat);
    InvertHole->MaterialExpressionEditorX = -20;
    InvertHole->MaterialExpressionEditorY =  500;
    InvertHole->Input.Expression = ContrastClamp;
    Exprs->Add(InvertHole);

    // =======================================================================
    // 6. Wire into MakeMaterialAttributes OpacityMask input
    // =======================================================================
    /// 6. Wire into MakeMaterialAttributes OpacityMask input (index 7)
    FExpressionInput* OpacityMaskInput = MakeMA->GetInput(7);
    UMaterialExpression* ExistingOpacity = OpacityMaskInput ? OpacityMaskInput->Expression : nullptr;

    if (ExistingOpacity)
    {
        int32 ExistingOutputIndex = OpacityMaskInput->OutputIndex;
        UMaterialExpressionMultiply* Combine =
            NewObject<UMaterialExpressionMultiply>(BaseMat);
        Combine->MaterialExpressionEditorX = 180;
        Combine->MaterialExpressionEditorY = 500;
        Combine->A.Expression = ExistingOpacity;
        Combine->A.OutputIndex = ExistingOutputIndex;
        Combine->B.Expression = InvertHole;
        Exprs->Add(Combine);

        OpacityMaskInput->Expression  = Combine;
        OpacityMaskInput->OutputIndex = 0;
    }
    else
    {
        OpacityMaskInput->Expression  = InvertHole;
        OpacityMaskInput->OutputIndex = 0;
    }
    
    // =======================================================================
    // 7. Idempotency tag
    // =======================================================================
    UMaterialExpressionScalarParameter* Tag =
        NewObject<UMaterialExpressionScalarParameter>(BaseMat);
    Tag->ParameterName             = FName(TEXT("DiggerOpacityInjected"));
    Tag->DefaultValue              = 1.f;
    Tag->bHidePreviewWindow        = true;
    Tag->MaterialExpressionEditorX = -900;
    Tag->MaterialExpressionEditorY =  700;
    Exprs->Add(Tag);

    BaseMat->UpdateCachedExpressionData();

    // ======================================================================
    // Post-injection: Force material recompile and save
    // ======================================================================
    
    // Nudge an expression to dirty the material graph — replicates the
    // "move a node slightly" that forces UE to recognize the graph changed
    if (Exprs->Num() > 0)
    {
        UMaterialExpression* AnyExpr = (*Exprs)[Exprs->Num() - 1]; // the tag node
        AnyExpr->MaterialExpressionEditorX += 1;
        AnyExpr->MaterialExpressionEditorX -= 1;
        BaseMat->Modify();
    }

    BaseMat->PreEditChange(nullptr);
    BaseMat->UpdateCachedExpressionData();
    BaseMat->PostEditChange();
    BaseMat->MarkPackageDirty();
    BaseMat->ForceRecompileForRendering();

    // Auto-save the material package
    UPackage* MatPkg = BaseMat->GetOutermost();
    if (MatPkg)
    {
        TArray<UPackage*> PkgsToSave = { MatPkg };
        FEditorFileUtils::PromptForCheckoutAndSave(PkgsToSave, false, false);
    }
    
    
    UE_LOG(LogTemp, Log,
        TEXT("InjectOpacityMaskNodes_MA: Done on '%s'. Wired hole mask into "
             "MakeMaterialAttributes OpacityMask (%s existing)."),
        *BaseMat->GetName(),
        ExistingOpacity ? TEXT("combined with") : TEXT("no"));
}


// ─────────────────────────────────────────────────────────────────────────────
// InjectDiggerRVTParameter — updates RVT pointer on base material AND inside
// Material Functions (like MF_SetAttribs) that have their own OpacityRVT node.
// ─────────────────────────────────────────────────────────────────────────────
// Changes: Added UpdateCachedExpressionData on modified functions, and
// guarded Material Function PostEditChange to also rebuild expression data.

void ADiggerManager::InjectDiggerRVTParameter(
    UMaterial* BaseMat,
    URuntimeVirtualTexture* OpacityRVT)
{
    if (!BaseMat || !OpacityRVT) return;

    bool bUpdatedAny = false;

    // 1. Update DiggerOpacityRVT on the base material itself
    for (UMaterialExpression* Expr :
         BaseMat->GetEditorOnlyData()->ExpressionCollection.Expressions)
    {
        if (auto* P = Cast<UMaterialExpressionRuntimeVirtualTextureSampleParameter>(Expr))
        {
            if (P->ParameterName == FName(TEXT("DiggerOpacityRVT")))
            {
                BaseMat->Modify();
                P->VirtualTexture = OpacityRVT;
                P->MaterialType = ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_Mask_YCoCg;
                bUpdatedAny = true;
                UE_LOG(LogTemp, Log,
                    TEXT("InjectDiggerRVTParameter: Updated DiggerOpacityRVT on '%s'."),
                    *BaseMat->GetName());
            }
        }
    }

    // 2. Walk into Material Function calls and update any RVT sample param
    //    with "Opacity" in its name.
    for (UMaterialExpression* Expr :
         BaseMat->GetEditorOnlyData()->ExpressionCollection.Expressions)
    {
        auto* FuncCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expr);
        if (!FuncCall || !FuncCall->MaterialFunction)
            continue;

        UMaterialFunctionInterface* FuncInterface = FuncCall->MaterialFunction.Get();
        UE_LOG(LogTemp, Log,
            TEXT("InjectDiggerRVTParameter: Found function call '%s' (class '%s')."),
            *FuncInterface->GetName(),
            *FuncInterface->GetClass()->GetName());

        UMaterialFunction* Func = Cast<UMaterialFunction>(FuncInterface);
        
        if (!Func)
        {
            if (UMaterialFunctionInstance* FuncInst = Cast<UMaterialFunctionInstance>(FuncInterface))
            {
                if (FuncInst->GetBaseFunction())
                    Func = Cast<UMaterialFunction>(FuncInst->GetBaseFunction());
            }
        }
        
        if (!Func)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("InjectDiggerRVTParameter: Could not resolve function '%s' to UMaterialFunction."),
                *FuncInterface->GetName());
            continue;
        }
        
        if (!Func->GetEditorOnlyData())
        {
            UE_LOG(LogTemp, Warning,
                TEXT("InjectDiggerRVTParameter: Function '%s' has no editor data."),
                *Func->GetName());
            continue;
        }

        const TArray<TObjectPtr<UMaterialExpression>>& FuncExprs =
            Func->GetEditorOnlyData()->ExpressionCollection.Expressions;
        
        UE_LOG(LogTemp, Log,
            TEXT("InjectDiggerRVTParameter: Scanning %d expressions inside '%s'."),
            FuncExprs.Num(), *Func->GetName());

        bool bUpdatedFunc = false;
        for (const TObjectPtr<UMaterialExpression>& FuncExprPtr : FuncExprs)
        {
            UMaterialExpression* FuncExpr = FuncExprPtr.Get();
            auto* RVTParam = Cast<UMaterialExpressionRuntimeVirtualTextureSampleParameter>(FuncExpr);
            if (!RVTParam) continue;

            FString ParamName = RVTParam->ParameterName.ToString();
            if (ParamName.Contains(TEXT("Opacity")) || ParamName.Contains(TEXT("opacity")))
            {
                Func->Modify();
                RVTParam->VirtualTexture = OpacityRVT;
                RVTParam->MaterialType = ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_Mask_YCoCg;
                bUpdatedAny = true;
                bUpdatedFunc = true;
                UE_LOG(LogTemp, Log,
                    TEXT("InjectDiggerRVTParameter: Updated '%s' inside '%s' -> RVT '%s'."),
                    *ParamName, *Func->GetName(), *OpacityRVT->GetName());
            }
        }

        if (bUpdatedFunc)
        {
            // ◄◄ FIX: Rebuild the function's cached expression data so its
            // texture reference table includes the new RVT pointer.
            // Without this, the parent material's compilation will fail
            // when it encounters the function's RVT sample node.
            Func->UpdateDependentFunctionCandidates();
            Func->PostEditChange();
            Func->MarkPackageDirty();

            // ◄◄ FIX: Save the function package immediately so the RVT
            // reference is persisted. A dirty-but-unsaved function causes
            // the invisible landscape on next level load.
            UPackage* FuncPkg = Func->GetOutermost();
            if (FuncPkg)
            {
                TArray<UPackage*> PkgsToSave = { FuncPkg };
                FEditorFileUtils::PromptForCheckoutAndSave(PkgsToSave, false, false);
            }
        }
    }

    // ◄◄ FIX: If we updated any RVT pointers on the base material itself,
    // rebuild its cached expression data too.
    if (bUpdatedAny)
    {
        BaseMat->UpdateCachedExpressionData();
    }

    if (!bUpdatedAny)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("InjectDiggerRVTParameter: No OpacityRVT params found in '%s' or its functions."),
            *BaseMat->GetName());
    }
}



// ─────────────────────────────────────────────────────────────────────────────
void ADiggerManager::AutoSetupLandscapeForDynamicHoles()
{
#if WITH_EDITOR
    UWorld* W = GetSafeWorld();
    if (!W)
    {
        UE_LOG(LogTemp, Error, TEXT("AutoSetup: No world available."));
        return;
    }

    // ── Step 1: Resolve or create the opacity RVT ────────────────────────
    URuntimeVirtualTexture* DiggerRVT = ResolveDiggerRVT();
    if (!DiggerRVT)
    {
        UE_LOG(LogTemp, Log,
            TEXT("AutoSetup: No existing Mask-channel RVT found — creating one."));
        DiggerRVT = EnsureDiggerRVTAsset();
    }
    if (!DiggerRVT)
    {
        UE_LOG(LogTemp, Error,
            TEXT("AutoSetup: Failed to create RVT asset. "
                 "Check that the Digger plugin content folder is writable."));
        return;
    }
    UE_LOG(LogTemp, Log,
        TEXT("AutoSetup: Using RVT '%s'."), *DiggerRVT->GetName());

    // ── Step 2: Collect ALL landscape proxies (including streaming) ──────
    TArray<ALandscapeProxy*> AllProxies;
    for (TActorIterator<ALandscapeProxy> It(W); It; ++It)
        if (*It) AllProxies.Add(*It);

    if (AllProxies.Num() == 0)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("AutoSetup: No landscape actors found in the scene."));
        return;
    }
    UE_LOG(LogTemp, Log,
        TEXT("AutoSetup: Found %d landscape proxy/proxies."), AllProxies.Num());

    // ── Step 3: Build union bounds across ALL proxies ────────────────────
    // ◄◄ FIX: Use component-level bounds for more accurate coverage
    FBox UnionBox(ForceInit);
    for (ALandscapeProxy* Proxy : AllProxies)
    {
        // GetActorBounds uses component bounds — this is correct for landscapes
        FVector Origin, Extent;
        Proxy->GetActorBounds(false, Origin, Extent);
        FBox ProxyBox(Origin - Extent, Origin + Extent);
        
        // ◄◄ FIX: Also check individual LandscapeComponents for tighter bounds
        TArray<ULandscapeComponent*> LandComps;
        Proxy->GetComponents<ULandscapeComponent>(LandComps);
        for (ULandscapeComponent* LC : LandComps)
        {
            if (!LC) continue;
            FBox CompBox = LC->Bounds.GetBox();
            if (CompBox.IsValid)
            {
                ProxyBox += CompBox;
            }
        }
        
        UnionBox += ProxyBox;
        
        UE_LOG(LogTemp, Log,
            TEXT("AutoSetup: Proxy '%s' bounds: Origin=%s Extent=%s"),
            *Proxy->GetName(), *Origin.ToString(), *Extent.ToString());
    }
    
    if (!UnionBox.IsValid)
    {
        UE_LOG(LogTemp, Error,
            TEXT("AutoSetup: Could not compute valid bounds from landscape proxies."));
        return;
    }
    
    // ◄◄ FIX: Log the final union box so we can verify coverage
    UE_LOG(LogTemp, Log,
        TEXT("AutoSetup: Union box: Min=%s Max=%s Center=%s Extent=%s"),
        *UnionBox.Min.ToString(), *UnionBox.Max.ToString(),
        *UnionBox.GetCenter().ToString(), *UnionBox.GetExtent().ToString());

    // ── Step 4: Create or resize the RTVV ────────────────────────────────
    ARuntimeVirtualTextureVolume* Volume =
        EnsureRVTVolumeForBounds(UnionBox, DiggerRVT, W);
    if (!Volume)
    {
        UE_LOG(LogTemp, Error,
            TEXT("AutoSetup: Could not create or find an RVT Volume."));
        return;
    }

// ---------------------------------------------------------------------------
// REPLACE: Step 5 in AutoSetupLandscapeForDynamicHoles
// ---------------------------------------------------------------------------
// KEY CHANGE: Set VirtualTextureRenderPassType = Always (2) so the landscape
// actually renders into its RVTs. Without this the RVT Output node has no
// effect because the engine skips the landscape's RVT render pass entirely.

  // ── Step 5: Configure every landscape proxy ───────────────────────────────
    int32 ProxiesConfigured = 0;
    for (ALandscapeProxy* Proxy : AllProxies)
    {
        if (!Proxy) continue;

        const bool bAlreadyRegistered =
            Proxy->RuntimeVirtualTextures.Contains(DiggerRVT);
        Proxy->Modify();
        Proxy->RuntimeVirtualTextures.AddUnique(DiggerRVT);

        // CRITICAL: Set VirtualTextureRenderPassType = Always (2)
        // Without this, the landscape never writes into the RVT and
        // the RVT stays black everywhere — indistinguishable from holes.
        {
            bool bSet = false;
            if (FByteProperty* ByteProp = CastField<FByteProperty>(
                ALandscapeProxy::StaticClass()->FindPropertyByName(
                    FName(TEXT("VirtualTextureRenderPassType")))))
            {
                uint8 CurrentVal = 0;
                ByteProp->GetValue_InContainer(Proxy, &CurrentVal);
                if (CurrentVal == 0)
                {
                    ByteProp->SetPropertyValue_InContainer(Proxy, (uint8)2);
                    bSet = true;
                }
            }
            else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(
                ALandscapeProxy::StaticClass()->FindPropertyByName(
                    FName(TEXT("VirtualTextureRenderPassType")))))
            {
                FNumericProperty* Under = EnumProp->GetUnderlyingProperty();
                int64 CurrentVal = Under->GetSignedIntPropertyValue(
                    EnumProp->ContainerPtrToValuePtr<void>(Proxy));
                if (CurrentVal == 0)
                {
                    Under->SetIntPropertyValue(
                        EnumProp->ContainerPtrToValuePtr<void>(Proxy),
                        (int64)2);
                    bSet = true;
                }
            }

            if (bSet)
            {
                UE_LOG(LogTemp, Log,
                    TEXT("AutoSetup: Set VirtualTextureRenderPassType=Always "
                         "on '%s'."),
                    *Proxy->GetName());
            }
        }

        Proxy->MarkPackageDirty();
        Proxy->PostEditChange();

        EnsureLandscapeMaterialHasOpacityMask(Proxy, DiggerRVT);

        ProxiesConfigured++;
        UE_LOG(LogTemp, Log,
            TEXT("AutoSetup: Configured proxy '%s' (RVT was %s)."),
            *Proxy->GetName(),
            bAlreadyRegistered
                ? TEXT("already registered")
                : TEXT("newly registered"));
    }

    
    // ── Step 6: Configure existing DynamicHole actors for RVT rendering ──
    for (TActorIterator<ADynamicHole> HoleIt(W); HoleIt; ++HoleIt)
    {
        if (ADynamicHole* Hole = *HoleIt)
        {
            if (URuntimeVirtualTexture* HoleRVT = ResolveDiggerRVTForPosition(Hole->GetActorLocation()))
                Hole->ConfigureRVTRendering(HoleRVT);
        }
    }

    // ── Step 7: Invalidate caches ────────────────────────────────────────
    InvalidateRVTCache();

    // ── Step 8: Force-invalidate the RVT volume ──────────────────────────
    if (Volume)
    {
        if (URuntimeVirtualTextureComponent* RVTComp =
            Volume->FindComponentByClass<URuntimeVirtualTextureComponent>())
            RVTComp->Invalidate(FBoxSphereBounds(UnionBox));
    }

    UE_LOG(LogTemp, Log,
        TEXT("AutoSetup: Complete. %d proxy/proxies configured. RTVV: '%s'."),
        ProxiesConfigured, *Volume->GetName());

    if (GEditor) GEditor->RedrawLevelEditingViewports();
#endif
}

#endif // WITH_EDITOR