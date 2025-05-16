#include "SparseVoxelGrid.h"
#include "DiggerManager.h"
#include "DrawDebugHelpers.h"
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
    DiggerManager = ParentChunk->GetDiggerManager();
}

void USparseVoxelGrid::InitializeDiggerManager()
{
    if (!GetWorld())
    {
        //UE_LOG(LogTemp, Warning, TEXT("World is null in InitializeDiggerManager."));
        return; // Ensure we have a valid world context
    }

    //UE_LOG(LogTemp, Warning, TEXT("World is not null in InitializeDiggerManager. Grabbing the DiggerManager."));
    // Find and assign DiggerManager if not already assigned and set the member variables from it
    EnsureDiggerManager();
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
            //UE_LOG(LogTemp, Error, TEXT("DiggerManager is null in VoxelToWorldSpace"));
            return false;
        }
        //UE_LOG(LogTemp, Warning, TEXT("DiggerManager ensured correctly in an instance of SVG!"));
    }
    return true;
}





bool USparseVoxelGrid::IsPointAboveLandscape(FVector& Point)
{
    if(!World) World = GetWorld();
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



void USparseVoxelGrid::SetVoxel(FIntVector Position, float SDFValue, bool &bDig)
{
    SetVoxel(Position.X, Position.Y, Position.Z, SDFValue, bDig);
}



// In USparseVoxelGrid, update SetVoxel to use world coordinates comparison
// Enhanced SetVoxel for better blending and transitions
void USparseVoxelGrid::SetVoxel(int32 X, int32 Y, int32 Z, float NewSDFValue, bool bDig)
{
    FIntVector VoxelKey(X, Y, Z);

    // If the SDF is air, remove the voxel from the sparse grid
    if (NewSDFValue >= SDF_AIR)
    {
        //VoxelData.Remove(VoxelKey);
    }
    else
    {
        // Use FindOrAdd to update existing voxels
        FVoxelData* ExistingData = VoxelData.Find(VoxelKey);
        if (ExistingData)
        {
            ExistingData->SDFValue = NewSDFValue;
        }
        else
        {
            VoxelData.Add(VoxelKey, FVoxelData(NewSDFValue));
        }
    }

    // Border dirty flag
    if (X == 0 || Y == 0 || Z == 0 ||
        X == ChunkSize - 1 || Y == ChunkSize - 1 || Z == ChunkSize - 1)
    {
        bBorderIsDirty = true;
    }

    if (ParentChunk)
        ParentChunk->MarkDirty();
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
        //UE_LOG(LogTemp, Warning, TEXT("Voxel Data isn't empty!"));
    }
    else
    {
        //UE_LOG(LogTemp, Error, TEXT("Voxel Data is empty!"));
    }
    /*for (const auto& VoxelPair : VoxelData)
    {
        UE_LOG(LogTemp, Warning, TEXT("Voxel at [%s] has SDF value: %f"), *VoxelPair.Key.ToString(), VoxelPair.Value.SDFValue);
    }*/
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


bool USparseVoxelGrid::ExtractIslandAtPosition(
    const FVector& Center,
    USparseVoxelGrid*& OutTempGrid,
    TArray<FIntVector>& OutVoxels)
{
    OutVoxels.Empty();
    OutTempGrid = nullptr;

    // Step 1: Convert Center from World Space to Voxel Space Correctly
    FIntVector CenterVoxelCoords = FVoxelConversion::WorldToLocalVoxel(Center);
    
    UE_LOG(LogTemp, Warning, TEXT("Island extraction started at world position: %s (voxel space: %s)"),
        *Center.ToString(), *CenterVoxelCoords.ToString());

    float SDFThreshold = 0.0f;

    auto IsSolid = [&](const FIntVector& Voxel) -> bool
    {
        const FVoxelData* Data = VoxelData.Find(Voxel);
        return Data && ((Data->SDFValue <= 0.0f) || (Data->SDFValue < SDFThreshold));
    };

    // Step 2: Find a solid voxel nearby
    FIntVector StartVoxel = CenterVoxelCoords;
    bool bFoundStart = false;

    const int32 SearchRadius = 5;
    for (int32 dx = -SearchRadius; dx <= SearchRadius && !bFoundStart; ++dx)
    for (int32 dy = -SearchRadius; dy <= SearchRadius && !bFoundStart; ++dy)
    for (int32 dz = -SearchRadius; dz <= SearchRadius && !bFoundStart; ++dz)
    {
        FIntVector TestVoxel = CenterVoxelCoords + FIntVector(dx, dy, dz);
        if (IsSolid(TestVoxel))
        {
            StartVoxel = TestVoxel;
            bFoundStart = true;
        }
    }

    if (!bFoundStart)
    {
        UE_LOG(LogTemp, Warning, TEXT("No solid voxel found near the provided position. Aborting extraction."));
        return false;
    }

    // Step 3: BFS to find connected solid region (island)
    TSet<FIntVector> Visited;
    TQueue<FIntVector> Queue;
    Queue.Enqueue(StartVoxel);
    Visited.Add(StartVoxel);

    FVector AccumulatedPos = FVector(StartVoxel);
    int32 Count = 1;
    OutVoxels.Add(StartVoxel);

    UE_LOG(LogTemp, Warning, TEXT("Island search started from voxel: %s"), *StartVoxel.ToString());

    static const FIntVector Dirs[] = {
        {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}
    };

    bool bFoundSecondNil = false;

    while (!Queue.IsEmpty())
    {
        FIntVector Current;
        Queue.Dequeue(Current);

        UE_LOG(LogTemp, Warning, TEXT("Processing voxel: %s"), *Current.ToString());

        for (const FIntVector& Dir : Dirs)
        {
            FIntVector Neighbor = Current + Dir;
            const FVoxelData* Data = VoxelData.Find(Neighbor);

            if (!Visited.Contains(Neighbor) && IsSolid(Neighbor))
            {
                Queue.Enqueue(Neighbor);
                Visited.Add(Neighbor);

                OutVoxels.Add(Neighbor);
                AccumulatedPos += FVector(Neighbor);
                Count++;
            }
            else if (Data == nullptr)
            {
                if (bFoundSecondNil)
                {
                    goto StopExtraction; // Stops once we hit the second nil voxel
                }
                bFoundSecondNil = true;
            }
        }
    }

StopExtraction:

    if (OutVoxels.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("No voxels found in island extraction."));
        return false;
    }

    // Step 4: Create the output grid
    OutTempGrid = NewObject<USparseVoxelGrid>(GetOuter());
    OutTempGrid->VoxelData.Reserve(OutVoxels.Num());
    OutTempGrid->LocalVoxelSize = FVoxelConversion::LocalVoxelSize;
    OutTempGrid->ParentChunk = this->ParentChunk;

    for (const FIntVector& Voxel : OutVoxels)
    {
        if (FVoxelData* Data = VoxelData.Find(Voxel))
        {
            OutTempGrid->VoxelData.Add(Voxel, *Data);
        }
    }

    RemoveVoxels(OutVoxels);

    // Step 5: Compute world-space center of mass correctly
    FVector CenterOfMass = AccumulatedPos / static_cast<float>(Count);
    FIntVector Rounded = FIntVector(FMath::RoundToInt(CenterOfMass.X),
                                    FMath::RoundToInt(CenterOfMass.Y),
                                    FMath::RoundToInt(CenterOfMass.Z));

    FVector WorldCenterOfMass = FVoxelConversion::LocalVoxelToWorld(Rounded);

    UE_LOG(LogTemp, Warning, TEXT("Extracted %d voxels for island. Center at voxel space: %s (world: %s)."),
        OutVoxels.Num(), *CenterOfMass.ToString(), *WorldCenterOfMass.ToString());

    return true;
}




TArray<FIslandData> USparseVoxelGrid::DetectIslands(float SDFThreshold /* usually 0.0f */) const
{
    TSet<FIntVector> Visited;
    TArray<FIslandData> Islands;
    int32 NextAvailableIslandID = 0; // Counter to assign unique IslandIDs

    // Lambda to check if a voxel is solid
    auto IsSolid = [&](const FIntVector& Voxel) -> bool
    {
        const FVoxelData* Data = VoxelData.Find(Voxel);
        return Data && Data->SDFValue < SDFThreshold;
    };

    for (const auto& Elem : VoxelData)
    {
        const FIntVector& Start = Elem.Key;
        if (Visited.Contains(Start) || Elem.Value.SDFValue >= SDFThreshold)
            continue;

        // Log detected voxel starting position
        UE_LOG(LogTemp, Warning, TEXT("Starting voxel detection: %s"), *Start.ToString());

        // New island found, initialize island data
        FIslandData NewIsland;
        NewIsland.IslandID = NextAvailableIslandID++; // Assign unique IslandID
        NewIsland.VoxelCount = 0; // Will be set after BFS
        NewIsland.Voxels.Empty();

        TQueue<FIntVector> Queue;
        Queue.Enqueue(Start);
        Visited.Add(Start);

        FVector IslandLocation = FVector(Start.X, Start.Y, Start.Z);

        while (!Queue.IsEmpty())
        {
            FIntVector Current;
            Queue.Dequeue(Current);

            // Log each voxel added to the island
            UE_LOG(LogTemp, Warning, TEXT("Voxel added to island: %s"), *Current.ToString());

            // Add voxel to the island
            NewIsland.Voxels.Add(Current);

            // Update island location (for center of mass)
            IslandLocation += FVector(Current.X, Current.Y, Current.Z);

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

        // Set VoxelCount and average location
        NewIsland.VoxelCount = NewIsland.Voxels.Num();
        if (NewIsland.VoxelCount > 0)
        {
            IslandLocation /= NewIsland.VoxelCount;
        }

        // Log island location before conversion
        UE_LOG(LogTemp, Warning, TEXT("Island location before conversion: %s"), *IslandLocation.ToString());

        NewIsland.Location = IslandLocation;
        NewIsland.Location = FVoxelConversion::LocalVoxelToWorld(FIntVector(IslandLocation));// * 2.2f;

        // Log final world space island location
        UE_LOG(LogTemp, Warning, TEXT("Final world-space island location: %s"), *NewIsland.Location.ToString());

        // Add the island data to the list
        Islands.Add(NewIsland);
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
        //UE_LOG(LogTemp, Error, TEXT("DiggerManager is null or invalid in SparseVoxelGrid RenderVoxels()!!"));
        return;
    }

    World = DiggerManager->GetWorldFromManager();
    if (!World) {
        //UE_LOG(LogTemp, Error, TEXT("World is null within SparseVoxelGrid RenderVoxels()!!"));
        return;
    }

    if (VoxelData.IsEmpty()) {
       // UE_LOG(LogTemp, Warning, TEXT("VoxelData is empty, no voxels to render!"));
        return;
    } else {
        //UE_LOG(LogTemp, Warning, TEXT("VoxelData contains %d voxels."), VoxelData.Num());
    }

    for (const auto& Voxel : VoxelData) {
        FIntVector VoxelCoords = Voxel.Key;
        FVoxelData VoxelDataValue = Voxel.Value;

        // Convert voxel coordinates to world space
        FVector WorldPos = FVoxelConversion::LocalVoxelToWorld(VoxelCoords);
        FVector Center = WorldPos + FVector(FVoxelConversion::LocalVoxelSize / 2); // Adjust to the center of the voxel

        float SDFValue = VoxelDataValue.SDFValue; // Access the SDF value from the VoxelData

        FColor VoxelColor;
        if (SDFValue > 0) {
            VoxelColor = FColor::Green; // Air (SDF > 0)
        } else if (SDFValue < 0) {
            VoxelColor = FColor::Red; // Solid (SDF < 0)
        } else {
            VoxelColor = FColor::Yellow; // Surface (SDF == 0)
        }

        // Draw the voxel cube with the correct color
        DrawDebugBox(World, Center, FVector(FVoxelConversion::LocalVoxelSize / 2), FQuat::Identity, VoxelColor, false, 15.f, 0, 2);

        // Optionally, draw a point at the center of the voxel
        DrawDebugPoint(World, Center, 2.0f, VoxelColor, false, 15.f, 0);
    }
}
