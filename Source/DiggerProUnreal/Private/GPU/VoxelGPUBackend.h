#pragma once

#include "CoreMinimal.h"

// Forward decls (avoid including heavy headers here)
class USparseVoxelGrid;
class UVoxelChunk;
struct FBrushStroke;

namespace VoxelGPU
{
        // When true, forces the system to bypass GPU code and execute the CPU
        // path.  Used by the stubbed GPU helpers to fall back without recursing
        // back into the GPU entry points.
        extern thread_local bool GForceCPU;

        // RAII helper that temporarily forces CPU execution within its scope.
        struct FForceCPU
        {
                FForceCPU()  { GForceCPU = true;  }
                ~FForceCPU() { GForceCPU = false; }
        };

        // Brush dispatch description (keep tiny for now; expand as we wire buffers)
        struct FBrushDispatchDesc
        {
                FVector ChunkOriginWS = FVector::ZeroVector;
		float   VoxelSize     = 1.0f;
		int32   VoxelsPerSide = 0;
		bool    bDig          = true;
		// NOTE: Real path will also need SDF buffers, region bounds, etc.
	};

	// Returns true if a GPU path actually ran. For now: always false (stub).
	bool TryApplyBrushGPU(const UVoxelChunk* Chunk, const FBrushStroke& Stroke, const FBrushDispatchDesc& Desc);
}

