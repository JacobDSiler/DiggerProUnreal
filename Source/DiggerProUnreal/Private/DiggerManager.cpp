#include "DiggerManager.h"

#include "CoreMinimal.h"

// Voxel & Mesh Systems
#include "FCustomSDFBrush.h"
#include "MarchingCubes.h"
#include "SparseVoxelGrid.h"
#include "VoxelChunk.h"
#include "VoxelConversion.h"

// Landscape & Island
#include "IslandActor.h"
#include "LandscapeEdit.h"
#include "LandscapeInfo.h"
#include "LandscapeProxy.h"

// Procedural & Static Meshes
#include "MeshDescription.h"
#include "ProceduralMeshComponent.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshDescription.h"
#include "StaticMeshOperations.h"

// Engine Systems
#include "TimerManager.h"
#include "Engine/World.h"

// Async & Performance
#include "Async/Async.h"
#include "Async/ParallelFor.h"

// Asset Management
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "HoleShapeLibrary.h" // <-- use the actual path to your class

// File & Path Management
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

// Kismet & Helpers
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"

// Rendering
#include "Rendering/PositionVertexBuffer.h"
#include "Rendering/StaticMeshVertexBuffer.h"

// UObject Utilities
#include "EditorSupportDelegates.h"
#include "EngineUtils.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/Package.h"
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


// Save and Load
#include "DiggerDebug.h"
#include "DiggerEdMode.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"
#include "Engine/Engine.h"

// Brush Shapes
#include "DynamicLightActor.h"
#include "VoxelLogManager.h"
#include "DiggerProUnreal/Utilities/FastDebugRenderer.h"
#include "Voxel/BrushShapes/CapsuleBrushShape.h"
#include "Voxel/BrushShapes/CylinderBrushShape.h"
#include "Voxel/BrushShapes/IcosphereBrushShape.h"
#include "Voxel/BrushShapes/PyramidBrushShape.h"
#include "Voxel/BrushShapes/SmoothBrushShape.h"
#include "Voxel/BrushShapes/TorusBrushShape.h"

// Hole Shape Library Requirements
#include "FSpawnedHoleData.h"
#include "HoleBPHelpers.h"
#include "Engine/StaticMesh.h"
#include "UObject/SoftObjectPath.h"

// Editor Tool Specific Includes
#if WITH_EDITOR
#include "DiggerEdModeToolkit.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "Engine/Selection.h"
#include "Editor/EditorEngine.h"
#include "DiggerEdModeToolkit.h"

// Light Component Includes
#include "Components/LightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "HAL/FileManagerGeneric.h"
#endif

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
    FFileManagerGeneric::Get().FindFiles(FoundFiles, *SearchPattern, true, false);
    
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
    FFileManagerGeneric::Get().FindFilesRecursive(FilesToDelete, *SaveDir, TEXT("*"), true, false);
    
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
    if (DiggerDebug::Brush) {
        UE_LOG(LogTemp, Error, TEXT("ApplyBrushInEditor Called!"));
    }
    
    // Create the brush stroke with all the editor properties
    FBrushStroke BrushStroke;
    BrushStroke.BrushPosition = EditorBrushPosition + EditorBrushOffset;
    BrushStroke.BrushRadius = EditorBrushRadius;
    BrushStroke.BrushRotation = EditorBrushRotation;
    BrushStroke.bHiddenSeam = EditorBrushHiddenSeam;
    UE_LOG(LogTemp, Warning, TEXT("Seam Type Received in DiggerManager type: %s"), EditorBrushHiddenSeam ? TEXT("true") : TEXT("false"));
    BrushStroke.BrushType = EditorBrushType;
    BrushStroke.bDig = bDig;
    BrushStroke.BrushLength = EditorBrushLength;
    BrushStroke.bIsFilled = EditorBrushIsFilled;
    BrushStroke.BrushAngle = EditorBrushAngle;
    BrushStroke.HoleShape = EditorBrushHoleShape;
    BrushStroke.bUseAdvancedCubeBrush = EditorbUseAdvancedCubeBrush;
    BrushStroke.AdvancedCubeHalfExtentX = EditorCubeHalfExtentX;
    BrushStroke.AdvancedCubeHalfExtentY = EditorCubeHalfExtentY;
    BrushStroke.AdvancedCubeHalfExtentZ = EditorCubeHalfExtentZ;
    BrushStroke.BrushStrength = EditorBrushStrength; // Make sure this is set
    // In your ApplyBrushInEditor method, add this line when creating the BrushStroke:
    BrushStroke.LightColor = EditorBrushLightColor;
    
    // Set light-specific properties if it's a light brush
    if (EditorBrushType == EVoxelBrushType::Light)
    {
        BrushStroke.LightType = EditorBrushLightType;
        BrushStroke.LightColor = EditorBrushLightColor;

        if (DiggerDebug::Lights || DiggerDebug::Brush)
        {
            UE_LOG(LogTemp, Warning, TEXT("Light brush setup: LightType = %d, Color = %s"),
                (int32)BrushStroke.LightType,
                *BrushStroke.LightColor.ToString());
        }

        ApplyLightBrushInEditor(BrushStroke);
        return;
    }

    
    if (EditorBrushType == EVoxelBrushType::Light)
    {
        // Default fallback
        //BrushStroke.LightType = ELightBrushType::Point;
    
        // Get both light type and color from toolkit
        if (GLevelEditorModeTools().IsModeActive(FDiggerEdMode::EM_DiggerEdModeId))
        {
            UE_LOG(LogTemp, Warning, TEXT("getting both light type and color from toolkit!"));
            FDiggerEdMode* DiggerMode = (FDiggerEdMode*)GLevelEditorModeTools().GetActiveMode(FDiggerEdMode::EM_DiggerEdModeId);
           // if (DiggerMode && DiggerMode->GetToolkit().IsValid())
            {
                TSharedPtr<FDiggerEdModeToolkit> Toolkit = StaticCastSharedPtr<FDiggerEdModeToolkit>(DiggerMode->GetToolkit());
                if (Toolkit.IsValid())
                {
                    BrushStroke.LightType = Toolkit->GetCurrentLightType();
                    BrushStroke.LightColor = Toolkit->GetCurrentLightColor(); // Add this line

                    if (DiggerDebug::Lights || DiggerDebug::Brush)
                    {
                        UE_LOG(LogTemp, Warning, TEXT("Light type from toolkit: %d, Color: %s"), 
                               (int32)BrushStroke.LightType, 
                               *BrushStroke.LightColor.ToString());
                    }
                }
            }
        }
        if (DiggerDebug::Lights || DiggerDebug::Brush)
        {
            UE_LOG(LogTemp, Warning, TEXT("ApplyBrushInEditor: this = %p"), this);
            UE_LOG(LogTemp, Warning, TEXT("ApplyBrushInEditor: EditorBrushType = %d"), (int32)EditorBrushType);
        }
    
        // Route to light handler with the full brush stroke
        ApplyLightBrushInEditor(BrushStroke);
        return;
    }
    
    // Handle voxel brushes (existing logic)
    if (DiggerDebug::Brush) {
        UE_LOG(LogTemp, Warning, TEXT("DiggerManager.cpp: Extent X: %.2f  Extent Y: %.2f  Extent Z: %.2f"),
               BrushStroke.AdvancedCubeHalfExtentX,
               BrushStroke.AdvancedCubeHalfExtentY,
               BrushStroke.AdvancedCubeHalfExtentZ);
    }
    
    if (DiggerDebug::Brush || DiggerDebug::Casts)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ApplyBrushInEditor] EditorBrushPosition (World): %s"), *EditorBrushPosition.ToString());
    }

    ApplyBrushToAllChunks(BrushStroke);
    ProcessDirtyChunks();

    if (GEditor)
    {
        GEditor->RedrawAllViewports();
    }

    // Update islands for the UI/toolkit
    TArray<FIslandData> DetectedIslands = DetectUnifiedIslands();
}

void ADiggerManager::RemoveIslandAtPosition(const FVector& IslandCenter, const FIntVector& ReferenceVoxel)
{
    UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] RemoveIslandAtPosition called at %s"), *IslandCenter.ToString());

    // Guard 1: Reference voxel must be valid
    if (!ensure(!ReferenceVoxel.IsZero()))
    {
        UE_LOG(LogTemp, Error, TEXT("[DiggerPro] No reference voxel provided."));
        return;
    }

    FIntVector ChunkCoords, LocalVoxel;
    FVoxelConversion::GlobalVoxelToChunkAndLocal_CenterAligned(ReferenceVoxel, ChunkCoords, LocalVoxel);

    // Guard 2: Chunk must exist and be valid
    UVoxelChunk** ChunkPtr = ChunkMap.Find(ChunkCoords);
    if (!ChunkPtr || !IsValid(*ChunkPtr))
    {
        UE_LOG(LogTemp, Error, TEXT("[DiggerPro] Invalid or missing chunk at coords %s"), *ChunkCoords.ToString());
        return;
    }

    UVoxelChunk* Chunk = *ChunkPtr;

    // Guard 3: Grid must exist and be valid
    USparseVoxelGrid* Grid = Chunk->GetSparseVoxelGrid();
    if (!IsValid(Grid))
    {
        UE_LOG(LogTemp, Error, TEXT("[DiggerPro] Invalid SparseVoxelGrid in chunk."));
        return;
    }

    // Guard 4: Attempt island extraction
    USparseVoxelGrid* ExtractedIsland = nullptr;
    TArray<FIntVector> ExtractedVoxels;

    if (!Grid->ExtractIslandByVoxel(LocalVoxel, ExtractedIsland, ExtractedVoxels) || !IsValid(ExtractedIsland) || ExtractedVoxels.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("[DiggerPro] Failed to extract island at voxel %s"), *LocalVoxel.ToString());
        return;
    }

    // Safe voxel removal (encapsulated)
    Grid->RemoveVoxels(ExtractedVoxels);
    Chunk->MarkDirty();

    UE_LOG(LogTemp, Display, TEXT("[DiggerPro] Successfully removed %d voxels at %s."), ExtractedVoxels.Num(), *IslandCenter.ToString());
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
    float ChunkWorldSize = FVoxelConversion::ChunkSize * FVoxelConversion::LocalVoxelSize;
    float ChunkDiagonal = ChunkWorldSize * 1.732f; // sqrt(3) for 3D diagonal
    float SafetyPadding = BrushEffectRadius + ChunkDiagonal;

    FVector Min = BrushStroke.BrushPosition - FVector(SafetyPadding);
    FVector Max = BrushStroke.BrushPosition + FVector(SafetyPadding);

    FIntVector MinChunk = FVoxelConversion::WorldToChunk(Min);
    FIntVector MaxChunk = FVoxelConversion::WorldToChunk(Max);

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
    FIntVector ChunkCoords = FVoxelConversion::WorldToChunk(WorldPos);
    FIntVector LocalVoxelIndex = FVoxelConversion::WorldToMinCornerVoxel(WorldPos);

    // Try to get or create the chunk
    if (UVoxelChunk* Chunk = GetOrCreateChunkAtChunk(ChunkCoords))
    {
        //Chunk->GetSparseVoxelGrid()->SetVoxel(LocalVoxelIndex, Value, true);

        if (DiggerDebug::Space || DiggerDebug::Chunks)
        {
            UE_LOG(LogTemp, Display, TEXT("[SetVoxelAtWorldPosition] WorldPos: %s → Chunk: %s, Voxel: %s, Value: %.2f"),
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

    VoxelSize = TerrainGridSize / Subdivisions;
    float ChunkWorldSize = ChunkSize * TerrainGridSize;

    if (DiggerDebug::Space)
    {
        UE_LOG(LogTemp, Display, TEXT("ChunkSize=%d, Subdivisions=%d, VoxelSize=%.2f, ChunkWorldSize=%.2f"),
               ChunkSize, Subdivisions, VoxelSize, ChunkWorldSize);
    }

    // Compute chunk coordinates from world position
    FIntVector ChunkCoords = FVoxelConversion::WorldToChunk(ClickPosition);

    if (DiggerDebug::Chunks || DiggerDebug::Space || DiggerDebug::Brush)
    {
        UE_LOG(LogTemp, Display, TEXT("ClickPos: %s → ChunkCoords: %s"),
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

    // Compute chunk origin and extent
    FVector ChunkCenter = FVector(ChunkCoords) * ChunkWorldSize;
    FVector ChunkExtent = FVector(ChunkWorldSize * 0.5f);
    FVector ChunkOrigin = ChunkCenter - ChunkExtent;

    FVector LocalInChunk = ClickPosition - ChunkOrigin;
    FIntVector LocalVoxel = FVoxelConversion::WorldToLocalVoxel(ClickPosition);
    FVector LocalVoxelToWorld = FVoxelConversion::LocalVoxelToWorld(LocalVoxel);

    if (DiggerDebug::Space || DiggerDebug::Voxels || DiggerDebug::Chunks)
    {
        UE_LOG(LogTemp, Error, TEXT("LocalInChunk: %s, LocalVoxel: %s, WorldPosition: %s"),
               *LocalInChunk.ToString(), *LocalVoxel.ToString(), *LocalVoxelToWorld.ToString());
    }

    // World-space center of the voxel
    FVector VoxelCenter = ChunkOrigin + FVector(LocalVoxel) * VoxelSize + FVector(VoxelSize * 0.5f);

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
            FastDebug->DrawSphere(ChunkOrigin, 20.0f, 
                                 FFastDebugConfig(FLinearColor(1.0f, 0.5f, 0.0f), 30.0f, 2.0f));

            const int32 ChunkVoxels = ChunkSize * Subdivisions;
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
                        FVector CurrentVoxelCenter = ChunkOrigin + FVector(VoxelCoord) * VoxelSize + FVector(VoxelSize * 0.5f);

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
    FVoxelConversion::WorldToChunkAndVoxel(WorldPosition, ChunkCoords, VoxelIndex);

    if (!FVoxelConversion::IsValidVoxelIndex(VoxelIndex))
    {
        UE_LOG(LogTemp, Warning, TEXT("Invalid voxel index at world position %s: Chunk %s, VoxelIndex %s"),
            *WorldPosition.ToString(), *ChunkCoords.ToString(), *VoxelIndex.ToString());
        return;
    }

    // Get the voxel center in world space
    FVector VoxelCenter = FVoxelConversion::MinCornerVoxelToWorld(ChunkCoords, VoxelIndex);

    // Draw using fast debug system
    if (auto* FastDebug = UFastDebugSubsystem::Get(this))
    {
        FastDebug->DrawBox(VoxelCenter, FVector(FVoxelConversion::LocalVoxelSize * 0.5f), FRotator::ZeroRotator,
                          FFastDebugConfig(BoxColor, Duration, Thickness));
    }

    if (DiggerDebug::Chunks || DiggerDebug::Voxels)
    {
        UE_LOG(LogTemp, Log, TEXT("Drew voxel at world position %s: Chunk %s, VoxelIndex %s, Center %s"),
            *WorldPosition.ToString(), *ChunkCoords.ToString(), *VoxelIndex.ToString(), *VoxelCenter.ToString());
    }
}

// Updated ADiggerManager::DrawDiagonalDebugVoxels()
void ADiggerManager::DrawDiagonalDebugVoxelsFast(FIntVector ChunkCoords)
{
    // Get the fast debug subsystem
    auto* FastDebug = UFastDebugSubsystem::Get(this);
    if (!FastDebug) return;

    // Calculate chunk dimensions
    const int32 VoxelsPerChunk = FVoxelConversion::ChunkSize * FVoxelConversion::Subdivisions;
    const float DebugDuration = 30.0f;
    
    // Get the chunk center using the conversion method
    FVector ChunkCenter = FVoxelConversion::ChunkToWorld(ChunkCoords);
    FVector ChunkExtent = FVector(FVoxelConversion::ChunkSize * FVoxelConversion::TerrainGridSize / 2.0f);
    
    // Calculate the minimum corner based on the center and extent
    FVector ChunkMinCorner = ChunkCenter - ChunkExtent;
    
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
        FVector VoxelCenter = ChunkMinCorner + 
                             FVector(i + 0.5f, i + 0.5f, i + 0.5f) * FVoxelConversion::LocalVoxelSize;
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
    FVector overflowOffsets[] = {
        FVector(-1, -1, -1),  // Corner overflow
        FVector(-1, 0, 0),    // X-axis overflow
        FVector(0, -1, 0),    // Y-axis overflow
        FVector(0, 0, -1)     // Z-axis overflow
    };
    
    for (const FVector& offset : overflowOffsets)
    {
        FVector VoxelCenter = ChunkMinCorner + 
                             (offset + FVector(0.5f)) * FVoxelConversion::LocalVoxelSize;
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
    const float ChunkWorldSize = FVoxelConversion::ChunkSize * FVoxelConversion::TerrainGridSize;
    
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


UStaticMesh* ADiggerManager::ConvertIslandToStaticMesh(const FIslandData& Island, bool bWorldOrigin, FString AssetName)
{
    if (DiggerDebug::Islands)
        UE_LOG(LogTemp, Warning, TEXT("Converting island %d to static mesh..."));

    // 1. Generate mesh data for the island using Marching Cubes
    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FColor> Colors;
    TArray<FProcMeshTangent> Tangents;

/* Single Island Mesh Generation in DiggerManager
            // You may need to create a temporary SparseVoxelGrid for just this island
            USparseVoxelGrid* TempGrid = NewObject<USparseVoxelGrid>();
            
            //TempGrid->AddVoxels(IslandVoxels);

            // Generate mesh for just this island
            MarchingCubes->GenerateMeshForIsland(TempGrid, Vertices, Triangles, Normals, UVs, Colors, Tangents);

            // 4. Remove island voxels from the main grid
            //TempGrid->RemoveVoxels(IslandVoxels);
    */

    // For demonstration, we'll log and return nullptr if not implemented
    if (Vertices.Num() == 0 || Triangles.Num() == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("No mesh data generated for island!"));
        return nullptr;
    }

    // 2. Create a temporary ProceduralMeshComponent
    UProceduralMeshComponent* TempProcMesh = NewObject<UProceduralMeshComponent>(this);
    TempProcMesh->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, true);

    // 3. Convert ProceduralMeshComponent to StaticMesh
    FString PackageName = FString::Printf(TEXT("/Game/Islands/%s"), *AssetName);
    UPackage* MeshPackage = CreatePackage(*PackageName);

    UStaticMesh* NewStaticMesh = NewObject<UStaticMesh>(MeshPackage, *AssetName, RF_Public | RF_Standalone);
    if (!NewStaticMesh)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create UStaticMesh object!"));
        return nullptr;
    }

    // Use the built-in conversion utility (UE 4.23+)
    bool bSuccess = false;//TempProcMesh->CreateStaticMesh(NewStaticMesh, MeshPackage, AssetName, true);
    if (!bSuccess)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to convert procedural mesh to static mesh!"));
        return nullptr;
    }

    // 4. Register and save the asset
    FAssetRegistryModule::AssetCreated(NewStaticMesh);
    NewStaticMesh->MarkPackageDirty();

#if WITH_EDITOR
    // Notify the content browser
    TArray<UObject*> ObjectsToSync;
    ObjectsToSync.Add(NewStaticMesh);
    GEditor->SyncBrowserToObjects(ObjectsToSync);
#endif

    UE_LOG(LogTemp, Warning, TEXT("Static mesh asset created: %s"), *PackageName);

    return NewStaticMesh;
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


FIslandMeshData ADiggerManager::ExtractAndGenerateIslandMesh(const FVector& IslandCenter)
{
    UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Starting ExtractAndGenerateIslandMesh at IslandCenter: %s"), *IslandCenter.ToString());

    FIslandMeshData Result;

    // Step 1: Find or create the voxel chunk at the given center
    UVoxelChunk* Chunk = FindOrCreateNearestChunk(IslandCenter);
    if (!Chunk)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] No chunk found or created near IslandCenter."));
        return Result;
    }

    FVector ChunkOrigin = FVoxelConversion::ChunkToWorld(Chunk->GetChunkCoordinates());
    FVector LocalPosition = IslandCenter - ChunkOrigin;

    FIntVector VoxelCoords = FIntVector(
        FMath::FloorToInt(LocalPosition.X / VoxelSize),
        FMath::FloorToInt(LocalPosition.Y / VoxelSize),
        FMath::FloorToInt(LocalPosition.Z / VoxelSize)
    );

    FVector VoxelWorldCenter = ChunkOrigin + FVector(VoxelCoords) * VoxelSize + FVector(FVoxelConversion::LocalVoxelSize * 0.5f);

    UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Chunk Origin: %s | LocalPosition: %s | VoxelCoords: %s | VoxelWorldCenter: %s"),
        *ChunkOrigin.ToString(),
        *LocalPosition.ToString(),
        *VoxelCoords.ToString(),
        *VoxelWorldCenter.ToString()
    );

    // Visual debugging
    if (true)
    {
        // Draw debug string above search origin
        DrawDebugString(
            GetSafeWorld(),
            VoxelWorldCenter + FVector(0, 0, VoxelSize),
            TEXT("Search Origin"),
            nullptr,
            FColor::White,
            60.0f,
            false
        );

        // RED: Voxel being searched
        DrawDebugBox(
            GetSafeWorld(),
            VoxelWorldCenter,
            FVector(VoxelSize * 0.5f),
            FColor::Red,
            false,
            60.0f,
            0,
            3.0f
        );

        // GREEN: 3x3x3 area around it
        for (int32 dx = -1; dx <= 1; dx++)
        {
            for (int32 dy = -1; dy <= 1; dy++)
            {
                for (int32 dz = -1; dz <= 1; dz++)
                {
                    FIntVector Offset = VoxelCoords + FIntVector(dx, dy, dz);
                    FVector OffsetWorldCenter = ChunkOrigin + FVector(Offset) * VoxelSize + FVector(VoxelSize * 0.5f);

                    DrawDebugBox(
                        GetSafeWorld(),
                        OffsetWorldCenter,
                        FVector(VoxelSize * 0.5f),
                        FColor::Green,
                        false,
                        60.0f,
                        0,
                        1.5f
                    );
                }
            }
        }
    }

    // Step 2: Get voxel grid
    USparseVoxelGrid* VoxelGrid = Chunk->GetSparseVoxelGrid();
    if (!VoxelGrid)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] No SparseVoxelGrid found on chunk."));
        return Result;
    }
    
    if (true)
    {
        for (const auto& Pair : VoxelGrid->VoxelData)
        {
            FVector WorldPos = FVoxelConversion::LocalVoxelToWorld(Pair.Key);
            DrawDebugBox(GetSafeWorld(), WorldPos, FVector(VoxelSize * 0.5f), FColor::Blue, false, 60.0f, 0, 1.5f);
        }
    }

    // Step 3: Find the nearest set voxel and extract the island
    USparseVoxelGrid* ExtractedGrid = nullptr;
    TArray<FIntVector> IslandVoxels;

    FIntVector StartVoxel;
    if (!VoxelGrid->FindNearestSetVoxel(VoxelCoords, StartVoxel))
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] No set voxel found near the provided position."));
        return Result;
    }

    // Make a backup of the original grid before extraction
    USparseVoxelGrid* OriginalGridBackup = nullptr;
   /* if (bMakeBackupBeforeExtraction)
    {
        OriginalGridBackup = DuplicateObject<USparseVoxelGrid>(VoxelGrid, this);
    }*/

    // Run extraction to get surface voxels
    bool bExtractionSuccess = VoxelGrid->ExtractIslandAtPosition(
        FVoxelConversion::LocalVoxelToWorld(StartVoxel), 
        ExtractedGrid, 
        IslandVoxels
    );

    if (!bExtractionSuccess || !ExtractedGrid || IslandVoxels.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Failed to extract island or ExtractedGrid is null."));
        
        // Restore from backup if needed
        /*if (bMakeBackupBeforeExtraction && OriginalGridBackup)
        {
            UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Restoring grid from backup."));
            Chunk->SetSparseVoxelGrid(OriginalGridBackup);
        }*/
        
        return Result;
    }

    // Ensure the extracted grid has the necessary references
    //ExtractedGrid->ParentChunk = nullptr; // Don't want to affect the original chunk
    //ExtractedGrid->DiggerManager = this;

    // Step 4: Generate mesh
    UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Starting mesh generation using Marching Cubes."));

    // Use the chunk origin for mesh generation
    MarchingCubes->GenerateMeshFromGrid(
        ExtractedGrid, ChunkOrigin, VoxelSize,
        Result.Vertices, Result.Triangles, Result.Normals
    );
    
    
    // Step 5: Finalize result
    Result.MeshOrigin = ChunkOrigin;
    Result.bValid = (Result.Vertices.Num() > 0 && Result.Triangles.Num() > 0);

    if (Result.Vertices.Num() > 0 && Result.Triangles.Num() > 0 && Result.Normals.Num() > 0) {
        // Call mesh creation on game thread
        AsyncTask(ENamedThreads::GameThread, [=]()
        {
            int IslandId=0;
            MarchingCubes->CreateIslandProceduralMesh(Result.Vertices, Result.Triangles, Result.Normals, ChunkOrigin, IslandId);
        });
    } else {
        UE_LOG(LogTemp, Warning, TEXT("Island mesh generation returned empty data"));
    }

    // Debug visualization of the extracted island
    //if (IsDebugging && Result.bValid)
    //{
        for (const FIntVector& Voxel : IslandVoxels)
        {
            FVector WorldPos = ChunkOrigin + FVector(Voxel) * VoxelSize + FVector(VoxelSize * 0.5f);
            DrawDebugBox(GetSafeWorld(), WorldPos, FVector(VoxelSize * 0.4f), FColor::Yellow, false, 60.0f, 0, 2.0f);
        }
   //}

    UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Mesh generation complete. Valid: %s, Vertices: %d, Triangles: %d"),
        Result.bValid ? TEXT("true") : TEXT("false"),
        Result.Vertices.Num(),
        Result.Triangles.Num() / 3
    );

    // Mark the chunk as dirty to trigger a rebuild
    Chunk->MarkDirty();

    return Result;
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
            FVoxelConversion::GlobalVoxelToChunkAndLocal_CenterAligned(GlobalVoxel, ChunkCoords, LocalVoxel);

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
    if (!MeshData.bValid) return nullptr;
    

    AIslandActor* IslandActor = World->SpawnActor<AIslandActor>(AIslandActor::StaticClass(), SpawnLocation, FRotator::ZeroRotator);
    if (IslandActor && IslandActor->ProcMesh)
    {
                // Feed the mesh vertices as-is. We attempted to shift the vertices
                // relative to the spawn location, but this prevented island detection
                // from working correctly when converting islands to static or physics
                // actors. Using the original world-space vertices keeps conversion
                // functionality intact.
                // Use vertices relative to the spawn location so the mesh is correctly
                // positioned when the actor is spawned at SpawnLocation
                TArray<FVector> LocalVertices = MeshData.Vertices;
        for (FVector& Vertex : LocalVertices)
        {
            Vertex -= SpawnLocation;
        }

                IslandActor->ProcMesh->CreateMeshSection_LinearColor(
                    0, LocalVertices, MeshData.Triangles, MeshData.Normals, {}, {}, {}, true
                );
        if (bEnablePhysics)
            IslandActor->ApplyPhysics();
        else
            IslandActor->RemovePhysics();
    }
    return IslandActor;
}




void ADiggerManager::ConvertIslandAtPositionToStaticMesh(const FVector& IslandCenter)
{
    UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] ConvertIslandAtPositionToStaticMesh called at %s"), *IslandCenter.ToString());
    FIslandMeshData MeshData = ExtractAndGenerateIslandMesh(IslandCenter);
    if (MeshData.bValid)
    {
        FString AssetName = FString::Printf(TEXT("Island_%s_StaticMesh"), *IslandCenter.ToString());
        SaveIslandMeshAsStaticMesh(AssetName, MeshData);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] ConvertIslandAtPositionToStaticMesh called with invalid static mesh data at %s"), *IslandCenter.ToString());
    }
}

void ADiggerManager::ConvertIslandAtPositionToActor(const FVector& IslandCenter, bool bEnablePhysics, FIntVector ReferenceVoxel)
{
    UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] ConvertIslandAtPositionToActor called at %s"), *IslandCenter.ToString());

    // Debug visualization
    DrawDebugSphere(GetSafeWorld(), IslandCenter, 50.0f, 16, FColor::Red, false, 10.0f, 0, 2.0f);
    DrawDebugString(GetSafeWorld(), IslandCenter + FVector(0, 0, 60.0f), TEXT("Island Search Point"), nullptr, FColor::White, 10.0f);

    if (ReferenceVoxel != FIntVector::ZeroValue)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Using reference voxel: %s"), *ReferenceVoxel.ToString());

        // Convert global voxel index to chunk + local voxel
        FIntVector ChunkCoords, LocalVoxel;
        FVoxelConversion::GlobalVoxelToChunkAndLocal_CenterAligned(ReferenceVoxel, ChunkCoords, LocalVoxel);

        // Find the chunk containing this voxel
        UVoxelChunk* Chunk = ChunkMap.FindRef(ChunkCoords);
        if (!Chunk)
        {
            UE_LOG(LogTemp, Error, TEXT("[DiggerPro] No chunk found for chunk coords: %s"), *ChunkCoords.ToString());
            return;
        }

        // Verify the voxel exists in the grid before extraction
        USparseVoxelGrid* Grid = Chunk->GetSparseVoxelGrid();
        if (!Grid || !Grid->IsVoxelSolid(LocalVoxel))
        {
            UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Reference voxel not found or not solid at local coords: %s"), *LocalVoxel.ToString());
            
            // Fallback to center-based extraction
            FIslandMeshData MeshData = ExtractIslandByCenter(IslandCenter, false, bEnablePhysics);
            if (MeshData.bValid)
            {
                AIslandActor* IslandActor = SpawnIslandActorWithMeshData(IslandCenter, MeshData, bEnablePhysics);
                if (IslandActor)
                {
                    UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Successfully created island actor using center-based extraction"));
                }
            }
            return;
        }

        // Assuming you have an FIslandData from your island detection system
        FIslandData MyIsland; // populated elsewhere
        FIslandMeshData MeshData = ExtractAndGenerateIslandMeshFromData(Chunk, MyIsland);

        if (MeshData.bValid)
        {
            // Use the generated mesh data
            UE_LOG(LogTemp, Log, TEXT("Successfully generated mesh for island at %s"), 
                   *MyIsland.Location.ToString());
        }
    }
    else
    {
        // Fallback to center-based extraction
        FIslandMeshData MeshData = ExtractIslandByCenter(IslandCenter, false, bEnablePhysics);
        if (MeshData.bValid)
        {
            AIslandActor* IslandActor = SpawnIslandActorWithMeshData(IslandCenter, MeshData, bEnablePhysics);
            if (IslandActor)
            {
                UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Successfully created island actor using center-based extraction"));
            }
        }
    }
}


// New function to extract island mesh from a specific voxel
FIslandMeshData ADiggerManager::ExtractIslandByCenter(const FVector& IslandCenter, bool bRemoveAfter, bool bEnablePhysics)
{
    FIslandMeshData Result;

    // Step 1: Get all islands from unified grid
    TArray<FIslandData> AllIslands = DetectUnifiedIslands();
    if (AllIslands.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] No islands found during extraction."));
        return Result;
    }

    // Step 2: Find closest island to center
    FIslandData* ClosestIsland = nullptr;
    float MinDist = FLT_MAX;

    for (FIslandData& Island : AllIslands)
    {
        float Dist = FVector::DistSquared(Island.Location, IslandCenter);
        if (Dist < MinDist)
        {
            MinDist = Dist;
            ClosestIsland = &Island;
        }
    }

    if (!ClosestIsland)
    {
        UE_LOG(LogTemp, Error, TEXT("[DiggerPro] No matching island found near %s"), *IslandCenter.ToString());
        return Result;
    }

    // Step 3: Build temporary grid from island voxels
    USparseVoxelGrid* TempGrid = NewObject<USparseVoxelGrid>();
    TMap<FIntVector, FVoxelData> IslandVoxelMap;

    for (const FIntVector& Global : ClosestIsland->Voxels)
    {
        FIntVector ChunkCoords, LocalVoxel;
        FVoxelConversion::GlobalVoxelToChunkAndLocal_CenterAligned(Global, ChunkCoords, LocalVoxel);

        UVoxelChunk** ChunkPtr = ChunkMap.Find(ChunkCoords);
        if (!ChunkPtr || !*ChunkPtr) continue;

        USparseVoxelGrid* Grid = (*ChunkPtr)->GetSparseVoxelGrid();
        if (!Grid) continue;

        const FVoxelData* Data = Grid->GetVoxelData(LocalVoxel);
        if (Data)
        {
            IslandVoxelMap.Add(Global, *Data);
        }
    }

    TempGrid->SetVoxelData(IslandVoxelMap);
    TempGrid->Initialize(nullptr); // no parent

    // Step 4: Generate mesh
    MarchingCubes->GenerateMeshFromGrid(
        TempGrid, FVector::ZeroVector, VoxelSize,
        Result.Vertices, Result.Triangles, Result.Normals
    );

    Result.MeshOrigin = FVector::ZeroVector;
    Result.bValid = Result.Vertices.Num() > 0;

    // Step 5: Optionally remove island from grid
    if (bRemoveAfter)
    {
        RemoveIslandVoxels(*ClosestIsland);
    }

    return Result;
}

// New function to extract island mesh from a specific voxel
FIslandMeshData ADiggerManager::ExtractAndGenerateIslandMeshFromData(UVoxelChunk* Chunk, const FIslandData& IslandData)
{
    FIslandMeshData Result;
    
    if (!Chunk)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] No chunk provided for extraction."));
        return Result;
    }
    
    USparseVoxelGrid* VoxelGrid = Chunk->GetSparseVoxelGrid();
    if (!VoxelGrid)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] No SparseVoxelGrid found on chunk."));
        return Result;
    }
    
    if (IslandData.Voxels.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Island data contains no voxels."));
        return Result;
    }
    
    // Validate that VoxelCount matches actual array size
    if (IslandData.VoxelCount != IslandData.Voxels.Num())
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Island VoxelCount (%d) doesn't match Voxels array size (%d)"), 
               IslandData.VoxelCount, IslandData.Voxels.Num());
    }
    
    FVector ChunkOrigin = FVoxelConversion::ChunkToWorld(Chunk->GetChunkCoordinates());
    
    // Create a temporary grid for the island
    USparseVoxelGrid* ExtractedGrid = NewObject<USparseVoxelGrid>();
    
    UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Extracting island at location %s with %d voxels (reference: %s)"), 
           *IslandData.Location.ToString(), 
           IslandData.Voxels.Num(),
           *IslandData.ReferenceVoxel.ToString());
    
    // Copy voxel data directly from the island data
    TMap<FIntVector, FVoxelData> ExtractedVoxelData;
    int32 ValidVoxels = 0;
    
    for (const FIntVector& Pos : IslandData.Voxels)
    {
        FVoxelData* Data = VoxelGrid->GetVoxelData(Pos);
        if (Data)
        {
            ExtractedVoxelData.Add(Pos, *Data);
            ValidVoxels++;
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Island voxel at %s not found in grid"), *Pos.ToString());
        }
    }
    
    if (ValidVoxels == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] No valid voxel data found for island."));
        return Result;
    }
    
    // Convert Voxels array to set for faster lookups when checking boundaries
    TSet<FIntVector> IslandVoxelSet(IslandData.Voxels);
    
    // Add boundary voxels for proper mesh generation
    TSet<FIntVector> BoundaryVoxels;
    for (const FIntVector& SolidVoxel : IslandData.Voxels)
    {
        for (int32 i = 0; i < 6; i++)
        {
            FIntVector Neighbor = SolidVoxel + FVoxelConversion::GetDirectionVector(i);
            
            // Only add boundary if it's not part of the island and not already added
            if (!IslandVoxelSet.Contains(Neighbor) && !BoundaryVoxels.Contains(Neighbor))
            {
                BoundaryVoxels.Add(Neighbor);
                
                // Check if this neighbor exists in the original grid
                FVoxelData* NeighborData = VoxelGrid->GetVoxelData(Neighbor);
                if (NeighborData)
                {
                    ExtractedVoxelData.Add(Neighbor, *NeighborData);
                }
                // Missing neighbors are implicitly AIR in sparse grid
            }
        }
    }
    
    // Set all the voxel data at once
    ExtractedGrid->SetVoxelData(ExtractedVoxelData);
    ExtractedGrid->Initialize(nullptr);
    
    UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Extracted grid contains %d voxels (%d solid + %d boundary)"), 
           ExtractedVoxelData.Num(), ValidVoxels, BoundaryVoxels.Num());
    
    // Generate mesh using marching cubes
    MarchingCubes->GenerateMeshFromGrid(
        ExtractedGrid, ChunkOrigin, VoxelSize,
        Result.Vertices, Result.Triangles, Result.Normals
    );
    
    Result.MeshOrigin = ChunkOrigin;
    Result.bValid = (Result.Vertices.Num() > 0 && Result.Triangles.Num() > 0);
    
    UE_LOG(LogTemp, Log, TEXT("[DiggerPro] Island mesh generation complete. Valid Voxels: %d, Vertices: %d, Triangles: %d"),
        ValidVoxels,
        Result.Vertices.Num(),
        Result.Triangles.Num() / 3
    );
    
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



void ADiggerManager::SaveIslandMeshAsStaticMesh(
    const FString& AssetName,
    const FIslandMeshData& MeshData
)
{
    if (!MeshData.bValid) return;

    TArray<FVector3f> FloatVerts = ConvertArray<FVector, FVector3f>(MeshData.Vertices);
    TArray<FVector3f> FloatNormals = ConvertArray<FVector, FVector3f>(MeshData.Normals);
    
    CreateStaticMeshFromRawData(
        GetTransientPackage(),
        AssetName,
        FloatVerts,
        MeshData.Triangles,
        FloatNormals,
        {}, {}, {}
    );
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
        VoxelSize = TerrainGridSize / Subdivisions;
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
        // We're close enough — use precise sample
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
                            FVector WorldPosition = FVoxelConversion::LocalVoxelToWorld(FIntVector(VoxelX, VoxelY, VoxelZ));

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
            const FVector ChunkPosition = FVoxelConversion::ChunkToWorld(Chunk->GetChunkCoordinates());
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
    FIntVector ChunkCoords = FVoxelConversion::WorldToChunk(Position);
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

        FVector WorldPos = FVoxelConversion::ChunkToWorld(Chunk->GetChunkCoordinates());
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
        ChunkCoords = FVoxelConversion::WorldToChunk(Position);
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
    FIntVector ChunkCoords = FVoxelConversion::WorldToChunk(Position);
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
        FVector WorldPos = FVoxelConversion::ChunkToWorld(Entry.Value->GetChunkCoordinates());
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
    int32 Reach = FMath::CeilToInt(Radius / (ChunkSize * TerrainGridSize));

    UE_LOG(LogTemp, Warning, TEXT("MarkNearbyChunksDirty: CenterPosition=%s, Radius=%f"), *CenterPosition.ToString(), Radius);
    UE_LOG(LogTemp, Warning, TEXT("FVoxelConversion: ChunkSize=%d, TerrainGridSize=%f, Origin=%s"), FVoxelConversion::ChunkSize, FVoxelConversion::TerrainGridSize, *FVoxelConversion::Origin.ToString());
    FIntVector CenterChunkCoords = FVoxelConversion::WorldToChunk(CenterPosition);
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

TArray<FIslandData> ADiggerManager::DetectUnifiedIslands()
{
    // Step 1: Build NON-DEDUPLICATED voxel map with ALL physical instances
    TMap<FIntVector, FVoxelData> UnifiedVoxelData; // This will be deduplicated for island detection
    TArray<FVoxelInstance> AllPhysicalVoxelInstances; // This preserves ALL instances including overflow
    
    for (const auto& Pair : ChunkMap)
    {
        UVoxelChunk* Chunk = Pair.Value;
        if (!Chunk || !Chunk->IsValidLowLevel()) continue;

        USparseVoxelGrid* Grid = Chunk->GetSparseVoxelGrid();
        if (!Grid || !Grid->IsValidLowLevel()) continue;

        FIntVector ChunkCoords = Chunk->GetChunkCoordinates();

        for (const auto& VoxelPair : Grid->GetAllVoxels())
        {
            const FIntVector& LocalIndex = VoxelPair.Key;
            const FVoxelData& Data = VoxelPair.Value;

            FIntVector GlobalIndex = FVoxelConversion::ChunkAndLocalToGlobalVoxel_CenterAligned(ChunkCoords, LocalIndex);
            
            // Store for deduplicated island detection
            UnifiedVoxelData.Add(GlobalIndex, Data);
            
            // Store ALL physical instances (including overflow duplicates)
            FVoxelInstance Instance(GlobalIndex, ChunkCoords, LocalIndex);
            AllPhysicalVoxelInstances.Add(Instance);
            
            // Special logging for negative overflow voxels
            if (LocalIndex.X == -1 || LocalIndex.Y == -1 || LocalIndex.Z == -1)
            {
                if (DiggerDebug::Islands)
                UE_LOG(LogTemp, Warning, 
                    TEXT("[DiggerPro] NEGATIVE OVERFLOW DETECTED: Global %s -> Chunk %s -> Local %s"),
                    *GlobalIndex.ToString(), *ChunkCoords.ToString(), *LocalIndex.ToString());
            }
        }
    }

    if (DiggerDebug::Islands)
    {
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Total physical voxel instances collected: %d"), AllPhysicalVoxelInstances.Num());
        UE_LOG(LogTemp, Warning, TEXT("[DiggerPro] Deduplicated voxels for island detection: %d"), UnifiedVoxelData.Num());
    }
    
    // Step 2: Create temporary sparse voxel grid for island detection (using deduplicated data)
    USparseVoxelGrid* TempGrid = NewObject<USparseVoxelGrid>();
    TempGrid->SetVoxelData(UnifiedVoxelData);

    // Step 3: Detect islands globally (this gives us deduplicated island boundaries)
    TArray<FIslandData> DeduplicatedIslands = TempGrid->DetectIslands(0.0f);

    // Step 4: For each deduplicated island, collect ALL physical instances that belong to it
    TArray<FIslandData> FinalIslands;
    
    for (const FIslandData& DeduplicatedIsland : DeduplicatedIslands)
    {
        FIslandData EnhancedIsland;
        
        // Copy the deduplicated island info for UI reporting
        EnhancedIsland.Location = DeduplicatedIsland.Location;
        EnhancedIsland.VoxelCount = DeduplicatedIsland.VoxelCount; // This is the deduplicated count for UI
        EnhancedIsland.Voxels = DeduplicatedIsland.Voxels; // Deduplicated global voxels for UI
        EnhancedIsland.ReferenceVoxel = DeduplicatedIsland.ReferenceVoxel;
        
        // Now find ALL physical instances that belong to this island
        TSet<FIntVector> IslandGlobalVoxels(DeduplicatedIsland.Voxels);
        
        for (const FVoxelInstance& Instance : AllPhysicalVoxelInstances)
        {
            // If this physical instance belongs to the current island, include it
            if (IslandGlobalVoxels.Contains(Instance.GlobalVoxel))
            {
                EnhancedIsland.VoxelInstances.Add(Instance);
                
                // Special logging for negative overflow voxels being added to islands
                if (Instance.LocalVoxel.X == -1 || Instance.LocalVoxel.Y == -1 || Instance.LocalVoxel.Z == -1)
                {
                    if (DiggerDebug::Islands)
                    UE_LOG(LogTemp, Warning, 
                        TEXT("[DiggerPro] NEGATIVE OVERFLOW ADDED TO ISLAND: Global %s -> Chunk %s -> Local %s"),
                        *Instance.GlobalVoxel.ToString(), *Instance.ChunkCoords.ToString(), *Instance.LocalVoxel.ToString());
                }
            }
            else if (Instance.LocalVoxel.X == -1 || Instance.LocalVoxel.Y == -1 || Instance.LocalVoxel.Z == -1)
            {
                // Log negative overflow voxels that DON'T belong to any island
                if (DiggerDebug::Islands)
                UE_LOG(LogTemp, Error, 
                    TEXT("[DiggerPro] ORPHANED NEGATIVE OVERFLOW: Global %s -> Chunk %s -> Local %s (not in any island)"),
                    *Instance.GlobalVoxel.ToString(), *Instance.ChunkCoords.ToString(), *Instance.LocalVoxel.ToString());
            }
        }
        
        FinalIslands.Add(EnhancedIsland);

        if (DiggerDebug::Islands)
        UE_LOG(LogTemp, Warning, 
            TEXT("[DiggerPro] Island complete: %d unique voxels (UI) -> %d total physical instances (removal)"),
            EnhancedIsland.VoxelCount, EnhancedIsland.VoxelInstances.Num());
    }

    // Step 5: Broadcast ONLY the deduplicated islands to UI (clean reporting)
    if (OnIslandsDetectionStarted.IsBound())  // or check .ExecuteIfBound with guards in the toolkit
    OnIslandsDetectionStarted.Broadcast();

    for (const FIslandData& Island : FinalIslands)
    {
        // Create a clean version for broadcasting (no VoxelInstances array)
        FIslandData BroadcastIsland;
        BroadcastIsland.Location = Island.Location;
        BroadcastIsland.VoxelCount = Island.VoxelCount; // Deduplicated count
        BroadcastIsland.Voxels = Island.Voxels; // Deduplicated voxels
        BroadcastIsland.ReferenceVoxel = Island.ReferenceVoxel;

        if (!OnIslandDetected.IsBound())
        {
            if (DiggerDebug::Islands)
                UE_LOG(LogTemp, Error, TEXT("OnIslandDetected not Bound."));
            continue;  // or check .ExecuteIfBound with guards in the toolkit
        }
        OnIslandDetected.Broadcast(BroadcastIsland);
        if (DiggerDebug::Islands)
        UE_LOG(LogTemp, Warning, TEXT("Unified Island Broadcast at %s with %d voxels"),
            *BroadcastIsland.Location.ToString(), BroadcastIsland.VoxelCount);
    }

    // Step 6: Return enhanced islands with ALL physical instances for removal
    return FinalIslands;
}


// Main function: always works in chunk coordinates
UVoxelChunk* ADiggerManager::GetOrCreateChunkAtChunk(const FIntVector& ChunkCoords)
{
    //Run Init on the static FVoxelCoversion struct so all of our conversions work properly even if the settings have changed!
    FVoxelConversion::InitFromConfig(ChunkSize,Subdivisions,TerrainGridSize, GetActorLocation());

    if (DiggerDebug::Chunks)
    UE_LOG(LogTemp, Warning, TEXT("➡️ Attempting chunk at coords: %s"), *ChunkCoords.ToString());
    
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
    FIntVector ChunkCoords = FVoxelConversion::WorldToChunk(WorldPosition);
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
            FVoxelConversion::GlobalVoxelToChunkAndLocal_CenterAligned(
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
    FVoxelConversion::GlobalVoxelToChunkAndLocal_CenterAligned(GlobalVoxel, CanonicalChunk, LocalVoxelOut);
    
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
                FVoxelConversion::GlobalVoxelToChunkAndLocal_CenterAligned(
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
                FVoxelConversion::GlobalVoxelToChunkAndLocal_CenterAligned(AdjustedGlobal, CandidateChunkCoords, LocalVoxel);

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
    FIntVector ChunkCoords = FVoxelConversion::WorldToChunk(WorldPosition);
    if (UVoxelChunk* Chunk = GetOrCreateChunkAtChunk(ChunkCoords))
    {
        //ToDo: set shape type based on brush used!
        Chunk->SpawnHole(HoleBPClass, WorldPosition, Rotation, Scale, EHoleShapeType::Sphere);
    }
}

bool ADiggerManager::RemoveHoleNear(FVector WorldPosition, float MaxDistance)
{
    FIntVector ChunkCoords = FVoxelConversion::WorldToChunk(WorldPosition);
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