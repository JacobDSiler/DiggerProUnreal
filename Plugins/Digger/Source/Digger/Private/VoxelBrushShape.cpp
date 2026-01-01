#include "VoxelBrushShape.h"

#include "DiggerDebug.h"
#include "DiggerManager.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "DrawDebugHelpers.h"
#include "DynamicHole.h"
#include "Editor.h"
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
        if (DiggerDebug::Manager())
        {
            UE_LOG(LogTemp, Error, TEXT("DiggerManager is null!"));
        }
        return true;
    }
    return false;
}

void UVoxelBrushShape::SetupSweptStroke(FBrushStroke& OutStroke, FVector Start, FVector End, float Radius)
{
    // The "Position" of a capsule is its center point between start and end
    OutStroke.BrushPosition = (Start + End) * 0.5f;
    
    // The "Length" is the total distance covered
    float Distance = FVector::Dist(Start, End);
    
    // Configure as a Capsule to represent the sweep
    OutStroke.BrushType = EVoxelBrushType::Capsule;
    OutStroke.BrushRadius = Radius;
    OutStroke.BrushLength = Distance; // Length of the movement
    
    // Reset Advanced Cube settings so they don't interfere
    OutStroke.bUseAdvancedCubeBrush = false;

    // Orientation: Rotate the capsule to point along the movement direction
    if (Distance > KINDA_SMALL_NUMBER)
    {
        FVector Direction = (End - Start).GetSafeNormal();
        
        // Unreal Capsules default to "Up" (Z-axis). 
        // We calculate the rotation needed to point "Up" towards our "Direction".
        OutStroke.BrushRotation = FQuat::FindBetweenNormals(FVector::UpVector, Direction).Rotator();
    }
    else
    {
        OutStroke.BrushRotation = FRotator::ZeroRotator;
    }
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
    // CRITICAL FIX: Pass the Light Type!
    // Without this, NewStroke.LightType is uninitialized or 0 (Point)
    NewStroke.LightType = this->LightType; 

    return NewStroke;
}


// --- CORE TRACING LOGIC ---

bool UVoxelBrushShape::GetCameraHitLocation(FHitResult& OutHitResult)
{
    FVector TraceStart;
    FVector TraceDir;
    bool bFoundOrigin = false;

#if WITH_EDITOR
    // 1. Editor Viewport (Mouse)
    if (GEditor && GEditor->GetActiveViewport() && !GetWorld()->IsGameWorld())
    {
        FEditorViewportClient* Client = (FEditorViewportClient*)GEditor->GetActiveViewport()->GetClient();
        if (Client)
        {
            FIntPoint MousePos;
            Client->Viewport->GetMousePos(MousePos);
            
            FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
                Client->Viewport, Client->GetScene(), Client->EngineShowFlags));
            
            FSceneView* View = Client->CalcSceneView(&ViewFamily);
            if (View)
            {
                View->DeprojectFVector2D(MousePos, TraceStart, TraceDir);
                bFoundOrigin = true;
            }
        }
    }
#endif

    // 2. Runtime Player Camera
    if (!bFoundOrigin)
    {
        if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
        {
            FVector CamLoc;
            FRotator CamRot;
            PC->GetPlayerViewPoint(CamLoc, CamRot);
            TraceStart = CamLoc;
            TraceDir = CamRot.Vector();
            bFoundOrigin = true;
        }
    }

    if (!bFoundOrigin) return false;

    FVector TraceEnd = TraceStart + (TraceDir * 100000.0f); // 1km

    if (bEnableDebugDrawing)
    {
        DrawDebugLine(GetWorld(), TraceStart, TraceEnd, FColor::Green, false, 0.1f);
    }

    // Use Recursive Trace to pierce holes
    TArray<AActor*> IgnoredActors;
    FHitResult Hit = RecursiveTraceThroughHoles(TraceStart, TraceEnd, IgnoredActors, false, false, 0);

    if (Hit.bBlockingHit)
    {
        OutHitResult = Hit;
        if (bEnableDebugDrawing)
        {
            DrawDebugSphere(GetWorld(), Hit.ImpactPoint, 10.0f, 12, FColor::Red, false, 0.1f);
        }
        return true;
    }

    return false;
}



FHitResult UVoxelBrushShape::PerformComplexTrace(FVector& Start, FVector& End, AActor* IgnoredActor) const
{
    if (!World)
    {
        if (DiggerDebug::Casts() || DiggerDebug::Context())
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
    if (Depth > 32) return FHitResult();

    FCollisionQueryParams Params(SCENE_QUERY_STAT(SmartTrace), true);
    Params.AddIgnoredActors(IgnoredActors);
    Params.bReturnPhysicalMaterial = false;

    FHitResult Hit;
    bool bHit = GetSafeWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);

    if (!bHit || !Hit.GetActor()) return FHitResult();

    AActor* HitActor = Hit.GetActor();

    // --- 1. HIT HOLE ACTOR ---
    if (IsHoleBPActor(HitActor))
    {
        // Ignore this specific hole cap actor
        IgnoredActors.Add(HitActor);

        // Nudge forward slightly
        FVector NewStart = Hit.Location + (OriginalDirection * 10.0f);
        
        // Mark that we are entering a hole context
        return RecursiveTraceThroughHoles_Internal(NewStart, End, IgnoredActors, Depth + 1, OriginalDirection, true);
    }

    // --- 2. HIT LANDSCAPE ---
    if (IsLandscape(HitActor))
    {
        if (bPassedThroughHole)
        {
            // CRITICAL FIX:
            // We hit the "Skin" inside the hole. 
            // We need to skip this surface, BUT we do NOT add Landscape to IgnoredActors.
            // If we did, we would miss the back wall of the cave/hill.
            
            // Just jump forward 100cm to clear the skin thickness
            FVector NewStart = Hit.Location + (OriginalDirection * 100.0f);
            
            if (DiggerDebug::Casts())
            {
                UE_LOG(LogTemp, Warning, TEXT("RecursiveTrace: Skipping Landscape Skin (Jump 100cm)."));
            }

            return RecursiveTraceThroughHoles_Internal(NewStart, End, IgnoredActors, Depth + 1, OriginalDirection, true);
        }
        else
        {
            // We hit landscape from the outside (normal ground). Stop here.
            return Hit;
        }
    }

    // --- 3. HIT VOXEL MESH / OTHER ---
    // Return whatever we hit (Tunnel Floor, Rock, etc)
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
        if (DiggerDebug::Casts() || DiggerDebug::Context())
        {
            UE_LOG(LogTemp, Error, TEXT("SmartTrace: World is null!"));
        }
        return FHitResult();
    }

    if (DiggerDebug::Casts())
    {
        UE_LOG(LogTemp, Warning, TEXT("SmartTrace: Start=%s End=%s"), *Start.ToString(), *End.ToString());
    }

    FHitResult FirstHit;
    FCollisionQueryParams Params;
    Params.bTraceComplex = true;

    bool bHit = World->LineTraceSingleByChannel(FirstHit, Start, End, ECC_Visibility, Params);

    if (!bHit || !FirstHit.GetActor())
    {
        if (DiggerDebug::Casts())
        {
            UE_LOG(LogTemp, Warning, TEXT("SmartTrace: No hit at all!"));
        }
        return FHitResult();
    }

    if (DiggerDebug::Casts())
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
        if (DiggerDebug::Casts())
        UE_LOG(LogTemp, Warning, TEXT("SmartTrace: Hit something else: %s"), *FirstHit.ToString());
        return FirstHit;
    }
}



bool UVoxelBrushShape::IsHoleBPActor(const AActor* Actor) const
{
    if (!Actor) return false;

    // 1. Check C++ Class (Fastest/Best)
    if (Actor->IsA(ADynamicHole::StaticClass()))
    {
        return true;
    }

    // 2. Check Class Name String (Fallback for Blueprints if C++ cast fails)
    const FString ClassName = Actor->GetClass()->GetName();
    if (ClassName.Contains(TEXT("Hole")) && ClassName.Contains(TEXT("BP")))
    {
        return true;
    }

    // 3. Check Actor Label/Name (Last Resort)
    const FString ActorName = Actor->GetName();
    if (ActorName.Contains(TEXT("HoleBP")))
    {
        return true;
    }

    return false;
}



// Helper to identify a Landscape actor
bool UVoxelBrushShape::IsLandscape(const AActor* Actor) const
{
    bool Result = Actor && Actor->IsA(ALandscapeProxy::StaticClass());
    
    // FIX: Added () to call the function
    // Also tweaked logging to print True/False text instead of 0/1 for readability
    if (DiggerDebug::Holes())
    {
        UE_LOG(LogTemp, Warning, TEXT("Is it a landscape actor? %s"), Result ? TEXT("True") : TEXT("False"));
    }
    return Result;
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