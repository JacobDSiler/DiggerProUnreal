#include "FastDebugRenderer.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/LineBatchComponent.h"
#include "DrawDebugHelpers.h"

// UFastDebugRenderer Implementation

void UFastDebugRenderer::DrawBoxLines(UWorld* World, const FVector& Center, const FVector& Extent, 
                                    const FRotator& Rotation, const FFastDebugConfig& Config)
{
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("World not valid in UFastDebugRenderer::DrawBoxLines!"));
        return;
    }
    
    // Generate box vertices
    TArray<FVector> Vertices;
    TArray<int32> Indices;
    GenerateBoxVertices(Center, Extent, Rotation, Vertices, Indices);
    
    // Draw lines using the vertices
    FColor DrawColor = Config.Color.ToFColor(true);
    
    for (int32 i = 0; i < Indices.Num(); i += 2)
    {
        if (i + 1 < Indices.Num())
        {
            const FVector& Start = Vertices[Indices[i]];
            const FVector& End = Vertices[Indices[i + 1]];
            
            DrawDebugLine(World, Start, End, DrawColor, false, Config.Duration, 0, Config.Thickness);
        }
    }
}

void UFastDebugRenderer::DrawBoxLinesBatch(UWorld* World, const TArray<FVector>& Centers, const FVector& Extent,
                                         const FFastDebugConfig& Config)
{
    if (!World || Centers.Num() == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("World not valid in UFastDebugRenderer::DrawBoxLinesBatch!"));
        return;
    }
    
    // Batch draw all boxes at once
    FColor DrawColor = Config.Color.ToFColor(true);
    
    for (const FVector& Center : Centers)
    {
        // Generate vertices for this box
        TArray<FVector> Vertices;
        TArray<int32> Indices;
        GenerateBoxVertices(Center, Extent, FRotator::ZeroRotator, Vertices, Indices);
        
        // Draw all lines for this box
        for (int32 i = 0; i < Indices.Num(); i += 2)
        {
            if (i + 1 < Indices.Num())
            {
                const FVector& Start = Vertices[Indices[i]];
                const FVector& End = Vertices[Indices[i + 1]];
                
                DrawDebugLine(World, Start, End, DrawColor, false, Config.Duration, 0, Config.Thickness);
            }
        }
    }
}

void UFastDebugRenderer::DrawSphereLines(UWorld* World, const FVector& Center, float Radius, int32 Segments,
                                       const FFastDebugConfig& Config)
{
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("World not valid in UFastDebugRenderer::DrawSphereLines!"));
        return;
    }
    
    // Generate sphere vertices
    TArray<FVector> Vertices;
    TArray<int32> Indices;
    GenerateSphereVertices(Center, Radius, Segments, Vertices, Indices);
    
    // Draw lines using the vertices
    FColor DrawColor = Config.Color.ToFColor(true);
    
    for (int32 i = 0; i < Indices.Num(); i += 2)
    {
        if (i + 1 < Indices.Num())
        {
            const FVector& Start = Vertices[Indices[i]];
            const FVector& End = Vertices[Indices[i + 1]];
            
            DrawDebugLine(World, Start, End, DrawColor, false, Config.Duration, 0, Config.Thickness);
        }
    }
}

void UFastDebugRenderer::DrawSphereLinesBatch(UWorld* World, const TArray<FVector>& Centers, float Radius,
                                            const FFastDebugConfig& Config)
{
    if (!World || Centers.Num() == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("World not valid in UFastDebugRenderer::DrawSphereLinesBatch!"));
        return;
    }
    
    FColor DrawColor = Config.Color.ToFColor(true);
    
    for (const FVector& Center : Centers)
    {
        // Generate vertices for this sphere
        TArray<FVector> Vertices;
        TArray<int32> Indices;
        GenerateSphereVertices(Center, Radius, 16, Vertices, Indices);
        
        // Draw all lines for this sphere
        for (int32 i = 0; i < Indices.Num(); i += 2)
        {
            if (i + 1 < Indices.Num())
            {
                const FVector& Start = Vertices[Indices[i]];
                const FVector& End = Vertices[Indices[i + 1]];
                
                DrawDebugLine(World, Start, End, DrawColor, false, Config.Duration, 0, Config.Thickness);
            }
        }
    }
}

void UFastDebugRenderer::DrawLine(UWorld* World, const FVector& Start, const FVector& End,
                                const FFastDebugConfig& Config)
{
    if (!World)    {
        UE_LOG(LogTemp, Error, TEXT("World not valid in UFastDebugRenderer::DrawSphereLinesBatch!"));
        return;
    }
    
    FColor DrawColor = Config.Color.ToFColor(true);
    DrawDebugLine(World, Start, End, DrawColor, false, Config.Duration, 0, Config.Thickness);
}

void UFastDebugRenderer::DrawLinesBatch(UWorld* World, const TArray<FVector>& StartPoints, const TArray<FVector>& EndPoints,
                                      const FFastDebugConfig& Config)
{
    if (!World || StartPoints.Num() == 0 || EndPoints.Num() != StartPoints.Num())
    {
        UE_LOG(LogTemp, Error, TEXT("World not valid in UFastDebugRenderer::DrawSphereLinesBatch!"));
        return;
    }
    
    FColor DrawColor = Config.Color.ToFColor(true);
    
    for (int32 i = 0; i < StartPoints.Num(); i++)
    {
        DrawDebugLine(World, StartPoints[i], EndPoints[i], DrawColor, false, Config.Duration, 0, Config.Thickness);
    }
}

void UFastDebugRenderer::DrawPoint(UWorld* World, const FVector& Location, float Size,
                                 const FFastDebugConfig& Config)
{
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("World not valid in UFastDebugRenderer::DrawPoint!"));
        return;
    }
    
    FColor DrawColor = Config.Color.ToFColor(true);
    DrawDebugPoint(World, Location, Size, DrawColor, false, Config.Duration, 0);
}

void UFastDebugRenderer::DrawPointsBatch(UWorld* World, const TArray<FVector>& Locations, float Size,
                                       const FFastDebugConfig& Config)
{
    if (!World || Locations.Num() == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("World not valid in UFastDebugRenderer::DrawPointsBatch!"));
        return;
    }
    
    FColor DrawColor = Config.Color.ToFColor(true);
    
    for (const FVector& Location : Locations)
    {
        DrawDebugPoint(World, Location, Size, DrawColor, false, Config.Duration, 0);
    }
}

void UFastDebugRenderer::GenerateBoxVertices(const FVector& Center, const FVector& Extent, const FRotator& Rotation,
                                           TArray<FVector>& OutVertices, TArray<int32>& OutIndices)
{
    // Generate the 8 corner vertices of a box
    OutVertices.Empty(8);
    
    // UE5's DrawDebugBox uses Extent as half-extents (distance from center to edge)
    // So we use Extent directly, not Extent * 0.5f
    FVector HalfExtent = Extent; // This IS the half extent already!
    FQuat RotQuat = Rotation.Quaternion();
    
    // 8 corners of the box (local space)
    TArray<FVector> LocalVertices = {
        FVector(-HalfExtent.X, -HalfExtent.Y, -HalfExtent.Z), // 0: Bottom-Back-Left
        FVector(+HalfExtent.X, -HalfExtent.Y, -HalfExtent.Z), // 1: Bottom-Back-Right
        FVector(+HalfExtent.X, +HalfExtent.Y, -HalfExtent.Z), // 2: Bottom-Front-Right
        FVector(-HalfExtent.X, +HalfExtent.Y, -HalfExtent.Z), // 3: Bottom-Front-Left
        FVector(-HalfExtent.X, -HalfExtent.Y, +HalfExtent.Z), // 4: Top-Back-Left
        FVector(+HalfExtent.X, -HalfExtent.Y, +HalfExtent.Z), // 5: Top-Back-Right
        FVector(+HalfExtent.X, +HalfExtent.Y, +HalfExtent.Z), // 6: Top-Front-Right
        FVector(-HalfExtent.X, +HalfExtent.Y, +HalfExtent.Z)  // 7: Top-Front-Left
    };
    
    // Transform vertices to world space
    for (const FVector& LocalVertex : LocalVertices)
    {
        FVector WorldVertex = Center + RotQuat.RotateVector(LocalVertex);
        OutVertices.Add(WorldVertex);
    }
    
    // Define the 12 edges of the box (24 indices for 12 lines)
    OutIndices = {
        // Bottom face edges
        0, 1,  1, 2,  2, 3,  3, 0,
        // Top face edges  
        4, 5,  5, 6,  6, 7,  7, 4,
        // Vertical edges
        0, 4,  1, 5,  2, 6,  3, 7
    };
}

void UFastDebugRenderer::GenerateSphereVertices(const FVector& Center, float Radius, int32 Segments,
                                              TArray<FVector>& OutVertices, TArray<int32>& OutIndices)
{
    OutVertices.Empty();
    OutIndices.Empty();
    
    // Generate 3 circles: XY, XZ, and YZ planes
    const int32 TotalVertices = Segments * 3;
    OutVertices.Reserve(TotalVertices);
    OutIndices.Reserve(Segments * 6); // 2 indices per line segment, 3 circles
    
    // XY Circle (around Z axis)
    for (int32 i = 0; i < Segments; i++)
    {
        float Angle = (i * 2.0f * PI) / Segments;
        FVector Vertex = Center + FVector(
            FMath::Cos(Angle) * Radius,
            FMath::Sin(Angle) * Radius,
            0.0f
        );
        OutVertices.Add(Vertex);
    }
    
    // XZ Circle (around Y axis)
    for (int32 i = 0; i < Segments; i++)
    {
        float Angle = (i * 2.0f * PI) / Segments;
        FVector Vertex = Center + FVector(
            FMath::Cos(Angle) * Radius,
            0.0f,
            FMath::Sin(Angle) * Radius
        );
        OutVertices.Add(Vertex);
    }
    
    // YZ Circle (around X axis)
    for (int32 i = 0; i < Segments; i++)
    {
        float Angle = (i * 2.0f * PI) / Segments;
        FVector Vertex = Center + FVector(
            0.0f,
            FMath::Cos(Angle) * Radius,
            FMath::Sin(Angle) * Radius
        );
        OutVertices.Add(Vertex);
    }
    
    // Generate indices for the three circles
    for (int32 Circle = 0; Circle < 3; Circle++)
    {
        int32 CircleStart = Circle * Segments;
        for (int32 i = 0; i < Segments; i++)
        {
            int32 Current = CircleStart + i;
            int32 Next = CircleStart + ((i + 1) % Segments);
            
            OutIndices.Add(Current);
            OutIndices.Add(Next);
        }
    }
}

// UFastDebugSubsystem Implementation

void UFastDebugSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    LineBatchComponent = nullptr;
}

void UFastDebugSubsystem::Deinitialize()
{
    LineBatchComponent = nullptr;
    Super::Deinitialize();
}

UFastDebugSubsystem* UFastDebugSubsystem::Get(const UObject* WorldContext)
{
    if (const UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::LogAndReturnNull))
    {
        return World->GetSubsystem<UFastDebugSubsystem>();
    }
    return nullptr;
}

void UFastDebugSubsystem::EnsureLineBatchComponent()
{
    if (!LineBatchComponent)
    {
        UWorld* World = GetWorld();
        if (World)
        {
            LineBatchComponent = World->LineBatcher;
            UE_LOG(LogTemp, Warning, TEXT("FastDebugSubsystem: Using world LineBatcher"));
        }
    }
}

void UFastDebugSubsystem::DrawBox(const FVector& Center, const FVector& Extent,
                                const FRotator& Rotation, const FFastDebugConfig& Config)
{
    UWorld* World = GetWorld();
    if (World)
    {
        UFastDebugRenderer::DrawBoxLines(World, Center, Extent, Rotation, Config);
    }
}

void UFastDebugSubsystem::DrawSphere(const FVector& Center, float Radius,
                                   const FFastDebugConfig& Config)
{
    UWorld* World = GetWorld();
    if (World)
    {
        UFastDebugRenderer::DrawSphereLines(World, Center, Radius, 16, Config);
    }
}

void UFastDebugSubsystem::DrawLine(const FVector& Start, const FVector& End,
                                 const FFastDebugConfig& Config)
{
    UWorld* World = GetWorld();
    if (World)
    {
        UFastDebugRenderer::DrawLine(World, Start, End, Config);
    }
}

void UFastDebugSubsystem::DrawPoint(const FVector& Location, float Size,
                                  const FFastDebugConfig& Config)
{
    UWorld* World = GetWorld();
    if (World)
    {
        UFastDebugRenderer::DrawPoint(World, Location, Size, Config);
    }
}

void UFastDebugSubsystem::DrawBoxes(const TArray<FVector>& Centers, const FVector& Extent,
                                  const FFastDebugConfig& Config)
{
    UWorld* World = GetWorld();
    if (World)
    {
        UE_LOG(LogTemp, Warning, TEXT("FastDebugSubsystem::DrawBoxes - Drawing %d boxes"), Centers.Num());
        UFastDebugRenderer::DrawBoxLinesBatch(World, Centers, Extent, Config);
    }
}

void UFastDebugSubsystem::DrawSpheres(const TArray<FVector>& Centers, float Radius,
                                    const FFastDebugConfig& Config)
{
    UWorld* World = GetWorld();
    if (World)
    {
        UFastDebugRenderer::DrawSphereLinesBatch(World, Centers, Radius, Config);
    }
}

void UFastDebugSubsystem::DrawLines(const TArray<FVector>& StartPoints, const TArray<FVector>& EndPoints,
                                  const FFastDebugConfig& Config)
{
    UWorld* World = GetWorld();
    if (World)
    {
        UFastDebugRenderer::DrawLinesBatch(World, StartPoints, EndPoints, Config);
    }
}

void UFastDebugSubsystem::DrawPoints(const TArray<FVector>& Locations, float Size,
                                   const FFastDebugConfig& Config)
{
    UWorld* World = GetWorld();
    if (World)
    {
        UFastDebugRenderer::DrawPointsBatch(World, Locations, Size, Config);
    }
}