#include "DiggerManager.h"

#include "CoreMinimal.h"

// Voxel & Mesh Systems
#include "SparseVoxelGrid.h"
#include "VoxelChunk.h"
#include "VoxelConversion.h"
#include "MarchingCubes.h"
#include "FCustomSDFBrush.h"

// Landscape & Islands
#include "IslandActor.h"
#include "LandscapeProxy.h"
#include "LandscapeInfo.h"

// Mesh/StaticMesh
#include "ProceduralMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "UObject/ConstructorHelpers.h"

// Engine Systems
#include "Engine/World.h"
#include "TimerManager.h"
#include "EngineUtils.h"

// Async
#include "Async/Async.h"
#include "Async/ParallelFor.h"

// Asset Management
#include "HoleShapeLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"

// File & Paths
#include "HAL/PlatformFilemanager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

// Kismet & Helpers
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"

// Rendering (ok in runtime)
#include "Rendering/PositionVertexBuffer.h"
#include "Rendering/StaticMeshVertexBuffer.h"

// Utilities
#include "DiggerDebug.h"
#include "Engine/Engine.h"
#include "DiggerProUnreal/Utilities/FastDebugRenderer.h"

// Brush Shapes (runtime-only classes)
#include "VoxelBrushShape.h"
#include "Voxel/BrushShapes/SphereBrushShape.h"
#include "Voxel/BrushShapes/CubeBrushShape.h"
#include "Voxel/BrushShapes/CylinderBrushShape.h"
#include "Voxel/BrushShapes/ConeBrushShape.h"
#include "Voxel/BrushShapes/CapsuleBrushShape.h"
#include "Voxel/BrushShapes/TorusBrushShape.h"
#include "Voxel/BrushShapes/PyramidBrushShape.h"
#include "Voxel/BrushShapes/IcosphereBrushShape.h"
#include "Voxel/BrushShapes/SmoothBrushShape.h"
#include "Voxel/BrushShapes/NoiseBrushShape.h"

// Hole BP helpers
#include "FSpawnedHoleData.h"
#include "HoleBPHelpers.h"
#include "UObject/SoftObjectPath.h"

// Lights (runtime safe)
#include "DynamicLightActor.h"
#include "LandscapeEdit.h"
#include "Components/LightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"

class ADynamicLightActor;
class Editor;
class FDiggerEdModeToolkit;
class FDiggerEdMode;
class MeshDescriptors;
class StaticMeshAttributes;



static UHoleShapeLibrary* LoadDefaultHoleLibrary()
{
    if (!GDefaultHoleLibraryPath || !*GDefaultHoleLibraryPath) return nullptr;
    UObject* Obj = StaticLoadObject(UHoleShapeLibrary::StaticClass(), nullptr, GDefaultHoleLibraryPath);
    return Cast<UHoleShapeLibrary>(Obj);
}

static TSubclassOf<AActor> LoadDefaultHoleBPClass()
{
    UClass* Cls = StaticLoadClass(AActor::StaticClass(), nullptr, GDefaultHoleBPPath);
    return Cls;
}

static UHoleShapeLibrary* CreateTransientHoleLibrary()
{
    // Create a transient instance so editor mode never crashes when no asset exists.
    UHoleShapeLibrary* Lib = NewObject<UHoleShapeLibrary>(GetTransientPackage(), UHoleShapeLibrary::StaticClass(), FName(TEXT("TransientHoleShapeLibrary")));
    if (Lib)
    {
        // Optionally seed defaults if your code expects entries:
        // Lib->AddDefaultSphere();
        // Lib->AddDefaultCube();
    }
    return Lib;
}




//New Multi Save Files System
// Updated ADiggerManager methods for multiple save file support

FString ADiggerManager::GetSaveFileDirectory(const FString& SaveFileName) const
{
    // Create a subdirectory for each save file
    return FPaths::ProjectContentDir() / VOXEL_DATA_DIRECTORY / SaveFileName;
}

FString ADiggerManager::GetChunkFilePath(const FIntVector& ChunkCoords, const FString& SaveFileName) const
{
    FString FileName = FString::Printf(TEXT("Chunk_%d_%d_%d%s"), 
        ChunkCoords.X, ChunkCoords.Y, ChunkCoords.Z, *CHUNK_FILE_EXTENSION);
    
    return GetSaveFileDirectory(SaveFileName) / FileName;
}

// Overload for backward compatibility - uses "Default" save file
FString ADiggerManager::GetChunkFilePath(const FIntVector& ChunkCoords) const
{
    return GetChunkFilePath(ChunkCoords, TEXT("Default"));
}

bool ADiggerManager::DoesSaveFileExist(const FString& SaveFileName) const
{
    FString SaveDir = GetSaveFileDirectory(SaveFileName);
    return FPaths::DirectoryExists(SaveDir);
}

bool ADiggerManager::DoesChunkFileExist(const FIntVector& ChunkCoords, const FString& SaveFileName) const
{
    FString FilePath = GetChunkFilePath(ChunkCoords, SaveFileName);
    return FPaths::FileExists(FilePath);
}

// Overload for backward compatibility
bool ADiggerManager::DoesChunkFileExist(const FIntVector& ChunkCoords) const
{
    return DoesChunkFileExist(ChunkCoords, TEXT("Default"));
}

void ADiggerManager::EnsureSaveFileDirectoryExists(const FString& SaveFileName) const
{
    FString SaveDir = GetSaveFileDirectory(SaveFileName);
    
    // Create the main voxel data directory first
    FString MainDir = FPaths::ProjectContentDir() / VOXEL_DATA_DIRECTORY;
    if (!FPaths::DirectoryExists(MainDir))
    {
        FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*MainDir);
        UE_LOG(LogTemp, Log, TEXT("Created main voxel data directory: %s"), *MainDir);
    }
    
    // Create the save file specific directory
    if (!FPaths::DirectoryExists(SaveDir))
    {
        FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*SaveDir);
        UE_LOG(LogTemp, Log, TEXT("Created save file directory: %s"), *SaveDir);
    }
}

bool ADiggerManager::SaveChunk(const FIntVector& ChunkCoords, const FString& SaveFileName)
{
    EnsureSaveFileDirectoryExists(SaveFileName);
    
    UVoxelChunk** ChunkPtr = ChunkMap.Find(ChunkCoords);
    if (!ChunkPtr || !*ChunkPtr)
    {
        UE_LOG(LogTemp, Warning, TEXT("Cannot save chunk at %s to save file '%s' - chunk not found in memory"), 
            *ChunkCoords.ToString(), *SaveFileName);
        return false;
    }
    
    UVoxelChunk* Chunk = *ChunkPtr;
    FString FilePath = GetChunkFilePath(ChunkCoords, SaveFileName);
    
    bool bSaveSuccess = Chunk->SaveChunkData(FilePath);
    
    if (bSaveSuccess)
    {
        UE_LOG(LogTemp, Log, TEXT("Successfully saved chunk %s to save file '%s'"), 
            *ChunkCoords.ToString(), *SaveFileName);
        
        // Invalidate cache so it gets refreshed
        InvalidateSavedChunkCache(SaveFileName);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to save chunk %s to save file '%s'"), 
            *ChunkCoords.ToString(), *SaveFileName);
    }
    
    return bSaveSuccess;
}

// Overload for backward compatibility
bool ADiggerManager::SaveChunk(const FIntVector& ChunkCoords)
{
    return SaveChunk(ChunkCoords, TEXT("Default"));
}

bool ADiggerManager::LoadChunk(const FIntVector& ChunkCoords, const FString& SaveFileName)
{
    FString FilePath = GetChunkFilePath(ChunkCoords, SaveFileName);
    
    if (!FPaths::FileExists(FilePath))
    {
        UE_LOG(LogTemp, Warning, TEXT("Cannot load chunk at %s from save file '%s' - file does not exist"), 
            *ChunkCoords.ToString(), *SaveFileName);
        return false;
    }
    
    // Get or create the chunk
    UVoxelChunk* Chunk = GetOrCreateChunkAtChunk(ChunkCoords);
    if (!Chunk)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to get or create chunk at %s for save file '%s'"), 
            *ChunkCoords.ToString(), *SaveFileName);
        return false;
    }
    
    // Load the chunk data
    bool bLoadSuccess = Chunk->LoadChunkData(FilePath);
    
    if (bLoadSuccess)
    {
        UE_LOG(LogTemp, Log, TEXT("Successfully loaded chunk %s from save file '%s'"), 
            *ChunkCoords.ToString(), *SaveFileName);
        
        // IMPORTANT: Force mesh regeneration after loading
        Chunk->ForceUpdate();
        
        UE_LOG(LogTemp, Log, TEXT("Triggered mesh regeneration for loaded chunk %s from save file '%s'"), 
            *ChunkCoords.ToString(), *SaveFileName);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load chunk %s from save file '%s'"), 
            *ChunkCoords.ToString(), *SaveFileName);
    }
    
    return bLoadSuccess;
}

// Overload for backward compatibility
bool ADiggerManager::LoadChunk(const FIntVector& ChunkCoords)
{
    return LoadChunk(ChunkCoords, TEXT("Default"));
}

bool ADiggerManager::SaveAllChunks(const FString& SaveFileName)
{
    EnsureSaveFileDirectoryExists(SaveFileName);
    
    if (ChunkMap.Num() == 0)
    {
        UE_LOG(LogTemp, Log, TEXT("No chunks to save to save file '%s' - ChunkMap is empty"), *SaveFileName);
        return true;
    }
    
    int32 SavedCount = 0;
    int32 FailedCount = 0;
    
    UE_LOG(LogTemp, Log, TEXT("Starting to save %d chunks to save file '%s'..."), ChunkMap.Num(), *SaveFileName);
    
    for (const auto& ChunkPair : ChunkMap)
    {
        const FIntVector& ChunkCoords = ChunkPair.Key;
        
        if (SaveChunk(ChunkCoords, SaveFileName))
        {
            SavedCount++;
        }
        else
        {
            FailedCount++;
        }
    }
    
    UE_LOG(LogTemp, Log, TEXT("Finished saving chunks to save file '%s': %d successful, %d failed"), 
        *SaveFileName, SavedCount, FailedCount);
    
    // Invalidate cache after batch save
    InvalidateSavedChunkCache(SaveFileName);
    
    return FailedCount == 0;
}

// Overload for backward compatibility
bool ADiggerManager::SaveAllChunks()
{
    return SaveAllChunks(TEXT("Default"));
}

bool ADiggerManager::LoadAllChunks(const FString& SaveFileName)
{
    TArray<FIntVector> SavedChunkCoords = GetAllSavedChunkCoordinates(SaveFileName, true); // Force refresh
    
    if (SavedChunkCoords.Num() == 0)
    {
        UE_LOG(LogTemp, Log, TEXT("No saved chunks found to load from save file '%s'"), *SaveFileName);
        return true;
    }
    
    int32 LoadedCount = 0;
    int32 FailedCount = 0;
    
    UE_LOG(LogTemp, Log, TEXT("Starting to load %d chunks from save file '%s'..."), SavedChunkCoords.Num(), *SaveFileName);
    
    for (const FIntVector& ChunkCoords : SavedChunkCoords)
    {
        if (LoadChunk(ChunkCoords, SaveFileName))
        {
            LoadedCount++;
        }
        else
        {
            FailedCount++;
        }
    }
    
    UE_LOG(LogTemp, Log, TEXT("Finished loading chunks from save file '%s': %d successful, %d failed"), 
        *SaveFileName, LoadedCount, FailedCount);
    
    return FailedCount == 0;
}

// Overload for backward compatibility
bool ADiggerManager::LoadAllChunks()
{
    return LoadAllChunks(TEXT("Default"));
}

TArray<FIntVector> ADiggerManager::GetAllSavedChunkCoordinates(const FString& SaveFileName, bool bForceRefresh)
{
    // Use per-save-file caching
    FString CacheKey = SaveFileName;
    
    if (!bForceRefresh && SavedChunkCache.Contains(CacheKey))
    {
        return SavedChunkCache[CacheKey];
    }
    
    TArray<FIntVector> SavedChunks;
    FString SaveDir = GetSaveFileDirectory(SaveFileName);
    
    if (!FPaths::DirectoryExists(SaveDir))
    {
        // Directory doesn't exist, so no saved chunks
        SavedChunkCache.Add(CacheKey, SavedChunks);
        return SavedChunks;
    }
    
    TArray<FString> FoundFiles;
    FString SearchPattern = SaveDir / FString::Printf(TEXT("*%s"), *CHUNK_FILE_EXTENSION);
    IFileManager::Get().FindFiles(FoundFiles, *SearchPattern, true, false);
    
    for (const FString& FileName : FoundFiles)
    {
        // Parse filename to extract coordinates
        FString BaseName = FPaths::GetBaseFilename(FileName);
        
        // Expected format: Chunk_X_Y_Z
        TArray<FString> Parts;
        BaseName.ParseIntoArray(Parts, TEXT("_"), true);
        
        if (Parts.Num() >= 4 && Parts[0] == TEXT("Chunk"))
        {
            int32 X = FCString::Atoi(*Parts[1]);
            int32 Y = FCString::Atoi(*Parts[2]);
            int32 Z = FCString::Atoi(*Parts[3]);
            
            SavedChunks.Add(FIntVector(X, Y, Z));
        }
    }
    
    // Cache the results
    SavedChunkCache.Add(CacheKey, SavedChunks);
    
    return SavedChunks;
}

// Overload for backward compatibility
TArray<FIntVector> ADiggerManager::GetAllSavedChunkCoordinates(bool bForceRefresh)
{
    return GetAllSavedChunkCoordinates(TEXT("Default"), bForceRefresh);
}

TArray<FString> ADiggerManager::GetAllSaveFileNames() const
{
    TArray<FString> SaveFileNames;
    FString MainDir = FPaths::ProjectContentDir() / VOXEL_DATA_DIRECTORY;
    
    UE_LOG(LogTemp, Log, TEXT("GetAllSaveFileNames: Searching in directory: %s"), *MainDir);
    
    if (!FPaths::DirectoryExists(MainDir))
    {
        UE_LOG(LogTemp, Log, TEXT("GetAllSaveFileNames: Main directory does not exist"));
        return SaveFileNames;
    }
    
    // Get all entries in the main directory
    TArray<FString> DirectoryContents;
    IFileManager& FileManager = IFileManager::Get();
    FileManager.FindFiles(DirectoryContents, *(MainDir / TEXT("*")), false, true);
    
    UE_LOG(LogTemp, Log, TEXT("Found %d potential directories"), DirectoryContents.Num());
    
    // Check each directory to see if it contains chunk files
    for (const FString& DirName : DirectoryContents)
    {
        FString FullDirPath = MainDir / DirName;
        
        UE_LOG(LogTemp, Log, TEXT("Checking directory: %s"), *DirName);
        
        // Skip any hidden directories or files
        if (DirName.StartsWith(TEXT(".")))
        {
            UE_LOG(LogTemp, Log, TEXT("Skipping hidden directory: %s"), *DirName);
            continue;
        }
        
        // Verify it's actually a directory
        if (!FPaths::DirectoryExists(FullDirPath))
        {
            UE_LOG(LogTemp, Log, TEXT("Not a directory: %s"), *DirName);
            continue;
        }
        
        // Check if this directory contains chunk files
        TArray<FString> ChunkFiles;
        FString ChunkSearchPattern = FullDirPath / FString::Printf(TEXT("*%s"), *CHUNK_FILE_EXTENSION);
        FileManager.FindFiles(ChunkFiles, *ChunkSearchPattern, true, false);
        
        UE_LOG(LogTemp, Log, TEXT("Directory '%s' contains %d chunk files"), *DirName, ChunkFiles.Num());
        
        if (ChunkFiles.Num() > 0)
        {
            SaveFileNames.Add(DirName);
            UE_LOG(LogTemp, Log, TEXT("Added '%s' to save files list"), *DirName);
        }
        else
        {
            UE_LOG(LogTemp, Log, TEXT("Skipping '%s' - no chunk files found"), *DirName);
        }
    }
    
    UE_LOG(LogTemp, Log, TEXT("GetAllSaveFileNames: Final result - %d save files: [%s]"), 
        SaveFileNames.Num(), *FString::Join(SaveFileNames, TEXT(", ")));
    
    return SaveFileNames;
}

bool ADiggerManager::DeleteSaveFile(const FString& SaveFileName)
{
    FString SaveDir = GetSaveFileDirectory(SaveFileName);
    
    if (!FPaths::DirectoryExists(SaveDir))
    {
        UE_LOG(LogTemp, Warning, TEXT("Cannot delete save file '%s' - directory does not exist"), *SaveFileName);
        return false;
    }
    
    // Delete all files in the directory first
    TArray<FString> FilesToDelete;
    IFileManager::Get().FindFilesRecursive(FilesToDelete, *SaveDir, TEXT("*"), true, false);
    
    bool bAllFilesDeleted = true;
    for (const FString& FileToDelete : FilesToDelete)
    {
        if (!FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*FileToDelete))
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to delete file: %s"), *FileToDelete);
            bAllFilesDeleted = false;
        }
    }
    
    // Delete the directory itself
    bool bDirectoryDeleted = FPlatformFileManager::Get().GetPlatformFile().DeleteDirectory(*SaveDir);
    
    if (bDirectoryDeleted && bAllFilesDeleted)
    {
        UE_LOG(LogTemp, Log, TEXT("Successfully deleted save file '%s'"), *SaveFileName);
        
        // Remove from cache
        InvalidateSavedChunkCache(SaveFileName);
        
        return true;
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to completely delete save file '%s'"), *SaveFileName);
        return false;
    }
}

void ADiggerManager::InvalidateSavedChunkCache(const FString& SaveFileName)
{
    if (SaveFileName.IsEmpty())
    {
        // Clear all cache entries
        SavedChunkCache.Empty();
    }
    else
    {
        // Clear specific save file cache
        SavedChunkCache.Remove(SaveFileName);
    }
}


//End New Multi Save Files System.


void ADiggerManager::ApplyBrushInEditor(bool bDig)
{
    if (DiggerDebug::Brush)
    {
        UE_LOG(LogTemp, Error, TEXT("ApplyBrushInEditor Called!"));
    }

    FBrushStroke BrushStroke;
    BrushStroke.BrushPosition            = EditorBrushPosition + EditorBrushOffset;
    BrushStroke.BrushRadius              = EditorBrushRadius;
    BrushStroke.BrushRotation            = EditorBrushRotation;
    BrushStroke.bHiddenSeam              = EditorBrushHiddenSeam;
    BrushStroke.BrushType                = EditorBrushType;
    BrushStroke.bDig                     = bDig;
    BrushStroke.BrushLength              = EditorBrushLength;
    BrushStroke.bIsFilled                = EditorBrushIsFilled;
    BrushStroke.BrushAngle               = EditorBrushAngle;
    BrushStroke.HoleShape                = EditorBrushHoleShape;
    BrushStroke.bUseAdvancedCubeBrush    = EditorbUseAdvancedCubeBrush;
    BrushStroke.AdvancedCubeHalfExtentX  = EditorCubeHalfExtentX;
    BrushStroke.AdvancedCubeHalfExtentY  = EditorCubeHalfExtentY;
    BrushStroke.AdvancedCubeHalfExtentZ  = EditorCubeHalfExtentZ;
    BrushStroke.BrushStrength            = EditorBrushStrength;
    BrushStroke.LightColor               = EditorBrushLightColor; // default

    if (EditorBrushType == EVoxelBrushType::Light)
    {
        // Reasonable defaults for non-editor (or if toolkit missing)
        BrushStroke.LightType  = EditorBrushLightType;
        BrushStroke.LightColor = EditorBrushLightColor;
        

        if (DiggerDebug::Lights || DiggerDebug::Brush)
        {
            UE_LOG(LogTemp, Warning, TEXT("Light brush setup: LightType=%d Color=%s"),
                (int32)BrushStroke.LightType,
                *BrushStroke.LightColor.ToString());
        }

        ApplyLightBrushInEditor(BrushStroke);
        return;
    }

    // Non-light brushes
    if (DiggerDebug::Brush)
    {
        UE_LOG(LogTemp, Warning, TEXT("DiggerManager.cpp: Extent X: %.2f  Y: %.2f  Z: %.2f"),
            BrushStroke.AdvancedCubeHalfExtentX,
            BrushStroke.AdvancedCubeHalfExtentY,
            BrushStroke.AdvancedCubeHalfExtentZ);
    }

    if (DiggerDebug::Brush || DiggerDebug::Casts)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ApplyBrushInEditor] EditorBrushPosition (World): %s"),
            *EditorBrushPosition.ToString());
    }

    ApplyBrushToAllChunks(BrushStroke);
    ProcessDirtyChunks();

#if WITH_EDITOR
    if (GEditor)
    {
        GEditor->RedrawAllViewports();
    }
#endif

    /* You can immediately access the returned islands if your use-case requires.
     * TArray<FIslandData> DetectedIslands = */
    DetectUnifiedIslands();
    
}





void ADiggerManager::ApplyLightBrushInEditor(const FBrushStroke& BrushStroke)
{
    if (DiggerDebug::Lights || DiggerDebug::Brush) {
        UE_LOG(LogTemp, Warning, TEXT("ApplyLightBrushInEditor called at position: %s"), *BrushStroke.BrushPosition.ToString());
        UE_LOG(LogTemp, Warning, TEXT("Light stroke - Type: %d, Radius: %f, Strength: %f, Rotation: %s"), 
               (int32)BrushStroke.LightType, 
               BrushStroke.BrushRadius, 
               BrushStroke.BrushStrength,
               *BrushStroke.BrushRotation.ToString());
    }

    // Now you have access to all brush stroke properties:
    // - BrushStroke.BrushPosition
    // - BrushStroke.BrushRadius  
    // - BrushStroke.BrushStrength
    // - BrushStroke.BrushRotation (for future light rotation)
    // - BrushStroke.LightType
    // - Any other properties you add later

    SpawnLight(BrushStroke);

    // Redraw viewports to show the new light
    if (GEditor)
    {
        GEditor->RedrawAllViewports();
    }
}




void ADiggerManager::OnConstruction(const FTransform& Transform)
{
        Super::OnConstruction(Transform);
#if WITH_EDITOR
        this->SetFolderPath(FName(TEXT("Digger"))); // top-level folder for the manager
#endif

    // Always ensure we have a library pointer (cheap)
    EnsureHoleShapeLibrary();

#if WITH_EDITOR
    if (!IsRuntimeLike())
    {
        InitEditorLightweight(); // cheap preview only
        return;
    }
#endif

    if (!bRuntimeInitDone)
    {
        InitRuntimeHeavy();
        bRuntimeInitDone = true;
    }
}



void ADiggerManager::InitHoleShapeLibrary()
{
    if (!HoleShapeLibrary)
    {
        // Load class dynamically
        const FSoftClassPath LibraryClassRef(TEXT("/Game/Blueprints/MyHoleShapeLibrary.MyHoleShapeLibrary_C"));
        UClass* LoadedClass = LibraryClassRef.TryLoadClass<UHoleShapeLibrary>();

        if (LoadedClass)
        {
            HoleShapeLibrary = NewObject<UHoleShapeLibrary>(this, LoadedClass);
            UE_LOG(LogTemp, Warning, TEXT("HoleShapeLibrary created at runtime successfully."));
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to load MyHoleShapeLibrary class dynamically."));
        }
    }
}



ADiggerManager::ADiggerManager()
{
#if WITH_EDITOR  
    this->SetFlags(RF_Transactional); // Enable undo/redo support in editor
#endif

    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> HighlightMatFinder(TEXT("/Game/Materials/M_VoxelHighlight.M_VoxelHighlight"));

    if (CubeMesh.Succeeded())
    {
        // Set up the instanced voxel debug component.
        VoxelDebugMesh = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("VoxelDebugMesh"));
        VoxelDebugMesh->SetStaticMesh(CubeMesh.Object);
        VoxelDebugMesh->SetMobility(EComponentMobility::Movable);
        VoxelDebugMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        VoxelDebugMesh->SetCastShadow(false);
        if (HighlightMatFinder.Succeeded()) 
        {
            VoxelDebugMesh->SetMaterial(0, HighlightMatFinder.Object); // Optional glowing material
        }
        else
        {
            if (DiggerDebug::InstancedDebug)
            UE_LOG(LogTemp, Warning, TEXT("Failed to load highlight material at /Game/Materials/M_VoxelHighlight"));
        }
    }


    // Initialize the ProceduralMeshComponent
    ProceduralMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("GeneratedMesh"));
    RootComponent = ProceduralMesh;

    // Initialize the voxel grid and marching cubes
    SparseVoxelGrid = CreateDefaultSubobject<USparseVoxelGrid>(TEXT("SparseVoxelGrid"));
    MarchingCubes = CreateDefaultSubobject<UMarchingCubes>(TEXT("MarchingCubes"));

    if (!HoleBP)
    { HoleBP = LoadDefaultHoleBPClass(); }

    // Load the material in the constructor
    static ConstructorHelpers::FObjectFinder<UMaterial> Material(TEXT("/Game/Materials/M_VoxelMat.M_VoxelMat"));
    if (Material.Succeeded())
    {
        TerrainMaterial = Material.Object;
    }
    else
    {
        if (DiggerDebug::UserConv)
        {
            UE_LOG(LogTemp, Error, TEXT("Material M_ProcGrid required in /Content/Materials/ folder. Please ensure it is there."));
        }
    }
}

void ADiggerManager::SpawnLight(const FBrushStroke& BrushStroke)
{
    if (!GetWorld())
    {
        if (DiggerDebug::Lights || DiggerDebug::Brush || DiggerDebug::Context)
        UE_LOG(LogTemp, Error, TEXT("SpawnLight: World is null"));
        return;
    }

    FVector FinalPosition = BrushStroke.BrushPosition + BrushStroke.BrushOffset;
    FRotator Rotation = BrushStroke.BrushRotation;

    ADynamicLightActor* Light = GetWorld()->SpawnActor<ADynamicLightActor>(
        ADynamicLightActor::StaticClass(), FinalPosition, Rotation);
    Light->InitLight(BrushStroke.LightType); // BrushType is your ELightBrushType
    UE_LOG(LogTemp, Warning, TEXT("Light Type Passed to InitLight: %s"), *GetLightTypeName(BrushStroke.LightType));
    Light->SetFolderPath(FName("Digger/DynamicLights"));

    if (Light)
    {
        Light->InitializeFromBrush(BrushStroke);
        SpawnedLights.Add(Light);

        if (DiggerDebug::Lights || DiggerDebug::Brush)
        UE_LOG(LogTemp, Warning, TEXT("Successfully spawned dynamic light actor of type %d"), (int32)BrushStroke.LightType);
    }
    else
    {
        if (DiggerDebug::Lights || DiggerDebug::Brush)
        UE_LOG(LogTemp, Error, TEXT("Failed to spawn dynamic light actor"));
    }
}


void ADiggerManager::PostInitProperties()
{
    Super::PostInitProperties();
    if (!HasAnyFlags(RF_ClassDefaultObject))
    {
        InitializeBrushShapes();
    }
}


// In ADiggerManager.cpp
void ADiggerManager::InitializeBrushShapes()
{
    // Clear existing brushes
    BrushShapeMap.Empty();

    // Initialize all available brush shapes
    BrushShapeMap.Add(EVoxelBrushType::Sphere, NewObject<USphereBrushShape>(this));
    BrushShapeMap.Add(EVoxelBrushType::Cube, NewObject<UCubeBrushShape>(this));
    BrushShapeMap.Add(EVoxelBrushType::Cone, NewObject<UConeBrushShape>(this));
    BrushShapeMap.Add(EVoxelBrushType::Cylinder, NewObject<UCylinderBrushShape>(this));
    BrushShapeMap.Add(EVoxelBrushType::Capsule, NewObject<UCapsuleBrushShape>(this));
    BrushShapeMap.Add(EVoxelBrushType::Torus, NewObject<UTorusBrushShape>(this));
    BrushShapeMap.Add(EVoxelBrushType::Pyramid, NewObject<UPyramidBrushShape>(this));
    BrushShapeMap.Add(EVoxelBrushType::Icosphere, NewObject<UIcosphereBrushShape>(this));
    BrushShapeMap.Add(EVoxelBrushType::Smooth, NewObject<USmoothBrushShape>(this));
    
    // Log initialization for debugging
    /*UE_LOG(LogTemp, Warning, TEXT("Initializing brush shapes..."));
    for (const auto& Pair : BrushShapeMap)
    {
        UE_LOG(LogTemp, Warning, TEXT("Initialized %s for brush type %d"), 
            *Pair.Value->GetClass()->GetName(), 
            (int32)Pair.Key);
    }*/

    // If there's an ActiveBrush member, we should probably handle it differently
    // Can you show me the ActiveBrush related code so we can fix that?
}

UVoxelBrushShape* ADiggerManager::GetActiveBrushShape(EVoxelBrushType BrushType) const
{
    if (UVoxelBrushShape* const* Found = BrushShapeMap.Find(BrushType))
    {
        return *Found;
    }
    return nullptr;
}

UVoxelBrushShape* ADiggerManager::GetBrushShapeForType(EVoxelBrushType BrushType)
{
    // Lazy init
    if (BrushShapeMap.Num() == 0)
    {
        InitializeBrushShapes();
    }

    if (UVoxelBrushShape* const* Found = BrushShapeMap.Find(BrushType))
    {
        return *Found;
    }

    // Fallback to Sphere if available
    if (UVoxelBrushShape* const* Sphere = BrushShapeMap.Find(EVoxelBrushType::Sphere))
    {
        return *Sphere;
    }

    return nullptr;
}



/*void ADiggerManager::ApplyBrushAtCameraHit()
{
    // Get the active brush shape
    const UVoxelBrushShape* ActiveBrushShape = GetActiveBrushShape(ActiveBrush->GetBrushType());
    if (!ActiveBrushShape)
    {
        UE_LOG(LogTemp, Error, TEXT("ActiveBrushShape is null"));
        return;
    }

    // Get hit location
    FHitResult HitResult;
    if (!ActiveBrushShape->GetCameraHitLocation(HitResult))
    {
        UE_LOG(LogTemp, Warning, TEXT("No valid hit found"));
        return;
    }

    // Create brush stroke with correct data
    FBrushStroke BrushStroke;
    BrushStroke.BrushPosition = HitResult.Location;
    BrushStroke.BrushRadius = ActiveBrush->GetBrushSize();
    BrushStroke.BrushFalloff = 0.f;
    BrushStroke.BrushStrength = 1.f;
    BrushStroke.bDig = ActiveBrush->GetDig();
    BrushStroke.BrushType = ActiveBrush->GetBrushType();

    //Log the brushstrok details
    UE_LOG(LogTemp, Warning, TEXT("PIE: === BRUSH STROKE DEBUG ==="));
    UE_LOG(LogTemp, Warning, TEXT("PIE: Position: %s"), *BrushStroke.BrushPosition.ToString());
    UE_LOG(LogTemp, Warning, TEXT("PIE: Radius: %f"), BrushStroke.BrushRadius);
    UE_LOG(LogTemp, Warning, TEXT("PIE: Falloff: %f"), BrushStroke.BrushFalloff);
    UE_LOG(LogTemp, Warning, TEXT("PIE: Strength: %f"), BrushStroke.BrushStrength);
    UE_LOG(LogTemp, Warning, TEXT("PIE: bDig: %s"), BrushStroke.bDig ? TEXT("true") : TEXT("false"));

    UE_LOG(LogTemp, Warning, TEXT("Applying brush at location: %s, radius: %f"), 
           *BrushStroke.BrushPosition.ToString(), BrushStroke.BrushRadius);

    // Apply the brush
    ApplyBrushToAllChunks(BrushStroke);
}*/

void ADiggerManager::ApplyBrushToAllChunksPIE(FBrushStroke& BrushStroke)
{
    // Get the hit location from camera first
    FHitResult HitResult;
    if (!ActiveBrush->GetCameraHitLocation(HitResult))
    {
        if (DiggerDebug::Brush)
        {
            UE_LOG(LogTemp, Warning, TEXT("No valid hit found"));
        }
        return;
    }

    // Create brush stroke with the hit location and brush settings
    BrushStroke = ActiveBrush->CreateBrushStroke(HitResult, ActiveBrush->GetDig());

    if (DiggerDebug::UserConv)
    {
        UE_LOG(LogTemp, Warning, TEXT("PIE Brush applied at: %s"), *BrushStroke.BrushPosition.ToString());
    }
    
    // Apply the brush
    ApplyBrushToAllChunks(BrushStroke);
}

void ADiggerManager::ApplyBrushToAllChunks(FBrushStroke& BrushStroke, bool ForceUpdate)
{
    ApplyBrushToAllChunks(BrushStroke);
}

void ADiggerManager::ApplyBrushToAllChunks(FBrushStroke& BrushStroke)
{
    const UVoxelBrushShape* ActiveBrushShape = GetActiveBrushShape(BrushStroke.BrushType);
    if (!ActiveBrushShape)
    {
        if (DiggerDebug::Brush)
        {
            UE_LOG(LogTemp, Error, TEXT("ActiveBrushShape is null for brush type %d"), (int32)BrushStroke.BrushType);
        }
        return;
    }

    /*UE_LOG(LogTemp, Warning, TEXT("ApplyBrushToAllChunks: === BRUSH STROKE DEBUG ==="));
    UE_LOG(LogTemp, Warning, TEXT("ApplyBrushToAllChunks: Position: %s"), *BrushStroke.BrushPosition.ToString());
    UE_LOG(LogTemp, Warning, TEXT("ApplyBrushToAllChunks: Radius: %f"), BrushStroke.BrushRadius);
    UE_LOG(LogTemp, Warning, TEXT("ApplyBrushToAllChunks: Falloff: %f"), BrushStroke.BrushFalloff);
    UE_LOG(LogTemp, Warning, TEXT("ApplyBrushToAllChunks: Strength: %f"), BrushStroke.BrushStrength);
    UE_LOG(LogTemp, Warning, TEXT("ApplyBrushToAllChunks: bDig: %s"), BrushStroke.bDig ? TEXT("true") : TEXT("false"));*/
    
    if (FVoxelConversion::LocalVoxelSize <= 0.0f)
    {
        FVoxelConversion::InitFromConfig(8, 4, 100.0f, FVector::ZeroVector);
    }

    // Handle hole spawn ONCE per brush stroke, before processing chunks
    if (BrushStroke.bDig)
    {
        if(BrushStroke.BrushPosition.Z - BrushStroke.BrushRadius + BrushStroke.BrushFalloff <= GetLandscapeHeightAt(BrushStroke.BrushPosition))
        {
            HandleHoleSpawn(BrushStroke);
        }
    }

    float BrushEffectRadius = BrushStroke.BrushRadius + BrushStroke.BrushFalloff;
    float ChunkWorldSize = FVoxelConversion::ChunkWorldSize();
    float ChunkDiagonal = ChunkWorldSize * 1.732f; // sqrt(3) for 3D diagonal
    float SafetyPadding = BrushEffectRadius + ChunkDiagonal;

    FVector Min = BrushStroke.BrushPosition - FVector(SafetyPadding);
    FVector Max = BrushStroke.BrushPosition + FVector(SafetyPadding);

    FIntVector MinChunk = FVoxelConversion::WorldToChunk_Min(Min);
    FIntVector MaxChunk = FVoxelConversion::WorldToChunk_Min(Max);

    for (int32 X = MinChunk.X; X <= MaxChunk.X; ++X)
    {
        for (int32 Y = MinChunk.Y; Y <= MaxChunk.Y; ++Y)
        {
            for (int32 Z = MinChunk.Z; Z <= MaxChunk.Z; ++Z)
            {
                FIntVector ChunkCoords(X, Y, Z);
                
                // Add this logging
                if (DiggerDebug::Chunks)
                {
                    UE_LOG(LogTemp, Warning, TEXT("Trying to get/create chunk at: %s"), *ChunkCoords.ToString());
                }
                
                if (UVoxelChunk* Chunk = GetOrCreateChunkAtChunk(ChunkCoords))
                {
                    if (DiggerDebug::Chunks)
                    {
                        UE_LOG(LogTemp, Warning, TEXT("Successfully got chunk at: %s"), *ChunkCoords.ToString());
                    }
                    
                    ENetMode NetMode = GetWorld()->GetNetMode();
                    if (NetMode == NM_Standalone)
                    {
                        // Single player: call the local method directly
                        Chunk->ApplyBrushStroke(BrushStroke);
                    }
                    else
                    {
                        // Multiplayer: call the multicast function (from the server)
                        Chunk->MulticastApplyBrushStroke(BrushStroke);
                    }
                }
                else
                {
                    if (DiggerDebug::Chunks)
                    {
                        UE_LOG(LogTemp, Error, TEXT("Failed to get/create chunk at: %s"), *ChunkCoords.ToString());
                    }
                }
            }
        }
    }
}

void ADiggerManager::SetVoxelAtWorldPosition(const FVector& WorldPos, float Value)
{
    // Safety checks
    if (TerrainGridSize <= 0.0f || Subdivisions <= 0)
    {
        if (DiggerDebug::Space || DiggerDebug::Chunks)
        {
            UE_LOG(LogTemp, Error, TEXT("SetVoxelAtWorldPosition: Invalid TerrainGridSize or Subdivisions!"));
        }
        return;
    }

    // Calculate chunk and voxel index
    FIntVector ChunkCoords = FVoxelConversion::WorldToChunk_Min(WorldPos);
    FIntVector OutChunk, LocalVoxelIndex;
    FVoxelConversion::WorldToChunkAndLocal_Min(WorldPos, OutChunk, LocalVoxelIndex);

    // Try to get or create the chunk
    if (UVoxelChunk* Chunk = GetOrCreateChunkAtChunk(ChunkCoords))
    {
        //Chunk->GetSparseVoxelGrid()->SetVoxel(LocalVoxelIndex, Value, true);

        if (DiggerDebug::Space || DiggerDebug::Chunks)
        {
            UE_LOG(LogTemp, Display, TEXT("[SetVoxelAtWorldPosition] WorldPos: %s â†’ Chunk: %s, Voxel: %s, Value: %.2f"),
                   *WorldPos.ToString(), *ChunkCoords.ToString(), *LocalVoxelIndex.ToString(), Value);
        }
    }
    else
    {
        if (DiggerDebug::Space || DiggerDebug::Chunks)
        {
            UE_LOG(LogTemp, Warning, TEXT("SetVoxelAtWorldPosition: Failed to get or create chunk at %s"), *ChunkCoords.ToString());
        }
    }
}


// Updated ADiggerManager::DebugBrushPlacement()
void ADiggerManager::DebugBrushPlacement(const FVector& ClickPosition)
{
    if (!IsInGameThread())
    {
        if (DiggerDebug::Brush || DiggerDebug::Threads)
        {
            UE_LOG(LogTemp, Error, TEXT("DebugBrushPlacement: Not called from game thread!"));
        }
        return;
    }

    UWorld* CurrentWorld = GetSafeWorld();
    if (!CurrentWorld)
    {
        if (DiggerDebug::Context)
        {
            UE_LOG(LogTemp, Error, TEXT("DebugBrushPlacement: GetSafeWorld() returned null!"));
        }
        return;
    }

    // Ensure parameters are valid
    if (TerrainGridSize <= 0.0f || Subdivisions <= 0)
    {
        if (DiggerDebug::Brush || DiggerDebug::Space)
        {
            UE_LOG(LogTemp, Error, TEXT("DebugBrushPlacement: Invalid TerrainGridSize or Subdivisions!"));
        }
        return;
    }

    VoxelSize = FVoxelConversion::LocalVoxelSize;
    const float ChunkWorldSize = FVoxelConversion::ChunkWorldSize();

    if (DiggerDebug::Space)
    {
        UE_LOG(LogTemp, Display, TEXT("ChunkSize=%d, Subdivisions=%d, VoxelSize=%.2f, ChunkWorldSize=%.2f"),
               ChunkSize, Subdivisions, VoxelSize, ChunkWorldSize);
    }

    // Compute chunk coordinates from world position
    FIntVector ChunkCoords = FVoxelConversion::WorldToChunk_Min(ClickPosition);

    if (DiggerDebug::Chunks || DiggerDebug::Space || DiggerDebug::Brush)
    {
        UE_LOG(LogTemp, Display, TEXT("ClickPos: %s â†’ ChunkCoords: %s"),
               *ClickPosition.ToString(), *ChunkCoords.ToString());
    }

    // Fetch or create the relevant chunk
    if (UVoxelChunk* Chunk = GetOrCreateChunkAtChunk(ChunkCoords))
    {
        Chunk->GetSparseVoxelGrid()->RenderVoxels();
        Chunk->GetSparseVoxelGrid()->LogVoxelData();
    }
    else
    {
        if (DiggerDebug::Chunks || DiggerDebug::Space)
        {
            UE_LOG(LogTemp, Warning, TEXT("Failed to get or create chunk at %s"), *ChunkCoords.ToString());
        }
        return;
    }

    // Compute chunk origin (min corner), center and extent using conversion helpers
    FVector ChunkMin = FVoxelConversion::ChunkToWorld_Min(ChunkCoords);
    FVector ChunkExtent = FVector(ChunkWorldSize * 0.5f);
    FVector ChunkCenter = ChunkMin + ChunkExtent;

    // Determine local and global voxel coordinates for the click
    FIntVector LocalVoxel;
    FVoxelConversion::WorldToChunkAndLocal_Min(ClickPosition, ChunkCoords, LocalVoxel);
    const FVector VoxelCenter = FVoxelConversion::ChunkVoxelToWorldCenter_Min(ChunkCoords, LocalVoxel);
    const FIntVector GlobalVoxel = FVoxelConversion::ChunkAndLocalToGlobalVoxel_Min(ChunkCoords, LocalVoxel);

    if (DiggerDebug::Space || DiggerDebug::Voxels || DiggerDebug::Chunks)
    {
        UE_LOG(LogTemp, Error, TEXT("Chunk:%s Local:%s Global:%s World:%s"),
               *ChunkCoords.ToString(), *LocalVoxel.ToString(), *GlobalVoxel.ToString(), *VoxelCenter.ToString());
    }

    if (DiggerDebug::Voxels)
    {
        UE_LOG(LogTemp, Display, TEXT("VoxelCenter: %s"), *VoxelCenter.ToString());
    }
    
    try
    {
        // Get the fast debug subsystem
        if (auto* FastDebug = UFastDebugSubsystem::Get(this))
        {
            // Draw the chunk bounding box (red)
            FastDebug->DrawBox(ChunkCenter, ChunkExtent, FRotator::ZeroRotator, 
                              FFastDebugConfig(FLinearColor::Red, 30.0f, 2.0f));

            // Draw chunk center (yellow sphere)
            FastDebug->DrawSphere(ChunkCenter, 30.0f, 
                                 FFastDebugConfig(FLinearColor::Yellow, 30.0f, 2.0f));

            // Draw chunk origin (orange sphere)
            FastDebug->DrawSphere(ChunkMin, 20.0f,
                                 FFastDebugConfig(FLinearColor(1.0f, 0.5f, 0.0f), 30.0f, 2.0f));

            const int32 ChunkVoxels = FVoxelConversion::VoxelsPerChunk();
            FVector SelectedVoxelCenter = FVector::ZeroVector;

            // Collect edge voxel positions for batch rendering
            TArray<FVector> EdgeVoxelPositions;
            EdgeVoxelPositions.Reserve(ChunkVoxels * ChunkVoxels * 6); // Rough estimate

            for (int32 X = 0; X < ChunkVoxels; X++)
            {
                for (int32 Y = 0; Y < ChunkVoxels; Y++)
                {
                    for (int32 Z = 0; Z < ChunkVoxels; Z++)
                    {
                        FIntVector VoxelCoord(X, Y, Z);
                        FVector CurrentVoxelCenter = FVoxelConversion::ChunkVoxelToWorldCenter_Min(ChunkCoords, VoxelCoord);

                        // Collect edge voxels
                        if (X == 0 || X == ChunkVoxels - 1 ||
                            Y == 0 || Y == ChunkVoxels - 1 ||
                            Z == 0 || Z == ChunkVoxels - 1)
                        {
                            EdgeVoxelPositions.Add(CurrentVoxelCenter);
                        }

                        // Track the selected voxel
                        if (VoxelCoord == LocalVoxel)
                        {
                            SelectedVoxelCenter = CurrentVoxelCenter;
                        }
                    }
                }
            }

            // Batch draw edge voxels as points (cyan)
            if (EdgeVoxelPositions.Num() > 0)
            {
                FastDebug->DrawPoints(EdgeVoxelPositions, 5.0f, 
                                     FFastDebugConfig(FColor::Cyan, 30.0f, 2.0f));
            }

            // Draw the selected voxel (magenta box)
            if (SelectedVoxelCenter != FVector::ZeroVector)
            {
                FastDebug->DrawBox(SelectedVoxelCenter, FVector(VoxelSize * 0.5f), FRotator::ZeroRotator,
                                  FFastDebugConfig(FLinearColor(1.0f, 0.0f, 1.0f), 30.0f, 2.0f));
            }

            // Draw interaction line
            FastDebug->DrawLine(ClickPosition, SelectedVoxelCenter, 
                               FFastDebugConfig(FLinearColor::Green, 30.0f, 3.0f));

            // Draw interaction points
            FastDebug->DrawSphere(ClickPosition, 20.0f, 
                                 FFastDebugConfig(FLinearColor::Blue, 30.0f, 2.0f));
            FastDebug->DrawSphere(SelectedVoxelCenter, 20.0f, 
                                 FFastDebugConfig(FLinearColor(1.0f, 0.0f, 1.0f), 30.0f, 2.0f));
        }

        if (DiggerDebug::Brush)
        {
            UE_LOG(LogTemp, Warning, TEXT("DebugBrushPlacement: Debug visuals drawn successfully"));
        }
    }
    catch (...)
    {
        UE_LOG(LogTemp, Error, TEXT("DebugBrushPlacement: Exception drawing debug visuals"));
    }

    // Diagonal marker boxes
    DrawDiagonalDebugVoxelsFast(ChunkCoords);
    // Individual voxels right nearest the click position
    DebugDrawVoxelAtWorldPositionFast(ClickPosition, FLinearColor::White, 25.0f, 2.0f);

    if (DiggerDebug::Brush)
    {
        UE_LOG(LogTemp, Warning, TEXT("DebugBrushPlacement: completed"));
    }
}
// Updated ADiggerManager::DebugDrawVoxelAtWorldPosition()
void ADiggerManager::DebugDrawVoxelAtWorldPositionFast(const FVector& WorldPosition, const FLinearColor& BoxColor, float Duration, float Thickness)
{
    FIntVector ChunkCoords;
    FIntVector VoxelIndex;
    FVoxelConversion::WorldToChunkAndLocal_Min(WorldPosition, ChunkCoords, VoxelIndex);
    const FIntVector GlobalVoxel = FVoxelConversion::ChunkAndLocalToGlobalVoxel_Min(ChunkCoords, VoxelIndex);

    if (!FVoxelConversion::IsValidLocalIndex_Min(VoxelIndex))
    {
        UE_LOG(LogTemp, Warning, TEXT("Invalid voxel index at world position %s: Chunk %s, VoxelIndex %s"),
            *WorldPosition.ToString(), *ChunkCoords.ToString(), *VoxelIndex.ToString());
        return;
    }

    // Get the voxel center in world space
    FVector VoxelCenter = FVoxelConversion::ChunkVoxelToWorldCenter_Min(ChunkCoords, VoxelIndex);

    // Draw using fast debug system
    if (auto* FastDebug = UFastDebugSubsystem::Get(this))
    {
        FastDebug->DrawBox(VoxelCenter, FVector(FVoxelConversion::LocalVoxelSize * 0.5f), FRotator::ZeroRotator,
                          FFastDebugConfig(BoxColor, Duration, Thickness));
    }

    if (DiggerDebug::Chunks || DiggerDebug::Voxels)
    {
        UE_LOG(LogTemp, Log, TEXT("Drew voxel at world position %s: Chunk %s, Local %s, Global %s, Center %s"),
            *WorldPosition.ToString(), *ChunkCoords.ToString(), *VoxelIndex.ToString(), *GlobalVoxel.ToString(), *VoxelCenter.ToString());
    }
}

// Updated ADiggerManager::DrawDiagonalDebugVoxels()
void ADiggerManager::DrawDiagonalDebugVoxelsFast(FIntVector ChunkCoords)
{
    // Get the fast debug subsystem
    auto* FastDebug = UFastDebugSubsystem::Get(this);
    if (!FastDebug) return;

    // Calculate chunk dimensions
    const int32 VoxelsPerChunk = FVoxelConversion::VoxelsPerChunk();
    const float DebugDuration = 30.0f;

    // Derive chunk min, extent and center using conversion helpers
    FVector ChunkMinCorner = FVoxelConversion::ChunkToWorld_Min(ChunkCoords);
    FVector ChunkExtent = FVector(FVoxelConversion::ChunkWorldSize() * 0.5f);
    FVector ChunkCenter = ChunkMinCorner + ChunkExtent;
    
    UE_LOG(LogTemp, Warning, TEXT("DrawDiagonalDebugVoxelsFast - ChunkCoords: %s"), *ChunkCoords.ToString());
    UE_LOG(LogTemp, Warning, TEXT("ChunkCenter: %s, ChunkMinCorner: %s"), 
           *ChunkCenter.ToString(), *ChunkMinCorner.ToString());
    UE_LOG(LogTemp, Warning, TEXT("VoxelsPerChunk: %d, LocalVoxelSize: %f"),
           VoxelsPerChunk, FVoxelConversion::LocalVoxelSize);
    
    // Collect diagonal voxel positions for batch rendering
    TArray<FVector> DiagonalVoxelPositions;
    DiagonalVoxelPositions.Reserve(VoxelsPerChunk);
    
    // Collect diagonal voxels from minimum corner to maximum corner
    for (int32 i = 0; i < VoxelsPerChunk; i++)
    {
        FIntVector Local = FIntVector(i, i, i);
        FVector VoxelCenter = FVoxelConversion::ChunkVoxelToWorldCenter_Min(ChunkCoords, Local);
        DiagonalVoxelPositions.Add(VoxelCenter);
    }
    
    // Batch draw diagonal voxels (green)
    if (DiagonalVoxelPositions.Num() > 0)
    {
        FastDebug->DrawBoxes(DiagonalVoxelPositions, 
                           FVector(FVoxelConversion::LocalVoxelSize * 0.45f),
                           FFastDebugConfig(FLinearColor::Green, DebugDuration, 1.5f));
    }
    
    // Collect overflow voxel positions
    TArray<FVector> OverflowVoxelPositions;
    FIntVector overflowOffsets[] = {
        FIntVector(-1, -1, -1),  // Corner overflow
        FIntVector(-1, 0, 0),    // X-axis overflow
        FIntVector(0, -1, 0),    // Y-axis overflow
        FIntVector(0, 0, -1)     // Z-axis overflow
    };

    for (const FIntVector& offset : overflowOffsets)
    {
        FVector VoxelCenter = FVoxelConversion::ChunkVoxelToWorldCenter_Min(ChunkCoords, offset);
        OverflowVoxelPositions.Add(VoxelCenter);

        UE_LOG(LogTemp, Warning, TEXT("Overflow voxel at local position %s, world position %s"),
               *offset.ToString(), *VoxelCenter.ToString());
    }
    
    // Batch draw overflow voxels (purple)
    if (OverflowVoxelPositions.Num() > 0)
    {
        FastDebug->DrawBoxes(OverflowVoxelPositions, 
                           FVector(FVoxelConversion::LocalVoxelSize * 0.45f),
                           FFastDebugConfig(FLinearColor(0.5f, 0.0f, 1.0f), DebugDuration, 2.0f));
    }
    
    // Draw reference boxes
    FastDebug->DrawBox(ChunkCenter, ChunkExtent, FRotator::ZeroRotator,
                      FFastDebugConfig(FLinearColor::Red, DebugDuration, 2.5f));
    
    FastDebug->DrawBox(ChunkMinCorner, FVector(FVoxelConversion::LocalVoxelSize * 0.75f), FRotator::ZeroRotator,
                      FFastDebugConfig(FLinearColor::Yellow, DebugDuration, 3.0f));
    
    FVector ChunkMaxCorner = ChunkCenter + ChunkExtent;
    FastDebug->DrawBox(ChunkMaxCorner, FVector(FVoxelConversion::LocalVoxelSize * 0.75f), FRotator::ZeroRotator,
                      FFastDebugConfig(FLinearColor::Blue, DebugDuration, 3.0f));
    
    FastDebug->DrawBox(FVector::ZeroVector, FVector(FVoxelConversion::LocalVoxelSize * 1.5f), FRotator::ZeroRotator,
                      FFastDebugConfig(FLinearColor(1.0f, 0.0f, 1.0f), DebugDuration, 3.0f));
    
    // Draw overflow regions on the minimum sides
    const float ChunkWorldSize = FVoxelConversion::ChunkWorldSize();
    
    // X-axis overflow region
    FVector XOverflowCenter = ChunkMinCorner + FVector(-FVoxelConversion::LocalVoxelSize * 0.5f, 
                                                       ChunkWorldSize * 0.5f, 
                                                       ChunkWorldSize * 0.5f);
    FVector XOverflowExtent = FVector(FVoxelConversion::LocalVoxelSize * 0.5f, ChunkExtent.Y, ChunkExtent.Z);
    
    FastDebug->DrawBox(XOverflowCenter, XOverflowExtent, FRotator::ZeroRotator,
                      FFastDebugConfig(FLinearColor(1.0f, 0.5f, 1.0f, 0.25f), DebugDuration, 1.0f));
    
    // Y-axis overflow region
    FVector YOverflowCenter = ChunkMinCorner + FVector(ChunkWorldSize * 0.5f, 
                                                       -FVoxelConversion::LocalVoxelSize * 0.5f, 
                                                       ChunkWorldSize * 0.5f);
    FVector YOverflowExtent = FVector(ChunkExtent.X, FVoxelConversion::LocalVoxelSize * 0.5f, ChunkExtent.Z);
    
    FastDebug->DrawBox(YOverflowCenter, YOverflowExtent, FRotator::ZeroRotator,
                      FFastDebugConfig(FLinearColor(1.0f, 0.5f, 1.0f, 0.25f), DebugDuration, 1.0f));
    
    // Z-axis overflow region  
    FVector ZOverflowCenter = ChunkMinCorner + FVector(ChunkWorldSize * 0.5f, 
                                                       ChunkWorldSize * 0.5f, 
                                                       -FVoxelConversion::LocalVoxelSize * 0.5f);
    FVector ZOverflowExtent = FVector(ChunkExtent.X, ChunkExtent.Y, FVoxelConversion::LocalVoxelSize * 0.5f);
    
    FastDebug->DrawBox(ZOverflowCenter, ZOverflowExtent, FRotator::ZeroRotator,
                      FFastDebugConfig(FLinearColor(1.0f, 0.5f, 1.0f, 0.25f), DebugDuration, 1.0f));
}


void ADiggerManager::AddDebugVoxelInstance(const FVector& WorldPosition, const FColor& Color)
{
    if (!VoxelDebugMesh) return;

    VoxelSize = FVoxelConversion::LocalVoxelSize;
    const FVector Scale = FVector(VoxelSize / 100.0f); // Convert to Unreal units (cube is 100cm)

    FTransform Transform;
    Transform.SetLocation(WorldPosition);
    Transform.SetScale3D(Scale);

    int32 InstanceIndex = VoxelDebugMesh->AddInstance(Transform);

    // Optional: set per-instance color if using a material with PerInstanceRandom or custom data
    // VoxelDebugMesh->SetCustomData(InstanceIndex, { Color.R / 255.0f, Color.G / 255.0f, Color.B / 255.0f });
}


void ADiggerManager::RemoveIslandByVoxel(const FIntVector& Voxel)
{
    FIslandData* Island = FindIsland(Voxel);
    if (!Island)
    {
        if (DiggerDebug::Islands || DiggerDebug::Error)
        {
            UE_LOG(LogTemp, Error, TEXT("[DiggerPro] RemoveIslandByVoxel: No island found for voxel %s"), *Voxel.ToString());
        }
        return;
    }

    TSet<FIntVector> AffectedChunks;
    int32 RemovedCount = 0;

    for (const FVoxelInstance& Instance : Island->VoxelInstances)
    {
        UVoxelChunk** ChunkPtr = ChunkMap.Find(Instance.ChunkCoords);
        if (!ChunkPtr || !IsValid(*ChunkPtr)) continue;

        UVoxelChunk* Chunk = *ChunkPtr;
        USparseVoxelGrid* Grid = Chunk->GetSparseVoxelGrid();
        if (!IsValid(Grid)) continue;

        Grid->RemoveVoxel(Instance.LocalVoxel);
        Chunk->MarkDirty();
        AffectedChunks.Add(Instance.ChunkCoords);
        ++RemovedCount;
    }

    UE_LOG(LogTemp, Display, TEXT("[DiggerPro] Removed %d voxel instances from island at %s"),
        RemovedCount, *Island->Location.ToString());

    for (const FIntVector& ChunkCoords : AffectedChunks)
    {
        UVoxelChunk** ChunkPtr = ChunkMap.Find(ChunkCoords);
        if (ChunkPtr && IsValid(*ChunkPtr))
        {
            (*ChunkPtr)->RefreshSectionMesh();
        }
    }

    // Clean up lookup maps
    for (const FVoxelInstance& Instance : Island->VoxelInstances)
    {
        VoxelToIslandIDMap.Remove(Instance.GlobalVoxel);
    }

    IslandIDMap.Remove(Island->IslandID);
}




// FindIsland overload methods: (Currently in use 23/08/2025
FIslandData* ADiggerManager::FindIsland(const FIntVector& Voxel)
{
    if (VoxelToIslandIDMap.Contains(Voxel))
    {
        const FName& IslandID = VoxelToIslandIDMap[Voxel];
        return FindIsland(IslandID); // Delegate to ID-based version
    }

    UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Voxel %s not found in VoxelToIslandIDMap."), *Voxel.ToString());
    return nullptr;
}

FIslandData* ADiggerManager::FindIsland(const FName& IslandID)
{
    FIslandData* Island = IslandIDMap.Find(IslandID);
    if (!Island)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Island ID %s not found in IslandIDMap."), *IslandID.ToString());
    }
    return Island;
}

FIslandData ADiggerManager::FindIslandByVoxel_BP(const FIntVector& Voxel)
{
    if (VoxelToIslandIDMap.Contains(Voxel))
    {
        const FName& IslandID = VoxelToIslandIDMap[Voxel];
        if (IslandIDMap.Contains(IslandID))
        {
            return IslandIDMap[IslandID];
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Voxel %s not found in VoxelToIslandIDMap."), *Voxel.ToString());
    return FIslandData(); // Return default if not found
}

FIslandData ADiggerManager::FindIslandByID_BP(const FName& IslandID)
{
    if (IslandIDMap.Contains(IslandID))
    {
        return IslandIDMap[IslandID];
    }

    UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Island ID %s not found."), *IslandID.ToString());
    return FIslandData(); // Return default
}


// Currently in use // Date: 25/08/2025
void ADiggerManager::ConvertIslandToStaticMesh(const FName& IslandID, bool bEnablePhysics)
{
    FIslandData* Island = FindIsland(IslandID);
    if (!Island)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] ConvertIslandToStaticMesh: Island %s not found."), *IslandID.ToString());
        return;
    }

    // 1) Generate: verts are LOCAL around 0; MeshOrigin is WORLD center
    FIslandMeshData MeshData = GenerateIslandMeshFromStoredData(*Island);
    if (!MeshData.bValid)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Mesh extraction failed for island %s"), *IslandID.ToString());
        return;
    }

    // 2) Save to static mesh asset
    const FString AssetName = FString::Printf(TEXT("Island_%s_StaticMesh"), *IslandID.ToString());
    UStaticMesh* SavedMesh = SaveIslandMeshAsStaticMesh(AssetName, MeshData);
    if (!SavedMesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Failed to save static mesh for island %s"), *IslandID.ToString());
        return;
    }

    // 3) Spawn exactly one StaticMeshActor at world origin = MeshOrigin
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    Island->Location = MeshData.MeshOrigin;

    AStaticMeshActor* MeshActor = GetWorld()->SpawnActor<AStaticMeshActor>(
        AStaticMeshActor::StaticClass(),
        Island->Location,
        FRotator::ZeroRotator,
        SpawnParams
    );

    if (!MeshActor)
    {
        UE_LOG(LogTemp, Error, TEXT("[DiggerPro] Failed to spawn StaticMeshActor for island %s"),
            *IslandID.ToString());
        return;
    }

    UStaticMeshComponent* SMC = MeshActor->GetStaticMeshComponent();
    if (SMC)
    {
        MeshActor->SetActorLabel(FString::Printf(TEXT("IslandActor_%s"), *IslandID.ToString()));
        SMC->SetStaticMesh(SavedMesh);
        SMC->SetMobility(EComponentMobility::Movable);
        SMC->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        SMC->SetCollisionObjectType(ECC_PhysicsBody);
        SMC->MarkRenderStateDirty();

        if (bEnablePhysics)
        {
            SMC->SetSimulatePhysics(true);
            SMC->SetEnableGravity(true);
            SMC->RecreatePhysicsState();
        }
    }

    UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Spawned StaticMeshActor for island %s at %s"),
        *IslandID.ToString(), *Island->Location.ToString());
}



void ADiggerManager::UpdateAllDirtyChunks()
{
    if (DiggerDebug::Islands)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Running ADM::UpdateAllDirtyChunks..."));
    }

    for (const auto& ChunkPair : ChunkMap)
    {
        UVoxelChunk* Chunk = ChunkPair.Value;
        if (Chunk)
        {
            Chunk->UpdateIfDirty(); // Only updates if marked dirty
        }
    }

    if (DiggerDebug::Islands)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Finished updating dirty chunks."));
    }
}


// In ADiggerManager.cpp
void ADiggerManager::RemoveIslandVoxels(const FIslandData& Island)
{
    if (DiggerDebug::Islands)
    {
        UE_LOG(LogTemp, Warning, TEXT("Running ADM::RemoveIslandVoxels!"));
    }

    struct FChunkModification
    {
        TWeakObjectPtr<UVoxelChunk> Chunk;
        TArray<FIntVector> LocalVoxels;
    };

    TMap<FIntVector, FChunkModification> AffectedChunks;
    TSet<FIntVector> DeletedGlobalVoxels;

    {
        FScopeLock Lock(&IslandRemovalMutex);

        for (const FIntVector& GlobalVoxel : Island.Voxels)
        {
            FIntVector ChunkCoords, LocalVoxel;
            FVoxelConversion::GlobalVoxelToChunkAndLocal_Min(GlobalVoxel, ChunkCoords, LocalVoxel);

            UVoxelChunk** ChunkPtr = ChunkMap.Find(ChunkCoords);
            if (!ChunkPtr || !*ChunkPtr) continue;

            USparseVoxelGrid* Grid = (*ChunkPtr)->GetSparseVoxelGrid();
            if (!Grid) continue;

            if (Grid->RemoveVoxel(LocalVoxel))
            {
                AffectedChunks.FindOrAdd(ChunkCoords).Chunk = *ChunkPtr;
                AffectedChunks[ChunkCoords].LocalVoxels.Add(LocalVoxel);
                DeletedGlobalVoxels.Add(GlobalVoxel);
            }
        }
    }

    auto RefreshChunks = [DeletedGlobalVoxels, AffectedChunks, this]()
    {
        int32 TotalRemoved = DeletedGlobalVoxels.Num();

        for (const auto& Pair : AffectedChunks)
        {
            const TWeakObjectPtr<UVoxelChunk>& ChunkPtr = Pair.Value.Chunk;
            if (!ChunkPtr.IsValid()) continue;

            USparseVoxelGrid* Grid = ChunkPtr->GetSparseVoxelGrid();
            if (!Grid) continue;

            // Access one of the deleted voxels to trigger mesh rebuild
            for (const FIntVector& LocalVoxel : Pair.Value.LocalVoxels)
            {
                FVoxelData Dummy;
                Grid->GetVoxel(LocalVoxel); // Ghost ping for mesh invalidation
                break; // Only need one
            }
            ChunkPtr->RefreshSectionMesh();
            ChunkPtr->MarkDirty();
            ChunkPtr->ForceUpdate(); // Single rebuild per chunk
        }

        if (DiggerDebug::Islands)
        {
            UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Island cleanup: Removed %d voxels across %d chunks"),
                   TotalRemoved, AffectedChunks.Num());
        }
        UpdateAllDirtyChunks();
    };

    if (!IsInGameThread())
    {
        AsyncTask(ENamedThreads::GameThread, MoveTemp(RefreshChunks));
        UpdateAllDirtyChunks();
    }
    else
    {
        RefreshChunks();
        UpdateAllDirtyChunks();
    }
}



void ADiggerManager::SaveIslandData(AIslandActor* IslandActor, const FIslandMeshData& MeshData)
{
    if (!IslandActor)
        return;
        
    FIslandSaveData SaveData;
    SaveData.MeshOrigin = MeshData.MeshOrigin;
    SaveData.Vertices = MeshData.Vertices;
    SaveData.Triangles = MeshData.Triangles;
    SaveData.Normals = MeshData.Normals;
    SaveData.bEnablePhysics = IslandActor->ProcMesh->IsSimulatingPhysics();
    
    SavedIslands.Add(SaveData);
}


AIslandActor* ADiggerManager::SpawnIslandActorWithMeshData(
    const FVector& SpawnLocation,
    const FIslandMeshData& MeshData,
    bool bEnablePhysics
)
{
    if (!MeshData.bValid || !World) return nullptr;

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    SpawnParams.Owner = nullptr;
    SpawnParams.bNoFail = true;

    AIslandActor* IslandActor = World->SpawnActor<AIslandActor>(
        AIslandActor::StaticClass(),
        SpawnLocation,
        FRotator::ZeroRotator,
        SpawnParams
    );

    if (IslandActor && IslandActor->ProcMesh)
    {
        // Offset mesh vertices relative to actor origin
        TArray<FVector> LocalVertices = MeshData.Vertices;
        for (FVector& Vertex : LocalVertices)
        {
            Vertex -= SpawnLocation;
        }

        IslandActor->ProcMesh->CreateMeshSection_LinearColor(
            0,
            LocalVertices,
            MeshData.Triangles,
            MeshData.Normals,
            {}, {}, {}, true
        );

        IslandActor->ProcMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        IslandActor->ProcMesh->SetCollisionObjectType(ECC_PhysicsBody);
        IslandActor->ProcMesh->SetNotifyRigidBodyCollision(true);

        if (bEnablePhysics)
        {
            IslandActor->ProcMesh->SetSimulatePhysics(true);
            IslandActor->ProcMesh->SetEnableGravity(true);
            IslandActor->ProcMesh->WakeAllRigidBodies();
        }
        else
        {
            IslandActor->ProcMesh->SetSimulatePhysics(false);
        }
    }

    return IslandActor;
}


static void Debug_IslandSDFStats(const FIslandData& Island)
{
    int32 Neg=0, Pos=0, Zer=0;
    float MinS=FLT_MAX, MaxS=-FLT_MAX;

    FIntVector MinI(INT_MAX), MaxI(INT_MIN);

    int32 n = 0;
    for (const auto& Pair : Island.VoxelDataMap)
    {
        const FIntVector G = Pair.Key;
        const float S = Pair.Value.SDFValue;
        MinS = FMath::Min(MinS, S);
        MaxS = FMath::Max(MaxS, S);
        if (S < 0) ++Neg; else if (S > 0) ++Pos; else ++Zer;

        MinI.X = FMath::Min(MinI.X, G.X); MinI.Y = FMath::Min(MinI.Y, G.Y); MinI.Z = FMath::Min(MinI.Z, G.Z);
        MaxI.X = FMath::Max(MaxI.X, G.X); MaxI.Y = FMath::Max(MaxI.Y, G.Y); MaxI.Z = FMath::Max(MaxI.Z, G.Z);

        if (++n <= 5)
            UE_LOG(LogTemp, Warning, TEXT("[IslandSDF] Sample G:%s S:%.3f"), *G.ToString(), S);
    }

    UE_LOG(LogTemp, Warning, TEXT("[IslandSDF] Count:%d Neg:%d Pos:%d Zer:%d  Range[%.3f..%.3f]  GMin:%s GMax:%s"),
        Island.VoxelDataMap.Num(), Neg, Pos, Zer, MinS, MaxS, *MinI.ToString(), *MaxI.ToString());
}

// Chunk drift check
void ADiggerManager::Debug_OffsetProbe(const FIslandData& Island)
{
    if (Island.VoxelInstances.Num() == 0) return;

    const FVoxelInstance& I = Island.VoxelInstances[0];
    const FVector W0 = FVoxelConversion::GlobalVoxelCenterToWorld(I.GlobalVoxel);
    const FVector W1 = FVoxelConversion::ChunkVoxelToWorldCenter_Min(I.ChunkCoords, I.LocalVoxel);

    const FVector D = W1 - W0;
    const float   CWS = FVoxelConversion::ChunkWorldSize();

    UE_LOG(LogTemp, Warning, TEXT("[Probe] W0=%s  W1=%s  Δ=%s  (Δ/CWS=%.3f,%.3f,%.3f)  CWS=%.1f"),
        *W0.ToString(), *W1.ToString(), *D.ToString(),
        D.X / CWS, D.Y / CWS, D.Z / CWS, CWS);
}


// We are using this GenerateIslandMeshFromStoredData! 25/08/2025
FIslandMeshData ADiggerManager::GenerateIslandMeshFromStoredData(const FIslandData& IslandData)
{
    FIslandMeshData Result;

    const int32 InstanceCount = IslandData.VoxelInstances.Num();
    const int32 VoxelDataCount = IslandData.VoxelDataMap.Num();
    if (InstanceCount == 0 || VoxelDataCount == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Island %s has no data (instances=%d, voxels=%d)."),
            *IslandData.IslandID.ToString(), InstanceCount, VoxelDataCount);
        return Result;
    }

    USparseVoxelGrid* Grid = NewObject<USparseVoxelGrid>();
    Grid->SetVoxelData(IslandData.VoxelDataMap);
    Grid->Initialize(nullptr);

    // 1) Compute WORLD pivot as the avg of GLOBAL-voxel centers → world
    FVector WorldCenter(0,0,0);
    for (const FVoxelInstance& Inst : IslandData.VoxelInstances)
    {
        WorldCenter += FVoxelConversion::GlobalVoxelCenterToWorld(Inst.GlobalVoxel);
    }
    WorldCenter /= FMath::Max(1, InstanceCount);

    // 2) Generate WORLD-SPACE mesh (mesher must NOT add any origin/offset)
    TArray<FVector> WorldVerts;
    TArray<int32>   Tris;
    TArray<FVector> Normals;

    const float VSize = FVoxelConversion::LocalVoxelSize;

    // If your existing signature is (Grid, Origin, VSize, ...), pass ZeroVector.
    // The mesher body (next section) ignores Origin and works entirely in global-voxel space.
    MarchingCubes->GenerateMeshFromGrid(Grid, /*Origin*/ FVector::ZeroVector, VSize, WorldVerts, Tris, Normals);

    if (WorldVerts.Num() == 0 || Tris.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Mesher returned empty for island %s"), *IslandData.IslandID.ToString());
        return Result;
    }

    // 3) Recentre to LOCAL around (0,0,0) for StaticMesh build
    Result.Vertices.Reserve(WorldVerts.Num());
    for (const FVector& V : WorldVerts)
    {
        Result.Vertices.Add(V - WorldCenter);
    }
    Result.Triangles = MoveTemp(Tris);
    Result.Normals   = MoveTemp(Normals);
    Result.MeshOrigin = WorldCenter;  // actor pivot in world
    Result.bValid = true;

    UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Mesh gen OK: verts=%d tris=%d origin=%s"),
        Result.Vertices.Num(), Result.Triangles.Num()/3, *WorldCenter.ToString());
    return Result;
}





void ADiggerManager::ClearAllIslandActors()
{
    for (AIslandActor* Actor : IslandActors)
    {
        if (Actor && IsValid(Actor))
        {
            Actor->Destroy();
        }
    }
    IslandActors.Empty();
    SavedIslands.Empty();
}

void ADiggerManager::DestroyIslandActor(AIslandActor* IslandActor)
{
    if (IslandActor && IsValid(IslandActor))
    {
        IslandActors.Remove(IslandActor);
        
        // Find and remove from saved islands
        for (int32 i = SavedIslands.Num() - 1; i >= 0; --i)
        {
            // This is a simple check - you might need a more sophisticated way to match
            if (FVector::DistSquared(SavedIslands[i].MeshOrigin, IslandActor->GetActorLocation()) < 1.0f)
            {
                SavedIslands.RemoveAt(i);
                break;
            }
        }
        
        IslandActor->Destroy();
    }
}

// This is in use for island removal 23/08/2025  ***********
void ADiggerManager::RemoveIslandByID(const FName& IslandID)
{
    FIslandData* Island = IslandIDMap.Find(IslandID);
    if (!Island) return;

    for (const FVoxelInstance& Instance : Island->VoxelInstances)
    {
        // Same removal logic as before...
    }

    for (const FVoxelInstance& Instance : Island->VoxelInstances)
    {
        VoxelToIslandIDMap.Remove(Instance.GlobalVoxel);
    }

    IslandIDMap.Remove(IslandID);
}

// Currently in use 23/08/2025
void ADiggerManager::HighlightIslandByID(const FName& IslandID)
{
    FIslandData* Island = FindIsland(IslandID);
    if (!Island || !IsValid(VoxelDebugMesh)) return;

    const FIslandData ConstIsland = *Island;
    Debug_OffsetProbe(ConstIsland);

    VoxelDebugMesh->ClearInstances();

    const float VoxelSizeWS = FVoxelConversion::LocalVoxelSize;   // local, don't touch member
    const FVector Scale = FVector(VoxelSizeWS / 100.f);

    int32 DebugCount = 0;
    FVector AccumCenter = FVector::ZeroVector;

    for (const FVoxelInstance& Instance : Island->VoxelInstances)
    {
        // ✅ Single source of truth: convert directly to world, no extra offsets
        const FVector WorldPos = FVoxelConversion::GlobalVoxelCenterToWorld(Instance.GlobalVoxel);

        if (DebugCount < 3)
        {
            UE_LOG(LogTemp, Warning, TEXT("[IslandDebug] Voxel %d  Chunk=%s  Local=%s  Global=%s  WorldPos=%s"),
                DebugCount, *Instance.ChunkCoords.ToString(), *Instance.LocalVoxel.ToString(),
                *Instance.GlobalVoxel.ToString(), *WorldPos.ToString());
        }

        FTransform T; 
        T.SetLocation(WorldPos);
        T.SetScale3D(Scale * 1.3f);

        // UE 5.3+: AddInstance with bWorldSpace=true; older: AddInstanceWorldSpace
    #if (ENGINE_MAJOR_VERSION > 5) || (ENGINE_MAJOR_VERSION==5 && ENGINE_MINOR_VERSION>=3)
        VoxelDebugMesh->AddInstance(T, /*bWorldSpace=*/true);
    #else
        VoxelDebugMesh->AddInstanceWorldSpace(T);
    #endif

        AccumCenter += WorldPos;
        ++DebugCount;
    }

    // Optional sanity: compare stored center with computed center
    const FVector ComputedCenter = (Island->VoxelInstances.Num() > 0)
        ? (AccumCenter / Island->VoxelInstances.Num())
        : Island->Location;

    // Center marker at the stored island location
    FTransform CenterT;
    CenterT.SetLocation(Island->Location);
    CenterT.SetScale3D(FVector(3.f));
#if (ENGINE_MAJOR_VERSION > 5) || (ENGINE_MAJOR_VERSION==5 && ENGINE_MINOR_VERSION>=3)
    VoxelDebugMesh->AddInstance(CenterT, /*bWorldSpace=*/true);
#else
    VoxelDebugMesh->AddInstanceWorldSpace(CenterT);
#endif

    if (!ComputedCenter.Equals(Island->Location, 1.f))
    {
        UE_LOG(LogTemp, Warning, TEXT("[IslandDebug] Center mismatch: stored=%s  computed=%s  Δ=%.2f"),
            *Island->Location.ToString(), *ComputedCenter.ToString(),
            FVector::Dist(ComputedCenter, Island->Location));
    }

    if (DiggerDebug::Islands)
    {
        UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Highlighted island with %d voxels"), Island->VoxelInstances.Num());
    }
}



// In Use // Date: 25/08/2025
UStaticMesh* ADiggerManager::SaveIslandMeshAsStaticMesh(const FString& AssetName, const FIslandMeshData& MeshData)
{
    if (!MeshData.bValid)
    {
        UE_LOG(LogTemp, Error, TEXT("[DiggerPro] MeshData is invalid. Cannot save static mesh: %s"), *AssetName);
        return nullptr;
    }

    TArray<FVector3f> FloatVerts = ConvertArray<FVector, FVector3f>(MeshData.Vertices);
    TArray<FVector3f> FloatNormals = ConvertArray<FVector, FVector3f>(MeshData.Normals);

    UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Saving static mesh: %s | Vertices: %d | Triangles: %d"),
        *AssetName, FloatVerts.Num(), MeshData.Triangles.Num() / 3);

    UStaticMesh* Mesh = CreateStaticMeshFromRawData(
        GetTransientPackage(),
        AssetName,
        FloatVerts,
        MeshData.Triangles,
        FloatNormals,
        {}, {}, {}
    );

    if (!Mesh)
    {
        UE_LOG(LogTemp, Error, TEXT("[DiggerPro] Static mesh creation failed for asset: %s"), *AssetName);
    }

    return Mesh;
}



bool ADiggerManager::EnsureWorldReference()
{
    if (World)
    {
        return true;
    }

    World = GetSafeWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("World not found in DiggerManager."));
        return false; // Exit if World is not found
    }
    return true;
}

void ADiggerManager::RecreateIslandFromSaveData(const FIslandSaveData& SaveData)
{
    // Create a temporary FIslandMeshData
    FIslandMeshData MeshData;
    MeshData.MeshOrigin = SaveData.MeshOrigin;
    MeshData.Vertices = SaveData.Vertices;
    MeshData.Triangles = SaveData.Triangles;
    MeshData.Normals = SaveData.Normals;
    MeshData.bValid = (MeshData.Vertices.Num() > 0 && MeshData.Triangles.Num() > 0);
    
    // Spawn the island actor
    SpawnIslandActorWithMeshData(SaveData.MeshOrigin, MeshData, SaveData.bEnablePhysics);
}


void ADiggerManager::PopulateAllCachedLandscapeHeights()
{
    // Clear caches if needed
    LandscapeHeightCaches.Empty();
    HeightCacheLoadingSet.Empty();

    // Find all landscapes and start async cache
    for (TActorIterator<ALandscapeProxy> It(GetWorld()); It; ++It)
    {
        ALandscapeProxy* Landscape = *It;
        if (Landscape)
        {
            HeightCacheLoadingSet.Add(Landscape);
            PopulateLandscapeHeightCacheAsync(Landscape);
        }
    }
}

void ADiggerManager::BeginPlay()
{
    Super::BeginPlay();

    UE_LOG(LogTemp, Warning, TEXT("=== PIE STARTED - CHUNK STATUS ==="));
    UE_LOG(LogTemp, Warning, TEXT("ChunkMap contains %d chunks"), ChunkMap.Num());

    QuickDebugTest();

    if (!bRuntimeInitDone)
    {
        InitRuntimeHeavy();
        bRuntimeInitDone = true;
    }
    
    int ChunkCount = 0;
    for (auto& ChunkPair : ChunkMap)
    {
        ChunkCount++;
        UE_LOG(LogTemp, Warning, TEXT("Found chunk at: %s"), *ChunkPair.Key.ToString());
    }
    UE_LOG(LogTemp, Warning, TEXT("ChunkCount: %i"), ChunkCount);

    if (!EnsureWorldReference())
    {
        UE_LOG(LogTemp, Error, TEXT("World is null in DiggerManager BeginPlay()! Continuing with default behavior."));
    }

    UpdateVoxelSize();

    StartHeightCaching();
        
    // ensure brushes are ready for PIE usage
    ActiveBrush->InitializeBrush(ActiveBrush->GetBrushType(),ActiveBrush->GetBrushSize(),ActiveBrush->GetBrushLocation(),this);
    InitializeBrushShapes();

    DestroyAllHoleBPs();
    ClearProceduralMeshes();
    //ClearAllVoxelData();
    //InitializeTerrainCache(); // or lazy-init fallback
    LoadAllChunks();
    
    // Start the timer to process dirty chunks
   // if (World)
   // {
   //     World->GetTimerManager().SetTimer(ChunkProcessTimerHandle, this, &ADiggerManager::ProcessDirtyChunks, 1.0f, true);
   // }

    // Delay the height caching logic until after level has loaded
    FTimerHandle UnusedHandle;
    GetWorld()->GetTimerManager().SetTimer(UnusedHandle, this, &ADiggerManager::StartHeightCaching, 0.1f, false);


    // Set the ProceduralMesh material if it's valid
    if (TerrainMaterial)
    {
        if (ProceduralMesh && ProceduralMesh->GetMaterial(0) != TerrainMaterial)
        {
            ProceduralMesh->SetMaterial(0, TerrainMaterial);
            UE_LOG(LogTemp, Warning, TEXT("Set Material M_ProcGrid at index 0"));
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Material M_ProcGrid is null. Please ensure it is loaded properly."));
    }

    // Recreate saved islands when entering PIE
    for (const FIslandSaveData& SavedIsland : SavedIslands)
    {
        RecreateIslandFromSaveData(SavedIsland);
    }
}

void ADiggerManager::StartHeightCaching()
{
    // Call your async height caching method here
    PopulateAllCachedLandscapeHeights();
}

void ADiggerManager::InitEditorLightweight()
{
    // Keep this near zero cost. No voxel allocation, no terrain scan.
    // e.g., set default preview radius/type so the EdMode can draw gizmos.
    // EditorBrushRadius = FMath::Max(EditorBrushRadius, 50.f);
}

void ADiggerManager::EditorDeferredInit()
{
#if WITH_EDITOR
    if (IsRuntimeLike()) return;        // safety
    if (bEditorInitDone)  return;

    // If you want the landscape cache for preview, do it once here (not in OnConstruction)
    HeightCacheLoadingSet.Reset();
    for (TActorIterator<ALandscapeProxy> It(GetWorld()); It; ++It)
    {
        if (ALandscapeProxy* Landscape = *It)
        {
            HeightCacheLoadingSet.Add(Landscape);
            PopulateLandscapeHeightCache(Landscape); // consider yielding/throttling if this is big
        }
    }

    // Light voxel/grid prep if you absolutely need it for preview (otherwise leave to runtime)
    // FVoxelConversion::InitFromConfig(ChunkSize, Subdivisions, TerrainGridSize, GetActorLocation());

    bEditorInitDone = true;
#endif
}

void ADiggerManager::EnsureHoleShapeLibrary()
{
    if (IsValid(HoleShapeLibrary)) return;

    // Prefer your project asset if you have one (leave null to force transient)
    if (UHoleShapeLibrary* Loaded = LoadDefaultHoleLibrary())
    {
        HoleShapeLibrary = Loaded;
        SeedHoleShapesFromFolder(HoleShapeLibrary);
        return;
    }

    // Otherwise create a transient library so editor mode works immediately
    UHoleShapeLibrary* Transient = NewObject<UHoleShapeLibrary>(
        GetTransientPackage(),
        UHoleShapeLibrary::StaticClass(),
        FName(TEXT("TransientHoleShapeLibrary"))
    );
    HoleShapeLibrary = Transient;

    SeedHoleShapesFromFolder(HoleShapeLibrary);
}



void ADiggerManager::InitRuntimeHeavy()
{
    // Full system init (runtime/PIE only)
    // 1) Library is ensured already
    // 2) Landscape cache
    HeightCacheLoadingSet.Reset();
    for (TActorIterator<ALandscapeProxy> It(GetWorld()); It; ++It)
    {
        if (ALandscapeProxy* Landscape = *It)
        {
            HeightCacheLoadingSet.Add(Landscape);
            PopulateLandscapeHeightCache(Landscape);
        }
    }

    // 3) Voxel config & chunk systems
    FVoxelConversion::InitFromConfig(ChunkSize, Subdivisions, TerrainGridSize, GetActorLocation());

    // 4) Any threads/async, proc mesh setup, etc.
}


void ADiggerManager::DestroyAllHoleBPs()
{
    if (!HoleBP) return; // Ensure the reference is valid

    World = GetSafeWorld();
    if (!World) return;

    for (TActorIterator<AActor> It(World, HoleBP); It; ++It)
    {
        AActor* HoleActor = *It;
        if (HoleActor && IsValid(HoleActor))
        {
            HoleActor->Destroy();
        }
    }
}



void ADiggerManager::ClearHolesFromChunkMap()
{
    UE_LOG(LogTemp, Warning, TEXT("Clearing Spawned Holes From Chunk Grid:"));

    if (ChunkMap.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("No chunks in ChunkMap!"));
        return;
    }

    // Assuming you have a map or array of chunks
    for (auto& ChunkPair : ChunkMap)
    {
        UVoxelChunk* Chunk = ChunkPair.Value;
        if (Chunk)
        {
            Chunk->ClearSpawnedHoles();
        }
    }
}



UStaticMesh* ADiggerManager::CreateStaticMeshFromRawData(
    UObject* Outer,
    const FString& AssetName,
    const TArray<FVector3f>& Vertices,
    const TArray<int32>& Triangles,
    const TArray<FVector3f>& Normals,
    const TArray<FVector2d>& UVs,
    const TArray<FColor>& Colors,
    const TArray<FProcMeshTangent>& Tangents)
{
    // Validate input
    if (Vertices.Num() == 0 || Triangles.Num() == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("Invalid mesh data"));
        return nullptr;
    }

    // Create package
    FString PackageName = FString::Printf(TEXT("/Game/Islands/%s"), *AssetName);
    UPackage* MeshPackage = CreatePackage(*PackageName);
    
    // Create static mesh
    UStaticMesh* StaticMesh = NewObject<UStaticMesh>(MeshPackage, *AssetName, RF_Public | RF_Standalone);
    if (!StaticMesh) return nullptr;

    // Setup mesh description
    FMeshDescription MeshDescription;
    FStaticMeshAttributes Attributes(MeshDescription);
    Attributes.Register();

    // Create polygon group
    FPolygonGroupID PolyGroupID = MeshDescription.CreatePolygonGroup();

    // Add vertices
    TMap<int32, FVertexID> IndexToVertexID;
    for (int32 i = 0; i < Vertices.Num(); ++i)
    {
        FVertexID VertexID = MeshDescription.CreateVertex();
        Attributes.GetVertexPositions()[VertexID] = Vertices[i];
        IndexToVertexID.Add(i, VertexID);
    }

    // Add triangles with attributes
    for (int32 i = 0; i < Triangles.Num(); i += 3)
    {
        TArray<FVertexInstanceID> VertexInstanceIDs;
        
        for (int32 j = 0; j < 3; ++j)
        {
            int32 VertexIndex = Triangles[i + j];
            if (!IndexToVertexID.Contains(VertexIndex)) continue;

            FVertexInstanceID InstanceID = MeshDescription.CreateVertexInstance(IndexToVertexID[VertexIndex]);

            // Set all attributes
            if (Normals.IsValidIndex(VertexIndex))
                Attributes.GetVertexInstanceNormals()[InstanceID] = Normals[VertexIndex];

            if (UVs.IsValidIndex(VertexIndex))
                Attributes.GetVertexInstanceUVs().Set(InstanceID, 0, FVector2f(UVs[VertexIndex]));

            if (Colors.IsValidIndex(VertexIndex))
                Attributes.GetVertexInstanceColors()[InstanceID] = FVector4f(Colors[VertexIndex]);

            if (Tangents.IsValidIndex(VertexIndex))
            {
                Attributes.GetVertexInstanceTangents()[InstanceID] = (FVector3f)Tangents[VertexIndex].TangentX;
                Attributes.GetVertexInstanceBinormalSigns()[InstanceID] = Tangents[VertexIndex].bFlipTangentY ? -1.0f : 1.0f;
            }

            VertexInstanceIDs.Add(InstanceID);
        }

        if (VertexInstanceIDs.Num() == 3)
            MeshDescription.CreatePolygon(PolyGroupID, VertexInstanceIDs);
    }

#if WITH_EDITORONLY_DATA
    // Configure build settings
    StaticMesh->AddSourceModel();
    FStaticMeshSourceModel& SourceModel = StaticMesh->GetSourceModel(0);
    SourceModel.BuildSettings.bRecomputeNormals = false;
    SourceModel.BuildSettings.bRecomputeTangents = false;

    // Add default material if needed
    if (StaticMesh->GetStaticMaterials().Num() == 0)
    {
        StaticMesh->GetStaticMaterials().Add(FStaticMaterial());
    }

    // Build mesh
    StaticMesh->BuildFromMeshDescriptions({ &MeshDescription });
#endif

    // Finalize mesh
    StaticMesh->CommitMeshDescription(0);
    StaticMesh->Build(false);
    StaticMesh->PostEditChange();

    // Notify asset system
    FAssetRegistryModule::AssetCreated(StaticMesh);
    MeshPackage->MarkPackageDirty();

    return StaticMesh;
}

void ADiggerManager::ClearAllVoxelData()
{
    UE_LOG(LogTemp, Warning, TEXT("Clearing all voxel data..."));

    // // Clear the sparse voxel grid
    // if (SparseVoxelGrid)
    // {
    //     SparseVoxelGrid->Clear(); // Assumes you have a Clear() method in your grid class
    // }

    // Destroy all spawned voxel chunks (Actors or Components)
    // for (AActor* ChunkActor : SpawnedChunks)
    // {
    //     if (IsValid(ChunkActor))
    //     {
    //         ChunkActor->Destroy();
    //     }
    // }
    // SpawnedChunks.Empty();

    // Destroy any mesh components stored directly on the DiggerManager
    ClearProceduralMeshes();

    // Reset any saved brush stroke data or undo queues
    //UndoQueue.Empty(); // if you have one
    // StrokeHistory.Empty(); // if applicable

    UE_LOG(LogTemp, Warning, TEXT("All voxel data cleared."));
}

void ADiggerManager::ClearProceduralMeshes()
{
        if (ProceduralMesh)
        {
            ProceduralMesh->ClearAllMeshSections();
            ProceduralMesh->MarkRenderStateDirty(); // Optional: forces update to visual
            UE_LOG(LogTemp, Warning, TEXT("Procedural mesh cleared."));
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("No ProceduralMesh found to clear."));
        }
}


void ADiggerManager::BakeToStaticMesh(bool bEnableCollision, bool bEnableNanite, float DetailReduction)
{
    UE_LOG(LogTemp, Warning, TEXT("BakeToStaticMesh called with:\n  Collision: %s\n  Nanite: %s\n  DetailReduction: %.2f"),
        bEnableCollision ? TEXT("True") : TEXT("False"),
        bEnableNanite ? TEXT("True") : TEXT("False"),
        DetailReduction);

/*#if WITH_EDITOR
    FString AssetName = FString::Printf(TEXT("SM_Baked_%s_%s_%.2f"),
        bEnableCollision ? TEXT("Col") : TEXT("NoCol"),
        bEnableNanite ? TEXT("Nanite") : TEXT("NoNanite"),
        DetailReduction);

    UStaticMesh* NewMesh = CreateStaticMeshFromProceduralMesh(
        GetTransientPackage(), // Or another suitable Outer
        AssetName,
        MyProceduralMeshComponent // <-- Replace with your actual component
    );

    if (NewMesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("Static mesh created: %s"), *AssetName);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create static mesh!"));
    }
#endif*/
}




void ADiggerManager::UpdateVoxelSize()
{
    if (Subdivisions != 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("Initializing FVoxelConversion in PIE BeginPlay"));
        FVoxelConversion::InitFromConfig(ChunkSize, Subdivisions, TerrainGridSize, FVector::ZeroVector);
        VoxelSize = FVoxelConversion::LocalVoxelSize;
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("Subdivisions value is zero. VoxelSize will not be updated to avoid division by zero."));
        VoxelSize = 1.0f; // Default fallback value
    }
}

void ADiggerManager::InitializeChunks()
{
    for (const auto& ChunkPair : ChunkMap)
    {
        const FIntVector& Coordinates = ChunkPair.Key;
        UVoxelChunk* NewChunk = NewObject<UVoxelChunk>(this);
        if (!NewChunk)
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to create a new chunk for coordinates: %s"), *Coordinates.ToString());
            continue;
        }

        UProceduralMeshComponent* NewMeshComponent = NewObject<UProceduralMeshComponent>(this);
        if (NewMeshComponent)
        {
            NewMeshComponent->RegisterComponent();
            NewMeshComponent->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
            NewChunk->InitializeMeshComponent(NewMeshComponent);
            ChunkMap.Add(Coordinates, NewChunk);
            ProceduralMeshComponents.Add(NewMeshComponent);
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to create a new ProceduralMeshComponent for coordinates: %s"), *Coordinates.ToString());
        }
    }
}

void ADiggerManager::InitializeSingleChunk(UVoxelChunk* Chunk)
{
    if (!Chunk)
    {
        UE_LOG(LogTemp, Error, TEXT("InitializeSingleChunk called with a null chunk."));
        return;
    }

    FIntVector Coordinates = Chunk->GetChunkCoordinates();
    UProceduralMeshComponent* NewMeshComponent = NewObject<UProceduralMeshComponent>(this);
    if (NewMeshComponent)
    {
        NewMeshComponent->RegisterComponent();
        NewMeshComponent->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
        Chunk->InitializeMeshComponent(NewMeshComponent);
        ChunkMap.Add(Coordinates, Chunk);
        ProceduralMeshComponents.Add(NewMeshComponent);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create a ProceduralMeshComponent for chunk at coordinates: %s"), *Coordinates.ToString());
    }
}

void ADiggerManager::UpdateLandscapeProxies()
{
    CachedLandscapeProxies.Empty();
    TArray<AActor*> FoundLandscapes;
    UGameplayStatics::GetAllActorsOfClass(GetSafeWorld(), ALandscapeProxy::StaticClass(), FoundLandscapes);

    for (AActor* Actor : FoundLandscapes)
    {
        if (ALandscapeProxy* Proxy = Cast<ALandscapeProxy>(Actor))
        {
            CachedLandscapeProxies.Add(Proxy);
        }
    }
}

TSharedPtr<TMap<FIntPoint, float>> ADiggerManager::GetOrCreateLandscapeHeightCache(ALandscapeProxy* Landscape)
{
    if (!IsValid(Landscape))
    {
        UE_LOG(LogTemp, Warning, TEXT("Invalid LandscapeProxy passed to GetOrCreateLandscapeHeightCache."));
        return nullptr;
    }

    if (!LandscapeHeightCaches.Contains(Landscape))
    {
        LandscapeHeightCaches.Add(Landscape, MakeShared<TMap<FIntPoint, float>>());
    }

    return LandscapeHeightCaches[Landscape];
}


float ADiggerManager::GetLandscapeHeightAt(FVector WorldPosition)
{
    ALandscapeProxy* LandscapeProxy = nullptr;

    // First, try the last used landscape if it's still valid
    if (LastUsedLandscape && IsValid(LastUsedLandscape) && 
        LastUsedLandscape->GetComponentsBoundingBox().IsInsideXY(WorldPosition))
    {
        LandscapeProxy = LastUsedLandscape;
    }
    else
    {
        // Use the more reliable iterator-based search
        LandscapeProxy = GetLandscapeProxyAt(WorldPosition);
        
        // Update the cache with the found proxy
        if (LandscapeProxy)
        {
            LastUsedLandscape = LandscapeProxy;
        }
    }

    if (!LandscapeProxy)
    {
        if (DiggerDebug::Landscape)
        UE_LOG(LogTemp, Warning, TEXT("No landscape found at location %s"), *WorldPosition.ToString());
        return -100000.0f;
    }

    // Try getting height using different sources in order of complexity
    TOptional<float> HeightResult;

    HeightResult = LandscapeProxy->GetHeightAtLocation(WorldPosition, EHeightfieldSource::Simple);

    if (!HeightResult.IsSet())
    {
        HeightResult = LandscapeProxy->GetHeightAtLocation(WorldPosition, EHeightfieldSource::Complex);
    }

    if (!HeightResult.IsSet() && GIsEditor)
    {
        HeightResult = LandscapeProxy->GetHeightAtLocation(WorldPosition, EHeightfieldSource::Editor);
    }

    if (!HeightResult.IsSet())
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to get height at location: %s for landscape: %s"), 
               *WorldPosition.ToString(), 
               LandscapeProxy ? *LandscapeProxy->GetName() : TEXT("NULL"));
        return -100000.0f;
    }

    return HeightResult.GetValue();
}

// Keep your more reliable GetLandscapeProxyAt function
ALandscapeProxy* ADiggerManager::GetLandscapeProxyAt(const FVector& WorldPos)
{
    for (TActorIterator<ALandscapeProxy> It(GetSafeWorld()); It; ++It)
    {
        ALandscapeProxy* Proxy = *It;
        if (Proxy && IsValid(Proxy) && Proxy->GetComponentsBoundingBox().IsInsideXY(WorldPos))
        {
            return Proxy;
        }
    }

    return nullptr;
}

void ADiggerManager::PopulateLandscapeHeightCache(ALandscapeProxy* Landscape)
{
    if (!Landscape) return;

    const float LocalVoxelSize = VoxelSize;
    FBox Bounds = Landscape->GetComponentsBoundingBox();

    TMap<FIntPoint, float> LocalMap;

    FVector Min = Bounds.Min;
    FVector Max = Bounds.Max;

    for (float X = Min.X; X < Max.X; X += LocalVoxelSize)
    {
        for (float Y = Min.Y; Y < Max.Y; Y += LocalVoxelSize)
        {
            FVector SamplePos(X, Y, 0);
            TOptional<float> Sampled = SampleLandscapeHeight(Landscape, SamplePos);
            if (Sampled.IsSet())
            {
                int32 GridX = FMath::FloorToInt(X / LocalVoxelSize);
                int32 GridY = FMath::FloorToInt(Y / LocalVoxelSize);
                FIntPoint Key(GridX, GridY);
                LocalMap.Add(Key, Sampled.GetValue());
            }
        }
    }

    LandscapeHeightCaches.FindOrAdd(Landscape) = MakeShared<TMap<FIntPoint, float>>(LocalMap);
    HeightCacheLoadingSet.Remove(Landscape);

    UE_LOG(LogTemp, Warning, TEXT("Synchronous height cache complete for landscape: %s (%d entries)"), *Landscape->GetName(), LocalMap.Num());
}

void ADiggerManager::PopulateLandscapeHeightCacheAsync(ALandscapeProxy* Landscape)
{
    if (!Landscape) return;

    const float LocalVoxelSize = VoxelSize;
    FBox Bounds = Landscape->GetComponentsBoundingBox();

    // Prepare data for async task
    FVector Min = Bounds.Min;
    FVector Max = Bounds.Max;
    TWeakObjectPtr<ALandscapeProxy> WeakLandscape = Landscape;
    TWeakObjectPtr<ADiggerManager> WeakSelf = this;

    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [WeakSelf, WeakLandscape, Min, Max, LocalVoxelSize]()
    {
        if (!WeakSelf.IsValid() || !WeakLandscape.IsValid()) return;

        TMap<FIntPoint, float> LocalMap;

        for (float X = Min.X; X < Max.X; X += LocalVoxelSize)
        {
            for (float Y = Min.Y; Y < Max.Y; Y += LocalVoxelSize)
            {
                FVector SamplePos(X, Y, 0);
                TOptional<float> Sampled = WeakSelf->SampleLandscapeHeight(WeakLandscape.Get(), SamplePos);
                if (Sampled.IsSet())
                {
                    int32 GridX = FMath::FloorToInt(X / LocalVoxelSize);
                    int32 GridY = FMath::FloorToInt(Y / LocalVoxelSize);
                    FIntPoint Key(GridX, GridY);
                    LocalMap.Add(Key, Sampled.GetValue());
                }
            }
        }

        // Copy back on game thread
        AsyncTask(ENamedThreads::GameThread, [WeakSelf, WeakLandscape, LocalMap = MoveTemp(LocalMap)]()
        {
            if (!WeakSelf.IsValid() || !WeakLandscape.IsValid()) return;

            WeakSelf->LandscapeHeightCaches.FindOrAdd(WeakLandscape.Get()) = MakeShared<TMap<FIntPoint, float>>(LocalMap);
            WeakSelf->HeightCacheLoadingSet.Remove(WeakLandscape.Get());

            UE_LOG(LogTemp, Warning, TEXT("Async height cache complete for landscape: %s (%d entries)"), *WeakLandscape->GetName(), LocalMap.Num());
        });
    });
}





/*ALandscapeProxy* ADiggerManager::GetLandscapeProxyAt(const FVector& WorldPos)
{
    for (TActorIterator<ALandscapeProxy> It(GetSafeWorld()); It; ++It)
    {
        ALandscapeProxy* Proxy = *It;
        if (Proxy && Proxy->GetComponentsBoundingBox().IsInsideXY(WorldPos))
        {
            return Proxy;
        }
    }

    return nullptr;
}*/

TOptional<float> ADiggerManager::SampleLandscapeHeight(ALandscapeProxy* Landscape, const FVector& WorldPos, bool bForcePrecise)
{
    if (!Landscape)
    {
        if (DiggerDebug::Landscape)
        {UE_LOG(LogTemp, Warning, TEXT("Landscape Proxy is NULL for position: %s"), *WorldPos.ToString());}
        return TOptional<float>(); // Early exit if there's no valid landscape
    }

    // Sample terrain height using Landscape's method
    TOptional<float> SampledHeight = Landscape->GetHeightAtLocation(WorldPos);

    if (SampledHeight.IsSet())
    {
        if (DiggerDebug::Landscape)
        {
            // Log if the sampled height is successful
            UE_LOG(LogTemp, Log, TEXT("Successfully sampled terrain height at %s: %.2f"), *WorldPos.ToString(), SampledHeight.GetValue());
        }
        return SampledHeight;
    }
    else
    {
        if (DiggerDebug::Landscape)
        {
            // Log if the height sampling failed
            UE_LOG(LogTemp, Warning, TEXT("Failed to sample terrain height at %s"), *WorldPos.ToString());
        }
    }

    // If no valid height was sampled, return empty
    return TOptional<float>(); // This can be adjusted to return a default value if needed
}


/* //Deprecated diggerManager height cach version of SampleLandscapeHeight
TOptional<float> ADiggerManager::SampleLandscapeHeight(ALandscapeProxy* Landscape, const FVector& WorldPos)
{
    // Null check for Landscape
    if (!Landscape || !IsValid(Landscape))
    {
        UE_LOG(LogTemp, Warning, TEXT("SampleLandscapeHeight: Invalid Landscape pointer"));
        return TOptional<float>();
    }

    // Check if we have a valid cache for the landscape height
    if (LandscapeHeightCaches.Contains(Landscape))
    {
        // SAFE ACCESS: Get the pointer first and null check it
        TSharedPtr<TMap<FIntPoint, float>>* CachePtr = LandscapeHeightCaches.Find(Landscape);
        if (CachePtr && CachePtr->IsValid())
        {
            const FVector LandscapeLocal = Landscape->GetTransform().InverseTransformPosition(WorldPos);
            int32 X = FMath::FloorToInt(LandscapeLocal.X / VoxelSize);
            int32 Y = FMath::FloorToInt(LandscapeLocal.Y / VoxelSize);

            TMap<FIntPoint, float>& CachedHeights = **CachePtr;
            FIntPoint Key(X, Y);

            if (CachedHeights.Contains(Key))
            {
                return CachedHeights[Key];
            }
        }
    }

    // If cache is not available, fall back to sampling height directly
    TOptional<float> SampledHeight = Landscape->GetHeightAtLocation(WorldPos);
    
    if (SampledHeight.IsSet())
    {
        // If sampled height is valid, cache it and return
        if (LandscapeHeightCaches.Contains(Landscape))
        {
            TSharedPtr<TMap<FIntPoint, float>>* CachePtr = LandscapeHeightCaches.Find(Landscape);
            if (CachePtr && CachePtr->IsValid())
            {
                FVector LandscapeLocal = Landscape->GetTransform().InverseTransformPosition(WorldPos);
                int32 X = FMath::FloorToInt(LandscapeLocal.X / VoxelSize);
                int32 Y = FMath::FloorToInt(LandscapeLocal.Y / VoxelSize);

                TMap<FIntPoint, float>& CachedHeights = **CachePtr;
                FIntPoint Key(X, Y);
                CachedHeights.Add(Key, SampledHeight.GetValue());
            }
        }

        return SampledHeight;
    }

    // If sampling from the terrain fails, return a fallback value
    UE_LOG(LogTemp, Warning, TEXT("Failed to sample terrain height at %s"), *WorldPos.ToString());
    return TOptional<float>();
}
*/
// Currently used precise Height Sampling Method
TOptional<float> ADiggerManager::SampleLandscapeHeight(ALandscapeProxy* Landscape, const FVector& WorldPos)
{
    // Null check for Landscape
    if (!Landscape || !IsValid(Landscape))
    {
        UE_LOG(LogTemp, Warning, TEXT("SampleLandscapeHeight: Invalid Landscape pointer"));
        return TOptional<float>();
    }

    // Always use direct sampling - bypass cache completely
    TOptional<float> SampledHeight = Landscape->GetHeightAtLocation(WorldPos);
    
    if (SampledHeight.IsSet())
    {
        return SampledHeight;
    }

    // If sampling from the terrain fails, return a fallback value
    UE_LOG(LogTemp, Warning, TEXT("Failed to sample terrain height at %s"), *WorldPos.ToString());
    return TOptional<float>();
}

// Delete this please after it works well!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// In DiggerManager.cpp  
void ADiggerManager::QuickDebugTest()
{
    UE_LOG(LogTemp, Warning, TEXT("Testing Fast Debug Renderer..."));
    
    // Test single shapes
    FAST_DEBUG_BOX(FVector(0, 0, 200), FVector(100), FLinearColor::Red);
    FAST_DEBUG_SPHERE(FVector(300, 0, 200), 75.0f, FLinearColor::Green);
    
    // Test batch rendering (the real performance boost)
    TArray<FVector> TestLocations;
    for (int32 i = 0; i < 100; i++)
    {
        TestLocations.Add(FVector(i * 50, 0, 100));
    }
    
    FAST_DEBUG_BOXES_BATCH(TestLocations, FVector(25), FLinearColor::Blue);
    
    UE_LOG(LogTemp, Warning, TEXT("Fast Debug Test Complete - Should see red box, green sphere, and 100 blue boxes"));
}


float ADiggerManager::GetSmartLandscapeHeightAt(const FVector& WorldPos)
{
    return GetSmartLandscapeHeightAt(WorldPos, false);
}

float ADiggerManager::GetSmartLandscapeHeightAt(const FVector& WorldPos, bool bForcePrecise)
{
    //const float HeightTolerance = VoxelSize * 0.5f; // Allowable vertical margin for refinement

   // float CachedHeight = GetCachedLandscapeHeightAt(WorldPos);

   // if (bForcePrecise)
    //{
        return GetLandscapeHeightAt(WorldPos);
   /* }

    // Compare Z difference (height) only, not full vector
    float VerticalDifference = FMath::Abs(WorldPos.Z - CachedHeight);

    if (VerticalDifference <= HeightTolerance)
    {
        // We're close enough â€” use precise sample
        return GetLandscapeHeightAt(WorldPos);
    }

    return CachedHeight;*/
}



bool ADiggerManager::GetHeightAtLocation(ALandscapeProxy* LandscapeProxy, const FVector& Location, float& OutHeight)
{
    if (!LandscapeProxy || !LandscapeProxy->GetLandscapeInfo())
    {
        return false;
    }

    // Convert world location to landscape local space
    FVector LocalPosition = LandscapeProxy->GetTransform().InverseTransformPosition(Location);

    // Try getting height using different sources
    TOptional<float> HeightResult = LandscapeProxy->GetHeightAtLocation(LocalPosition, EHeightfieldSource::Simple);

    if (!HeightResult.IsSet())
    {
        HeightResult = LandscapeProxy->GetHeightAtLocation(LocalPosition, EHeightfieldSource::Complex);
    }

    if (HeightResult.IsSet())
    {
        // Convert the height to world space
        OutHeight = static_cast<float>(HeightResult.GetValue() * LandscapeProxy->GetActorScale3D().Z +
            LandscapeProxy->GetActorLocation().Z);
        return true;
    }

    return false;
}


FVector ADiggerManager::GetLandscapeNormalAt(const FVector& WorldPosition)
{
    // Get all landscape actors in the level
    TArray<AActor*> FoundLandscapes;
    UGameplayStatics::GetAllActorsOfClass(GetSafeWorld(), ALandscapeProxy::StaticClass(), FoundLandscapes);

    if (FoundLandscapes.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("No landscape found in level"));
        return FVector::UpVector;
    }

    ALandscapeProxy* LandscapeProxy = Cast<ALandscapeProxy>(FoundLandscapes[0]);
    if (!LandscapeProxy)
    {
        UE_LOG(LogTemp, Warning, TEXT("Failed to cast landscape actor"));
        return FVector::UpVector;
    }

    // Use a small offset for sampling (in cm)
    const float Delta = 5.0f;

    FVector PosX = WorldPosition + FVector(Delta, 0, 0);
    FVector NegX = WorldPosition - FVector(Delta, 0, 0);
    FVector PosY = WorldPosition + FVector(0, Delta, 0);
    FVector NegY = WorldPosition - FVector(0, Delta, 0);

    float HeightX1 = GetLandscapeHeightAt(PosX);
    float HeightX0 = GetLandscapeHeightAt(NegX);
    float HeightY1 = GetLandscapeHeightAt(PosY);
    float HeightY0 = GetLandscapeHeightAt(NegY);

    // Calculate gradient
    FVector Gradient;
    Gradient.X = (HeightX1 - HeightX0) / (2 * Delta);
    Gradient.Y = (HeightY1 - HeightY0) / (2 * Delta);
    Gradient.Z = 1.0f;

    // The normal is the inverse of the gradient in X and Y, with Z up
    FVector Normal = FVector(-Gradient.X, -Gradient.Y, 1.0f).GetSafeNormal();
    return Normal;
}


UWorld* ADiggerManager::GetSafeWorld() const
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

#if WITH_EDITOR
void ADiggerManager::EnsureDefaultHoleBP()
{
    if (!HoleBP)
    {
        FSoftObjectPath AssetPath(GDefaultHoleBPPath);
        UObject* LoadedObj = AssetPath.TryLoad();

        if (UClass* LoadedClass = Cast<UClass>(LoadedObj))
        {
            HoleBP = LoadedClass;
            UE_LOG(LogTemp, Log, TEXT("Loaded Default HoleBP from %s"), GDefaultHoleBPPath);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Failed to load HoleBP from %s"), GDefaultHoleBPPath);
        }
    }
}
#endif

void ADiggerManager::HandleHoleSpawn(const FBrushStroke& Stroke)
{
    if (!HoleBP)
    {
        EnsureDefaultHoleBP();
        if (!HoleBP)
        {
            UE_LOG(LogTemp, Error, TEXT("HoleBP or ActiveBrush is null"));
            return;
        }
    }

    if (!ActiveBrush)
    {
#if WITH_EDITOR
        // Active Brush is only used in PIE
        // Do anything we may need to do right here for the editor brush stuff.
#else
        if (GetWorld() && GetWorld()->IsPlayInEditor())
        {
            if (!ActiveBrush)
            {
                ActiveBrush = CreateDefaultSubobject<UVoxelBrushShape>(TEXT("ActiveBrush"));
                
                // Ensure brushes are ready forusage
                ActiveBrush->InitializeBrush(ActiveBrush->GetBrushType(),ActiveBrush->GetBrushSize(),ActiveBrush->GetBrushLocation(),this);
                InitializeBrushShapes();
            }
        }

        if (!ActiveBrush)
            return;
#endif
    }

    FVector SpawnLocation = Stroke.BrushPosition;
    FRotator SpawnRotation = Stroke.BrushRotation;
    FVector SpawnScale = FVector(Stroke.BrushRadius / 40.0f);

    // Early out if subterranean
    if (GetLandscapeHeightAt(SpawnLocation) > SpawnLocation.Z + Stroke.BrushRadius * 0.6f)
    {
        if (DiggerDebug::Casts || DiggerDebug::Holes)
        UE_LOG(LogTemp, Warning, TEXT("Subterranean hit at %s, not spawning hole."), *SpawnLocation.ToString());
        return;
    }

    if (SpawnLocation.IsNearlyZero())
    {
        SpawnLocation.Z = 100.f;
    }

    FHitResult HitResult;
    if (ActiveBrush->GetCameraHitLocation(HitResult))
    {

        AActor* HitActor = HitResult.GetActor();
        if (!HitActor)
        {
            if (DiggerDebug::Casts || DiggerDebug::Holes)
            UE_LOG(LogTemp, Warning, TEXT("No actor hit, not spawning hole."));
            return;
        }
        if (!ActiveBrush->IsLandscape(HitActor))
        {
            if (DiggerDebug::Casts || DiggerDebug::Holes)
            UE_LOG(LogTemp, Warning, TEXT("Hit actor is not landscape (is %s), not spawning hole."), *HitActor->GetName());
            return;
        }

        FVector SafeNormal = HitResult.ImpactNormal.GetSafeNormal();
        if (SafeNormal.IsNearlyZero() || FMath::Abs(FVector::DotProduct(SafeNormal, FVector::UpVector)) < 0.1f)
        {
            SafeNormal = FVector::UpVector;
        }

        SpawnRotation = FRotationMatrix::MakeFromZ(SafeNormal).Rotator();
        SpawnLocation = HitResult.Location;
    }

    // Find which chunk owns this location
    UVoxelChunk* TargetChunk = GetOrCreateChunkAtWorld(SpawnLocation);

    if (TargetChunk)
    {
        TargetChunk->SaveHoleData(SpawnLocation, SpawnRotation, SpawnScale);
        TargetChunk->SpawnHoleFromData(FSpawnedHoleData(SpawnLocation, SpawnRotation, SpawnScale, Stroke.HoleShape));
        if (DiggerDebug::Holes)
        UE_LOG(LogTemp, Log, TEXT("Delegated hole spawn to chunk at location %s"), *SpawnLocation.ToString());
    }
    else
    {
        if (DiggerDebug::Chunks || DiggerDebug::Holes)
        UE_LOG(LogTemp, Error, TEXT("No chunk found at location %s"), *SpawnLocation.ToString());
    }
}





void ADiggerManager::DuplicateLandscape(ALandscapeProxy* Landscape)
{
    if (!Landscape) return;

    ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
    if (!LandscapeInfo) return;

    // Get landscape extent
    int32 LandscapeMinX, LandscapeMinY, LandscapeMaxX, LandscapeMaxY;
    LandscapeInfo->GetLandscapeExtent(LandscapeMinX, LandscapeMinY, LandscapeMaxX, LandscapeMaxY);

    // Create data access interface
    FLandscapeEditDataInterface LandscapeData(LandscapeInfo);

    int32 ChunkVoxelCount = ChunkSize / VoxelSize;

    // Get landscape transform for proper world position calculation
    FTransform LandscapeTransform = Landscape->GetActorTransform();
    float LandscapeScale = LandscapeTransform.GetScale3D().Z;

    // Iterate over the landscape extent in chunks
    for (int32 Y = LandscapeMinY; Y <= LandscapeMaxY; Y += ChunkVoxelCount)
    {
        for (int32 X = LandscapeMinX; X <= LandscapeMaxX; X += ChunkVoxelCount)
        {
            FIntVector ChunkPosition(X, Y, 0);
            UVoxelChunk* Chunk = GetOrCreateChunkAtChunk(ChunkPosition);

            if (Chunk)
            {
                // Calculate chunk boundaries in landscape space
                int32 ChunkEndX = FMath::Min(X + ChunkVoxelCount, LandscapeMaxX);
                int32 ChunkEndY = FMath::Min(Y + ChunkVoxelCount, LandscapeMaxY);

                // Get height data for the entire chunk area
                TMap<FIntPoint, uint16> HeightData;
                int32 StartX = X;
                int32 StartY = Y;
                int32 EndX = ChunkEndX;
                int32 EndY = ChunkEndY;
                LandscapeData.GetHeightData(StartX, StartY, EndX, EndY, HeightData);

                for (int32 VoxelY = 0; VoxelY < ChunkVoxelCount; ++VoxelY)
                {
                    for (int32 VoxelX = 0; VoxelX < ChunkVoxelCount; ++VoxelX)
                    {
                        int32 LandscapeX = X + VoxelX;
                        int32 LandscapeY = Y + VoxelY;

                        // Skip if we're outside landscape bounds
                        if (LandscapeX > LandscapeMaxX || LandscapeY > LandscapeMaxY)
                            continue;

                        // Get height value from the height data map
                        FIntPoint Key(LandscapeX, LandscapeY);
                        float Height = 0.0f;

                        if (const uint16* HeightValue = HeightData.Find(Key))
                        {
                            // Convert from uint16 to world space height
                            // LandscapeDataAccess.h: Landscape uses USHRT_MAX as "32768" = 0.0f world space
                            const float ScaleFactor = LandscapeScale / 128.0f; // Landscape units to world units
                            Height = ((float)*HeightValue - 32768.0f) * ScaleFactor;
                            Height += LandscapeTransform.GetLocation().Z; // Add landscape base height
                        }

                        for (int32 VoxelZ = 0; VoxelZ < ChunkVoxelCount; ++VoxelZ)
                        {
                            // Convert voxel coordinates to world space
                            FVector WorldPosition = FVoxelConversion::GlobalVoxelCenterToWorld(FIntVector(VoxelX, VoxelY, VoxelZ));

                            // Calculate SDF value (distance from surface)
                            // Positive values are above the surface, negative values are below
                            float SDFValue = WorldPosition.Z - Height;

                            // Optional: Normalize SDF value by voxel size
                            SDFValue /= VoxelSize;

                            // Set the voxel value
                            bool bDig = false;
                            Chunk->SetVoxel(VoxelX, VoxelY, VoxelZ, SDFValue, bDig);
                        }
                    }
                }

                // Mark the chunk for update
                Chunk->MarkDirty();
            }
        }
    }
}


void ADiggerManager::DebugLogVoxelChunkGrid() const
{
    UE_LOG(LogTemp, Warning, TEXT("Logging Voxel Chunk Grid:"));

    if (ChunkMap.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("No chunks in ChunkMap!"));
        return;
    }

    // Assuming you have a map or array of chunks
    for (auto& ChunkPair : ChunkMap)
    {
        UVoxelChunk* Chunk = ChunkPair.Value;
        if (Chunk)
        {
            Chunk->DebugDrawChunk();
        }
    }
}

void ADiggerManager::DebugVoxels()
{
    if (!ChunkMap.IsEmpty())
    {
        DebugDrawChunkSectionIDs();
        DebugLogVoxelChunkGrid();
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("No chunks available for rendering."));
    }
}

void ADiggerManager::ProcessDirtyChunks()
{
    // for (auto& Elem : ChunkMap)
    // {
    //     UVoxelChunk* Chunk = Elem.Value;
    //     if (Chunk)
    //     {
    //         Chunk->ForceUpdate();
    //     }
    // }

    UpdateAllDirtyChunks();

    /*if (World)
    {
        AsyncTask(ENamedThreads::GameThread, [this]()
        {
            if (World)
            {
                World->GetTimerManager().ClearTimer(ChunkProcessTimerHandle);
                World->GetTimerManager().SetTimer(ChunkProcessTimerHandle, this, &ADiggerManager::ProcessDirtyChunks, 2.0f, true);
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("World is null when resetting the ProcessDirtyChunks timer."));
            }
        });
    }*/
}



int32 ADiggerManager::GetHitSectionIndex(const FHitResult& HitResult)
{
    if (!HitResult.GetComponent()) return -1;

    UProceduralMeshComponent* HitProceduralMesh = Cast<UProceduralMeshComponent>(HitResult.GetComponent());
    if (!HitProceduralMesh) return -1;

    const FVector HitLocation = HitResult.ImpactPoint;

    for (int32 SectionIndex = 0; SectionIndex < HitProceduralMesh->GetNumSections(); ++SectionIndex)
    {
        const FProcMeshSection* Section = HitProceduralMesh->GetProcMeshSection(SectionIndex);
        if (!Section || Section->ProcVertexBuffer.Num() == 0) continue;

        FBox SectionBox(Section->ProcVertexBuffer[0].Position, Section->ProcVertexBuffer[0].Position);
        for (const FProcMeshVertex& Vertex : Section->ProcVertexBuffer)
        {
            SectionBox += Vertex.Position;
        }

        if (SectionBox.IsInside(HitLocation))
        {
            return SectionIndex;
        }
    }

    return -1; // Section not found
}

UVoxelChunk* ADiggerManager::GetChunkBySectionIndex(int32 SectionIndex)
{
    for (auto& Entry : ChunkMap)
    {
        if (Entry.Value->GetSectionIndex() == SectionIndex)
        {
            return Entry.Value;
        }
    }

    return nullptr; // Chunk not found
}

void ADiggerManager::UpdateChunkFromSectionIndex(const FHitResult& HitResult)
{
    int32 SectionIndex = GetHitSectionIndex(HitResult);
    if (SectionIndex == -1) return;

    UVoxelChunk* HitChunk = GetChunkBySectionIndex(SectionIndex);
    if (HitChunk)
    {
        HitChunk->MarkDirty();
    }
}

void ADiggerManager::DebugDrawChunkSectionIDs()
{
    for (auto& Entry : ChunkMap)
    {
        UVoxelChunk* Chunk = Entry.Value;
        if (Chunk)
        {
            Chunk->GetSparseVoxelGrid()->RenderVoxels();
            const FVector ChunkMin = FVoxelConversion::ChunkToWorld_Min(Chunk->GetChunkCoordinates());
            const FVector ChunkPosition = ChunkMin + FVector(FVoxelConversion::ChunkWorldSize() * 0.5f);
            const FString SectionIDText = FString::Printf(TEXT("ID: %d"), Chunk->GetSectionIndex());
            DrawDebugString(GetSafeWorld(), ChunkPosition, SectionIDText, nullptr, FColor::Green, 5.0f, true);
        }
    }
}

void ADiggerManager::ApplyBrush()
{
    if (!ActiveBrush)
    {
        UE_LOG(LogTemp, Error, TEXT("ActiveBrush is null. Cannot apply brush."));
        return;
    }

    FVector BrushPosition = ActiveBrush->GetBrushLocation();
    float BrushRadius = ActiveBrush->GetBrushSize();
    FBrushStroke BrushStroke;
    BrushStroke.BrushPosition = BrushPosition;
    BrushStroke.BrushRadius = BrushRadius;
    BrushStroke.bDig = ActiveBrush->GetDig();
    BrushStroke.BrushType = ActiveBrush->GetBrushType();

    ApplyBrushToAllChunks(BrushStroke);


    if (BrushStroke.bDig)
    {
        MarkNearbyChunksDirty(BrushPosition, BrushRadius);
    }
}

UVoxelChunk* ADiggerManager::FindOrCreateNearestChunk(const FVector& Position)
{
    FIntVector ChunkCoords = FVoxelConversion::WorldToChunk_Min(Position);
    UVoxelChunk** ExistingChunk = ChunkMap.Find(ChunkCoords);
    if (ExistingChunk && *ExistingChunk)
    {
        UE_LOG(LogTemp, Warning, TEXT("Chunk found at %s"), *ChunkCoords.ToString());
        return *ExistingChunk;
    }

    
    // Find nearest chunk if no exact match found
    UVoxelChunk* NearestChunk = nullptr;
    float MinDistance = FLT_MAX;

    for (auto& Entry : ChunkMap)
    {
        UVoxelChunk* Chunk = Entry.Value;
        if (!Chunk) continue;

        const FVector WorldPos = FVoxelConversion::ChunkToWorld_Min(Chunk->GetChunkCoordinates()) +
                                FVector(FVoxelConversion::ChunkWorldSize() * 0.5f);
        float Distance = FVector::Dist(Position, WorldPos);

        if (Distance < MinDistance)
        {
            MinDistance = Distance;
            NearestChunk = Chunk;
        }
    }


    // If no nearby chunk is found, create a new one
    if (!NearestChunk)
    {
        UE_LOG(LogTemp, Warning, TEXT("[FindOrCreateNearestChunk] Input Position (World): %s"), *Position.ToString());
        ChunkCoords = FVoxelConversion::WorldToChunk_Min(Position);
        UE_LOG(LogTemp, Warning, TEXT("[FindOrCreateNearestChunk] ChunkCoords: %s"), *ChunkCoords.ToString());

        
        UVoxelChunk* NewChunk = GetOrCreateChunkAtCoordinates(ChunkCoords.X, ChunkCoords.Y, ChunkCoords.Z);
        if (NewChunk)
        {
            NewChunk->InitializeChunk(ChunkCoords, this);
            ChunkMap.Add(ChunkCoords, NewChunk);
            return NewChunk;
        }
    }

    return NearestChunk;
}

UVoxelChunk* ADiggerManager::FindNearestChunk(const FVector& Position)
{
    FIntVector ChunkCoords = FVoxelConversion::WorldToChunk_Min(Position);
    UVoxelChunk** ExistingChunk = ChunkMap.Find(ChunkCoords);
    if (ExistingChunk && *ExistingChunk)
    {
        return *ExistingChunk;
    }

    // Find nearest chunk if no exact match found
    UVoxelChunk* NearestChunk = nullptr;
    float MinDistance = FLT_MAX;
    for (auto& Entry : ChunkMap)
    {
        const FVector WorldPos = FVoxelConversion::ChunkToWorld_Min(Entry.Value->GetChunkCoordinates()) +
                                FVector(FVoxelConversion::ChunkWorldSize() * 0.5f);
        float Distance = FVector::Dist(Position, WorldPos);
        if (Distance < MinDistance)
        {
            MinDistance = Distance;
            NearestChunk = Entry.Value;
        }
    }


    return NearestChunk;
}


void ADiggerManager::MarkNearbyChunksDirty(const FVector& CenterPosition, float Radius)
{
    int32 Reach = FMath::CeilToInt(Radius / FVoxelConversion::ChunkWorldSize());

    UE_LOG(LogTemp, Warning, TEXT("MarkNearbyChunksDirty: CenterPosition=%s, Radius=%f"), *CenterPosition.ToString(), Radius);
    UE_LOG(LogTemp, Warning, TEXT("FVoxelConversion: ChunkSize=%d, TerrainGridSize=%f, Origin=%s"), FVoxelConversion::ChunkSize, FVoxelConversion::TerrainGridSize, *FVoxelConversion::Origin.ToString());
    FIntVector CenterChunkCoords = FVoxelConversion::WorldToChunk_Min(CenterPosition);
    UE_LOG(LogTemp, Warning, TEXT("WorldToChunk: CenterPosition=%s -> CenterChunkCoords=%s"), *CenterPosition.ToString(), *CenterChunkCoords.ToString());


    for (int32 X = -Reach; X <= Reach; ++X)
    {
        for (int32 Y = -Reach; Y <= Reach; ++Y)
        {
            for (int32 Z = -Reach; Z <= Reach; ++Z)
            {
                FIntVector NearbyChunkCoords = CenterChunkCoords + FIntVector(X, Y, Z);
                UVoxelChunk** NearbyChunk = ChunkMap.Find(NearbyChunkCoords);
                if (NearbyChunk && *NearbyChunk)
                {
                    (*NearbyChunk)->MarkDirty();
                    UE_LOG(LogTemp, Log, TEXT("Marked nearby chunk dirty at position: %s"), *NearbyChunkCoords.ToString());
                }
            }
        }
    }
}


FVector ADiggerManager::CalculateIslandCenter(const TArray<FIntVector>& Voxels)
{
    if (Voxels.Num() == 0) return FVector::ZeroVector;

    FVector Sum(0, 0, 0);
    for (const FIntVector& V : Voxels)
    {
        Sum += FVector(V);
    }

    return Sum / Voxels.Num();
}

void ADiggerManager::RotateIslandByID(const FName& IslandID, const FRotator& Rotation)
{
    FIslandData* Island = FindIsland(IslandID);
    if (!Island || Island->VoxelInstances.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] RotateIslandByID: Invalid island or empty voxel set."));
        return;
    }

    // Step 1: Calculate center of island in world space
    FVector Center(0, 0, 0);
    for (const FVoxelInstance& Instance : Island->VoxelInstances)
    {
        Center += FVector(Instance.GlobalVoxel);
    }
    Center /= Island->VoxelInstances.Num();

    TSet<FIntVector> AffectedChunks;

    // Step 2: Remove original voxels and apply rotation
    for (const FVoxelInstance& Instance : Island->VoxelInstances)
    {
        UVoxelChunk** ChunkPtr = ChunkMap.Find(Instance.ChunkCoords);
        if (!ChunkPtr || !IsValid(*ChunkPtr)) continue;

        UVoxelChunk* Chunk = *ChunkPtr;
        USparseVoxelGrid* Grid = Chunk->GetSparseVoxelGrid();
        if (!IsValid(Grid)) continue;

        // Remove original voxel
        float OriginalSDF = Grid->GetVoxel(Instance.LocalVoxel);
        Grid->RemoveVoxel(Instance.LocalVoxel);

        // Step 3: Rotate around center
        FVector LocalOffset = FVector(Instance.GlobalVoxel) - Center;
        FVector RotatedOffset = Rotation.RotateVector(LocalOffset);
        FVector NewGlobalF = Center + RotatedOffset;
        FIntVector NewGlobal = FIntVector(FMath::RoundToInt(NewGlobalF.X), FMath::RoundToInt(NewGlobalF.Y), FMath::RoundToInt(NewGlobalF.Z));

        // Step 4: Convert to chunk/local coordinates
        FIntVector NewChunkCoords;
        FIntVector NewLocalCoords;
        FVoxelConversion::GlobalVoxelToChunkAndLocal_Min(NewGlobal, NewChunkCoords, NewLocalCoords);

        // Step 5: Insert rotated voxel
        UVoxelChunk** TargetChunkPtr = ChunkMap.Find(NewChunkCoords);
        if (!TargetChunkPtr || !IsValid(*TargetChunkPtr)) continue;

        UVoxelChunk* TargetChunk = *TargetChunkPtr;
        USparseVoxelGrid* TargetGrid = TargetChunk->GetSparseVoxelGrid();
        if (!IsValid(TargetGrid)) continue;

        //TargetGrid->SetVoxel(NewLocalCoords, FVoxelData()); // You may want to preserve original data
        TargetGrid->SetVoxel(NewLocalCoords, OriginalSDF, OriginalSDF > 0.0f? true : false);
        TargetChunk->MarkDirty();
        AffectedChunks.Add(NewChunkCoords);
    }

    // Step 6: Refresh affected chunks
    for (const FIntVector& ChunkCoords : AffectedChunks)
    {
        if (UVoxelChunk** ChunkPtr = ChunkMap.Find(ChunkCoords))
        {
            if (IsValid(*ChunkPtr))
            {
                (*ChunkPtr)->RefreshSectionMesh();
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Rotated island %s around center %s by %s"),
        *IslandID.ToString(), *Center.ToString(), *Rotation.ToString());
}


TArray<FIslandData> ADiggerManager::DetectUnifiedIslands()
{
    IslandIDMap.Empty();
    VoxelToIslandIDMap.Empty();

    // ---- Step 1: unify voxels to GLOBAL space ----
    TMap<FIntVector, FVoxelData> UnifiedVoxelData;   // GLOBAL -> data
    TArray<FVoxelInstance>       AllPhysical;        // coherent (Chunk, Local, Global)

    for (const auto& Pair : ChunkMap)
    {
        UVoxelChunk* Chunk = Pair.Value;
        if (!IsValid(Chunk)) continue;

        USparseVoxelGrid* Grid = Chunk->GetSparseVoxelGrid();
        if (!IsValid(Grid)) continue;

        const FIntVector CC = Chunk->GetChunkCoordinates();
        // Grid->GetAllVoxels() is LOCAL per-chunk
        for (const auto& VoxelPair : Grid->GetAllVoxels())
        {
            const FIntVector Local = VoxelPair.Key;     // LOCAL (0..VPC-1)
            const FVoxelData& Data = VoxelPair.Value;

            const FIntVector Global = FVoxelConversion::ChunkAndLocalToGlobalVoxel_Min(CC, Local);

            UnifiedVoxelData.Add(Global, Data);

            FVoxelInstance I;
            I.ChunkCoords = CC;
            I.LocalVoxel  = Local;
            I.GlobalVoxel = Global;
            AllPhysical.Add(I);
        }
    }

    // ---- Step 2: deduplicate islands from GLOBAL-keyed grid ----
    USparseVoxelGrid* Temp = NewObject<USparseVoxelGrid>(GetTransientPackage());
    Temp->Initialize(nullptr);                  // sizes/origin from FVoxelConversion
    Temp->SetVoxelData(UnifiedVoxelData);       // keys are GLOBAL indices

    const TArray<FIslandData> DedupIslands = Temp->DetectIslands(0.0f);
    TArray<FIslandData> FinalIslands;
    FinalIslands.Reserve(DedupIslands.Num());

    // ---- Step 3: rebuild each island strictly from GLOBAL keys ----
    for (const FIslandData& D : DedupIslands)
    {
        FIslandData Out;
        Out.IslandID      = FName(*FString::Printf(TEXT("Island_%s"), *GenerateIslandHash(D.Voxels)));
        Out.IslandName    = FString::Printf(TEXT("Island %d"), FinalIslands.Num());
        Out.PersistentUID = FGuid::NewGuid();

        const TSet<FIntVector> GlobalSet(D.Voxels);   // must be GLOBAL indices
        Out.VoxelCount = GlobalSet.Num();

        FVector CenterWS = FVector::ZeroVector;
        TMap<FIntVector, FVoxelData> DataMap;
        Out.Voxels.Reserve(Out.VoxelCount);
        Out.VoxelInstances.Reserve(Out.VoxelCount);

        const int32 VPC = FVoxelConversion::VoxelsPerChunk();

        for (const FVoxelInstance& Src : AllPhysical)
        {
            if (!GlobalSet.Contains(Src.GlobalVoxel)) continue;

            // Coherent instance (Chunk/Local were built from Global earlier)
            Out.VoxelInstances.Add(Src);
            Out.Voxels.Add(Src.GlobalVoxel);

            if (const FVoxelData* Dp = UnifiedVoxelData.Find(Src.GlobalVoxel))
            {
                DataMap.Add(Src.GlobalVoxel, *Dp);
            }

            CenterWS += FVoxelConversion::GlobalVoxelCenterToWorld(Src.GlobalVoxel);

            // Guard: Local must be in [0..VPC-1]
            ensureMsgf(
                Src.LocalVoxel.X >= 0 && Src.LocalVoxel.X < VPC &&
                Src.LocalVoxel.Y >= 0 && Src.LocalVoxel.Y < VPC &&
                Src.LocalVoxel.Z >= 0 && Src.LocalVoxel.Z < VPC,
                TEXT("[DetectUnifiedIslands] Local out of range; mixed spaces for Global=%s, Local=%s, Chunk=%s"),
                *Src.GlobalVoxel.ToString(), *Src.LocalVoxel.ToString(), *Src.ChunkCoords.ToString());
        }

        Out.VoxelDataMap = MoveTemp(DataMap);

        if (Out.VoxelInstances.Num() > 0)
        {
            Out.Location       = CenterWS / Out.VoxelInstances.Num();           // recompute from GLOBAL→world (center)
            Out.ReferenceVoxel = Out.VoxelInstances[0].GlobalVoxel;             // GLOBAL index
        }
        else
        {
            Out.Location       = FVector::ZeroVector;
            Out.ReferenceVoxel = FIntVector::ZeroValue;
        }

        // Indexing for lookups
        FinalIslands.Add(Out);
        IslandIDMap.Add(Out.IslandID, Out);
        for (const FVoxelInstance& VI : Out.VoxelInstances)
        {
            VoxelToIslandIDMap.Add(VI.GlobalVoxel, Out.IslandID);
        }
    }

    // ---- Step 4: notify ----
    if (OnIslandsDetectionStarted.IsBound())
        OnIslandsDetectionStarted.Broadcast();

    for (const FIslandData& I : FinalIslands)
        if (OnIslandDetected.IsBound())
            OnIslandDetected.Broadcast(I);

    return FinalIslands;
}





void ADiggerManager::DuplicateIslandAtLocation(const FName& SourceIslandID, const FVector& TargetLocation)
{
    FIslandData* SourceIsland = FindIsland(SourceIslandID);
    if (!SourceIsland || SourceIsland->VoxelInstances.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] DuplicateIslandAtLocation: Invalid source island."));
        return;
    }

    FVector SourceCenter(0, 0, 0);
    for (const FVoxelInstance& Instance : SourceIsland->VoxelInstances)
    {
        SourceCenter += FVector(Instance.GlobalVoxel);
    }
    SourceCenter /= SourceIsland->VoxelInstances.Num();

    FVector Offset = TargetLocation - SourceCenter;

    TArray<FVoxelInstance> DuplicatedInstances;
    TSet<FIntVector> AffectedChunks;

    for (const FVoxelInstance& Instance : SourceIsland->VoxelInstances)
    {
        FVector NewGlobalF = FVector(Instance.GlobalVoxel) + Offset;
        FIntVector NewGlobal = FIntVector(FMath::RoundToInt(NewGlobalF.X), FMath::RoundToInt(NewGlobalF.Y), FMath::RoundToInt(NewGlobalF.Z));

        FIntVector NewChunkCoords;
        FIntVector NewLocalCoords;
        FVoxelConversion::GlobalVoxelToChunkAndLocal_Min(NewGlobal, NewChunkCoords, NewLocalCoords);

        UVoxelChunk** ChunkPtr = ChunkMap.Find(NewChunkCoords);
        if (!ChunkPtr || !IsValid(*ChunkPtr)) continue;

        UVoxelChunk* Chunk = *ChunkPtr;
        USparseVoxelGrid* Grid = Chunk->GetSparseVoxelGrid();
        if (!IsValid(Grid)) continue;

        //Grid->SetVoxelData(NewLocalCoords, FVoxelData()); // Optionally copy original data
        
        Chunk->MarkDirty();
        AffectedChunks.Add(NewChunkCoords);

        DuplicatedInstances.Add(FVoxelInstance(NewGlobal, NewChunkCoords, NewLocalCoords));
    }

    for (const FIntVector& ChunkCoords : AffectedChunks)
    {
        if (UVoxelChunk** ChunkPtr = ChunkMap.Find(ChunkCoords))
        {
            if (IsValid(*ChunkPtr))
            {
                (*ChunkPtr)->RefreshSectionMesh();
            }
        }
    }

    // Create new island data
    FIslandData NewIsland;
    NewIsland.VoxelInstances = DuplicatedInstances;
    NewIsland.VoxelCount = DuplicatedInstances.Num();
    NewIsland.Location = TargetLocation;
    NewIsland.ReferenceVoxel = DuplicatedInstances[0].GlobalVoxel;
    // TODO: sort this out so it works as intended: NewIsland.Voxels = DuplicatedInstances.Array(); // Optional: deduplicate if needed
    NewIsland.IslandID = FName(*FString::Printf(TEXT("Island_%s"), *GenerateIslandHash(NewIsland.Voxels)));
    NewIsland.PersistentUID = FGuid::NewGuid();
    NewIsland.IslandName = FString::Printf(TEXT("Copy of %s"), *SourceIsland->IslandName);

    IslandIDMap.Add(NewIsland.IslandID, NewIsland);
    for (const FVoxelInstance& Instance : NewIsland.VoxelInstances)
    {
        VoxelToIslandIDMap.Add(Instance.GlobalVoxel, NewIsland.IslandID);
    }

    UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Duplicated island %s to new island %s at %s"),
        *SourceIslandID.ToString(), *NewIsland.IslandID.ToString(), *TargetLocation.ToString());
}



FString ADiggerManager::GenerateIslandHash(const TArray<FIntVector>& Voxels) const
{
    TArray<FIntVector> SortedVoxels = Voxels;
    SortedVoxels.Sort([](const FIntVector& A, const FIntVector& B)
    {
        if (A.X != B.X) return A.X < B.X;
        if (A.Y != B.Y) return A.Y < B.Y;
        return A.Z < B.Z;
    });

    FString Combined;
    for (const FIntVector& V : SortedVoxels)
    {
        Combined += V.ToString(); // Format: X=...,Y=...,Z=...
    }

    return FMD5::HashAnsiString(*Combined); // Produces a stable hash
}



// Main function: always works in chunk coordinates
UVoxelChunk* ADiggerManager::GetOrCreateChunkAtChunk(const FIntVector& ChunkCoords)
{
    //Run Init on the static FVoxelCoversion struct so all of our conversions work properly even if the settings have changed!
    FVoxelConversion::InitFromConfig(ChunkSize,Subdivisions,TerrainGridSize, GetActorLocation());

    if (DiggerDebug::Chunks)
    UE_LOG(LogTemp, Warning, TEXT("âž¡ï¸ Attempting chunk at coords: %s"), *ChunkCoords.ToString());
    
    if (UVoxelChunk** ExistingChunk = ChunkMap.Find(ChunkCoords))
    {
        if (DiggerDebug::Chunks)
        UE_LOG(LogTemp, Log, TEXT("Found existing chunk at position: %s"), *ChunkCoords.ToString());
        return *ExistingChunk;
    }

    UVoxelChunk* NewChunk = NewObject<UVoxelChunk>(this);
    if (NewChunk)
    {
        NewChunk->InitializeChunk(ChunkCoords, this);
        NewChunk->InitializeDiggerManager(this);
        ChunkMap.Add(ChunkCoords, NewChunk);

        if (DiggerDebug::Chunks)
        UE_LOG(LogTemp, Log, TEXT("Created a new chunk at position: %s"), *ChunkCoords.ToString());
        return NewChunk;
    }
    else
    {
        if (DiggerDebug::Chunks || DiggerDebug::Error)
        UE_LOG(LogTemp, Error, TEXT("Failed to create a new chunk at position: %s"), *ChunkCoords.ToString());
        return nullptr;
    }
}


bool ADiggerManager::IsGameWorld() const
{
    UWorld* W = GetWorld();
    return W && (W->WorldType == EWorldType::Game || W->WorldType == EWorldType::PIE);
}






UVoxelChunk* ADiggerManager::GetOrCreateChunkAtCoordinates(const float& ProposedChunkX, const float& ProposedChunkY, const float& ProposedChunkZ)
{
    return GetOrCreateChunkAtWorld(FVector(ProposedChunkX, ProposedChunkY, ProposedChunkZ));
}

// Thin wrapper: converts world position to chunk coordinates
UVoxelChunk* ADiggerManager::GetOrCreateChunkAtWorld(const FVector& WorldPosition)
{
    FIntVector ChunkCoords = FVoxelConversion::WorldToChunk_Min(WorldPosition);
    return GetOrCreateChunkAtChunk(ChunkCoords);
}


void ADiggerManager::RemoveUnifiedIslandVoxels(const FIslandData& Island)
{
    int32 TotalRemoved = 0;
    TSet<UVoxelChunk*> ChunksToUpdate;

    if (DiggerDebug::Islands || DiggerDebug::Voxels || DiggerDebug::Chunks)
    UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Starting unified island removal with %d pre-computed voxel instances"), Island.VoxelInstances.Num());
    
    // Group voxel instances by their storage chunks for efficient batch removal
    TMap<UVoxelChunk*, TArray<FIntVector>> VoxelsByChunk;
    
    // Simply iterate through the pre-computed voxel instances
    for (const FVoxelInstance& Instance : Island.VoxelInstances)
    {
        UVoxelChunk* Chunk = ChunkMap.FindRef(Instance.ChunkCoords);
        if (!Chunk || !Chunk->IsValidLowLevel()) 
        {
            if (DiggerDebug::Chunks || DiggerDebug::Error)
            UE_LOG(LogTemp, Error, TEXT("[DiggerPro] Chunk %s not found or invalid"), *Instance.ChunkCoords.ToString());
            continue;
        }
        
        USparseVoxelGrid* Grid = Chunk->GetSparseVoxelGrid();
        if (!Grid || !Grid->IsValidLowLevel()) 
        {
            if (DiggerDebug::Islands || DiggerDebug::Voxels || DiggerDebug::Chunks)
            UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Grid for chunk %s not found or invalid"), *Instance.ChunkCoords.ToString());
            continue;
        }
        
        // Verify the voxel still exists before adding to removal list
        if (Grid->HasVoxelAt(Instance.LocalVoxel))
        {
            VoxelsByChunk.FindOrAdd(Chunk).Add(Instance.LocalVoxel);
            ChunksToUpdate.Add(Chunk);
            
            // Special logging for negative overflow voxels being queued for removal
            if (Instance.LocalVoxel.X == -1 || Instance.LocalVoxel.Y == -1 || Instance.LocalVoxel.Z == -1)
            {
                if (DiggerDebug::Islands || DiggerDebug::Voxels || DiggerDebug::Chunks)
                UE_LOG(LogTemp, Warning, 
                    TEXT("[DiggerPro] NEGATIVE OVERFLOW QUEUED FOR REMOVAL: Global %s -> Chunk %s -> Local %s"),
                    *Instance.GlobalVoxel.ToString(), *Instance.ChunkCoords.ToString(), *Instance.LocalVoxel.ToString());
            }
        }
        else
        {
            if (DiggerDebug::Islands || DiggerDebug::Voxels || DiggerDebug::Chunks)
            UE_LOG(LogTemp, Warning, 
                TEXT("[DiggerPro] Voxel not found at Local %s in chunk %s (Global %s)"),
                *Instance.LocalVoxel.ToString(), *Instance.ChunkCoords.ToString(), *Instance.GlobalVoxel.ToString());
        }
    }
    
    // Perform batch removal on each affected chunk
    for (auto& ChunkVoxelPair : VoxelsByChunk)
    {
        UVoxelChunk* Chunk = ChunkVoxelPair.Key;
        TArray<FIntVector>& LocalVoxels = ChunkVoxelPair.Value;
        TArray<FIntVector>& NonOverflowVoxels = ChunkVoxelPair.Value;
        
        if (Chunk && Chunk->IsValidLowLevel())
        {
            USparseVoxelGrid* Grid = Chunk->GetSparseVoxelGrid();
            if (Grid && Grid->IsValidLowLevel())
            {
                if (DiggerDebug::Islands || DiggerDebug::Voxels || DiggerDebug::Chunks)
                UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Removing %d voxel instances from chunk at %s"), 
                    LocalVoxels.Num(), *Chunk->GetChunkCoordinates().ToString());
                
                // Count negative overflow voxels being removed
                int32 NegativeOverflowCount = 0;
                for (const FIntVector& LocalVoxel : LocalVoxels)
                {
                    if (LocalVoxel.X == -1 || LocalVoxel.Y == -1 || LocalVoxel.Z == -1)
                    {
                        NegativeOverflowCount++;
                        if (DiggerDebug::Islands || DiggerDebug::Voxels || DiggerDebug::Chunks)
                        UE_LOG(LogTemp, Warning, 
                            TEXT("[DiggerPro] REMOVING NEGATIVE OVERFLOW: Local %s from chunk %s"),
                            *LocalVoxel.ToString(), *Chunk->GetChunkCoordinates().ToString());
                    }
                }
                
                if (NegativeOverflowCount > 0)
                {
                    if (DiggerDebug::Islands || DiggerDebug::Voxels || DiggerDebug::Chunks)
                    UE_LOG(LogTemp, Warning, 
                        TEXT("[DiggerPro] About to remove %d negative overflow voxels from chunk %s"),
                        NegativeOverflowCount, *Chunk->GetChunkCoordinates().ToString());
                }
                    
                Grid->RemoveSpecifiedVoxels(LocalVoxels);
                TotalRemoved += LocalVoxels.Num();
                if (DiggerDebug::Islands || DiggerDebug::Voxels || DiggerDebug::Chunks)
                UE_LOG(LogTemp, Warning, 
                    TEXT("[DiggerPro] Successfully removed %d voxels from chunk %s"),
                    LocalVoxels.Num(), *Chunk->GetChunkCoordinates().ToString());
            }
        }
    }
    
    // Update all affected chunks to regenerate meshes
    for (UVoxelChunk* Chunk : ChunksToUpdate)
    {
        if (Chunk && Chunk->IsValidLowLevel())
        {
            Chunk->ForceUpdate();
        }
    }
    if (DiggerDebug::Islands || DiggerDebug::Chunks || DiggerDebug::Voxels)
    UE_LOG(LogTemp, Warning,
        TEXT("[DiggerPro] Unified island removal complete: %d voxel instances removed across %d chunks"),
        TotalRemoved, ChunksToUpdate.Num());
}

// New helper method to perform flood fill across chunk boundaries
TSet<FIntVector> ADiggerManager::PerformCrossChunkFloodFill(const FIntVector& StartGlobalVoxel)
{
    TSet<FIntVector> VisitedVoxels;
    TQueue<FIntVector> VoxelsToCheck;
    
    // Start the flood fill
    VoxelsToCheck.Enqueue(StartGlobalVoxel);
    
    while (!VoxelsToCheck.IsEmpty())
    {
        FIntVector CurrentGlobal;
        VoxelsToCheck.Dequeue(CurrentGlobal);
        
        if (VisitedVoxels.Contains(CurrentGlobal))
            continue;
            
        // Check if this global voxel is solid in any storage chunk
        bool bFoundSolidVoxel = false;
        TArray<FIntVector> StorageChunkCoords = GetAllPhysicalStorageChunks(CurrentGlobal);
        
        for (const FIntVector& ChunkCoord : StorageChunkCoords)
        {
            UVoxelChunk* Chunk = ChunkMap.FindRef(ChunkCoord);
            if (!Chunk || !Chunk->IsValidLowLevel()) continue;
            
            USparseVoxelGrid* Grid = Chunk->GetSparseVoxelGrid();
            if (!Grid || !Grid->IsValidLowLevel()) continue;
            
            // Convert global to local coordinates for this candidate chunk
            FIntVector LocalVoxel, OutChunkCoord;
            FVoxelConversion::GlobalVoxelToChunkAndLocal_Min(
                CurrentGlobal,
                OutChunkCoord,
                LocalVoxel
            );
            
            if (Grid->HasVoxelAt(LocalVoxel) && Grid->IsVoxelSolid(LocalVoxel))
            {
                bFoundSolidVoxel = true;
                break;
            }
        }
        
        if (!bFoundSolidVoxel)
            continue;
            
        // Mark as visited and add neighbors
        VisitedVoxels.Add(CurrentGlobal);
        
        // Check all 6 neighboring voxels
        static const FIntVector Neighbors[6] = {
            FIntVector(1, 0, 0), FIntVector(-1, 0, 0),
            FIntVector(0, 1, 0), FIntVector(0, -1, 0),
            FIntVector(0, 0, 1), FIntVector(0, 0, -1)
        };
        
        for (const FIntVector& Offset : Neighbors)
        {
            FIntVector NeighborGlobal = CurrentGlobal + Offset;
            if (!VisitedVoxels.Contains(NeighborGlobal))
            {
                VoxelsToCheck.Enqueue(NeighborGlobal);
            }
        }
    }
    
    return VisitedVoxels;
}

// Helper method to find ALL chunks that actually contain this voxel
// This includes the canonical owner AND adjacent chunks with overflow slabs
TArray<FIntVector> ADiggerManager::GetAllPhysicalStorageChunks(const FIntVector& GlobalVoxel)
{
    TArray<FIntVector> StorageChunks;
    
    // Start with the canonical owning chunk
    FIntVector CanonicalChunk, LocalVoxelOut;
    FVoxelConversion::GlobalVoxelToChunkAndLocal_Min(GlobalVoxel, CanonicalChunk, LocalVoxelOut);
    
    // Check all 27 possible chunks (canonical + 26 neighbors) to see which ones actually store this voxel
    for (int32 dx = -1; dx <= 1; dx++)
    {
        for (int32 dy = -1; dy <= 1; dy++)
        {
            for (int32 dz = -1; dz <= 1; dz++)
            {
                FIntVector CandidateChunk = CanonicalChunk + FIntVector(dx, dy, dz);
                
                // Check if this chunk exists and actually contains the voxel
                UVoxelChunk* Chunk = ChunkMap.FindRef(CandidateChunk);
                if (!Chunk || !Chunk->IsValidLowLevel()) continue;
                
                USparseVoxelGrid* Grid = Chunk->GetSparseVoxelGrid();
                if (!Grid || !Grid->IsValidLowLevel()) continue;
                
                // Convert global to local coordinates for this candidate chunk
                FIntVector LocalVoxel;
                FVoxelConversion::GlobalVoxelToChunkAndLocal_Min(
                    GlobalVoxel,
                    CandidateChunk,
                    LocalVoxel
                );
                
                // If this chunk actually has the voxel, add it to storage list
                if (Grid->HasVoxelAt(LocalVoxel))
                {
                    StorageChunks.AddUnique(CandidateChunk);

                    if (DiggerDebug::Chunks || DiggerDebug::Voxels)
                    UE_LOG(LogTemp, Warning, 
                        TEXT("[DiggerPro] Global voxel %s found in chunk %s at local %s"),
                        *GlobalVoxel.ToString(), *CandidateChunk.ToString(), *LocalVoxel.ToString());
                }
                else
                {
                    if (DiggerDebug::Chunks || DiggerDebug::Voxels)
                    // Debug: Log when we don't find expected voxels
                    UE_LOG(LogTemp, VeryVerbose, 
                        TEXT("[DiggerPro] Global voxel %s NOT found in chunk %s (would be local %s)"),
                        *GlobalVoxel.ToString(), *CandidateChunk.ToString(), *LocalVoxel.ToString());
                }
            }
        }
    }
    
    // If we didn't find the voxel anywhere, this is a problem!
    if (StorageChunks.Num() == 0)
    {
        if (DiggerDebug::Chunks || DiggerDebug::Voxels)
        UE_LOG(LogTemp, Error, 
            TEXT("[DiggerPro] CRITICAL: Global voxel %s was not found in any chunk! Canonical chunk: %s"),
            *GlobalVoxel.ToString(), *CanonicalChunk.ToString());
    }
    
    return StorageChunks;
}

TArray<FIntVector> ADiggerManager::GetPossibleOwningChunks(const FIntVector& GlobalIndex)
{
    TArray<FIntVector> PossibleChunks;

    // Scan all 8 neighboring chunks where this voxel might overflow into
    for (int32 dx = 0; dx <= 1; dx++)
        for (int32 dy = 0; dy <= 1; dy++)
            for (int32 dz = 0; dz <= 1; dz++)
            {
                FIntVector Offset(dx, dy, dz);
                FIntVector AdjustedGlobal = GlobalIndex - Offset;

                FIntVector CandidateChunkCoords, LocalVoxel;
                FVoxelConversion::GlobalVoxelToChunkAndLocal_Min(AdjustedGlobal, CandidateChunkCoords, LocalVoxel);

                if (ChunkMap.Contains(CandidateChunkCoords))
                {
                    PossibleChunks.Add(CandidateChunkCoords);
                }
            }

    return PossibleChunks;
}


#if WITH_EDITOR
void ADiggerManager::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    // Sync updated settings to FVoxelConversion using the actor's position as origin
    FVoxelConversion::InitFromConfig(ChunkSize,Subdivisions, TerrainGridSize, GetActorLocation());

    if (!IsGameWorld())
    {
        // avoid full rebuilds in editor
        bEditorInitDone = false;
        return;
    }
    
    // Respond to property changes (e.g., ChunkSize, TerrainGridSize, etc.)
    EditorUpdateChunks();
}


void ADiggerManager::PostEditMove(bool bFinished)
{
    Super::PostEditMove(bFinished);

    // Update FVoxelConversion origin if the actor is moved in the editor
    FVoxelConversion::InitFromConfig(ChunkSize,Subdivisions, TerrainGridSize, GetActorLocation());

    if (bFinished && !IsRuntimeLike())
    {
        EditorDeferredInit();
    }
    else if (bFinished)
    {
        EditorUpdateChunks();
    }
}

void ADiggerManager::PostEditUndo()
{
    Super::PostEditUndo();

    // Update FVoxelConversion in case undo/redo changed position or settings
    FVoxelConversion::InitFromConfig(ChunkSize, Subdivisions, TerrainGridSize, GetActorLocation());

    EditorUpdateChunks();
}

void ADiggerManager::EditorUpdateChunks()
{
    // Call your chunk update logic here
    ProcessDirtyChunks();
}

void ADiggerManager::EditorRebuildAllChunks()
{
    // Optionally clear and rebuild all chunks
    InitializeChunks();
}


#endif // WITH_EDITOR

// Static constants
const FString ADiggerManager::VOXEL_DATA_DIRECTORY = TEXT("VoxelData");
const FString ADiggerManager::CHUNK_FILE_EXTENSION = TEXT(".VoxelData");

void ADiggerManager::EnsureVoxelDataDirectoryExists() const
{
    FString VoxelDataPath = FPaths::ProjectContentDir() / VOXEL_DATA_DIRECTORY;
    
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlatformFile.DirectoryExists(*VoxelDataPath))
    {
        if (PlatformFile.CreateDirectoryTree(*VoxelDataPath))
        {
            if (DiggerDebug::IO)
            UE_LOG(LogTemp, Log, TEXT("Created VoxelData directory at: %s"), *VoxelDataPath);
        }
        else
        {
            if (DiggerDebug::IO)
            UE_LOG(LogTemp, Error, TEXT("Failed to create VoxelData directory at: %s"), *VoxelDataPath);
        }
    }
}

void ADiggerManager::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    ProcessDirtyChunks();
    
    // Update cache refresh timer
    SavedChunkCacheRefreshTimer += DeltaTime;
    if (SavedChunkCacheRefreshTimer >= CACHE_REFRESH_INTERVAL)
    {
        SavedChunkCacheRefreshTimer = 0.0f;
        if (!bSavedChunkCacheValid)
        {
            RefreshSavedChunkCache();
        }
    }
    
    // Your existing tick code...
}

void ADiggerManager::QueueBrushPoint(const FVector& HitPointWS, float Radius, float Strength, float Hardness, uint8 Shape, uint8 Op)
{
    const float Spacing = (BrushResampleSpacing > 0.f) ? BrushResampleSpacing : (Radius * 0.5f);
    if (!bHasLastSample || FVector::DistSquared(LastSamplePos, HitPointWS) >= Spacing*Spacing)
    {
        PendingStroke.Add({ HitPointWS, Radius, Strength, Hardness, Shape, Op });
        LastSamplePos = HitPointWS;
        bHasLastSample = true;
    }
}

void ADiggerManager::ApplyPendingBrushSamples()
{
    if (PendingStroke.Num() == 0) return;

    // TODO: replace this loop with your chunk/SDF application logic
    for (const FBrushSample& S : PendingStroke)
    {
        // Currently you'd call your existing ApplyBrush(S.WorldPos, S.Radius, ...) here
    }

    PendingStroke.Reset();
    bHasLastSample = false;
}




void ADiggerManager::CreateHoleAt(FVector WorldPosition, FRotator Rotation, FVector Scale, TSubclassOf<AActor> HoleBPClass)
{
    FIntVector ChunkCoords = FVoxelConversion::WorldToChunk_Min(WorldPosition);
    if (UVoxelChunk* Chunk = GetOrCreateChunkAtChunk(ChunkCoords))
    {
        //ToDo: set shape type based on brush used!
        Chunk->SpawnHole(HoleBPClass, WorldPosition, Rotation, Scale, EHoleShapeType::Sphere);
    }
}

bool ADiggerManager::RemoveHoleNear(FVector WorldPosition, float MaxDistance)
{
    FIntVector ChunkCoords = FVoxelConversion::WorldToChunk_Min(WorldPosition);
    if (UVoxelChunk* Chunk = GetOrCreateChunkAtChunk(ChunkCoords))
    {
        return Chunk->RemoveNearestHole(WorldPosition, MaxDistance);
    }
    return false;
}



TArray<FIntVector> ADiggerManager::GetAllSavedChunkCoordinates(bool bForceRefresh) const
{
    // Use cached data if valid and not forcing refresh
    if (!bForceRefresh && bSavedChunkCacheValid)
    {
        return CachedSavedChunkCoordinates;
    }
    
    // If we need to refresh, do it on the mutable cache
    const_cast<ADiggerManager*>(this)->RefreshSavedChunkCache();
    return CachedSavedChunkCoordinates;
}

void ADiggerManager::RefreshSavedChunkCache()
{
    CachedSavedChunkCoordinates.Empty();
    
    FString VoxelDataPath = FPaths::ProjectContentDir() / VOXEL_DATA_DIRECTORY;
    
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    
    if (!PlatformFile.DirectoryExists(*VoxelDataPath))
    {
        bSavedChunkCacheValid = true;
        return;
    }
    
    // Find all .VoxelData files
    TArray<FString> FoundFiles;
    PlatformFile.FindFiles(FoundFiles, *VoxelDataPath, *CHUNK_FILE_EXTENSION);
    
    for (const FString& FileName : FoundFiles)
    {
        // Parse filename to extract coordinates
        FString BaseName = FPaths::GetBaseFilename(FileName);
        
        if (BaseName.StartsWith(TEXT("Chunk")))
        {
            FString CoordString = BaseName.RightChop(5); // Remove "Chunk" prefix
            
            TArray<FString> CoordParts;
            CoordString.ParseIntoArray(CoordParts, TEXT("_"), true);
            
            if (CoordParts.Num() == 3)
            {
                int32 X = FCString::Atoi(*CoordParts[0]);
                int32 Y = FCString::Atoi(*CoordParts[1]);
                int32 Z = FCString::Atoi(*CoordParts[2]);
                
                CachedSavedChunkCoordinates.Add(FIntVector(X, Y, Z));
            }
        }
    }
    
    bSavedChunkCacheValid = true;
    
    // Only log when actually refreshing to avoid spam
    static int32 LastCachedCount = -1;
    if (CachedSavedChunkCoordinates.Num() != LastCachedCount)
    {
        if (DiggerDebug::Chunks)
        {
            UE_LOG(LogTemp, Warning, TEXT("Refreshed saved chunk cache: found %d files"), 
                CachedSavedChunkCoordinates.Num());
        }
        LastCachedCount = CachedSavedChunkCoordinates.Num();
    }
}


bool ADiggerManager::DeleteChunkFile(const FIntVector& ChunkCoords)
{
    FString FilePath = GetChunkFilePath(ChunkCoords);
    
    if (!FPaths::FileExists(FilePath))
    {
        if (DiggerDebug::IO)
        {
            UE_LOG(LogTemp, Warning, TEXT("Cannot delete chunk file - file does not exist: %s"), *FilePath);
        }
        return false;
    }
    
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    bool bDeleteSuccess = PlatformFile.DeleteFile(*FilePath);
    
    if (bDeleteSuccess)
    {
        if (DiggerDebug::IO)
        {
            UE_LOG(LogTemp, Log, TEXT("Successfully deleted chunk file for chunk %s"), *ChunkCoords.ToString());
        }
        
        // Invalidate cache after deletion
        InvalidateSavedChunkCache(FilePath);
    }
    else
    {
        if (DiggerDebug::IO)
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to delete chunk file for chunk %s"), *ChunkCoords.ToString());
        }
    }
    
    
    return bDeleteSuccess;
}
