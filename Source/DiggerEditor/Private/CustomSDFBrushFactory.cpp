#include "CustomSDFBrushFactory.h"
#include "UCustomSDFBrushAsset.h"
#include "Misc/FileHelper.h"

UCustomSDFBrushFactory::UCustomSDFBrushFactory()
{
    bCreateNew = false;
    bEditorImport = true;
    SupportedClass = UCustomSDFBrushAsset::StaticClass();
    Formats.Add(TEXT("sdfbrush;SDF Brush"));
}

UObject* UCustomSDFBrushFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName,
    EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
    TArray<uint8> Data;
    if (!FFileHelper::LoadFileToArray(Data, *Filename))
        return nullptr;

    int32 Offset = 0;
    auto Read = [&](void* Ptr, int32 Size) {
        FMemory::Memcpy(Ptr, Data.GetData() + Offset, Size);
        Offset += Size;
    };

    uint32 Magic, Version;
    Read(&Magic, sizeof(Magic));
    Read(&Version, sizeof(Version));
    if (Magic != 0x53444642) return nullptr;

    FIntVector Dimensions;
    float VoxelSize;
    FVector OriginOffset;
    Read(&Dimensions, sizeof(FIntVector));
    Read(&VoxelSize, sizeof(float));
    Read(&OriginOffset, sizeof(FVector));

    int32 NumVoxels = Dimensions.X * Dimensions.Y * Dimensions.Z;
    TArray<float> SDFValues;
    SDFValues.SetNumUninitialized(NumVoxels);
    Read(SDFValues.GetData(), NumVoxels * sizeof(float));

    UCustomSDFBrushAsset* Asset = NewObject<UCustomSDFBrushAsset>(InParent, InClass, InName, Flags);
    Asset->Dimensions = Dimensions;
    Asset->VoxelSize = VoxelSize;
    Asset->OriginOffset = OriginOffset;
    Asset->SDFValues = MoveTemp(SDFValues);

    return Asset;
}

