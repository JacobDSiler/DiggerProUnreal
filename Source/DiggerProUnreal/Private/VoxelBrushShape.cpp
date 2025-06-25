#include "VoxelBrushShape.h"

#include "DiggerDebug.h"
#include "DiggerManager.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/GameModeBase.h"
#include "Kismet/GameplayStatics.h"

UVoxelBrushShape::UVoxelBrushShape()
    : BrushSize(150.0f), SDFChange(0), bIsDigging(false), World(nullptr), BrushRotation(FRotator::ZeroRotator),
      BrushLength(1.0f),
      BrushAngle(45.f), DiggerManager(nullptr), TargetChunk(nullptr), BrushType(EVoxelBrushType::Sphere)
{
}



bool UVoxelBrushShape::EnsureDiggerManager()
{
    // Get the terrain and grid settings from the DiggerManager
    if (!DiggerManager)
    {
        for (TActorIterator<ADiggerManager> It(World); It; ++It)
        {
            DiggerManager = *It;
            break;
        }
    }

    if (!DiggerManager)
    {
        if (DiggerDebug::Manager)
        {
            UE_LOG(LogTemp, Error, TEXT("DiggerManager is null!"));
        }
        return true;
    }
    return false;
}


void UVoxelBrushShape::InitializeBrush(EVoxelBrushType InBrushType, float InSize, FVector InLocation, ADiggerManager* DiggerManagerRef)
{
    BrushType = InBrushType;
    BrushSize = InSize;
    BrushLocation = InLocation;

    if(!World) World = GetSafeWorld();
    DiggerManager = DiggerManagerRef;
}


// Helper function to retrieve the target chunk from a brush position
UVoxelChunk* UVoxelBrushShape::GetTargetChunkFromBrushPosition(const FVector3d& BrushPosition)
{
    // Get a reference to the Digger Manager (or the system managing your chunks)
    if (!DiggerManager) return nullptr;

    // Calculate the chunk position based on the brush position
    FIntVector ChunkPosition = FVoxelConversion::WorldToChunk(BrushPosition);

    // Retrieve the chunk from the map
    UVoxelChunk* NewTargetChunk = DiggerManager->GetOrCreateChunkAtChunk(ChunkPosition);
    
    // Return the chunk if found, otherwise return nullptr
    return (NewTargetChunk) ? NewTargetChunk : nullptr;
}

UWorld* UVoxelBrushShape::GetSafeWorld() const
{
#if WITH_EDITOR
    if (GIsEditor && GEditor)
    {
        return GEditor->GetEditorWorldContext().World();
    }
#endif
    return GetWorld();
}

// Add this method to VoxelBrushShape
FBrushStroke UVoxelBrushShape::CreateBrushStroke(const FHitResult& HitResult, bool bIsDig) const
{
    FBrushStroke NewStroke;
    NewStroke.BrushPosition = HitResult.Location;  // This is the key - get position from hit
    NewStroke.BrushRadius = BrushSize;
    NewStroke.BrushFalloff = 0.f;
    NewStroke.BrushStrength = 1.f;
    NewStroke.bDig = bIsDig;
    NewStroke.BrushType = BrushType;

    return NewStroke;
}


bool UVoxelBrushShape::GetCameraHitLocation(FHitResult& OutHitResult)
{
#if WITH_EDITOR
    if (GIsEditor)
    {
        static bool bWarned = false;
        if (!bWarned)
        {
            if (DiggerDebug::Casts)
            {
                UE_LOG(LogTemp, Warning, TEXT("GetCameraHitLocation called in editor mode. This method is intended for runtime use with a PlayerController."));
            }
            bWarned = true;
        }
        return false;
    }
#endif

    SetWorld(GetWorld());
    if (!World)
    {
        if (DiggerDebug::Casts || DiggerDebug::Context)
        {
            UE_LOG(LogTemp, Warning, TEXT("GetCameraHitLocation:: No valid world found"));
        }
        return false;
    }

    APlayerController* PlayerController = World->GetFirstPlayerController();
    if (!PlayerController)
    {
        if (DiggerDebug::Casts)
        {
            UE_LOG(LogTemp, Warning, TEXT("GetCameraHitLocation:: No PlayerController found"));
        }
        return false;
    }

    float MouseX, MouseY;
    if (!PlayerController->GetMousePosition(MouseX, MouseY))
    {
        if (DiggerDebug::Casts)
        {
            UE_LOG(LogTemp, Warning, TEXT("GetCameraHitLocation:: Could not get mouse position"));
        }
        return false;
    }

    FVector WorldPosition, WorldDirection;
    if (!PlayerController->DeprojectScreenPositionToWorld(MouseX, MouseY, WorldPosition, WorldDirection))
    {
        if (DiggerDebug::Casts)
        {
            UE_LOG(LogTemp, Warning, TEXT("GetCameraHitLocation:: Could not deproject mouse position"));
        }
        return false;
    }

    FVector TraceStart = WorldPosition;
    FVector TraceEnd = WorldPosition + (WorldDirection * 50000.0f);

    DebugDrawLineIfEnabled(TraceStart, TraceEnd, FColor::Green, 2.0f);

    // Start the recursive trace through holes
    TArray<AActor*> IgnoredActors;
    FHitResult Hit = RecursiveTraceThroughHoles(TraceStart, TraceEnd, IgnoredActors, false, false, 0);

    if (Hit.bBlockingHit)
    {
        OutHitResult = Hit;
        DebugDrawSphereIfEnabled(OutHitResult.ImpactPoint, FColor::Red, 25.0f, 5.0f);

        if (OutHitResult.GetActor())
        {
            if (DiggerDebug::Casts)
            {
                UE_LOG(LogTemp, Warning, TEXT("GetCameraHitLocation:: Final Hit Actor: %s, Component: %s"), 
                    *OutHitResult.GetActor()->GetName(), 
                    OutHitResult.GetComponent() ? *OutHitResult.GetComponent()->GetName() : TEXT("None"));
            }
        }
        return true;
    }

    if (DiggerDebug::Casts)
    {
        UE_LOG(LogTemp, Warning, TEXT("GetCameraHitLocation:: No valid hit found."));
    }
    return false;
}

FHitResult UVoxelBrushShape::PerformComplexTrace(FVector& Start, FVector& End, AActor* IgnoredActor) const
{
    if (!World)
    {
        if (DiggerDebug::Casts || DiggerDebug::Context)
        {
            UE_LOG(LogTemp, Error, TEXT("World is null in PerformComplexTrace!"));
        }
        return FHitResult();
    }

    TArray<AActor*> IgnoredActors;
    if (IgnoredActor)
    {
        IgnoredActors.Add(IgnoredActor);
    }

    // Start with bPassedThroughHole = false, bIgnoreHolesNow = false, Depth = 0
    return RecursiveTraceThroughHoles(Start, End, IgnoredActors, false, false, 0);
}

FHitResult UVoxelBrushShape::RecursiveTraceThroughHoles_Internal(
    FVector& Start,
    FVector& End,
    TArray<AActor*>& IgnoredActors,
    int32 Depth,
    const FVector& OriginalDirection,
    bool bPassedThroughHole
) const
{
    if (Depth > 32)
        return FHitResult();

    FCollisionQueryParams Params;
    Params.bTraceComplex = true;
    Params.AddIgnoredActors(IgnoredActors);

    FHitResult Hit;
    bool bHit = World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);

    if (!bHit || !Hit.GetActor())
        return FHitResult();

    AActor* HitActor = Hit.GetActor();

    // If we hit a HoleBP, ignore it and keep tracing
    if (IsHoleBPActor(HitActor))
    {
        bPassedThroughHole = true;
        if (!IgnoredActors.Contains(HitActor))
        {
            IgnoredActors.Add(HitActor); // Only add HoleBP
        }
        FVector NewStart = Hit.Location + OriginalDirection * 0.1f;
        return RecursiveTraceThroughHoles_Internal(NewStart, End, IgnoredActors, Depth + 1, OriginalDirection, true);
    }

    // When we hit the landscape
    if (IsLandscape(HitActor))
    {
        if (bPassedThroughHole)
        {
            if (!IgnoredActors.Contains(HitActor))
            {
                IgnoredActors.Add(HitActor); // Only add Landscape
            }
            if (DiggerDebug::Casts)
            {
                UE_LOG(LogTemp, Error,
                       TEXT(
                           "Hit Landscape after passing through a hole, hopping Backward 1cm before the trace continues!"
                       ));
            }
            FVector NewStart = Hit.Location + OriginalDirection * -1; // Jump back 1cm
            return RecursiveTraceThroughHoles_Internal(NewStart, End, IgnoredActors, Depth + 1, OriginalDirection,
                                                       bPassedThroughHole);
        }
        else
        {
            if (DiggerDebug::Casts)
            {
                UE_LOG(LogTemp, Error, TEXT("Returning a landscape hit with !bPassedThroughHole!"));
            }
            // If we haven't passed through a hole, return the landscape hit
            return Hit;
        }
    }

    if (DiggerDebug::Casts)
    {
        UE_LOG(LogTemp, Error, TEXT("Returning Fallback Hit!"));
    }
    // Return the first non-HoleBP, non-landscape hit (e.g., procedural mesh)
    return Hit;
}




FHitResult UVoxelBrushShape::RecursiveTraceThroughHoles(
    FVector& Start,
    FVector& End,
    TArray<AActor*>& IgnoredActors,
    bool bPassedThroughHole,
    bool bIgnoreHolesNow,
    int32 Depth
) const
{
    const FVector OriginalDirection = (End - Start).GetSafeNormal();
    return RecursiveTraceThroughHoles_Internal(Start, End, IgnoredActors, 0, OriginalDirection, false);
}


FHitResult UVoxelBrushShape::SmartTrace(const FVector& Start, const FVector& End)
{
    World = GetSafeWorld();
    if (!World)
    {
        if (DiggerDebug::Casts || DiggerDebug::Context)
        {
            UE_LOG(LogTemp, Error, TEXT("SmartTrace: World is null!"));
        }
        return FHitResult();
    }

    if (DiggerDebug::Casts)
    {
        UE_LOG(LogTemp, Warning, TEXT("SmartTrace: Start=%s End=%s"), *Start.ToString(), *End.ToString());
    }

    FHitResult FirstHit;
    FCollisionQueryParams Params;
    Params.bTraceComplex = true;

    bool bHit = World->LineTraceSingleByChannel(FirstHit, Start, End, ECC_Visibility, Params);

    if (!bHit || !FirstHit.GetActor())
    {
        if (DiggerDebug::Casts)
        {
            UE_LOG(LogTemp, Warning, TEXT("SmartTrace: No hit at all!"));
        }
        return FHitResult();
    }

    if (DiggerDebug::Casts)
    {
        UE_LOG(LogTemp, Warning, TEXT("SmartTrace: First hit %s at %s"), *FirstHit.GetActor()->GetName(), *FirstHit.Location.ToString());
    }

    // Make Sure the Manager Is Set and Valid. Otherwise, it will exit early.
    EnsureDiggerManager();
    
    if (IsHoleBPActor(FirstHit.GetActor()))
    {
        // Start recursive trace from just past the HoleBP
        TArray<AActor*> IgnoredActors;
        IgnoredActors.Add(FirstHit.GetActor());
        FVector NewStart = FirstHit.Location + (End - Start).GetSafeNormal() * 0.1f;
        FVector NewEnd = End;
        // Call the public version (which sets OriginalDirection internally)
        return RecursiveTraceThroughHoles(NewStart, NewEnd, IgnoredActors, true, false, 1);
    }
    else
    {
        // Hit something else (landscape, mesh, etc.)
        return FirstHit;
    }
}



bool UVoxelBrushShape::IsHoleBPActor(const AActor* Actor) const
{
    if (!Actor)
    {
        if (DiggerDebug::Casts)
        {
            UE_LOG(LogTemp, Warning, TEXT("IsHoleBPActor: Actor is null."));
        }
        return false;
    }
    if (!DiggerManager)
    {
        if (DiggerDebug::Casts || DiggerDebug::Manager)
        {
            UE_LOG(LogTemp, Warning, TEXT("IsHoleBPActor: DiggerManager is null."));
        }
        return false;
    }
    if (!DiggerManager->HoleBP)
    {
        if (DiggerDebug::Casts || DiggerDebug::Manager)
        {
            UE_LOG(LogTemp, Warning, TEXT("IsHoleBPActor: DiggerManager->HoleBP is null."));
        }
        return false;
    }
    if (DiggerDebug::Casts || DiggerDebug::Manager)
    {
        UE_LOG(LogTemp, Warning, TEXT("IsHoleBPActor: Hit actor class: %s, HoleBP class: %s"),
            *Actor->GetClass()->GetName(),
            *DiggerManager->HoleBP->GetClass()->GetName());
    }

    if (Actor->IsA(DiggerManager->HoleBP))
    {
        if (DiggerDebug::Casts || DiggerDebug::Manager)
        {
            UE_LOG(LogTemp, Warning, TEXT("IsHoleBPActor: Hit a HoleBP at location %s."), *Actor->GetActorLocation().ToString());
        }
        return true;
    }
    else
    {
        if (DiggerDebug::Casts)
        {
            UE_LOG(LogTemp, Warning, TEXT("IsHoleBPActor: Hit actor %s (class: %s) at location %s, not a HoleBP."),
                *Actor->GetName(),
                *Actor->GetClass()->GetName(),
                *Actor->GetActorLocation().ToString());
        }
        return false;
    }
}



// Helper to identify a Landscape actor
bool UVoxelBrushShape::IsLandscape(const AActor* Actor) const
{
    return Actor && Actor->IsA(ALandscapeProxy::StaticClass());
}

// Helper to identify a Procedural Mesh actor
bool UVoxelBrushShape::IsProceduralMesh(const AActor* Actor) const
{
    // Replace with your actual class check, e.g.:
    // return Actor && Actor->IsA(AProceduralMeshActor::StaticClass());
    return Actor && Actor->GetName().Contains(TEXT("ProceduralMesh")); // Example fallback
}

void UVoxelBrushShape::DebugDrawLineIfEnabled(FVector& Start, FVector& End, FColor Color, float Duration) const
{
    if (World && bEnableDebugDrawing)
    {
        DrawDebugLine(World, Start, End, Color, false, Duration, 0, 2.0f);
    }
}


bool UVoxelBrushShape::IsWithinBounds(const FVector& WorldPos, const FBrushStroke& Stroke) const
{
    return true;
}

void UVoxelBrushShape::DebugDrawPointIfEnabled(FVector& Location, FColor Color, float Size, float Duration) const
{
    if (World && bEnableDebugDrawing)
    {
        DrawDebugPoint(World, Location, Size, Color, false, Duration, 0);
    }
}

void UVoxelBrushShape::DebugDrawSphereIfEnabled(FVector& Location, FColor Color, float Size, float Duration) const
{
    if (World && bEnableDebugDrawing)
    {
        DrawDebugSphere(World, Location, Size, 12, FColor::Red, false, Duration, 0, 0);
    }
}

void UVoxelBrushShape::SetDebugDrawingEnabled(bool bEnabled)
{
    bEnableDebugDrawing = true;
}


bool UVoxelBrushShape::GetDebugDrawingEnabled()
{
    return bEnableDebugDrawing;
}