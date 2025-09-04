// FVoxelSDFHelper.h
#pragma once

#include "VoxelConversion.h"

class FVoxelSDFHelper
{
public:
    // For digging (subtractive): air wins
    static float BlendDig(float ExistingSDF, float NewSDF)
    {
        return FMath::Max(ExistingSDF, NewSDF);
    }

    // For adding (additive): solid wins
    static float BlendAdd(float ExistingSDF, float NewSDF)
    {
        return FMath::Min(ExistingSDF, NewSDF);
    }

    // For new voxels in a sparse grid
    static float InitialSDF(bool bDig)
    {
        return bDig ? FVoxelConversion::SDF_AIR : FVoxelConversion::SDF_SOLID;
    }
};

