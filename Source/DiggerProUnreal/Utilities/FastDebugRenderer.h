#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "Components/LineBatchComponent.h"
#include "FastDebugRenderer.generated.h"

USTRUCT(BlueprintType)
struct FFastDebugConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FLinearColor Color = FLinearColor::Red;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Duration = 5.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Thickness = 2.0f;

    FFastDebugConfig() = default;
    FFastDebugConfig(const FLinearColor& InColor, float InDuration = 0.0f, float InThickness = 2.0f)
        : Color(InColor), Duration(InDuration), Thickness(InThickness) {}
};

// Simple vertex structure for our custom rendering
USTRUCT()
struct FDebugVertex
{
    GENERATED_BODY()
    
    FVector Position;
    FLinearColor Color;
    
    FDebugVertex() = default;
    FDebugVertex(const FVector& InPos, const FLinearColor& InColor)
        : Position(InPos), Color(InColor) {}
};

UCLASS()
class DIGGERPROUNREAL_API UFastDebugRenderer : public UObject
{
    GENERATED_BODY()

public:
    // Batch line drawing - uses UE5's optimized line batch system
    static void DrawBoxLines(UWorld* World, const FVector& Center, const FVector& Extent, 
                           const FRotator& Rotation = FRotator::ZeroRotator,
                           const FFastDebugConfig& Config = FFastDebugConfig());
    
    static void DrawBoxLinesBatch(UWorld* World, const TArray<FVector>& Centers, const FVector& Extent,
                                const FFastDebugConfig& Config = FFastDebugConfig());
    
    static void DrawSphereLines(UWorld* World, const FVector& Center, float Radius, int32 Segments = 16,
                              const FFastDebugConfig& Config = FFastDebugConfig());
    
    static void DrawSphereLinesBatch(UWorld* World, const TArray<FVector>& Centers, float Radius,
                                   const FFastDebugConfig& Config = FFastDebugConfig());
    
    static void DrawLine(UWorld* World, const FVector& Start, const FVector& End,
                        const FFastDebugConfig& Config = FFastDebugConfig());
    
    static void DrawLinesBatch(UWorld* World, const TArray<FVector>& StartPoints, const TArray<FVector>& EndPoints,
                             const FFastDebugConfig& Config = FFastDebugConfig());
    
    static void DrawPoint(UWorld* World, const FVector& Location, float Size = 10.0f,
                         const FFastDebugConfig& Config = FFastDebugConfig());
    
    static void DrawPointsBatch(UWorld* World, const TArray<FVector>& Locations, float Size = 10.0f,
                               const FFastDebugConfig& Config = FFastDebugConfig());

private:
    // Generate box outline vertices
    static void GenerateBoxVertices(const FVector& Center, const FVector& Extent, const FRotator& Rotation,
                                   TArray<FVector>& OutVertices, TArray<int32>& OutIndices);
    
    // Generate sphere outline vertices  
    static void GenerateSphereVertices(const FVector& Center, float Radius, int32 Segments,
                                      TArray<FVector>& OutVertices, TArray<int32>& OutIndices);
};

UCLASS()
class DIGGERPROUNREAL_API UFastDebugSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    UFUNCTION(BlueprintCallable, Category = "Fast Debug", meta = (CallInEditor = "true"))
    static UFastDebugSubsystem* Get(const UObject* WorldContext);

    // Main drawing functions
    UFUNCTION(BlueprintCallable, Category = "Fast Debug")
    void DrawBox(const FVector& Center, const FVector& Extent = FVector(50.0f),
                 const FRotator& Rotation = FRotator::ZeroRotator,
                 const FFastDebugConfig& Config = FFastDebugConfig());

    UFUNCTION(BlueprintCallable, Category = "Fast Debug")
    void DrawSphere(const FVector& Center, float Radius = 50.0f,
                    const FFastDebugConfig& Config = FFastDebugConfig());

    UFUNCTION(BlueprintCallable, Category = "Fast Debug")
    void DrawLine(const FVector& Start, const FVector& End,
                  const FFastDebugConfig& Config = FFastDebugConfig());

    UFUNCTION(BlueprintCallable, Category = "Fast Debug")
    void DrawPoint(const FVector& Location, float Size = 10.0f,
                   const FFastDebugConfig& Config = FFastDebugConfig());

    // Batch functions for maximum performance
    UFUNCTION(BlueprintCallable, Category = "Fast Debug")
    void DrawBoxes(const TArray<FVector>& Centers, const FVector& Extent = FVector(50.0f),
                   const FFastDebugConfig& Config = FFastDebugConfig());

    UFUNCTION(BlueprintCallable, Category = "Fast Debug") 
    void DrawSpheres(const TArray<FVector>& Centers, float Radius = 50.0f,
                     const FFastDebugConfig& Config = FFastDebugConfig());

    UFUNCTION(BlueprintCallable, Category = "Fast Debug")
    void DrawLines(const TArray<FVector>& StartPoints, const TArray<FVector>& EndPoints,
                   const FFastDebugConfig& Config = FFastDebugConfig());

    UFUNCTION(BlueprintCallable, Category = "Fast Debug")
    void DrawPoints(const TArray<FVector>& Locations, float Size = 10.0f,
                    const FFastDebugConfig& Config = FFastDebugConfig());

private:
    UPROPERTY()
    ULineBatchComponent* LineBatchComponent;
    
    void EnsureLineBatchComponent();
};

// Convenience macros that actually work
#define FAST_DEBUG_BOX(Location, Extent, Color) \
    if (auto* FastDebug = UFastDebugSubsystem::Get(this)) { \
        FastDebug->DrawBox(Location, Extent, FRotator::ZeroRotator, FFastDebugConfig(Color, 0.0f, 2.0f)); \
    }

#define FAST_DEBUG_SPHERE(Location, Radius, Color) \
    if (auto* FastDebug = UFastDebugSubsystem::Get(this)) { \
        FastDebug->DrawSphere(Location, Radius, FFastDebugConfig(Color, 0.0f, 2.0f)); \
    }

#define FAST_DEBUG_BOXES_BATCH(Locations, Extent, Color) \
    if (auto* FastDebug = UFastDebugSubsystem::Get(this)) { \
        FastDebug->DrawBoxes(Locations, Extent, FFastDebugConfig(Color, 0.0f, 2.0f)); \
    }

#define FAST_DEBUG_POINTS_BATCH(Locations, Size, Color) \
    if (auto* FastDebug = UFastDebugSubsystem::Get(this)) { \
        FastDebug->DrawPoints(Locations, Size, FFastDebugConfig(Color, 0.0f, 2.0f)); \
    }