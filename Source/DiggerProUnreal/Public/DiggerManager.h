#pragma once

#include <mutex>
#include <queue>

#include "CoreMinimal.h"
#include "Landscape.h"
#include "ProceduralMeshComponent.h"
#include "VoxelBrushShape.h"
#include "VoxelBrushTypes.h"
#include "GameFramework/Actor.h"
#include "DiggerManager.generated.h"

class UTerrainHoleComponent;

// Define the FBrushStroke struct
USTRUCT(BlueprintType)
struct FBrushStroke {
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite)
    FVector BrushPosition;

    UPROPERTY(BlueprintReadWrite)
    float BrushRadius;

    UPROPERTY(BlueprintReadWrite)
    EVoxelBrushType BrushType;

    UPROPERTY(BlueprintReadWrite)
    bool bDig;

    FBrushStroke()
        : BrushPosition(FVector::ZeroVector),
          BrushRadius(0.f),
          BrushType(EVoxelBrushType::Sphere),
          bDig(false)
    {}
};

UCLASS(Blueprintable)
class DIGGERPROUNREAL_API ADiggerManager : public AActor
{
    GENERATED_BODY()

public:
    ADiggerManager();

    // Set this true to make the actor never get culled despite distance
    UPROPERTY(EditAnywhere, Category="Digger System")
    bool bNeverCull = true;

    bool EnsureWorldReference();

    // Reference to the terrain hole Blueprint
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Holes")
    TSubclassOf<AActor> HoleBP;

    UFUNCTION(BlueprintCallable, Category="Holes")
    void SpawnTerrainHole(const FVector& Location);

    UFUNCTION(BlueprintCallable, Category = "Custom")
    void DuplicateLandscape(ALandscapeProxy* Landscape);

    UPROPERTY(EditAnywhere, Category="Digger Brush|Settings")
    FVector EditorBrushPosition;

    UPROPERTY(EditAnywhere, Category="Brush")
    EVoxelBrushType EditorBrushType = EVoxelBrushType::Sphere;

    UPROPERTY(EditAnywhere, Category="Digger Brush|Settings")
    float EditorBrushRadius = 100.0f;

    UPROPERTY(EditAnywhere, Category="Digger Brush|Settings")
    bool EditorBrushDig = true;

    UFUNCTION(CallInEditor, Category="Digger Brush|Actions")
    void ApplyBrushInEditor();

    
protected:
    virtual void BeginPlay() override;
    void UpdateVoxelSize();
    void ProcessDirtyChunks();

#if WITH_EDITOR
    // Editor support
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
    virtual void PostEditMove(bool bFinished) override;
    virtual void PostEditUndo() override;

    // Expose manual update/rebuild in editor
    UFUNCTION(CallInEditor, Category = "Editor Tools")
    void EditorUpdateChunks();

    UFUNCTION(CallInEditor, Category = "Editor Tools")
    void EditorRebuildAllChunks();

#endif

public:

    FVector ChunkToWorldCoordinates(int32 XInd, int32 YInd, int32 ZInd) const
    {
        return FVector(XInd * ChunkSize * TerrainGridSize, YInd * ChunkSize * TerrainGridSize, ZInd * ChunkSize * TerrainGridSize);
    }

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Settings")
    int32 ChunkSize = 32;  // Number of subdivisions per grid size

    // Chunk and grid settings
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Settings")
    int TerrainGridSize = 100;  // Default size of 1 meter

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Settings")
    int Subdivisions = 4;  // Number of subdivisions per grid size

    int VoxelSize=TerrainGridSize/Subdivisions;

    // The active brush shape
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel System")
    UVoxelBrushShape* ActiveBrush;

    UFUNCTION(BlueprintCallable, Category = "Custom")
    int32 GetHitSectionIndex(const FHitResult& HitResult);

    UFUNCTION(BlueprintCallable, Category = "Custom")
    UVoxelChunk* GetChunkBySectionIndex(int32 SectionIndex);

    UFUNCTION(BlueprintCallable, Category = "Custom")
    void UpdateChunkFromSectionIndex(const FHitResult& HitResult);
    void DebugDrawChunkSectionIDs();

    // Apply the active brush to the terrain
    UFUNCTION(BlueprintCallable, Category = "Brush System")
    void ApplyBrush();
    UVoxelChunk* FindOrCreateNearestChunk(const FVector& Position);
    UVoxelChunk* FindNearestChunk(const FVector& Position);
    void MarkNearbyChunksDirty(const FVector& CenterPosition, float Radius);

    // Get the relevant voxel chunk based on position
    UFUNCTION(BlueprintCallable, Category = "Chunk Management")
    UVoxelChunk* GetOrCreateChunkAt(const FIntVector& ProposedChunkPosition);

    UFUNCTION(BlueprintCallable, Category = "Debug")
    void DebugLogVoxelChunkGrid() const;

    //Helper functions
    //Calculate a chunk position according to the chunk grid.
    FIntVector CalculateChunkPosition(const FIntVector3& WorldPosition) const;
    FIntVector CalculateChunkPosition(const FVector3d& WorldPosition) const;
    UVoxelChunk* GetOrCreateChunkAt(const FVector3d& ProposedChunkPosition);
    UVoxelChunk* GetOrCreateChunkAt(const float& ProposedChunkX, const float& ProposedChunkY,
                                    const float& ProposedChunkZ);

    // Debug voxel system
    UFUNCTION(BlueprintCallable, Category = "Voxel Debug")
    void DebugVoxels();

    // Procedural mesh component for visual representation
    UPROPERTY(VisibleAnywhere)
    UProceduralMeshComponent* ProceduralMesh;

    void InitializeChunks();  // Initialize all chunks
    void InitializeSingleChunk(UVoxelChunk* Chunk);  // Initialize a single chunk

    //The world map of the voxel chunks
    UPROPERTY()
    TMap<FIntVector, UVoxelChunk*> ChunkMap;

private:
    std::mutex ChunkProcessingMutex;
    // Reference to the sparse voxel grid and marching cubes
    UPROPERTY()
    USparseVoxelGrid* SparseVoxelGrid;

    UPROPERTY()
    UMarchingCubes* MarchingCubes;

    UPROPERTY()
    UVoxelChunk* ZeroChunk;

    UPROPERTY()
    UVoxelChunk* OneChunk;

    UPROPERTY()
    UMaterial* TerrainMaterial;

    UPROPERTY()
    TArray<UProceduralMeshComponent*> ProceduralMeshComponents;

public:
    [[nodiscard]] UMaterial* GetTerrainMaterial() const
    {
        return TerrainMaterial;
    }

    void SetTerrainMaterial(UMaterial* SetTerrainMaterial)
    {
        this->TerrainMaterial = SetTerrainMaterial;
    }

    UPROPERTY()
    UWorld* World;

    [[nodiscard]] UWorld* GetWorldFromManager()
    {
        if(EnsureWorldReference())
            return World;
        else
            return nullptr;
    }
    UFUNCTION(BlueprintCallable, Category = "Landscape Tools")
    float GetLandscapeHeightAt(FVector WorldPosition);
    bool GetHeightAtLocation(ALandscapeProxy* LandscapeProxy, const FVector& Location, float& OutHeight);

private:

    std::queue<FBrushStroke> BrushStrokeQueue;
    const int32 MaxUndoLength = 10; // Example limit

    FTimerHandle ChunkProcessTimerHandle;

    //Space Conversion Helper Methods
    FIntVector WorldToChunkCoordinates(float x, float y, float z) const
    {
        const float ChunkWorldSize = ChunkSize * TerrainGridSize;
        return FIntVector(
            FMath::FloorToInt(x / ChunkWorldSize),
            FMath::FloorToInt(y / ChunkWorldSize),
            FMath::FloorToInt(z / ChunkWorldSize)
        );
    }
};
