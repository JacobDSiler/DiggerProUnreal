#include "SparseVoxelGrid.h"
#include "DiggerManager.h"
#include "DrawDebugHelpers.h"
#include "C:\Users\serpe\Documents\Unreal Projects\DiggerProUnreal\Source\DiggerProUnreal\Public\Voxel\FVoxelSDFHelper.h"
#include "VoxelChunk.h"
#include "VoxelConversion.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "AssetRegistry/AssetRegistryModule.h"
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
            UE_LOG(LogTemp, Error, TEXT("DiggerManager is null in VoxelToWorldSpace"));
            return false;
        }
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
        //UE_LOG(LogTemp, Error, TEXT("World is null in IsPointAboveLandscape"));
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

void USparseVoxelGrid::SetVoxel(int32 X, int32 Y, int32 Z, float NewSDFValue, bool bDig)
{
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







void USparseVoxelGrid::SynchronizeBordersIfDirty()
{
    if (bBorderIsDirty)
    {
        SynchronizeBordersWithNeighbors();
        bBorderIsDirty = false;
    }
}


bool USparseVoxelGrid::SaveVoxelDataToFile(const FString& FilePath)
{
    FBufferArchive ToBinary;

    // Serialize metadata
    ToBinary << TerrainGridSize;
    ToBinary << Subdivisions;
    ToBinary << ChunkSize;

    // Serialize the voxel data map
    for (const auto& VoxelPair : VoxelData)
    {
        int32 X = VoxelPair.Key.X;
        int32 Y = VoxelPair.Key.Y;
        int32 Z = VoxelPair.Key.Z;
        float SDFValue = VoxelPair.Value.SDFValue;

        ToBinary << X;
        ToBinary << Y;
        ToBinary << Z;
        ToBinary << SDFValue;
    }


    if (FFileHelper::SaveArrayToFile(ToBinary, *FilePath))
    {
        ToBinary.FlushCache();
        return true;
    }
    return false;
}

bool USparseVoxelGrid::LoadVoxelDataFromFile(const FString& FilePath)
{
    TArray<uint8> BinaryArray;

    if (FFileHelper::LoadFileToArray(BinaryArray, *FilePath))
    {
        FMemoryReader FromBinary = FMemoryReader(BinaryArray, true);
        FromBinary.Seek(0);

        FromBinary << TerrainGridSize;
        FromBinary << Subdivisions;
        FromBinary << ChunkSize;

        VoxelData.Empty();

        while (FromBinary.Tell() < FromBinary.TotalSize())
        {
            int32 X, Y, Z;
            float SDFValue;

            FromBinary << X;
            FromBinary << Y;
            FromBinary << Z;
            FromBinary << SDFValue;

            FIntVector VoxelCoords(X, Y, Z);
            FVoxelData Voxel;
            Voxel.SDFValue = SDFValue;
            VoxelData.Add(VoxelCoords, Voxel);
        }


        FromBinary.FlushCache();
        return true;
    }

    return false;
}


float USparseVoxelGrid::GetVoxel(FIntVector Vector)
{
   return GetVoxel(Vector.X, Vector.Y, Vector.Z);
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

bool USparseVoxelGrid::FindNearestSetVoxel(const FIntVector& StartCoords, FIntVector& OutVoxel)
{
    // First check if the start position itself is a set voxel
    if (VoxelData.Contains(StartCoords))
    {
        OutVoxel = StartCoords;
        UE_LOG(LogTemp, Log, TEXT("Found voxel at start position: %s"), *StartCoords.ToString());
        return true;
    }
    
    // Debug: Log how many voxels are in the grid and some sample voxels
    UE_LOG(LogTemp, Warning, TEXT("Grid contains %d voxels. Searching for nearest voxel to %s"), 
        VoxelData.Num(), *StartCoords.ToString());
    
    // Log some sample voxels for debugging
    int32 SampleCount = 0;
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
            UE_LOG(LogTemp, Log, TEXT("Searched radius %d, no voxels found yet"), radius);
        }
    }
    
    // If we get here, we didn't find any voxels within the search radius
    UE_LOG(LogTemp, Warning, TEXT("No voxels found within search radius %d of %s"), 
        MaxSearchRadius, *StartCoords.ToString());
    
    // As a last resort, try to find any voxel in the grid
    if (VoxelData.Num() > 0)
    {
        auto It = VoxelData.CreateConstIterator();
        OutVoxel = It.Key();
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



TMap<FVector, float> USparseVoxelGrid::GetVoxels() const
{
    TMap<FVector, float> Voxels;
    for (const auto& VoxelPair : VoxelData)
    {
        Voxels.Add(FVector(VoxelPair.Key), VoxelPair.Value.SDFValue);
        //UE_LOG(LogTemp, Warning, TEXT("Voxel at %s has SDF value: %f"), *VoxelPair.Key.ToString(), VoxelPair.Value.SDFValue);
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
        UE_LOG(LogTemp, Warning, TEXT("Voxel Data isn't empty!"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Voxel Data is empty!"));
    }
    for (const auto& VoxelPair : VoxelData)
    {
        UE_LOG(LogTemp, Warning, TEXT("Voxel at [%s] has SDF value: %f"), *VoxelPair.Key.ToString(), VoxelPair.Value.SDFValue);
    }
}


void USparseVoxelGrid::RemoveVoxels(const TArray<FIntVector>& VoxelsToRemove)
{
    for (const FIntVector& Voxel : VoxelsToRemove)
    {
        VoxelData.Remove(Voxel);
    }
}



// Returns all voxels belonging to the island at the given position
bool USparseVoxelGrid::CollectIslandAtPosition(const FVector& Center, TArray<FIntVector>& OutVoxels)
{
    OutVoxels.Empty();

    // Convert world position to voxel space using the helper
    FIntVector StartVoxel = FVoxelConversion::WorldToLocalVoxel(Center);

    // Log conversion info
    UE_LOG(LogTemp, Warning, TEXT("Island extraction started at world position: %s (voxel space: %s)"), *Center.ToString(), *StartVoxel.ToString());

    // SDF threshold (use your default or expose as parameter)
    float SDFThreshold = 0.0f;

    // Lambda to check if a voxel is solid
    auto IsSolid = [&](const FIntVector& Voxel) -> bool
    {
        const FVoxelData* Data = VoxelData.Find(Voxel);
        return Data && Data->SDFValue < SDFThreshold;
    };

    // Early out if the start voxel is not solid
    if (!IsSolid(StartVoxel))
    {
        UE_LOG(LogTemp, Warning, TEXT("Start voxel is not solid. Aborting extraction."));

        // Log surrounding voxel data for debugging
        for (int32 dx = -1; dx <= 1; ++dx)
        for (int32 dy = -1; dy <= 1; ++dy)
        for (int32 dz = -1; dz <= 1; ++dz)
        {
            FIntVector P = StartVoxel + FIntVector(dx, dy, dz);
            const FVoxelData* D = VoxelData.Find(P);
            if (D)
            {
                UE_LOG(LogTemp, Warning, TEXT("Neighbor voxel %s: SDF=%f"), *P.ToString(), D->SDFValue);
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("Neighbor voxel %s: not found"), *P.ToString());
            }
        }

        return false;
    }

    TSet<FIntVector> Visited;
    TQueue<FIntVector> Queue;
    Queue.Enqueue(StartVoxel);
    Visited.Add(StartVoxel);

    while (!Queue.IsEmpty())
    {
        FIntVector Current;
        Queue.Dequeue(Current);

        OutVoxels.Add(Current);

        // 6-connected neighbors
        static const FIntVector Dirs[] = {
            {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}
        };
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

    return OutVoxels.Num() > 0;
}



bool USparseVoxelGrid::ExtractIslandAtPosition(const FVector& WorldPosition, USparseVoxelGrid*& OutGrid, TArray<FIntVector>& OutVoxels)
{
    OutVoxels.Empty();
    OutGrid = nullptr;

    const FIntVector StartVoxel = FVoxelConversion::WorldToLocalVoxel(WorldPosition);
    float SDFThreshold = 0.0f;

    // Function to check if a voxel is solid
    auto IsSolid = [&](const FIntVector& Voxel) -> bool
    {
        const FVoxelData* Data = VoxelData.Find(Voxel);
        return Data && Data->SDFValue < SDFThreshold;
    };

    // Function to check if a voxel is a surface voxel (has at least one air neighbor)
    auto IsSurfaceVoxel = [&](const FIntVector& Voxel) -> bool
    {
        if (!IsSolid(Voxel))
            return false;

        static const FIntVector Dirs[] = {
            {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}
        };

        for (const FIntVector& Dir : Dirs)
        {
            FIntVector Neighbor = Voxel + Dir;
            const FVoxelData* Data = VoxelData.Find(Neighbor);
            
            // If neighbor doesn't exist or is air, this is a surface voxel
            if (!Data || Data->SDFValue >= SDFThreshold)
                return true;
        }
        
        return false;
    };

    if (!IsSolid(StartVoxel))
    {
        UE_LOG(LogTemp, Warning, TEXT("ExtractIslandAtPosition: Start voxel %s is not solid."), *StartVoxel.ToString());
        return false;
    }

    // Create a new grid for the extracted island
    USparseVoxelGrid* NewGrid = NewObject<USparseVoxelGrid>(this);
    NewGrid->ParentChunk = ParentChunk;
    NewGrid->DiggerManager = DiggerManager;

    // First pass: Find all connected solid voxels (flood fill)
    TSet<FIntVector> AllConnectedVoxels;
    TQueue<FIntVector> FloodQueue;
    
    FloodQueue.Enqueue(StartVoxel);
    AllConnectedVoxels.Add(StartVoxel);
    
    while (!FloodQueue.IsEmpty())
    {
        FIntVector Current;
        FloodQueue.Dequeue(Current);
        
        static const FIntVector Dirs[] = {
            {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}
        };

        for (const FIntVector& Dir : Dirs)
        {
            FIntVector Neighbor = Current + Dir;

            if (!AllConnectedVoxels.Contains(Neighbor) && IsSolid(Neighbor))
            {
                FloodQueue.Enqueue(Neighbor);
                AllConnectedVoxels.Add(Neighbor);
            }
        }
    }
    
    // Second pass: Identify surface voxels and extract them
    TSet<FIntVector> SurfaceVoxels;
    TSet<FIntVector> InternalVoxels;
    
    for (const FIntVector& Voxel : AllConnectedVoxels)
    {
        if (IsSurfaceVoxel(Voxel))
        {
            SurfaceVoxels.Add(Voxel);
            OutVoxels.Add(Voxel);
            
            // Copy the voxel data to the new grid
            if (const FVoxelData* ExistingData = VoxelData.Find(Voxel))
            {
                NewGrid->SetVoxel(Voxel.X, Voxel.Y, Voxel.Z, ExistingData->SDFValue, false);
            }
        }
        else
        {
            InternalVoxels.Add(Voxel);
        }
    }
    
    // Add a shell of internal voxels to ensure proper mesh generation
    for (const FIntVector& SurfaceVoxel : SurfaceVoxels)
    {
        static const FIntVector Dirs[] = {
            {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}
        };

        for (const FIntVector& Dir : Dirs)
        {
            FIntVector Neighbor = SurfaceVoxel + Dir;
            
            // If it's an internal voxel, add it to the new grid
            if (InternalVoxels.Contains(Neighbor))
            {
                if (const FVoxelData* ExistingData = VoxelData.Find(Neighbor))
                {
                    NewGrid->SetVoxel(Neighbor.X, Neighbor.Y, Neighbor.Z, ExistingData->SDFValue, false);
                }
            }
            // If it's outside the solid region, add an air voxel to ensure proper surface
            else if (!SurfaceVoxels.Contains(Neighbor))
            {
                NewGrid->SetVoxel(Neighbor.X, Neighbor.Y, Neighbor.Z, 1.0f, false); // Air voxel
            }
        }
    }

    // CRITICAL: Remove extracted voxels from the original grid
    // This is the part that wasn't working correctly
    UE_LOG(LogTemp, Warning, TEXT("Removing %d voxels from source grid"), AllConnectedVoxels.Num());
    
    for (const FIntVector& Voxel : AllConnectedVoxels)
    {
        // Remove the voxel from the source grid
        VoxelData.Remove(Voxel);
    }

    // Mark the parent chunk as dirty to trigger a rebuild
    if (ParentChunk)
    {
        ParentChunk->MarkDirty();
    }

    OutGrid = NewGrid;
    return OutVoxels.Num() > 0;
}





TArray<FIslandData> USparseVoxelGrid::DetectIslands(float SDFThreshold)
{
    TArray<FIslandData> Islands;
    TSet<FIntVector> Visited;
    
    // Function to check if a voxel is solid
    auto IsSolid = [this, SDFThreshold](const FIntVector& Voxel) -> bool
    {
        const FVoxelData* Data = VoxelData.Find(Voxel);
        return Data && Data->SDFValue < SDFThreshold;
    };
    
    // Iterate through all voxels in the grid
    for (const auto& Pair : VoxelData)
    {
        const FIntVector& VoxelCoords = Pair.Key;
        
        // Skip if already visited or not solid
        if (Visited.Contains(VoxelCoords) || !IsSolid(VoxelCoords))
            continue;
        
        // This is a new island, perform flood fill
        TArray<FIntVector> IslandVoxels;
        TQueue<FIntVector> Queue;
        
        Queue.Enqueue(VoxelCoords);
        Visited.Add(VoxelCoords);
        IslandVoxels.Add(VoxelCoords);
        
        while (!Queue.IsEmpty())
        {
            FIntVector Current;
            Queue.Dequeue(Current);
            
            static const FIntVector Dirs[] = {
                {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}
            };
            
            for (const FIntVector& Dir : Dirs)
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
        
        // Calculate island center and size
        FVector Center = FVector::ZeroVector;
        for (const FIntVector& Voxel : IslandVoxels)
        {
            // Convert voxel coordinates to world position
            FVector WorldPos = FVoxelConversion::LocalVoxelToWorld(Voxel);
            Center += WorldPos;
        }
        
        if (IslandVoxels.Num() > 0)
        {
            Center /= IslandVoxels.Num();
            
            // Create island data
            FIslandData Island;
            Island.Location = Center;
            Island.VoxelCount = IslandVoxels.Num();
            
            // Store a reference voxel for this island
            Island.ReferenceVoxel = IslandVoxels[0];
            
            Islands.Add(Island);
            
            UE_LOG(LogTemp, Warning, TEXT("Detected island at %s with %d voxels. Reference voxel: %s"),
                *Center.ToString(), IslandVoxels.Num(), *Island.ReferenceVoxel.ToString());
        }
    }
    
    return Islands;
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