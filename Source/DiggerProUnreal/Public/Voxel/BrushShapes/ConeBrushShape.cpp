// ConeBrushShape.cpp

#include "ConeBrushShape.h"
#include "VoxelConversion.h"
#include "Math/UnrealMathUtility.h"

float UConeBrushShape::CalculateSDF_Implementation(
    const FVector& WorldPos,
    const FVector& BrushCenter,
    float Radius,         // Cone base radius
    float Length,         // Use Strength param
    float Falloff,
    float bFilledParam,   // Use TerrainHeight param as bFilled (0.0 = false, >0.5 = true)
    bool bDig
) const
{

    // Interpret bFilled from bFilledParam (0.0 = false, >0.5 = true)
    bool bFilled = bFilledParam > 0.5f;

    // Assume cone is aligned along +Z, tip at BrushCenter, base at BrushCenter + (0,0,Length)
    FVector Local = WorldPos - BrushCenter;
    float h = Length;
    float r = Radius;

    // Project point onto cone axis (Z)
    float z = Local.Z;
    float radial = FVector2D(Local.X, Local.Y).Size();

    // SDF for a finite cone (see Inigo Quilez, "Distance to a cone")
    float s = (h - z) / h;
    float coneRadiusAtZ = r * s;
    float distToSurface = radial - coneRadiusAtZ;

    float Distance;
    if (bFilled)
    {
        // Inside the cone volume
        Distance = FMath::Max(distToSurface, -z); // -z: below tip is outside
    }
    else
    {
        // Only the shell
        Distance = FMath::Abs(distToSurface) - Falloff * 0.5f;
    }

    // Smooth falloff
    float t = FMath::Clamp((Distance + Falloff) / FMath::Max(Falloff, 0.0001f), 0.0f, 1.0f);

    float TargetSDF = bDig ? FVoxelConversion::SDF_AIR : FVoxelConversion::SDF_SOLID;
    float SDF = FMath::Lerp(TargetSDF, 0.0f, t);

    return SDF;
}
