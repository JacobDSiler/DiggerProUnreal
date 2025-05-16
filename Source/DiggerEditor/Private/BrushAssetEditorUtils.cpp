#include "BrushAssetEditorUtils.h"

#include "FCustomSDFBrush.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFilemanager.h"
#include "Async/ParallelFor.h"

bool FBrushAssetEditorUtils::SaveSDFBrushToFile(const FCustomSDFBrush& Brush, const FString& FilePath)
{
    TArray<uint8> Data;

    // Header: magic, version, dimensions, voxel size, origin offset
    const uint32 Magic = 0x53444642; // 'SDFB'
    const uint32 Version = 1;
    Data.Append(reinterpret_cast<const uint8*>(&Magic), sizeof(Magic));
    Data.Append(reinterpret_cast<const uint8*>(&Version), sizeof(Version));
    Data.Append(reinterpret_cast<const uint8*>(&Brush.Dimensions), sizeof(FIntVector));
    Data.Append(reinterpret_cast<const uint8*>(&Brush.VoxelSize), sizeof(float));
    Data.Append(reinterpret_cast<const uint8*>(&Brush.OriginOffset), sizeof(FVector));

    // Payload: SDF values
    Data.Append(reinterpret_cast<const uint8*>(Brush.SDFValues.GetData()), Brush.SDFValues.Num() * sizeof(float));

    bool bSaved = FFileHelper::SaveArrayToFile(Data, *FilePath);
    UE_LOG(LogTemp, Log, TEXT("Saving SDF brush to %s: %s"), *FilePath, bSaved ? TEXT("Success") : TEXT("Failed"));
    return bSaved;
}

bool FBrushAssetEditorUtils::LoadSDFBrushFromFile(const FString& FilePath, FCustomSDFBrush& OutBrush)
{
    TArray<uint8> Data;
    if (!FFileHelper::LoadFileToArray(Data, *FilePath))
        return false;

    int32 Offset = 0;
    auto Read = [&](void* Ptr, int32 Size) {
        FMemory::Memcpy(Ptr, Data.GetData() + Offset, Size);
        Offset += Size;
    };

    uint32 Magic, Version;
    Read(&Magic, sizeof(Magic));
    Read(&Version, sizeof(Version));
    if (Magic != 0x53444642) return false;

    Read(&OutBrush.Dimensions, sizeof(FIntVector));
    Read(&OutBrush.VoxelSize, sizeof(float));
    Read(&OutBrush.OriginOffset, sizeof(FVector));

    int32 NumVoxels = OutBrush.Dimensions.X * OutBrush.Dimensions.Y * OutBrush.Dimensions.Z;
    OutBrush.SDFValues.SetNumUninitialized(NumVoxels);
    Read(OutBrush.SDFValues.GetData(), NumVoxels * sizeof(float));

    return true;
}

bool FBrushAssetEditorUtils::GenerateSDFBrushFromStaticMesh(UStaticMesh* Mesh, const FTransform& MeshTransform, float InVoxelSize, FCustomSDFBrush& OutBrush)
{
#if WITH_EDITOR
    if (!Mesh || !Mesh->GetRenderData() || Mesh->GetRenderData()->LODResources.Num() == 0)
        return false;

    const FStaticMeshLODResources& LOD = Mesh->GetRenderData()->LODResources[0];
    const FPositionVertexBuffer& PosBuffer = LOD.VertexBuffers.PositionVertexBuffer;
    const FRawStaticIndexBuffer& IndexBuffer = LOD.IndexBuffer;

    // Determine bounds
    FBox LocalBounds(ForceInit);
    for (uint32 i = 0; i < PosBuffer.GetNumVertices(); ++i)
    {
        FVector WorldPos = MeshTransform.TransformPosition((FVector)PosBuffer.VertexPosition(i));
        LocalBounds += WorldPos;
    }

    FVector Extents = LocalBounds.GetSize();
    FVector Min = LocalBounds.Min;

    FIntVector Dimensions = FIntVector(
        FMath::CeilToInt(Extents.X / InVoxelSize),
        FMath::CeilToInt(Extents.Y / InVoxelSize),
        FMath::CeilToInt(Extents.Z / InVoxelSize)
    );

    OutBrush.Dimensions = Dimensions;
    OutBrush.VoxelSize = InVoxelSize;
    OutBrush.OriginOffset = Min;
    OutBrush.SDFValues.SetNumZeroed(Dimensions.X * Dimensions.Y * Dimensions.Z);

    // Voxel center sampling
    ParallelFor(Dimensions.X, [&](int32 X)
    {
        for (int32 Y = 0; Y < Dimensions.Y; ++Y)
        for (int32 Z = 0; Z < Dimensions.Z; ++Z)
        {
            FVector SamplePoint = Min + FVector(X + 0.5f, Y + 0.5f, Z + 0.5f) * InVoxelSize;
            float ClosestDistance = FLT_MAX;

            // Iterate triangles
            for (int32 TriIdx = 0; TriIdx < IndexBuffer.GetNumIndices(); TriIdx += 3)
            {
                FVector V0 = MeshTransform.TransformPosition((FVector)PosBuffer.VertexPosition(IndexBuffer.GetIndex(TriIdx)));
                FVector V1 = MeshTransform.TransformPosition((FVector)PosBuffer.VertexPosition(IndexBuffer.GetIndex(TriIdx + 1)));
                FVector V2 = MeshTransform.TransformPosition((FVector)PosBuffer.VertexPosition(IndexBuffer.GetIndex(TriIdx + 2)));

                FVector Closest = FMath::ClosestPointOnTriangleToPoint(SamplePoint, V0, V1, V2);
                float Dist = FVector::Dist(SamplePoint, Closest);

                if (Dist < ClosestDistance)
                    ClosestDistance = Dist;
            }

            int32 Index = OutBrush.GetIndex(X, Y, Z);
            OutBrush.SDFValues[Index] = ClosestDistance;
        }
    });

    return true;
#else
    return false;
#endif
}
