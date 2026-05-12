#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "LandscapeProxy.h"
#include "DiggerLandscapeCache.generated.h"

UCLASS()
class DIGGER_API UDiggerLandscapeCache : public UObject
{
	GENERATED_BODY()

public:
	// Initialize (pass the World context)
	void Initialize(UWorld* InWorld);

	// The Single Source of Truth
	float GetHeight(const FVector& Location);

	// Called by DiggerManager::Tick
	void TickProcessQueue(float TimeLimit = 0.005f);

	// Clears everything (On EndPlay)
	void Clear();

	// Represents "No Data" or "No Landscape Found". 
	// Using a large negative number ensures that "Z > Height" (Above Terrain) defaults to true, which is safe (Air).
	// Static constant: Accessible anywhere via UDiggerLandscapeCache::INVALID_LANDSCAPE_HEIGHT
	static constexpr float INVALID_LANDSCAPE_HEIGHT = -3.4e38f;
	
	// New Method for the button
	void RefreshLandscapeCache();
	
	// Returns true if the location is physically below the landscape surface.
	// Returns false if above, or if no landscape exists there.
	UFUNCTION(BlueprintCallable, Category = "Digger Queries")
	bool IsPointBelowLandscape(const FVector& Location);

	// Returns the vertical distance to the landscape.
	// Positive = Above ground (Air). Negative = Below ground (Dirt).
	// Returns 0.0 if no landscape found.
	UFUNCTION(BlueprintCallable, Category = "Digger Queries")
	float GetDistanceFromLandscape(const FVector& Location);

	UFUNCTION(BlueprintCallable, Category = "Digger Queries")
	ALandscapeProxy* GetLandscapeProxyAt(const FVector& WorldPos) const;

	UFUNCTION(BlueprintCallable, Category = "Digger Debug")
	void DebugSample(const FVector& WorldPos);
	
private:
	UPROPERTY()
	UWorld* WorldContext;

	// --- DATA ---
	// Map: LandscapeProxy -> (Map: GridCoordinate -> HeightZ)
	// We use a raw map here because this object owns the data.
	typedef TMap<FIntPoint, float> FHeightMap;
	// Precise height cache (XY → Z)
	TMap<FIntPoint, float> HeightCache;
	TMap<ALandscapeProxy*, TSharedPtr<FHeightMap>> Cache;
	
	// Which proxies have we finished scanning?
	UPROPERTY()
	TSet<ALandscapeProxy*> ProcessedProxies;
	
	// Optimization: The last proxy we successfully found.
	// This avoids raycasting 10,000 times for the same chunk.
	UPROPERTY()
	ALandscapeProxy* LastAccessedProxy;

	// Queue for background processing
	UPROPERTY()
	TArray<ALandscapeProxy*> PendingQueue;

	// Resume index for incremental BuildCacheForProxy — tracks how many
	// grid rows have been completed so we can pick up where we left off.
	TMap<ALandscapeProxy*, int32> BuildResumeRow;

	// Thread Safety
	FRWLock Lock;
public:
	// --- HELPERS ---
	void BuildCacheForProxy(ALandscapeProxy* Proxy);
	FIntPoint WorldToGrid(const FVector& Pos) const;
	ALandscapeProxy* FindProxy(const FVector& Pos) const;
	UWorld* GetSafeWorld() const;
public:

	TOptional<float> SampleLandscapeHeight(ALandscapeProxy*, const FVector&);
	TOptional<float> SampleLandscapeHeight(ALandscapeProxy*, const FVector&, bool bForcePrecise);
	float GetLandscapeHeightAt(const FVector&);

	// Worker/Helper that actually gets this done.
	TOptional<float> SampleLandscapeHeightPrecise(const FVector& WorldPos);

};