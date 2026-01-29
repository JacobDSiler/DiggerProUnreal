#include "VoxelBrushShape.h"

#include "DiggerDebug.h"
#include "DiggerManager.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "DrawDebugHelpers.h"
#include "DynamicHole.h"
#include "Editor.h"
#include "VoxelConversion.h"
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
    // Try to resolve the manager if missing
    if (!DiggerManager)
    {
        DiggerManager = ADiggerManager::FindDiggerManager(this);
    }

    // If still missing, report failure
    if (!DiggerManager)
    {
        if (DiggerDebug::Manager())
        {
            UE_LOG(LogTemp, Error, TEXT("EnsureDiggerManager: DiggerManager is NULL!"));
        }
        return false; // manager NOT valid
    }

    return true; // manager valid
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

    // Final safety check: if we somehow returned a hole actor, fail.
    if (Hit.bBlockingHit && IsHoleBPActor(Hit.GetActor()))
    {
        return false;
    }

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
    const FVector& End,
    TArray<AActor*>& IgnoredActors,
    int32 Depth,
    const FVector& OriginalDirection,
    bool bHoleContext,
    bool bStartedInsideHole
) const
{
    if (Depth > 32)
        return FHitResult();

    FCollisionQueryParams Params(SCENE_QUERY_STAT(SmartTrace), true);
    Params.AddIgnoredActors(IgnoredActors);
    Params.bReturnPhysicalMaterial = false;
    
    if (DiggerDebug::SmartTrace())
    {
        DiggerDebug::DrawTraceSegment(GetWorld(), Start, End, FColor::Yellow, 1.0f);
    }
    

    FHitResult Hit;
    bool bHit = GetSafeWorld()->LineTraceSingleByChannel(
        Hit, Start, End, ECC_Visibility, Params);

    if (!bHit || !Hit.GetActor())
        return FHitResult();

    AActor* HitActor = Hit.GetActor();

    // --- 1. LANDSCAPE ---
    if (IsLandscape(HitActor))
    {
        if (bHoleContext)
        {
            // We are in hole context → skip landscape skin
            const float JumpDistance = 20.0f;
            FVector NewStart = Hit.Location + (OriginalDirection * JumpDistance);

            UE_LOG(LogTemp, Warning,
                TEXT("RecursiveTrace: Skipping Landscape Skin. Jumping %f cm."), JumpDistance);

            return RecursiveTraceThroughHoles_Internal(
                NewStart,
                End,
                IgnoredActors,
                Depth + 1,
                OriginalDirection,
                true,               // still in hole context
                bStartedInsideHole  // preserve original start state
            );
        }

        // Not in hole context → normal landscape hit
        return Hit;
    }

    // --- 2. HOLE ACTOR (if you ever re-enable collision) ---
    if (IsHoleBPActor(HitActor))
    {
        IgnoredActors.Add(HitActor);

        FVector NewStart = Hit.Location + (OriginalDirection * 5.0f);

        return RecursiveTraceThroughHoles_Internal(
            NewStart,
            End,
            IgnoredActors,
            Depth + 1,
            OriginalDirection,
            true,               // entering hole context
            bStartedInsideHole
        );
    }

    if (!Hit.bBlockingHit)
    {
        FVector ReverseStart = End;
        FVector ReverseEnd   = Start;

        if (DiggerDebug::SmartTrace())
        {
            UE_LOG(LogTemp, Warning,
                TEXT("Forward trace MISSED. Trying reverse trace."));
            DiggerDebug::DrawTraceSegment(GetSafeWorld(), ReverseStart, ReverseEnd, FColor::Blue, 1.0f);
        }

        FHitResult ReverseHit;
        GetSafeWorld()->LineTraceSingleByChannel(
            ReverseHit, ReverseStart, ReverseEnd, ECC_Visibility);

        if (ReverseHit.bBlockingHit)
        {
            if (DiggerDebug::SmartTrace())
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("Reverse trace HIT: %s (%s)"),
                    *ReverseHit.Location.ToString(),
                    *ReverseHit.GetActor()->GetName());

                DrawDebugSphere(GetSafeWorld(), ReverseHit.Location, 25.0f, 16, FColor::Blue);
            }

            return ReverseHit;
        }
    }


    // --- 3. ANY OTHER ACTOR (voxel mesh, props, etc.) ---
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

    return RecursiveTraceThroughHoles_Internal(
        Start,
        End,
        IgnoredActors,
        Depth,
        OriginalDirection,
        bPassedThroughHole,   // hole context (skip landscape skin)
        bIgnoreHolesNow       // started inside hole (controls hole spawning)
    );
}


FHitResult UVoxelBrushShape::SmartTrace(const FVector& Start, const FVector& End) 
{
    TArray<AActor*> IgnoredActors;

    // 1. Detect if the trace STARTS inside a hole
    bool bStartedInsideHole = false;
    if (DiggerManager && DiggerManager->IsInsideHole(Start))
    {
        bStartedInsideHole = true;
        UE_LOG(LogTemp, Error, TEXT("SmartTrace: Start point is inside a hole. Entering hole context."));
    }

    // 2. Pass this into the recursive trace as the initial hole context
    bool bHoleContext = bStartedInsideHole;

    FVector MutableStart = Start;
    return RecursiveTraceThroughHoles_Internal(
        MutableStart,
        End,
        IgnoredActors,
        0,
        (End - Start).GetSafeNormal(),
        bHoleContext,
        bStartedInsideHole
    );
}


bool UVoxelBrushShape::IsHoleBPActor(const AActor* Actor) const
{
    if (!Actor) return false;

    // 1. Strong: C++ type
    if (Actor->IsA(ADynamicHole::StaticClass()))
    {
        return true;
    }

    // 2. Strong: explicit tag
    if (Actor->ActorHasTag(FName("DiggerHole")))
    {
        return true;
    }

    // 3. (Optional) Very last resort: name heuristic
    // Comment this out if it ever misfires in real projects.
    /*
    const FString ClassName = Actor->GetClass()->GetName();
    if (ClassName.Contains(TEXT("HoleBP"), ESearchCase::IgnoreCase))
    {
        return true;
    }
    */

    return false;
}


// Helper to identify a Landscape actor
bool UVoxelBrushShape::IsLandscape(const AActor* Actor) const
{
    bool Result = Actor && Actor->IsA(ALandscapeProxy::StaticClass());
    if (DiggerDebug::Holes())
    {
        UE_LOG(LogTemp, Verbose, TEXT("Is it a landscape actor? %s"), Result ? TEXT("True") : TEXT("False"));
    }
    return Result;
}

// Helper to identify a Procedural Mesh actor
bool UVoxelBrushShape::IsProceduralMesh(const AActor* Actor) const
{
    return Actor && Actor->GetName().Contains(TEXT("ProceduralMesh")); 
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
    bEnableDebugDrawing = bEnabled; // Fixed assignment
}


bool UVoxelBrushShape::GetDebugDrawingEnabled()
{
    return bEnableDebugDrawing;
}