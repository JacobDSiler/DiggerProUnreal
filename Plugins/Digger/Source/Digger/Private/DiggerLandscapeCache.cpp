#include "DiggerLandscapeCache.h"
#include "DiggerDebug.h"
#include "EngineUtils.h" // Required for TActorIterator
#include "VoxelConversion.h" // For VoxelSize
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

void UDiggerLandscapeCache::Initialize(UWorld* InWorld)
{
    WorldContext = InWorld;
    Clear();
}

void UDiggerLandscapeCache::Clear()
{
    FWriteScopeLock WriteLock(Lock);
    Cache.Empty();
    ProcessedProxies.Empty();
    PendingQueue.Empty();
}

FIntPoint UDiggerLandscapeCache::WorldToGrid(const FVector& Pos) const
{
    int32 GridSize = FVoxelConversion::LocalVoxelSize; 
    if (GridSize <= 0) GridSize = 100;
    return FIntPoint(FMath::RoundToInt(Pos.X / GridSize), FMath::RoundToInt(Pos.Y / GridSize));
}


ALandscapeProxy* UDiggerLandscapeCache::FindProxy(const FVector& Pos) const
{
    if (!WorldContext) return nullptr;

    // 1. OPTIMIZATION: Check Last Accessed
    if (LastAccessedProxy && IsValid(LastAccessedProxy))
    {
        FBox Bounds = LastAccessedProxy->GetComponentsBoundingBox();
        // Expand bounds slightly to handle seams
        if (Bounds.ExpandBy(100.0f).IsInsideXY(Pos))
        {
            return LastAccessedProxy;
        }
    }

    // 2. ITERATE (Math-based, robust)
    for (TActorIterator<ALandscapeProxy> It(WorldContext); It; ++It)
    {
        ALandscapeProxy* Proxy = *It;
        if (!Proxy) continue;

        FBox Bounds = Proxy->GetComponentsBoundingBox();
        if (Bounds.IsInsideXY(Pos))
        {
            return Proxy;
        }
    }

    return nullptr;
}



UWorld* UDiggerLandscapeCache::GetSafeWorld() const
{
#if WITH_EDITOR
    if (GIsEditor && GEditor)
    {
        return GEditor->GetEditorWorldContext().World();
    }
#else
    return GetWorld();
#endif
    return GetWorld();
}

// --- HELPER: Exact logic from Backup ---
float UDiggerLandscapeCache::SampleLandscapeHeight(ALandscapeProxy* Proxy, const FVector& WorldPos)
{
    if (!Proxy) return INVALID_LANDSCAPE_HEIGHT;

    // Transform to Local Space for the API
    FVector LocalPos = Proxy->GetTransform().InverseTransformPosition(WorldPos);
    
    // 1. Try Simple (Physics)
    TOptional<float> H = Proxy->GetHeightAtLocation(LocalPos, EHeightfieldSource::Simple);
    
    // 2. Try Complex (Mesh)
    if (!H.IsSet()) 
    {
        H = Proxy->GetHeightAtLocation(LocalPos, EHeightfieldSource::Complex);
    }

    // 3. Try Editor (Data)
#if WITH_EDITOR
    if (!H.IsSet() && GIsEditor)
    {
         H = Proxy->GetHeightAtLocation(LocalPos, EHeightfieldSource::Editor);
    }
#endif

    if (H.IsSet())
    {
        // Convert Local Z to World Z
        return (H.GetValue() * Proxy->GetActorScale3D().Z) + Proxy->GetActorLocation().Z;
    }

    return INVALID_LANDSCAPE_HEIGHT;
}

float UDiggerLandscapeCache::GetHeight(const FVector& Location)
{
    if (!WorldContext) return INVALID_LANDSCAPE_HEIGHT;

    // 1. Find Proxy
    ALandscapeProxy* Proxy = FindProxy(Location);
    
    if (!Proxy) return INVALID_LANDSCAPE_HEIGHT;

    // Update Hot Cache
    const_cast<UDiggerLandscapeCache*>(this)->LastAccessedProxy = Proxy;

    FIntPoint GridKey = WorldToGrid(Location);

    // 2. CACHE LOOKUP
    {
        FReadScopeLock ReadLock(Lock);
        if (auto* MapPtr = Cache.Find(Proxy))
        {
            if (float* H = (*MapPtr)->Find(GridKey))
            {
                return *H;
            }
        }
    }

    // 3. CACHE MISS
    if (!IsInGameThread()) 
    {
        // Async thread can't call API. Return Invalid (ApplyBrushStroke handles fallback).
        return INVALID_LANDSCAPE_HEIGHT;
    }

    // 4. GAME THREAD RECOVERY
    // Instead of forcing a full build immediately (which might miss this specific point due to grid alignment),
    // we just Sample the point directly. This is 100% accurate.
    float DirectHeight = SampleLandscapeHeight(Proxy, Location);
    
    if (DirectHeight > (INVALID_LANDSCAPE_HEIGHT + 1.0f))
    {
        // We found it! Add to cache for next time (e.g. Async workers)
        // We need to upgrade to Write Lock briefly
        // (Optimization: In a real implementation, you'd check if cache exists first)
        // For now, let's just trigger the full build if cache is missing, 
        // OR just return the value.
        
        // Let's trigger build to help future async tasks
        if (!ProcessedProxies.Contains(Proxy))
        {
            BuildCacheForProxy(Proxy);
        }
        
        return DirectHeight;
    }

    return INVALID_LANDSCAPE_HEIGHT;
}


void UDiggerLandscapeCache::RefreshLandscapeCache()
{
    FWriteScopeLock WriteLock(Lock);
    
    Cache.Empty();
    ProcessedProxies.Empty();
    PendingQueue.Empty();
    LastAccessedProxy = nullptr;

    if (DiggerDebug::Cache())
        UE_LOG(LogTemp, Warning, TEXT("Digger: Landscape Height Cache Cleared."));
}

bool UDiggerLandscapeCache::IsPointBelowLandscape(const FVector& Location)
{
    // GetHeight() already handles:
    // - LastAccessedProxy optimization
    // - Cache lookup
    // - Cache building
    // - Sentinel return
    float TerrainZ = GetHeight(Location);

    // Invalid → treat as "no landscape here"
    if (TerrainZ <= (INVALID_LANDSCAPE_HEIGHT + 1.0f))
    {
        return false;
    }

    return Location.Z < TerrainZ;
}


float UDiggerLandscapeCache::GetDistanceFromLandscape(const FVector& Location)
{
    float TerrainZ = GetHeight(Location);

    if (TerrainZ <= (UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT + 1.0f))
    {
        return 0.0f; // Unknown/Void
    }

    // Result: 
    // If Location.Z is 200, Terrain is 100 -> Returns 100 (Above)
    // If Location.Z is 50, Terrain is 100 -> Returns -50 (Below)
    return Location.Z - TerrainZ;
}


void UDiggerLandscapeCache::BuildCacheForProxy(ALandscapeProxy* Proxy)
{
    if (!Proxy || !WorldContext) return;
    if (ProcessedProxies.Contains(Proxy)) return;

    FBox Bounds = Proxy->GetComponentsBoundingBox();
    int32 GridSize = FVoxelConversion::LocalVoxelSize;
    if (GridSize <= 0) GridSize = 100;

    int32 NumX = FMath::CeilToInt((Bounds.Max.X - Bounds.Min.X) / GridSize) + 4;
    int32 NumY = FMath::CeilToInt((Bounds.Max.Y - Bounds.Min.Y) / GridSize) + 4;
    
    TSharedPtr<FHeightMap> NewMap = MakeShared<FHeightMap>();
    NewMap->Reserve(NumX * NumY);

    // Scan using the Helper
    for (float X = Bounds.Min.X; X <= Bounds.Max.X; X += GridSize)
    {
        for (float Y = Bounds.Min.Y; Y <= Bounds.Max.Y; Y += GridSize)
        {
            FVector WorldPos(X, Y, 0);
            float H = SampleLandscapeHeight(Proxy, WorldPos);
            
            if (H > (INVALID_LANDSCAPE_HEIGHT + 1.0f))
            {
                NewMap->Add(WorldToGrid(WorldPos), H);
            }
        }
    }

    {
        FWriteScopeLock WriteLock(Lock);
        Cache.Add(Proxy, NewMap);
        ProcessedProxies.Add(Proxy);
        PendingQueue.Remove(Proxy);
    }
    
    if (DiggerDebug::Performance())
    {
        UE_LOG(LogTemp, Log, TEXT("Cache Built for %s. Entries: %d"), *Proxy->GetName(), NewMap->Num());
    }
}


void UDiggerLandscapeCache::TickProcessQueue(float TimeLimit)
{
    if (PendingQueue.IsEmpty()) return;

    double Start = FPlatformTime::Seconds();
    while (PendingQueue.Num() > 0)
    {
        if ((FPlatformTime::Seconds() - Start) > TimeLimit) break;

        ALandscapeProxy* Proxy = PendingQueue.Pop();
        if (IsValid(Proxy) && !ProcessedProxies.Contains(Proxy))
        {
            BuildCacheForProxy(Proxy);
        }
    }
}