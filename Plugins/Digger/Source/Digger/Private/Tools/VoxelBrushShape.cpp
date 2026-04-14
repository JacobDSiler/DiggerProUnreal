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
    UVoxelChunk* NewTargetChunk = DiggerManager->GetOrCreateChunkAtCoords(ChunkPosition);
    
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
        UE_LOG(LogTemp, Error, TEXT("Hit a hole actor, this should not ever return."));
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
    bool bStartInHole
) const
{
    if (Depth > 32)
        return FHitResult();

    World = GetSafeWorld();
    if (!World)
        return FHitResult();

    // Brush radius from manager
    float BrushRadius = 10.0f;
    if (DiggerManager)
    {
        BrushRadius = DiggerManager->EditorBrushRadius;
    }

    FCollisionQueryParams Params(SCENE_QUERY_STAT(SmartTrace), true);
    Params.AddIgnoredActors(IgnoredActors);
    Params.bReturnPhysicalMaterial = false;

    if (DiggerDebug::SmartTrace())
    {
        DiggerDebug::DrawTraceSegment(World, Start, End, FColor::Yellow, 0.5f);
    }

    FHitResult Hit;
    bool bHit = World->LineTraceSingleByChannel(
        Hit,
        Start,
        End,
        ECC_Visibility,
        Params
    );


    if (!bHit || !Hit.GetActor())
        return FHitResult();

    AActor* HitActor = Hit.GetActor();

    // ---------------------------------------------------------------------
    // ⭐ Correct hole detection using sweep center (critical for continuous digging)
    // ---------------------------------------------------------------------
    bool bHitInHole = false;

    if (DiggerManager)
    {
        // Sweep center is inside the hole even when the surface hit is not
        FVector SweepCenter = Hit.Location - Hit.Normal * BrushRadius;

        if (DiggerManager->IsInsideHole(SweepCenter))
        {
            bHitInHole = true;
        }
    }
    // ---------------------------------------------------------------------

    if (DiggerDebug::SmartTrace())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("RecursiveTrace[%d]: Hit %s at %s (StartInHole=%d, HitInHole=%d)"),
            Depth,
            *HitActor->GetName(),
            *Hit.Location.ToString(),
            bStartInHole ? 1 : 0,
            bHitInHole ? 1 : 0);
    }

    // ---------------------------------------------------------------------
    // ⭐ LANDSCAPE LOGIC
    // ---------------------------------------------------------------------
    if (IsLandscape(HitActor))
    {
        bool bShouldSkipLandscape = false;

        // Case 1: Hit is inside hole volume → skip landscape
        if (bHitInHole)
        {
            bShouldSkipLandscape = true;
        }

        // Case 2: Camera in hole, aiming at fresh landscape → DO NOT skip
        if (bStartInHole && !bHitInHole)
        {
            bShouldSkipLandscape = false;
        }

        if (bShouldSkipLandscape)
        {
            const float JumpDistance = 1.0f;
            FVector NewStart = Hit.Location + (OriginalDirection * JumpDistance);

            if (DiggerDebug::SmartTrace())
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("RecursiveTrace[%d]: Skipping landscape (StartInHole=%d, HitInHole=%d), jumping %f cm to %s"),
                    Depth, bStartInHole ? 1 : 0, bHitInHole ? 1 : 0,
                    JumpDistance, *NewStart.ToString());

                DiggerDebug::DrawTraceSegment(World, Hit.Location, NewStart, FColor::Magenta, 0.5f);
            }

            return RecursiveTraceThroughHoles_Internal(
                NewStart,
                End,
                IgnoredActors,
                Depth + 1,
                OriginalDirection,
                bStartInHole
            );
        }

        // Normal landscape hit (preview / hole spawn)
        return Hit;
    }

    // ---------------------------------------------------------------------
    // ⭐ HOLE ACTOR (if collision ever enabled)
    // ---------------------------------------------------------------------
    if (IsHoleBPActor(HitActor))
    {
        IgnoredActors.Add(HitActor);

        FVector NewStart = Hit.Location + (OriginalDirection * 5.0f);

        if (DiggerDebug::SmartTrace())
        {
            UE_LOG(LogTemp, Warning,
                TEXT("RecursiveTrace[%d]: Hit HoleBPActor %s"),
                Depth, *HitActor->GetName());

            DiggerDebug::DrawTraceSegment(World, Hit.Location, NewStart, FColor::Cyan, 0.5f);
        }

        return RecursiveTraceThroughHoles_Internal(
            NewStart,
            End,
            IgnoredActors,
            Depth + 1,
            OriginalDirection,
            bStartInHole
        );
    }

    // ---------------------------------------------------------------------
    // ⭐ FINAL HIT (voxel mesh, props, etc.)
    // ---------------------------------------------------------------------
    if (DiggerDebug::SmartTrace())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("RecursiveTrace[%d]: FINAL HIT at %s (%s)"),
            Depth,
            *Hit.Location.ToString(),
            *HitActor->GetName());

#if WITH_EDITOR
        DrawDebugSphere(World, Hit.Location, 25.0f, 16, FColor::Green, false, 0.5f);
#endif
    }

    if(DiggerDebug::Casts() && Depth > 2)
    {
        UE_LOG(LogTemp, Warning, TEXT("Casted through %d holes before returning..."), Depth);
    }

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
        bPassedThroughHole
    );
}


#if WITH_EDITOR
class UDiggerEditorEventHub
{
public:
    static void BroadcastMessage(const FString& Msg);
};
#endif


FHitResult UVoxelBrushShape::SmartTrace(const FVector& Start, const FVector& End) const
{
    TArray<AActor*> IgnoredActors;

    // Brush radius (used only for hole-spawn logic)
    float BrushRadius = 10.0f;
    if (DiggerManager)
    {
        BrushRadius = DiggerManager->EditorBrushRadius;
    }

    // Check if camera starts inside a hole
    const bool bStartInHole =
        (DiggerManager && DiggerManager->IsInsideHole(Start));

    if (DiggerDebug::SmartTrace())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("SmartTrace: Start=%s, End=%s, StartInHole=%d, BrushRadius=%.1f"),
            *Start.ToString(), *End.ToString(), bStartInHole ? 1 : 0, BrushRadius);
    }

    // ---------------------------------------------------------
    // 1. OPTIONAL: Spawn a hole at the first landscape hit
    // ---------------------------------------------------------
    // This is the "Auto Hole Spawn" feature. It does NOT affect the
    // actual trace result — it only triggers hole creation if needed.
    if (DiggerManager)
    {
        World = DiggerManager->GetWorld();
        if (World)
        {
            FCollisionQueryParams Params(SCENE_QUERY_STAT(SmartTrace_LandscapeHit), false);
            Params.AddIgnoredActors(IgnoredActors);

            FHitResult LandscapeHit;
            if (World->LineTraceSingleByChannel(
                    LandscapeHit,
                    Start,
                    End,
                    ECC_Visibility,
                    Params))
            {
                AActor* HitActor = LandscapeHit.GetActor();
                if (HitActor && HitActor->IsA(ALandscapeProxy::StaticClass()))
                {
                    // Prepare stroke for hole spawn
                    FBrushStroke HoleStroke;
                    HoleStroke.BrushPosition = LandscapeHit.ImpactPoint;
                    HoleStroke.BrushOffset   = DiggerManager->EditorBrushOffset;
                    HoleStroke.BrushRadius   = BrushRadius;
                    HoleStroke.BrushRotation = DiggerManager->EditorBrushRotation;
                    HoleStroke.BrushStrength = DiggerManager->EditorBrushStrength;
                    HoleStroke.bDig          = DiggerManager->EditorBrushDig;
                    HoleStroke.BrushType     = DiggerManager->EditorBrushType;
                    HoleStroke.BrushLength   = BrushRadius;

                    if (DiggerDebug::SmartTrace())
                    {
                        UE_LOG(LogTemp, Warning,
                            TEXT("SmartTrace: Spawning hole at landscape hit %s"),
                            *LandscapeHit.ImpactPoint.ToString());
                    }

                    // Only spawn holes while painting AND in dig mode
                    if (DiggerManager->bIsEditorPainting && DiggerManager->EditorBrushDig)
                    {
                        bool bAllowHoleSpawn = true;

                        // Block hole spawning if modifiers are held
                        if (GEditor && GEditor->GetActiveViewport())
                        {
                            FViewport* VP = GEditor->GetActiveViewport();

                            const bool bAlt =
                                VP->KeyState(EKeys::LeftAlt) ||
                                VP->KeyState(EKeys::RightAlt);

                            const bool bCtrl =
                                VP->KeyState(EKeys::LeftControl) ||
                                VP->KeyState(EKeys::RightControl);

                            const bool bShift =
                                VP->KeyState(EKeys::LeftShift) ||
                                VP->KeyState(EKeys::RightShift);

                            if (bAlt || bCtrl || bShift)
                            {
                                DiggerManager->OnModifierBlocked.Broadcast(true);
                                bAllowHoleSpawn = false;
                            }
                            else
                            {
                                DiggerManager->OnModifierBlocked.Broadcast(false);
                            }
                        }

                        if (bAllowHoleSpawn)
                        {
                            DiggerManager->HandleHoleSpawn(HoleStroke);
                        }
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------
    // 2. Recursive Trace (The Source of Truth)
    // ---------------------------------------------------------
    // This is the ONLY trace that determines where the brush actually is.
    // It handles:
    //   - entering holes
    //   - exiting holes
    //   - skipping landscape when inside a hole
    //   - continuing past hole meshes
    //   - multi-hop recursion through multiple hole layers
    FVector MutableStart = Start;

    FHitResult FinalHit = RecursiveTraceThroughHoles_Internal(
        MutableStart,
        End,
        IgnoredActors,
        0,
        (End - Start).GetSafeNormal(),
        bStartInHole
    );

    // ---------------------------------------------------------
    // ⭐ NO FALLBACKS
    // ---------------------------------------------------------
    // We previously had logic here that said:
    //   "If Hit Z < 1.0f, clamp to landscape"
    //
    // That is REMOVED.
    //
    // If we hit deep underground, we hit deep underground.
    // If we hit a hole mesh, we hit a hole mesh.
    // If we are inside a hole, landscape hits are ignored.
    //
    // The recursive function already handles all of this correctly.

    // ---------------------------------------------------------
    // 3. Return the authoritative hit
    // ---------------------------------------------------------
    return FinalHit;
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

bool UVoxelBrushShape::IsWithinInterior(const FVector& WorldPos, const FBrushStroke& Stroke) const
{
    return true;
}

void UVoxelBrushShape::GetPreviewData(FVector& OutCenter, FVector& OutExtents, FQuat& OutRotation, float& OutFalloff,
    EVoxelBrushType& OutBrushType, const FBrushStroke& Stroke) const
{
    // This is a Pure Virtual Function.
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