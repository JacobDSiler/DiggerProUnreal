#pragma once

#include "CoreMinimal.h"

// Forward decls (avoid including heavy headers here)
class USparseVoxelGrid;
class UVoxelChunk;
struct FBrushStroke;

namespace VoxelGPU
{
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
