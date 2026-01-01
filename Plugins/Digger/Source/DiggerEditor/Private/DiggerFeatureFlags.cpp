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
bool FDiggerFeatureFlags::bEnableExportData = true;
bool FDiggerFeatureFlags::bEnableBuild = false;
bool FDiggerFeatureFlags::bEnableDeveloperSettings = false;

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
bool FDiggerFeatureFlags::bEnableGenerationSection = false;

void FDiggerFeatureFlags::LoadFlagsFromPluginConfig()
{
	// Ensure this runs only once per editor session.
	if (bLoaded)
	{
		return;
	}
	bLoaded = true;

	// Section name in the ini
	const TCHAR* Section = TEXT("/Script/DiggerEditor.FeatureFlags");

	// Candidate locations - try project plugins folder first, then engine plugins folder.
	TArray<FString> CandidatePaths;
	{
		// Plugins/Digger/Config/FeatureFlags.ini inside the project
		FString ProjectPluginConfig = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("Digger/Config/FeatureFlags.ini"));
		CandidatePaths.Add(ProjectPluginConfig);

		// Plugins/Digger/Config/FeatureFlags.ini inside the engine (if distributed there)
		FString EnginePluginConfig = FPaths::Combine(FPaths::EnginePluginsDir(), TEXT("Digger/Config/FeatureFlags.ini"));
		CandidatePaths.Add(EnginePluginConfig);

		// Also try the relative Plugins folder in case running from a module root
		FString RelativePluginConfig = FPaths::Combine(FPaths::ProjectDir(), TEXT("Plugins/Digger/Config/FeatureFlags.ini"));
		CandidatePaths.Add(RelativePluginConfig);
	}

	// If no config file exists in any of the candidate locations, keep defaults and return gracefully.
	FString FoundPath;
	for (const FString& Path : CandidatePaths)
	{
		if (IFileManager::Get().FileExists(*Path))
		{
			FoundPath = Path;
			break;
		}
	}

	// If no file found, leave defaults and return quietly (required for runtime/shipping).
	if (FoundPath.IsEmpty())
	{
		return;
	}

	// Read booleans from the found ini file. If a key is missing, the default (already set above) remains.
	bool TempBool = false;
	
	GConfig->GetBool(Section, TEXT("bEnableBrushTools"), TempBool, *FoundPath);
	bEnableBrushTools = TempBool;

	GConfig->GetBool(Section, TEXT("bEnableEnvironment"), TempBool, *FoundPath);
	bEnableEnvironment = TempBool;

	GConfig->GetBool(Section, TEXT("bEnableIslands"), TempBool, *FoundPath);
	bEnableIslands = TempBool;

	GConfig->GetBool(Section, TEXT("bEnableAdditionalTools"), TempBool, *FoundPath);
	bEnableAdditionalTools = TempBool;

	GConfig->GetBool(Section, TEXT("bEnableExportData"), TempBool, *FoundPath);
	bEnableExportData = TempBool;

	GConfig->GetBool(Section, TEXT("bEnableDeveloperSettings"), TempBool, *FoundPath);
	bEnableDeveloperSettings = TempBool;

	// Per-brush flags
	GConfig->GetBool(Section, TEXT("bEnableBrush_Sphere"), TempBool, *FoundPath);
	bEnableBrush_Sphere = TempBool;

	GConfig->GetBool(Section, TEXT("bEnableBrush_Cube"), TempBool, *FoundPath);
	bEnableBrush_Cube = TempBool;

	GConfig->GetBool(Section, TEXT("bEnableBrush_Cylinder"), TempBool, *FoundPath);
	bEnableBrush_Cylinder = TempBool;

	GConfig->GetBool(Section, TEXT("bEnableBrush_Capsule"), TempBool, *FoundPath);
	bEnableBrush_Capsule = TempBool;

	GConfig->GetBool(Section, TEXT("bEnableBrush_Cone"), TempBool, *FoundPath);
	bEnableBrush_Cone = TempBool;

	GConfig->GetBool(Section, TEXT("bEnableBrush_Torus"), TempBool, *FoundPath);
	bEnableBrush_Torus = TempBool;

	GConfig->GetBool(Section, TEXT("bEnableBrush_Pyramid"), TempBool, *FoundPath);
	bEnableBrush_Pyramid = TempBool;

	GConfig->GetBool(Section, TEXT("bEnableBrush_Icosphere"), TempBool, *FoundPath);
	bEnableBrush_Icosphere = TempBool;

	GConfig->GetBool(Section, TEXT("bEnableBrush_Stairs"), TempBool, *FoundPath);
	bEnableBrush_Stairs = TempBool;

	GConfig->GetBool(Section, TEXT("bEnableBrush_Custom"), TempBool, *FoundPath);
	bEnableBrush_Custom = TempBool;

	GConfig->GetBool(Section, TEXT("bEnableBrush_Smooth"), TempBool, *FoundPath);
	bEnableBrush_Smooth = TempBool;

	GConfig->GetBool(Section, TEXT("bEnableBrush_Noise"), TempBool, *FoundPath);
	bEnableBrush_Noise = TempBool;

	GConfig->GetBool(Section, TEXT("bEnableBrush_Light"), TempBool, *FoundPath);
	bEnableBrush_Light = TempBool;

	GConfig->GetBool(Section, TEXT("bEnableBrush_Debug"), TempBool, *FoundPath);
	bEnableBrush_Debug = TempBool;

	GConfig->GetBool(Section, TEXT("bEnableGenerationSection"), TempBool, *FoundPath);
	bEnableGenerationSection = TempBool;
}