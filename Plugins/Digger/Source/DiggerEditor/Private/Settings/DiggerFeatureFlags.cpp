#include "DiggerFeatureFlags.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Misc/OutputDevice.h"

// Defaults for closed testing - curated for testers.
// These are overridden by Config/FeatureFlags.ini if present.
bool FDiggerFeatureFlags::bLoaded                  = false;
bool FDiggerFeatureFlags::bEnableBrushTools        = true;
bool FDiggerFeatureFlags::bEnableBrushShapes       = true;
bool FDiggerFeatureFlags::bEnableCustomBrushes     = false;
bool FDiggerFeatureFlags::bEnableEnvironment       = true;
bool FDiggerFeatureFlags::bEnableNavigation        = true;
bool FDiggerFeatureFlags::bEnableWorklight         = true;
bool FDiggerFeatureFlags::bEnableAdditionalTools   = false;
bool FDiggerFeatureFlags::bEnableIslands           = true;
bool FDiggerFeatureFlags::bEnableMaterialManager   = true;
bool FDiggerFeatureFlags::bEnableCaveImporter      = false;
bool FDiggerFeatureFlags::bEnableDMM               = false;
bool FDiggerFeatureFlags::bEnableExportData        = true;
bool FDiggerFeatureFlags::bEnableBuild             = false;
bool FDiggerFeatureFlags::bEnableDeveloperSettings = false;

// Per-brush defaults - all polished brushes enabled for testing
bool FDiggerFeatureFlags::bEnableBrush_Sphere      = true;
bool FDiggerFeatureFlags::bEnableBrush_Cube        = true;
bool FDiggerFeatureFlags::bEnableBrush_Cylinder    = true;
bool FDiggerFeatureFlags::bEnableSplineBrush       = false;
bool FDiggerFeatureFlags::bEnableBrush_Capsule     = true;
bool FDiggerFeatureFlags::bEnableBrush_Cone        = true;
bool FDiggerFeatureFlags::bEnableBrush_Torus       = true;
bool FDiggerFeatureFlags::bEnableBrush_Pyramid     = true;
bool FDiggerFeatureFlags::bEnableBrush_Icosphere   = true;
bool FDiggerFeatureFlags::bEnableBrush_Stairs      = false;
bool FDiggerFeatureFlags::bEnableBrush_Custom      = false;
bool FDiggerFeatureFlags::bEnableBrush_Smooth      = true;
bool FDiggerFeatureFlags::bEnableBrush_Noise       = true;
bool FDiggerFeatureFlags::bEnableBrush_Light       = true;
bool FDiggerFeatureFlags::bEnableBrush_Debug       = false;
bool FDiggerFeatureFlags::bEnableGenerationSection = true;



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
		CandidatePaths.Add(FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("Digger/Config/FeatureFlags.ini")));
		CandidatePaths.Add(FPaths::Combine(FPaths::EnginePluginsDir(), TEXT("Digger/Config/FeatureFlags.ini")));
		CandidatePaths.Add(FPaths::Combine(FPaths::ProjectDir(), TEXT("Plugins/Digger/Config/FeatureFlags.ini")));
	}

	// Find first existing config file
	FString FoundPath;
	for (const FString& Path : CandidatePaths)
	{
		if (IFileManager::Get().FileExists(*Path))
		{
			FoundPath = Path;
			break;
		}
	}

	// No config found → keep defaults
	if (FoundPath.IsEmpty())
	{
		return;
	}

	// Core feature flags
	GConfig->GetBool(Section, TEXT("bEnableBrushTools"),        bEnableBrushTools,        *FoundPath);
	GConfig->GetBool(Section, TEXT("bEnableEnvironment"),       bEnableEnvironment,       *FoundPath);
	GConfig->GetBool(Section, TEXT("bEnableNavigation"),        bEnableNavigation,        *FoundPath);
	GConfig->GetBool(Section, TEXT("bEnableWorklight"),         bEnableWorklight,         *FoundPath);
	GConfig->GetBool(Section, TEXT("bEnableAdditionalTools"),   bEnableAdditionalTools,   *FoundPath);
	GConfig->GetBool(Section, TEXT("bEnableIslands"),           bEnableIslands,           *FoundPath);
	GConfig->GetBool(Section, TEXT("bEnableMaterialManager"),   bEnableMaterialManager,   *FoundPath);
	GConfig->GetBool(Section, TEXT("bEnableExportData"),        bEnableExportData,        *FoundPath);
	GConfig->GetBool(Section, TEXT("bEnableDeveloperSettings"), bEnableDeveloperSettings, *FoundPath);

	// Per-brush flags
	GConfig->GetBool(Section, TEXT("bEnableBrush_Sphere"),     bEnableBrush_Sphere,     *FoundPath);
	GConfig->GetBool(Section, TEXT("bEnableBrush_Cube"),       bEnableBrush_Cube,       *FoundPath);
	GConfig->GetBool(Section, TEXT("bEnableBrush_Cylinder"),   bEnableBrush_Cylinder,   *FoundPath);
	GConfig->GetBool(Section, TEXT("bEnableBrush_Capsule"),    bEnableBrush_Capsule,    *FoundPath);
	GConfig->GetBool(Section, TEXT("bEnableBrush_Cone"),       bEnableBrush_Cone,       *FoundPath);
	GConfig->GetBool(Section, TEXT("bEnableBrush_Torus"),      bEnableBrush_Torus,      *FoundPath);
	GConfig->GetBool(Section, TEXT("bEnableBrush_Pyramid"),    bEnableBrush_Pyramid,    *FoundPath);
	GConfig->GetBool(Section, TEXT("bEnableBrush_Icosphere"),  bEnableBrush_Icosphere,  *FoundPath);
	GConfig->GetBool(Section, TEXT("bEnableBrush_Stairs"),     bEnableBrush_Stairs,     *FoundPath);
	GConfig->GetBool(Section, TEXT("bEnableBrush_Custom"),     bEnableBrush_Custom,     *FoundPath);
	GConfig->GetBool(Section, TEXT("bEnableBrush_Smooth"),     bEnableBrush_Smooth,     *FoundPath);
	GConfig->GetBool(Section, TEXT("bEnableBrush_Noise"),      bEnableBrush_Noise,      *FoundPath);
	GConfig->GetBool(Section, TEXT("bEnableBrush_Light"),      bEnableBrush_Light,      *FoundPath);
	GConfig->GetBool(Section, TEXT("bEnableBrush_Debug"),      bEnableBrush_Debug,      *FoundPath);

	// Sections
	GConfig->GetBool(Section, TEXT("bEnableGenerationSection"), bEnableGenerationSection, *FoundPath);
}
