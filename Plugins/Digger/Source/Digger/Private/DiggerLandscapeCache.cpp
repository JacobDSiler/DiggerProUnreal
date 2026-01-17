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
TOptional<float> UDiggerLandscapeCache::SampleLandscapeHeightPrecise(const FVector& WorldPos)
{
    // Cache key (XY only)
    const FIntPoint Key(
        FMath::FloorToInt(WorldPos.X),
        FMath::FloorToInt(WorldPos.Y)
    );

    // Check cache
    if (float* Cached = HeightCache.Find(Key))
    {
        return *Cached;
    }

    // Get landscape proxy
    ALandscapeProxy* Landscape = GetLandscapeProxyAt(WorldPos);
    if (!Landscape)
    {
        if (DiggerDebug::Landscape())
        {
            UE_LOG(LogTemp, Warning, TEXT("SampleLandscapeHeightPrecise: No landscape at %s"), *WorldPos.ToString());
        }
        return TOptional<float>();
    }

    // Direct precise sampling
    TOptional<float> Sampled = Landscape->GetHeightAtLocation(WorldPos);

    if (Sampled.IsSet())
    {
        HeightCache.Add(Key, Sampled.GetValue());

        if (DiggerDebug::Landscape())
        {
            UE_LOG(LogTemp, Log, TEXT("Precise height at %s = %.2f"), *WorldPos.ToString(), Sampled.GetValue());
        }

        return Sampled;
    }

    if (DiggerDebug::Landscape())
    {
        UE_LOG(LogTemp, Warning, TEXT("SampleLandscapeHeightPrecise: Failed at %s"), *WorldPos.ToString());
    }

    return TOptional<float>();
}

TOptional<float> UDiggerLandscapeCache::SampleLandscapeHeight(ALandscapeProxy* Landscape, const FVector& WorldPos)
{
    return SampleLandscapeHeightPrecise(WorldPos);
}

TOptional<float> UDiggerLandscapeCache::SampleLandscapeHeight(ALandscapeProxy* Landscape, const FVector& WorldPos, bool bForcePrecise)
{
    return SampleLandscapeHeightPrecise(WorldPos);
}

float UDiggerLandscapeCache::GetLandscapeHeightAt(const FVector& WorldPos)
{
    TOptional<float> H = SampleLandscapeHeightPrecise(WorldPos);
    return H.IsSet() ? H.GetValue() : INVALID_LANDSCAPE_HEIGHT;
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
    TOptional<float> Direct = SampleLandscapeHeightPrecise(Location);
    float DirectHeight = Direct.IsSet() ? Direct.GetValue() : INVALID_LANDSCAPE_HEIGHT;
    
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

ALandscapeProxy* UDiggerLandscapeCache::GetLandscapeProxyAt(const FVector& WorldPos) const
{
    return FindProxy(WorldPos);
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
            TOptional<float> HOpt = SampleLandscapeHeightPrecise(WorldPos);
            if (HOpt.IsSet())
            {
                NewMap->Add(WorldToGrid(WorldPos), HOpt.GetValue());
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

void UDiggerLandscapeCache::DebugSample(const FVector& WorldPos)
{
    TOptional<float> H = SampleLandscapeHeightPrecise(WorldPos);

    if (!H.IsSet())
    {
        DrawDebugSphere(WorldContext, WorldPos, 20.f, 12, FColor::Red, false, 5.f);
        UE_LOG(LogTemp, Error, TEXT("DebugSample FAILED at %s"), *WorldPos.ToString());
        return;
    }

    FVector Hit(WorldPos.X, WorldPos.Y, H.GetValue());
    DrawDebugSphere(WorldContext, Hit, 20.f, 12, FColor::Green, false, 5.f);

    UE_LOG(LogTemp, Warning, TEXT("DebugSample Height=%.2f at %s"), H.GetValue(), *Hit.ToString());
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