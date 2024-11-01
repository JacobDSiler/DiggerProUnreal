#include "LandscapeHoleManager.h"

#include "VT/RuntimeVirtualTexture.h"

// LandscapeHoleManager.cpp
void ULandscapeHoleManager::Initialize()
{
	if (!HoleMaskTexture || !TargetLandscape)
		return;

	// Create the material instance if needed
	if (!HoleMaskMaterial)
	{
		UMaterialInterface* BaseMaterial = Cast<UMaterialInterface>(
			StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, 
				TEXT("/Game/Materials/M_LandscapeHole")));
        
		if (BaseMaterial)
		{
			HoleMaskMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		}
	}

	// Initialize the RVT if needed
	//HoleMaskTexture->Init(HoleMaskResolution.X, HoleMaskResolution.Y, ERuntimeVirtualTextureMaterialType::BaseColor);
}

void ULandscapeHoleManager::CreateHole(FVector Location, float Radius, float FalloffDistance)
{
	if (!HoleMaskMaterial || !HoleMaskTexture)
		return;

	FVector2D HoleUV = WorldToTextureUV(Location);
    
	// Update the material parameters
	HoleMaskMaterial->SetVectorParameterValue("HolePosition", FLinearColor(HoleUV.X, HoleUV.Y, 0, 0));
	HoleMaskMaterial->SetScalarParameterValue("HoleRadius", Radius);
	HoleMaskMaterial->SetScalarParameterValue("HoleFalloff", FalloffDistance);
    
	// Trigger RVT update
	//HoleMaskTexture->MarkRenderDirty();
}

void ULandscapeHoleManager::RemoveHole(FVector Location, float Radius)
{
	//ToDo: Code to remove a given hole to go here.
}

FVector2D ULandscapeHoleManager::WorldToTextureUV(const FVector& WorldLocation) const
{
	/*if (!TargetLandscape)
		return FVector2D::ZeroVector;

	FVector LandscapeOrigin = TargetLandscape->GetActorLocation();
	FVector LandscapeScale = TargetLandscape->GetActorScale3D();
    
	// Convert world position to landscape-relative position
	FVector RelativePos = WorldLocation - LandscapeOrigin;
    
	// Convert to UV coordinates (0-1 range)
	float U = (RelativePos.X / (LandscapeScale.X * TargetLandscape->GetActorScale3D().X)) + 0.5f;
	float V = (RelativePos.Y / (LandscapeScale.Y * TargetLandscape->GetActorScale3D().Y)) + 0.5f;
    
	return FVector2D(U, V);*/
	return FVector2d(0,0);
}
