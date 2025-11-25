#include "DiggerFeatureFlags.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Misc/OutputDevice.h"

// Initialize defaults to match the example and to be safe if no config is found.
bool FDiggerFeatureFlags::bLoaded = false;
bool FDiggerFeatureFlags::bEnableBrushTools = true;
bool FDiggerFeatureFlags::bEnableBrushShapes = true;
bool FDiggerFeatureFlags::bEnableCustomBrushes = false;
bool FDiggerFeatureFlags::bEnableEnvironment = true;
bool FDiggerFeatureFlags::bEnableIslands = false;
bool FDiggerFeatureFlags::bEnableAdditionalTools = false;
bool FDiggerFeatureFlags::bEnableDMM = false;
bool FDiggerFeatureFlags::bEnableExportData = false;
bool FDiggerFeatureFlags::bEnableBuild = false;
bool FDiggerFeatureFlags::bEnableDeveloperSettings = true;

// Per-brush defaults (true = enabled by default; false = disabled)
bool FDiggerFeatureFlags::bEnableBrush_Sphere      = true;
bool FDiggerFeatureFlags::bEnableBrush_Cube        = true;
bool FDiggerFeatureFlags::bEnableBrush_Cylinder    = false;
bool FDiggerFeatureFlags::bEnableBrush_Capsule     = false;
bool FDiggerFeatureFlags::bEnableBrush_Cone        = false;
bool FDiggerFeatureFlags::bEnableBrush_Torus       = false;
bool FDiggerFeatureFlags::bEnableBrush_Pyramid     = false;
bool FDiggerFeatureFlags::bEnableBrush_Icosphere   = false;
bool FDiggerFeatureFlags::bEnableBrush_Stairs      = false;
bool FDiggerFeatureFlags::bEnableBrush_Custom      = false;
bool FDiggerFeatureFlags::bEnableBrush_Smooth      = false;
bool FDiggerFeatureFlags::bEnableBrush_Noise       = false;
bool FDiggerFeatureFlags::bEnableBrush_Light       = true;
bool FDiggerFeatureFlags::bEnableBrush_Debug       = false;
bool FDiggerFeatureFlags::bShowHiddenSeam          = false;
bool FDiggerFeatureFlags::bEnableGenerationSection = true;

void FDiggerFeatureFlags::LoadFlagsFromPluginConfig()
{
    if (bLoaded)
    {
        return;
    }
    bLoaded = true;

    const TCHAR* Section = TEXT("/Script/DiggerEditor.FeatureFlags");

    // Candidate paths to check
    TArray<FString> CandidatePaths;
    CandidatePaths.Add(FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("Digger/Config/FeatureFlags.ini")));
    CandidatePaths.Add(FPaths::Combine(FPaths::EnginePluginsDir(), TEXT("Digger/Config/FeatureFlags.ini")));
    CandidatePaths.Add(FPaths::Combine(FPaths::ProjectDir(), TEXT("Plugins/Digger/Config/FeatureFlags.ini")));

    // Find the first existing file
    FString FoundPath;
    for (const FString& Path : CandidatePaths)
    {
        if (IFileManager::Get().FileExists(*Path))
        {
            FoundPath = Path;
            break;
        }
    }

    // Nothing found? Keep defaults.
    if (FoundPath.IsEmpty())
    {
        return;
    }

    bool TempBool = false;

    // --- MAIN FEATURE FLAGS ---
    if (GConfig->GetBool(Section, TEXT("bEnableBrushTools"), TempBool, *FoundPath))
        bEnableBrushTools = TempBool;

    if (GConfig->GetBool(Section, TEXT("bEnableBrushShapes"), TempBool, *FoundPath))
        bEnableBrushShapes = TempBool;

    if (GConfig->GetBool(Section, TEXT("bEnableCustomBrushes"), TempBool, *FoundPath))
        bEnableCustomBrushes = TempBool;

    if (GConfig->GetBool(Section, TEXT("bEnableEnvironment"), TempBool, *FoundPath))
        bEnableEnvironment = TempBool;

    if (GConfig->GetBool(Section, TEXT("bEnableIslands"), TempBool, *FoundPath))
        bEnableIslands = TempBool;

    if (GConfig->GetBool(Section, TEXT("bEnableAdditionalTools"), TempBool, *FoundPath))
        bEnableAdditionalTools = TempBool;

    if (GConfig->GetBool(Section, TEXT("bEnableDMM"), TempBool, *FoundPath))
        bEnableDMM = TempBool;

    if (GConfig->GetBool(Section, TEXT("bEnableExportData"), TempBool, *FoundPath))
        bEnableExportData = TempBool;

    if (GConfig->GetBool(Section, TEXT("bEnableBuild"), TempBool, *FoundPath))
        bEnableBuild = TempBool;

    if (GConfig->GetBool(Section, TEXT("bEnableDeveloperSettings"), TempBool, *FoundPath))
        bEnableDeveloperSettings = TempBool;

    // --- PER-BRUSH FLAGS ---
    if (GConfig->GetBool(Section, TEXT("bEnableBrush_Sphere"), TempBool, *FoundPath))
        bEnableBrush_Sphere = TempBool;

    if (GConfig->GetBool(Section, TEXT("bEnableBrush_Cube"), TempBool, *FoundPath))
        bEnableBrush_Cube = TempBool;

    if (GConfig->GetBool(Section, TEXT("bEnableBrush_Cylinder"), TempBool, *FoundPath))
        bEnableBrush_Cylinder = TempBool;

    if (GConfig->GetBool(Section, TEXT("bEnableBrush_Capsule"), TempBool, *FoundPath))
        bEnableBrush_Capsule = TempBool;

    if (GConfig->GetBool(Section, TEXT("bEnableBrush_Cone"), TempBool, *FoundPath))
        bEnableBrush_Cone = TempBool;

    if (GConfig->GetBool(Section, TEXT("bEnableBrush_Torus"), TempBool, *FoundPath))
        bEnableBrush_Torus = TempBool;

    if (GConfig->GetBool(Section, TEXT("bEnableBrush_Pyramid"), TempBool, *FoundPath))
        bEnableBrush_Pyramid = TempBool;

    if (GConfig->GetBool(Section, TEXT("bEnableBrush_Icosphere"), TempBool, *FoundPath))
        bEnableBrush_Icosphere = TempBool;

    if (GConfig->GetBool(Section, TEXT("bEnableBrush_Stairs"), TempBool, *FoundPath))
        bEnableBrush_Stairs = TempBool;

    if (GConfig->GetBool(Section, TEXT("bEnableBrush_Custom"), TempBool, *FoundPath))
        bEnableBrush_Custom = TempBool;

    if (GConfig->GetBool(Section, TEXT("bEnableBrush_Smooth"), TempBool, *FoundPath))
        bEnableBrush_Smooth = TempBool;

    if (GConfig->GetBool(Section, TEXT("bEnableBrush_Noise"), TempBool, *FoundPath))
        bEnableBrush_Noise = TempBool;

    if (GConfig->GetBool(Section, TEXT("bEnableBrush_Light"), TempBool, *FoundPath))
        bEnableBrush_Light = TempBool;

    if (GConfig->GetBool(Section, TEXT("bEnableBrush_Debug"), TempBool, *FoundPath))
        bEnableBrush_Debug = TempBool;
    
    if (GConfig->GetBool(Section, TEXT("bShowHiddenSeam"), TempBool, *FoundPath))
        bShowHiddenSeam = TempBool;

    if (GConfig->GetBool(Section, TEXT("bEnableGenerationSection"), TempBool, *FoundPath))
        bEnableGenerationSection = TempBool;
}
