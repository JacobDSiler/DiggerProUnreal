#include "SparseVoxelGrid.h"
#include "DiggerManager.h"
#include "DrawDebugHelpers.h"
#include "C:\Users\serpe\Documents\Unreal Projects\DiggerProUnreal\Source\DiggerProUnreal\Public\Voxel\FVoxelSDFHelper.h"
#include "DiggerDebug.h"
#include "Editor.h"
#include "VoxelChunk.h"
#include "VoxelConversion.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Async.h"
#include "Serialization/BufferArchive.h"

constexpr float SDF_SOLID = -1.0f; // or whatever your convention is
constexpr float SDF_AIR   = 1.0f;



USparseVoxelGrid::USparseVoxelGrid(): DiggerManager(nullptr), ParentChunk(nullptr), World(nullptr),
                                      ParentChunkCoordinates(0, 0, 0),
                                      TerrainGridSize(0),
                                      Subdivisions(0)
{
    // Constructor logic
}

void USparseVoxelGrid::Initialize(UVoxelChunk* ParentChunkReference)
{
    ParentChunk=ParentChunkReference;
    ParentChunkCoordinates = ParentChunk->GetChunkPosition();
        
    // Only set DiggerManager if ParentChunk is valid
    if (ParentChunk && IsValid(ParentChunk))
    {
        DiggerManager = ParentChunk->GetDiggerManager();
    }
    else
    {
        DiggerManager = nullptr;
    }
}

void USparseVoxelGrid::InitializeDiggerManager()
{
    // Find and assign DiggerManager if not already assigned and set the member variables from it
    EnsureDiggerManager();
    if (!DiggerManager->GetSafeWorld())
    {
        UE_LOG(LogTemp, Warning, TEXT("World is null in InitializeDiggerManager."));
        return; // Ensure we have a valid world context
    }

    //UE_LOG(LogTemp, Warning, TEXT("World is not null in InitializeDiggerManager. Grabbing the DiggerManager."));

        if (DiggerManager)
        {
            ChunkSize=DiggerManager->ChunkSize;
            TerrainGridSize=DiggerManager->TerrainGridSize;
            Subdivisions=DiggerManager->Subdivisions;
        }
}


bool USparseVoxelGrid::EnsureDiggerManager()
{
    if (!DiggerManager)
    {
        DiggerManager = ParentChunk->GetDiggerManager();
        
        if (!DiggerManager)
        {
            if (DiggerDebug::Manager)
            UE_LOG(LogTemp, Error, TEXT("DiggerManager is null in VoxelToWorldSpace"));
            return false;
        }
        if (DiggerDebug::Voxels)
        UE_LOG(LogTemp, Warning, TEXT("DiggerManager ensured correctly in an instance of SVG!"));
    }
    return true;
}


UWorld* USparseVoxelGrid::GetSafeWorld() const
{
#if WITH_EDITOR
    if (GIsEditor && GEditor)
    {
        return GEditor->GetEditorWorldContext().World();
    }
#endif
    return GetWorld();
}


bool USparseVoxelGrid::IsPointAboveLandscape(FVector& Point)
{
    if(!World) World = GetSafeWorld();
    if (!World) 
    {
        if (DiggerDebug::Context)
        UE_LOG(LogTemp, Error, TEXT("World is null in IsPointAboveLandscape"));
        return false;
    }

    FVector Start = Point;
    FVector End = Point + FVector(0, 0, 10000.0f); // Arbitrary large downward value

    FHitResult HitResult;
    FCollisionQueryParams CollisionParams;
   // CollisionParams.AddIgnoredActor(); // Optional: Ignore the owning actor if needed

    bool bHit = World->LineTraceSingleByChannel(HitResult, Start, End, ECC_Visibility, CollisionParams);

    return bHit;
}


float USparseVoxelGrid::GetLandscapeHeightAtPoint(FVector Position)
{
    return DiggerManager->GetLandscapeHeightAt(Position);
}



void USparseVoxelGrid::SetVoxel(FIntVector Position, float SDFValue, bool bDig)
{
    SetVoxel(Position.X, Position.Y, Position.Z, SDFValue, bDig);
}



// In USparseVoxelGrid, update SetVoxel to use world coordinates comparison
// Enhanced SetVoxel for better blending and transitions
// USparseVoxelGrid.cpp

// In USparseVoxelGrid.cpp
void USparseVoxelGrid::SetVoxel(int32 X, int32 Y, int32 Z, float NewSDFValue, bool bDig)
{
    // Lock the method in the VoxelData Mutex for threadsafe operation.
    FScopeLock Lock(&VoxelDataMutex);
    constexpr float SDF_THRESHOLD = 0.001f;

    // Ignore negligible changes
    if (FMath::IsNearlyZero(NewSDFValue, SDF_THRESHOLD))
        return;

    FIntVector VoxelKey(X, Y, Z);
    FVoxelData* ExistingVoxel = VoxelData.Find(VoxelKey);

    if (ExistingVoxel)
    {
        float CurrentValue = ExistingVoxel->SDFValue;
        float BlendedValue;

        if (bDig)
        {
            // When digging, use the maximum value (more air-like)
            BlendedValue = FMath::Max(CurrentValue, NewSDFValue);
        }
        else
        {
            // When adding, use the minimum value (more solid-like)
            BlendedValue = FMath::Min(CurrentValue, NewSDFValue);
        }

        ExistingVoxel->SDFValue = BlendedValue;
    }
    else
    {
        // For new voxels, just use the new value directly
        VoxelData.Add(VoxelKey, FVoxelData(NewSDFValue));
    }

    if (ParentChunk)
    {
        ParentChunk->MarkDirty();
    }
}







FArchive operator<<(const FArchive& Ar, int Int);

bool USparseVoxelGrid::SerializeToArchive(FArchive& Ar)
{
    // Serialize core metadata
    Ar << TerrainGridSize;
    Ar << Subdivisions;
    Ar << ChunkSize;

    // Serialize voxel count
    int32 VoxelCount = VoxelData.Num();
    Ar << VoxelCount;

    if (Ar.IsSaving())
    {
        for (auto& Pair : VoxelData)
        {
            FIntVector& Key = Pair.Key;
            FVoxelData& Voxel = Pair.Value;

            Ar << Key.X;
            Ar << Key.Y;
            Ar << Key.Z;
            Ar << Voxel.SDFValue;
        }
    }

    return true;
}

bool USparseVoxelGrid::SerializeFromArchive(FArchive& Ar)
{
    Ar << TerrainGridSize;
    Ar << Subdivisions;
    Ar << ChunkSize;

    int32 VoxelCount = 0;
    Ar << VoxelCount;

    if (VoxelCount < 0 || VoxelCount > 100000000) // sanity check
    {
        UE_LOG(LogTemp, Error, TEXT("Invalid voxel count: %d"), VoxelCount);
        return false;
    }

    if (Ar.IsLoading())
    {
        VoxelData.Empty(); // Safe to ignore pre-allocation
        for (int32 i = 0; i < VoxelCount; ++i)
        {
            int32 X, Y, Z;
            float SDFValue;

            Ar << X << Y << Z << SDFValue;
            VoxelData.Add(FIntVector(X, Y, Z), FVoxelData{ SDFValue });
        }
    }

    return true;
}


const FVoxelData* USparseVoxelGrid::GetVoxelData(const FIntVector& Voxel) const
{
    return VoxelData.Find(Voxel);
}

FVoxelData* USparseVoxelGrid::GetVoxelData(const FIntVector& Voxel)
{
    return VoxelData.Find(Voxel);
}


float USparseVoxelGrid::GetVoxel(FIntVector Vector)
{
   return GetVoxel(Vector.X, Vector.Y, Vector.Z);
}

bool USparseVoxelGrid::IsVoxelSolid(const FIntVector& VoxelIndex) const
{
    const FVoxelData* Data = GetVoxelData(VoxelIndex);
    if (!Data)
    {
        // In a sparse grid, missing voxels are typically air
        // You could also return (FVoxelConversion::SDF_AIR <= 0.0f) if you want to be explicit
        return false;
    }
    
    // SDF-based solidity: negative values are solid, positive are air
    return Data->SDFValue <= 0.0f;
}


float USparseVoxelGrid::GetVoxel(int32 X, int32 Y, int32 Z)
{
    FIntVector VoxelKey(X, Y, Z);
    FVoxelData* ExistingVoxel = VoxelData.Find(VoxelKey);

    if (ExistingVoxel)
    {
        return ExistingVoxel->SDFValue;
    }
    else
    {
        FVector WorldPos = FVoxelConversion::LocalVoxelToWorld(FIntVector(X, Y, Z));
        float LandscapeHeight = DiggerManager->GetLandscapeHeightAt(FVector(WorldPos.X, WorldPos.Y, 0));
        return (WorldPos.Z < LandscapeHeight) ? SDF_SOLID : SDF_AIR;
    }
}


float USparseVoxelGrid::GetVoxel(int32 X, int32 Y, int32 Z) const
{
    FIntVector VoxelKey(X, Y, Z);
    const FVoxelData* ExistingVoxel = VoxelData.Find(VoxelKey);

    if (ExistingVoxel)
    {
        return ExistingVoxel->SDFValue;
    }
    else
    {
        FVector WorldPos = FVoxelConversion::LocalVoxelToWorld(FIntVector(X, Y, Z));
        float LandscapeHeight = DiggerManager->GetLandscapeHeightAt(FVector(WorldPos.X, WorldPos.Y, 0));
        return (WorldPos.Z < LandscapeHeight) ? SDF_SOLID : SDF_AIR;
    }
}


bool USparseVoxelGrid::FindNearestSetVoxel(const FIntVector& StartCoords, FIntVector& OutVoxel)
{
    // First check if the start position itself is a set voxel
    if (VoxelData.Contains(StartCoords))
    {
        OutVoxel = StartCoords;
        if (DiggerDebug::Voxels || DiggerDebug::Islands)
        UE_LOG(LogTemp, Log, TEXT("Found voxel at start position: %s"), *StartCoords.ToString());
        return true;
    }
    
    // Debug: Log how many voxels are in the grid and some sample voxels
    if (DiggerDebug::Voxels || DiggerDebug::Islands)
    UE_LOG(LogTemp, Warning, TEXT("Grid contains %d voxels. Searching for nearest voxel to %s"), 
        VoxelData.Num(), *StartCoords.ToString());
    
    // Log some sample voxels for debugging
    int32 SampleCount = 0;
    if (DiggerDebug::Voxels || DiggerDebug::Islands)
    for (const auto& Pair : VoxelData)
    {
        if (SampleCount++ < 10)
        {
            UE_LOG(LogTemp, Warning, TEXT("Sample voxel %d: %s with SDF value %f"), 
                SampleCount, *Pair.Key.ToString(), Pair.Value.SDFValue);
        }
    }
    
    // If grid is empty, return early
    if (VoxelData.Num() == 0)
    {
        if (DiggerDebug::Voxels || DiggerDebug::Islands)
        UE_LOG(LogTemp, Warning, TEXT("Grid is empty, no voxels to find"));
        return false;
    }
    
    // Search in expanding cubes around the start position
    const int32 MaxSearchRadius = 30; // Increase search radius further
    
    for (int32 radius = 1; radius <= MaxSearchRadius; radius++)
    {
        // Check all voxels at the current search radius
        for (int32 dx = -radius; dx <= radius; dx++)
        {
            for (int32 dy = -radius; dy <= radius; dy++)
            {
                for (int32 dz = -radius; dz <= radius; dz++)
                {
                    // Only check voxels at the current radius (on the surface of the cube)
                    if (FMath::Abs(dx) == radius || FMath::Abs(dy) == radius || FMath::Abs(dz) == radius)
                    {
                        FIntVector TestVoxel = StartCoords + FIntVector(dx, dy, dz);
                        
                        if (VoxelData.Contains(TestVoxel))
                        {
                            OutVoxel = TestVoxel;
                            if (DiggerDebug::Voxels || DiggerDebug::Islands)
                            UE_LOG(LogTemp, Warning, TEXT("Found voxel at radius %d: %s"), radius, *TestVoxel.ToString());
                            return true;
                        }
                    }
                }
            }
        }
        
        // Log progress every few iterations
        if (radius % 5 == 0)
        {
            if (DiggerDebug::Voxels || DiggerDebug::Islands)
            UE_LOG(LogTemp, Log, TEXT("Searched radius %d, no voxels found yet"), radius);
        }
    }
    
    // If we get here, we didn't find any voxels within the search radius
    if (DiggerDebug::Voxels || DiggerDebug::Islands)
    UE_LOG(LogTemp, Warning, TEXT("No voxels found within search radius %d of %s"), 
        MaxSearchRadius, *StartCoords.ToString());
    
    // As a last resort, try to find any voxel in the grid
    if (VoxelData.Num() > 0)
    {
        auto It = VoxelData.CreateConstIterator();
        OutVoxel = It.Key();
        if (DiggerDebug::Voxels || DiggerDebug::Islands)
        UE_LOG(LogTemp, Warning, TEXT("Falling back to first voxel in grid: %s with SDF value %f"), 
            *OutVoxel.ToString(), It.Value().SDFValue);
        return true;
    }
    
    return false;
}

bool USparseVoxelGrid::HasVoxelAt(const FIntVector& Key) const
{
    return VoxelData.Contains(Key);
}


bool USparseVoxelGrid::HasVoxelAt(int32 X, int32 Y, int32 Z) const
{
    return VoxelData.Contains(FIntVector(X, Y, Z));
}


TMap<FIntVector, float> USparseVoxelGrid::GetAllVoxels() const
{
    TMap<FIntVector, float> Voxels;
    for (const auto& VoxelPair : VoxelData)
    {
        Voxels.Add(FIntVector(VoxelPair.Key), VoxelPair.Value.SDFValue);
        if (DiggerDebug::Voxels)
        {
            UE_LOG(LogTemp, Warning, TEXT("Voxel at %s has SDF value: %f"), *VoxelPair.Key.ToString(), VoxelPair.Value.SDFValue);
        }
    }

    return Voxels;
}

bool USparseVoxelGrid::VoxelExists(int32 X, int32 Y, int32 Z) const
{
    return VoxelData.Contains(FIntVector(X, Y, Z));
}

void USparseVoxelGrid::LogVoxelData() const
{
    if(!VoxelData.IsEmpty())
    {
        if (DiggerDebug::Voxels)
        UE_LOG(LogTemp, Warning, TEXT("Voxel Data isn't empty!"));
    }
    else
    {
        if (DiggerDebug::Voxels)
        UE_LOG(LogTemp, Error, TEXT("Voxel Data is empty!"));
    }
    for (const auto& VoxelPair : VoxelData)
    {
        if (DiggerDebug::Voxels)
        UE_LOG(LogTemp, Warning, TEXT("Voxel at [%s] has SDF value: %f"), *VoxelPair.Key.ToString(), VoxelPair.Value.SDFValue);
    }
}


void USparseVoxelGrid::RemoveVoxels(const TArray<FIntVector>& VoxelsToRemove)
{
    // Lock the voxel data for thread-safe removal
    {
        FScopeLock Lock(&VoxelDataMutex);
        for (const FIntVector& Voxel : VoxelsToRemove)
        {
            if (DiggerDebug::Voxels || DiggerDebug::Islands)
            {
                // Queue debug drawing on game thread
                AsyncTask(ENamedThreads::GameThread, [this, Voxel]()
                {
                    DrawDebugBox(GetWorld(), 
                        FVoxelConversion::ChunkVoxelToWorld(GetParentChunk()->GetChunkPosition(), Voxel),
                        FVector(FVoxelConversion::LocalVoxelSize / 2.0f), 
                        FColor::Red, false, 5.0f);
                });
            }
            
            VoxelData.Remove(Voxel);
        }
    }

    // Queue mesh update on game thread
    if (!IsInGameThread())
    {
        AsyncTask(ENamedThreads::GameThread, [WeakThis = MakeWeakObjectPtr(this)]()
        {
            if (WeakThis.IsValid() && WeakThis->ParentChunk)
            {
                WeakThis->ParentChunk->ForceUpdate();
            }
        });
    }
    else
    {
        if (ParentChunk)
        {
            ParentChunk->ForceUpdate();
        }
    }
}



// Returns all voxels belonging to the island at the given position
bool USparseVoxelGrid::CollectIslandAtPosition(const FVector& Center, TArray<FIntVector>& OutVoxels)
{
    OutVoxels.Empty();

    // Convert world position to voxel space
    FIntVector StartVoxel = FVoxelConversion::WorldToLocalVoxel(Center);

    if (DiggerDebug::Islands || DiggerDebug::Space)
    {
        UE_LOG(LogTemp, Display, TEXT("[Island] Extraction at position: %s → Voxel: %s"), *Center.ToString(), *StartVoxel.ToString());
    }

    const float SDFThreshold = 0.0f;

    auto IsSolid = [&](const FIntVector& Voxel) -> bool
    {
        const FVoxelData* Data = VoxelData.Find(Voxel);
        return Data && FMath::IsFinite(Data->SDFValue) && Data->SDFValue < SDFThreshold;
    };

    // Guard 1: Make sure this grid is valid
    if (!IsValid(this))
    {
        UE_LOG(LogTemp, Error, TEXT("[Island] Grid instance invalid."));
        return false;
    }

    // Guard 2: Ensure voxel data exists
    if (VoxelData.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[Island] VoxelData is empty."));
        return false;
    }

    // Guard 3: Check if the start voxel is solid
    if (!IsSolid(StartVoxel))
    {
        UE_LOG(LogTemp, Warning, TEXT("[Island] Start voxel is not solid at %s."), *StartVoxel.ToString());

        // Debug neighbors
        if (DiggerDebug::Islands)
        {
            for (int32 dx = -1; dx <= 1; ++dx)
                for (int32 dy = -1; dy <= 1; ++dy)
                    for (int32 dz = -1; dz <= 1; ++dz)
                    {
                        FIntVector P = StartVoxel + FIntVector(dx, dy, dz);
                        const FVoxelData* D = VoxelData.Find(P);
                        FString Log = D ? FString::Printf(TEXT("SDF=%f"), D->SDFValue) : TEXT("not found");
                        UE_LOG(LogTemp, Verbose, TEXT("Neighbor %s: %s"), *P.ToString(), *Log);
                    }
        }

        return false;
    }

    // Begin flood fill
    TSet<FIntVector> Visited;
    TQueue<FIntVector> Queue;
    Queue.Enqueue(StartVoxel);
    Visited.Add(StartVoxel);

    const int32 MaxIterations = 500000;
    int32 IterationCount = 0;

    static const FIntVector Dirs[] = {
        {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}
    };

    while (!Queue.IsEmpty() && IterationCount++ < MaxIterations)
    {
        FIntVector Current;
        Queue.Dequeue(Current);
        OutVoxels.Add(Current);

        for (const FIntVector& Dir : Dirs)
        {
            FIntVector Neighbor = Current + Dir;
            if (!Visited.Contains(Neighbor) && IsSolid(Neighbor))
            {
                Queue.Enqueue(Neighbor);
                Visited.Add(Neighbor);
            }
        }
    }

    if (IterationCount >= MaxIterations)
    {
        UE_LOG(LogTemp, Error, TEXT("[Island] Iteration limit reached (%d). Possible infinite island."), MaxIterations);
    }

    UE_LOG(LogTemp, Display, TEXT("[Island] Collected %d voxels from island at %s."), OutVoxels.Num(), *Center.ToString());
    return OutVoxels.Num() > 0;
}



bool USparseVoxelGrid::ExtractIslandByVoxel(const FIntVector& LocalVoxelCoords, USparseVoxelGrid*& OutIslandGrid, TArray<FIntVector>& OutIslandVoxels)
{
    OutIslandVoxels.Empty();
    OutIslandGrid = nullptr;

    // Guard 1: Ensure this grid is valid and has voxel data
    if (!IsValid(this) || VoxelData.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("[ExtractIslandByVoxel] Invalid or empty grid."));
        return false;
    }

    const float SDFThreshold = 0.0f;
    auto IsSolid = [this, SDFThreshold](const FIntVector& Voxel) -> bool
    {
        const FVoxelData* Data = VoxelData.Find(Voxel);
        return Data && Data->SDFValue < SDFThreshold;
    };

    // Guard 2: Check if the target voxel is actually solid
    if (!IsSolid(LocalVoxelCoords))
    {
        UE_LOG(LogTemp, Warning, TEXT("[ExtractIslandByVoxel] Start voxel not solid: %s"), *LocalVoxelCoords.ToString());
        return false;
    }

    // Guard 3: Allocate the new grid safely
    OutIslandGrid = NewObject<USparseVoxelGrid>(GetTransientPackage()); // Safe outer
    if (!IsValid(OutIslandGrid))
    {
        UE_LOG(LogTemp, Error, TEXT("[ExtractIslandByVoxel] Failed to allocate new island grid."));
        return false;
    }

    OutIslandGrid->Initialize(nullptr); // no chunk owner

    // Flood fill setup
    TSet<FIntVector> Visited;
    TQueue<FIntVector> Queue;
    Queue.Enqueue(LocalVoxelCoords);
    Visited.Add(LocalVoxelCoords);

    while (!Queue.IsEmpty())
    {
        FIntVector Current;
        Queue.Dequeue(Current);
        OutIslandVoxels.Add(Current);

        const FVoxelData* Original = VoxelData.Find(Current);
        if (Original && IsValid(OutIslandGrid))
        {
            OutIslandGrid->VoxelData.Add(Current, *Original);
        }

        static const FIntVector Dirs[] = {
            {1,0,0}, {-1,0,0},
            {0,1,0}, {0,-1,0},
            {0,0,1}, {0,0,-1}
        };

        for (const FIntVector& Dir : Dirs)
        {
            FIntVector Neighbor = Current + Dir;
            if (!Visited.Contains(Neighbor) && IsSolid(Neighbor))
            {
                Visited.Add(Neighbor);
                Queue.Enqueue(Neighbor);
            }
        }
    }

    if (OutIslandVoxels.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[ExtractIslandByVoxel] No voxels gathered for island."));
        OutIslandGrid = nullptr;
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("[ExtractIslandByVoxel] Extracted %d voxels into new island grid."), OutIslandVoxels.Num());
    return true;
}




bool USparseVoxelGrid::ExtractIslandAtPosition(const FVector& WorldPosition, USparseVoxelGrid*& OutGrid, TArray<FIntVector>& OutVoxels)
{
    OutVoxels.Empty();
    OutGrid = nullptr;

    const FIntVector StartVoxel = FVoxelConversion::WorldToLocalVoxel(WorldPosition);
    const float SDFThreshold = 0.0f;

    // Safety: Grid validity and voxel data
    if (!IsValid(this) || VoxelData.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("[ExtractIsland] Grid invalid or empty."));
        return false;
    }

    auto IsSolid = [&](const FIntVector& Voxel)
    {
        const FVoxelData* Data = VoxelData.Find(Voxel);
        return Data && FMath::IsFinite(Data->SDFValue) && Data->SDFValue < SDFThreshold;
    };

    auto IsSurfaceVoxel = [&](const FIntVector& Voxel)
    {
        if (!IsSolid(Voxel)) return false;

        static const FIntVector Dirs[] = {
            {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}
        };

        for (const FIntVector& Dir : Dirs)
        {
            const FIntVector Neighbor = Voxel + Dir;
            const FVoxelData* Data = VoxelData.Find(Neighbor);
            if (!Data || Data->SDFValue >= SDFThreshold)
                return true;
        }

        return false;
    };

    // Guard: Starting voxel
    if (!IsSolid(StartVoxel))
    {
        if (DiggerDebug::Islands || DiggerDebug::Voxels)
        {
            UE_LOG(LogTemp, Warning, TEXT("[ExtractIsland] Start voxel %s not solid."), *StartVoxel.ToString());
        }
        return false;
    }

    // Grid creation
    USparseVoxelGrid* NewGrid = NewObject<USparseVoxelGrid>(this);
    if (!IsValid(NewGrid))
    {
        UE_LOG(LogTemp, Fatal, TEXT("[ExtractIsland] Failed to allocate new grid."));
        return false;
    }

    NewGrid->ParentChunk = ParentChunk;
    NewGrid->DiggerManager = DiggerManager;

    // Flood fill – connected voxels
    TSet<FIntVector> AllConnectedVoxels;
    TQueue<FIntVector> FloodQueue;

    const int32 MaxIterations = 500000;
    int32 IterationCount = 0;

    FloodQueue.Enqueue(StartVoxel);
    AllConnectedVoxels.Add(StartVoxel);

    static const FIntVector Dirs[] = {
        {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}
    };

    while (!FloodQueue.IsEmpty() && IterationCount++ < MaxIterations)
    {
        FIntVector Current;
        FloodQueue.Dequeue(Current);

        for (const FIntVector& Dir : Dirs)
        {
            const FIntVector Neighbor = Current + Dir;
            if (!AllConnectedVoxels.Contains(Neighbor) && IsSolid(Neighbor))
            {
                FloodQueue.Enqueue(Neighbor);
                AllConnectedVoxels.Add(Neighbor);
            }
        }
    }

    if (IterationCount >= MaxIterations)
    {
        UE_LOG(LogTemp, Error, TEXT("[ExtractIsland] Iteration limit hit — possible infinite island."));
        return false;
    }

    // Surface and internal classification
    TSet<FIntVector> SurfaceVoxels;
    TSet<FIntVector> InternalVoxels;

    for (const FIntVector& Voxel : AllConnectedVoxels)
    {
        const FVoxelData* Data = VoxelData.Find(Voxel);
        if (!Data || !FMath::IsFinite(Data->SDFValue)) continue;

        if (IsSurfaceVoxel(Voxel))
        {
            SurfaceVoxels.Add(Voxel);
            OutVoxels.Add(Voxel);
            NewGrid->SetVoxel(Voxel.X, Voxel.Y, Voxel.Z, Data->SDFValue, false);
        }
        else
        {
            InternalVoxels.Add(Voxel);
        }
    }

    // Generate shell around surface for mesh stability
    for (const FIntVector& SurfaceVoxel : SurfaceVoxels)
    {
        for (const FIntVector& Dir : Dirs)
        {
            FIntVector Neighbor = SurfaceVoxel + Dir;

            if (InternalVoxels.Contains(Neighbor))
            {
                if (const FVoxelData* Data = VoxelData.Find(Neighbor))
                {
                    NewGrid->SetVoxel(Neighbor.X, Neighbor.Y, Neighbor.Z, Data->SDFValue, false);
                }
            }
            else if (!SurfaceVoxels.Contains(Neighbor))
            {
                NewGrid->SetVoxel(Neighbor.X, Neighbor.Y, Neighbor.Z, 1.0f, false); // Air
            }
        }
    }

    // Optional: Encapsulate this in `RemoveVoxels()` for safety
    for (const FIntVector& Voxel : AllConnectedVoxels)
    {
        VoxelData.Remove(Voxel);
    }

    if (ParentChunk)
    {
        ParentChunk->MarkDirty();
    }

    OutGrid = NewGrid;
    UE_LOG(LogTemp, Display, TEXT("[ExtractIsland] Extracted %d surface voxels."), OutVoxels.Num());
    return OutVoxels.Num() > 0;
}






TArray<FIslandData> USparseVoxelGrid::DetectIslands(float SDFThreshold)
{
    TArray<FIslandData> Islands;
    TSet<FIntVector> Visited;

    // Lambda to check if a voxel is solid
    auto IsSolid = [this, SDFThreshold](const FIntVector& Voxel) -> bool
    {
        const FVoxelData* Data = VoxelData.Find(Voxel);
        return Data && Data->SDFValue < SDFThreshold;
    };

    // 26-directional neighbors (includes diagonals)
    TArray<FIntVector> Directions;
    for (int32 dx = -1; dx <= 1; ++dx)
    for (int32 dy = -1; dy <= 1; ++dy)
    for (int32 dz = -1; dz <= 1; ++dz)
    {
        if (dx != 0 || dy != 0 || dz != 0)
        {
            Directions.Add(FIntVector(dx, dy, dz));
        }
    }

    for (const auto& Pair : VoxelData)
    {
        const FIntVector& VoxelCoords = Pair.Key;

        if (Visited.Contains(VoxelCoords) || !IsSolid(VoxelCoords))
            continue;

        // Begin flood fill
        TArray<FIntVector> IslandVoxels;
        TQueue<FIntVector> Queue;

        Queue.Enqueue(VoxelCoords);
        Visited.Add(VoxelCoords);
        IslandVoxels.Add(VoxelCoords);

        while (!Queue.IsEmpty())
        {
            FIntVector Current;
            Queue.Dequeue(Current);

            for (const FIntVector& Dir : Directions)
            {
                FIntVector Neighbor = Current + Dir;

                if (!Visited.Contains(Neighbor) && IsSolid(Neighbor))
                {
                    Queue.Enqueue(Neighbor);
                    Visited.Add(Neighbor);
                    IslandVoxels.Add(Neighbor);
                }
            }
        }

        // Compute world-space center
        FVector Center = FVector::ZeroVector;
        for (const FIntVector& GlobalVoxel : IslandVoxels)
        {
            Center += FVoxelConversion::GlobalVoxelToWorld_CenterAligned(GlobalVoxel);
        }

        if (IslandVoxels.Num() > 0)
        {
            Center /= IslandVoxels.Num();

            FIslandData Island;
            Island.Location = Center + DebugRenderOffset;
            Island.VoxelCount = IslandVoxels.Num();
            Island.ReferenceVoxel = IslandVoxels[0];
            Island.Voxels = IslandVoxels;

            Islands.Add(Island);
        }
    }

    // Optional: Log any missed solid voxels
    #if WITH_EDITOR
    for (const auto& Pair : VoxelData)
    {
        const FIntVector& VoxelCoords = Pair.Key;
        if (IsSolid(VoxelCoords) && !Visited.Contains(VoxelCoords))
        {
            UE_LOG(LogTemp, Warning, TEXT("[IslandDetection] Missed solid voxel at %s"), *VoxelCoords.ToString());
        }
    }
    #endif

    return Islands;
}


void USparseVoxelGrid::RemoveSpecifiedVoxels(const TArray<FIntVector>& LocalVoxels)
{
    FScopeLock Lock(&VoxelDataMutex);
    for (const FIntVector& Voxel : LocalVoxels)
    {
        if (DiggerDebug::Voxels || DiggerDebug::Islands)
        {
            AsyncTask(ENamedThreads::GameThread, [this, Voxel]()
            {
                DrawDebugBox(GetWorld(), 
                    FVoxelConversion::ChunkVoxelToWorld(GetParentChunk()->GetChunkPosition(), Voxel),
                    FVector(FVoxelConversion::LocalVoxelSize / 2.0f), 
                    FColor::Red, false, 5.0f);
            });
        }
        
        VoxelData.Remove(Voxel);
    }
}

bool USparseVoxelGrid::RemoveVoxel(const FIntVector& LocalVoxel)
{
    FScopeLock Lock(&VoxelDataMutex);
    
    if (DiggerDebug::Voxels || DiggerDebug::Islands)
    {
        AsyncTask(ENamedThreads::GameThread, [this, LocalVoxel]()
        {
            DrawDebugBox(GetWorld(), 
                FVoxelConversion::ChunkVoxelToWorld(GetParentChunk()->GetChunkPosition(), LocalVoxel),
                FVector(FVoxelConversion::LocalVoxelSize / 2.0f), 
                FColor::Red, false, 5.0f);
        });
    }
    
    return VoxelData.Remove(LocalVoxel) > 0;
}


// In USparseVoxelGrid.cpp
void USparseVoxelGrid::SynchronizeBordersWithNeighbors()
{
/*    if (!ParentChunk || !DiggerManager) return;

    FIntVector ThisChunkCoords = ParentChunkCoordinates;
    int32 N = ChunkSize * Subdivisions;
    const float SDFTransitionBand = FVoxelConversion::LocalVoxelSize * 3.0f;

    for (int Axis = 0; Axis < 3; ++Axis)
    {
        for (int Dir = -1; Dir <= 1; Dir += 2)
        {
            FIntVector NeighborCoords = ThisChunkCoords;
            NeighborCoords[Axis] += Dir;

            UVoxelChunk* NeighborChunk = DiggerManager->GetOrCreateChunkAtChunk(NeighborCoords);
            if (!NeighborChunk) continue;

            USparseVoxelGrid* NeighborGrid = NeighborChunk->GetSparseVoxelGrid();
            if (!NeighborGrid) continue;

            for (int32 i = 0; i < N; ++i)
            for (int32 j = 0; j < N; ++j)
            {
                FIntVector ThisBorder = FIntVector::ZeroValue;
                FIntVector NeighborBorder = FIntVector::ZeroValue;

                // Correct border mapping for all axes
                switch (Axis)
                {
                    case 0: // X axis
                        ThisBorder.X = (Dir == -1) ? 0 : N - 1;
                        NeighborBorder.X = (Dir == -1) ? N - 1 : 0;
                        ThisBorder.Y = NeighborBorder.Y = i;
                        ThisBorder.Z = NeighborBorder.Z = j;
                        break;
                    case 1: // Y axis
                        ThisBorder.Y = (Dir == -1) ? 0 : N - 1;
                        NeighborBorder.Y = (Dir == -1) ? N - 1 : 0;
                        ThisBorder.X = NeighborBorder.X = i;
                        ThisBorder.Z = NeighborBorder.Z = j;
                        break;
                    case 2: // Z axis
                        ThisBorder.Z = (Dir == -1) ? 0 : N - 1;
                        NeighborBorder.Z = (Dir == -1) ? N - 1 : 0;
                        ThisBorder.X = NeighborBorder.X = i;
                        ThisBorder.Y = NeighborBorder.Y = j;
                        break;
                }

                bool HasA = VoxelData.Contains(ThisBorder);
                bool HasB = NeighborGrid->VoxelData.Contains(NeighborBorder);

                // Only synchronize if both sides have a voxel (i.e., are not air)
                if (HasA && HasB)
                {
                    float SDF_A = GetVoxel(ThisBorder.X, ThisBorder.Y, ThisBorder.Z);
                    float SDF_B = NeighborGrid->GetVoxel(NeighborBorder.X, NeighborBorder.Y, NeighborBorder.Z);

                    if (FMath::Abs(SDF_A - SDF_B) > 0)//KINDA_SMALL_NUMBER)
                    {
                        if (FMath::Abs(SDF_A) < SDFTransitionBand || FMath::Abs(SDF_B) < SDFTransitionBand)
                        {
                            float AverageSDF = 0.5f * (SDF_A + SDF_B);

                            SetVoxel(ThisBorder.X, ThisBorder.Y, ThisBorder.Z, AverageSDF, false);
                            NeighborGrid->SetVoxel(NeighborBorder.X, NeighborBorder.Y, NeighborBorder.Z, AverageSDF,
                                                   false);

                            bBorderIsDirty = true;
                            NeighborGrid->bBorderIsDirty = true;
                        }
                    }
                }
            }
        }
    }*/
}







void USparseVoxelGrid::RenderVoxels() {
    if (!EnsureDiggerManager()) {
        UE_LOG(LogTemp, Error, TEXT("DiggerManager is null or invalid in SparseVoxelGrid RenderVoxels()!!"));
        return;
    }

    World = DiggerManager->GetSafeWorld();
    if (!World) {
        UE_LOG(LogTemp, Error, TEXT("World is null within SparseVoxelGrid RenderVoxels()!!"));
        return;
    }

    if (VoxelData.IsEmpty()) {
        UE_LOG(LogTemp, Warning, TEXT("VoxelData is empty, no voxels to render!"));
        return;
    }

    // Get the parent chunk coordinates
    FIntVector ChunkCoords = GetParentChunkCoordinates();
    
    UE_LOG(LogTemp, Warning, TEXT("Rendering voxels for chunk %s"), *ChunkCoords.ToString());

    // Render each voxel
    for (const auto& Voxel : VoxelData) {
        const FIntVector& LocalVoxelCoords = Voxel.Key;
        const FVoxelData& VoxelDataValue = Voxel.Value;

        // Convert local voxel coordinates to global voxel coordinates
        FIntVector GlobalVoxelCoords = FVoxelConversion::ChunkAndLocalToGlobalVoxel_CenterAligned(
            ChunkCoords, 
            LocalVoxelCoords
        );
        
        // Convert global voxel coordinates to world position
        FVector WorldPosition = FVoxelConversion::GlobalVoxelToWorld_CenterAligned(GlobalVoxelCoords);
        FVector Center = WorldPosition + DebugRenderOffset;

        const float SDFValue = VoxelDataValue.SDFValue;

        FColor VoxelColor = FColor::White;
        if (SDFValue > 0.0f) {
            VoxelColor = FColor::Green; // Air
        } else if (SDFValue < 0.0f) {
            VoxelColor = FColor::Red;   // Solid
        } else {
            VoxelColor = FColor::Yellow; // Surface
        }

        DrawDebugBox(World, 
                    Center + FVector(FVoxelConversion::LocalVoxelSize / 2.0f), 
                    FVector(FVoxelConversion::LocalVoxelSize / 2.0f), 
                    FQuat::Identity, 
                    VoxelColor, 
                    false, 
                    15.f, 
                    0, 
                    2);
                    
        DrawDebugPoint(World, 
                      Center + FVector(FVoxelConversion::LocalVoxelSize / 2.0f), 
                      2.0f, 
                      VoxelColor, 
                      false, 
                      15.f, 
                      0);

        UE_LOG(LogTemp, Warning, TEXT("Voxel: Local %s -> Global %s -> World %s"), 
               *LocalVoxelCoords.ToString(), *GlobalVoxelCoords.ToString(), *Center.ToString());
    }
}