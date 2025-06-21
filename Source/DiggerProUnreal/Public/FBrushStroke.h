#pragma once

#include "CoreMinimal.h"
#include "VoxelBrushTypes.h"
#include "FBrushStroke.generated.h"

USTRUCT(BlueprintType)
struct FBrushStroke
{
    GENERATED_BODY()
    
    FORCEINLINE bool IsValid() const
    {
        // Example: require a positive radius and a nonzero position
        return BrushRadius > 0.0f && !BrushPosition.IsNearlyZero();
    }

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Brush")
    EVoxelBrushType BrushType;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Brush")
    FVector BrushPosition;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Brush")
    float BrushRadius;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Brush")
    float BrushStrength;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Brush")
    float BrushFalloff;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Brush")
    FRotator BrushRotation;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Brush")
    float BrushLength;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Brush")
    float BrushAngle;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Brush")
    FVector BrushOffset;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Brush")
    bool bDig;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Brush")
    bool bUseAdvancedCubeBrush;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Brush")
    float AdvancedCubeHalfExtentX;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Brush")
    float AdvancedCubeHalfExtentY;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Brush")
    float AdvancedCubeHalfExtentZ;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Brush")
    float TorusInnerRadius;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Brush")
    int32 NumSteps;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Brush")
    bool bSpiral;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Brush")
    bool bIsFilled;

    UPROPERTY(Transient)
    UVoxelBrushShape* BrushShape;

    // In FBrushStroke constructor
    FBrushStroke()
        : BrushType(EVoxelBrushType::Sphere)
        , BrushPosition(FVector::ZeroVector)
        , BrushRadius(100.0f)
        , BrushStrength(1.0f)
        , BrushFalloff(0.2f)
        , BrushRotation(FRotator::ZeroRotator)
        , BrushLength(200.0f)
        , BrushAngle(0.0f)  // 0 degrees for strict shapes by default
        , BrushOffset(FVector::ZeroVector)
        , bDig(true)
        , bUseAdvancedCubeBrush(false)
        , AdvancedCubeHalfExtentX(50.0f)
        , AdvancedCubeHalfExtentY(50.0f)
        , AdvancedCubeHalfExtentZ(50.0f)
        , TorusInnerRadius(25.0f)
        , NumSteps(5)
        , bSpiral(false)
        , bIsFilled(false)
        , BrushShape(nullptr)
    {}
};
