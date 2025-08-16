#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RHIResources.h"
#include "RHIDefinitions.h"

class UVoxelChunk;
struct FBrushStroke;

// Per-chunk GPU resources
struct FVoxelGPUResources
{
	int32 N = 0; // voxels per dimension
	FTexture3DRHIRef SDFTex;                // R32_FLOAT
	FUnorderedAccessViewRHIRef SDF_UAV;     // RW access
	FShaderResourceViewRHIRef  SDF_SRV;     // sampling if needed
	bool IsValid() const { return SDFTex.IsValid() && SDF_UAV.IsValid(); }
};

class FVoxelGPUBackend
{
public:
	static FVoxelGPUBackend& Get();

	// Ensure GPU resources for this chunk (creates if missing)
	void InitForChunk(UVoxelChunk* Chunk, int32 VoxelsPerDim);

	// Apply a brush to the 3D SDF texture (GPU). Returns approximate modified voxel count.
	int32 ApplyBrush(UVoxelChunk* Chunk, const FBrushStroke& Stroke);

	// Optional: clear/destroy on chunk destroy
	void ReleaseForChunk(UVoxelChunk* Chunk);

	// Accessor for other systems (marching on GPU later)
	const FVoxelGPUResources* GetResources(UVoxelChunk* Chunk) const;

private:
	TMap<TWeakObjectPtr<UVoxelChunk>, FVoxelGPUResources> Resources;

	// Internal helpers
	void CreateSDFTexture(FVoxelGPUResources& Out, int32 N);
};
