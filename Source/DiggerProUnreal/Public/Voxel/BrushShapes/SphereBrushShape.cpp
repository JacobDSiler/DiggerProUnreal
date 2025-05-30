#include "SphereBrushShape.h"
#include "VoxelConversion.h"
#include "Math/UnrealMathUtility.h"

float USphereBrushShape::CalculateSDF_Implementation(
    const FVector& WorldPos,
    const FVector& BrushCenter,
    float Radius,
    float Strength,
    float Falloff,
    float TerrainHeight, // This TerrainHeight is passed from the brush application logic
    bool bDig
) const
{
    // SDF conventions
    const float SDF_SOLID = FVoxelConversion::SDF_SOLID; // e.g., -1.0f
    const float SDF_AIR   = FVoxelConversion::SDF_AIR;   // e.g., 1.0f

    // Calculate distance from brush center
    float Distance = FVector::Dist(WorldPos, BrushCenter);

    // Early out if outside the brush's influence
    float OuterRadius = Radius + Falloff;
    if (Distance > OuterRadius)
        return 0.0f;

    // Calculate SDF value based on distance
    float SDFValue;

    // Determine if the current WorldPos is below the terrain height provided to the brush
    // Note: This TerrainHeight should ideally be the landscape height at WorldPos.XY
    bool bIsBelowTerrain = WorldPos.Z < TerrainHeight;

    if (bDig)
    {
        // For digging operations (creating air)
        if (Distance <= Radius)
        {
            // Inside the main radius: full air value
            SDFValue = SDF_AIR;
        }
        else
        {
            // In the falloff zone: blend from air
            float t = (Distance - Radius) / Falloff;
            t = FMath::SmoothStep(0.0f, 1.0f, t);

            if (bIsBelowTerrain)
            {
                // Below terrain, digging: blend from air (SDF_AIR) to solid (SDF_SOLID) in the falloff.
                // This explicitly creates a solid boundary at the edge of the brush below terrain.
                SDFValue = FMath::Lerp(SDF_AIR, SDF_SOLID, t);
            }
            else
            {
                // Above terrain, digging: blend from air (SDF_AIR) to zero.
                SDFValue = FMath::Lerp(SDF_AIR, 0.0f, t);
            }
        }
    }
    else // !bDig (Adding)
    {
        // For adding operations (creating solid)
        if (Distance <= Radius)
        {
            // Inside the main radius: full solid value
            SDFValue = SDF_SOLID;
        }
        else
        {
            // In the falloff zone: blend from solid to zero.
            // This behavior is generally fine regardless of terrain height for adding.
            float t = (Distance - Radius) / Falloff;
            t = FMath::SmoothStep(0.0f, 1.0f, t);
            SDFValue = FMath::Lerp(SDF_SOLID, 0.0f, t);
        }
    }

    // Apply strength
    return SDFValue * Strength;
}
