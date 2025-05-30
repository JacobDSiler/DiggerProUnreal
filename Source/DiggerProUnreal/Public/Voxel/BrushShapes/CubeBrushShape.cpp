// CubeBrushShape.cpp

#include "CubeBrushShape.h"
#include "VoxelConversion.h"
#include "Math/UnrealMathUtility.h"

float UCubeBrushShape::CalculateSDF_Implementation(
    const FVector& WorldPos,
    const FVector& BrushCenter,
    float SizeX,      // Use Radius param
    float SizeY,      // Use Strength param
    float SizeZ,      // Use Falloff param
    float TerrainHeight,
    bool bDig
) const
{
    const float SDF_SOLID = FVoxelConversion::SDF_SOLID;
    const float SDF_AIR   = FVoxelConversion::SDF_AIR;

    FVector HalfSize(SizeX * 0.5f, SizeY * 0.5f, SizeZ * 0.5f);
    FVector Local = WorldPos - BrushCenter;
    FVector D = FVector(
        FMath::Abs(Local.X) - HalfSize.X,
        FMath::Abs(Local.Y) - HalfSize.Y,
        FMath::Abs(Local.Z) - HalfSize.Z
    );

    float OutsideDist = FVector(
        FMath::Max(D.X, 0.0f),
        FMath::Max(D.Y, 0.0f),
        FMath::Max(D.Z, 0.0f)
    ).Size();
    float InsideDist = FMath::Min(FMath::Max3(D.X, D.Y, D.Z), 0.0f);
    float Distance = OutsideDist + InsideDist;

    // Use TerrainHeight as falloff for cube, or set a default
    float Falloff = FMath::Max(TerrainHeight, 0.0001f);
    float t = FMath::Clamp((Distance + Falloff) / Falloff, 0.0f, 1.0f);

    float TargetSDF = bDig ? SDF_AIR : SDF_SOLID;
    float SDF = FMath::Lerp(TargetSDF, 0.0f, t);

    // Use bDig as a strength multiplier if needed, or always 1.0
    return SDF;
}
