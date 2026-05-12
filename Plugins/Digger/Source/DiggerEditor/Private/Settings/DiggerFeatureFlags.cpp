#include "DiggerFeatureFlags.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Misc/OutputDevice.h"

// ============================================================================
// Defaults for closed tester release.
// These MUST match FeatureFlags.ini so that the UI is correct even if the
// INI fails to load (e.g. custom project layout, GConfig timing issues).
// ============================================================================
bool FDiggerFeatureFlags::bLoaded                  = false;

// --- Core sections ---
bool FDiggerFeatureFlags::bEnableBrushTools        = true;
bool FDiggerFeatureFlags::bEnableBrushShapes       = true;
bool FDiggerFeatureFlags::bEnableCustomBrushes     = false;
bool FDiggerFeatureFlags::bEnableEnvironment       = true;
bool FDiggerFeatureFlags::bEnableNavigation        = true;
bool FDiggerFeatureFlags::bEnableWorklight         = true;
bool FDiggerFeatureFlags::bEnableAdditionalTools   = false;
bool FDiggerFeatureFlags::bEnableIslands           = false;   // tester: OFF
bool FDiggerFeatureFlags::bEnableMaterialManager   = true;    // tester: ON (landscape opacity setup)
bool FDiggerFeatureFlags::bEnableCaveImporter      = false;
bool FDiggerFeatureFlags::bEnableDMM               = false;
bool FDiggerFeatureFlags::bEnableExportData        = true;    // tester: ON (save/load/clear)
bool FDiggerFeatureFlags::bEnableBuild             = false;
bool FDiggerFeatureFlags::bEnableDeveloperSettings = true;    // tester: ON (troubleshooting)
bool FDiggerFeatureFlags::bEnableGenerationSection = false;   // tester: OFF

// --- Per-brush defaults (4 core + debug for testers) ---
bool FDiggerFeatureFlags::bEnableBrush_Sphere      = true;
bool FDiggerFeatureFlags::bEnableBrush_Cube        = true;
bool FDiggerFeatureFlags::bEnableBrush_Cylinder    = false;   // tester: OFF
bool FDiggerFeatureFlags::bEnableSplineBrush       = false;
bool FDiggerFeatureFlags::bEnableBrush_Capsule     = false;   // tester: OFF
bool FDiggerFeatureFlags::bEnableBrush_Cone        = false;   // tester: OFF
bool FDiggerFeatureFlags::bEnableBrush_Torus       = false;   // tester: OFF
bool FDiggerFeatureFlags::bEnableBrush_Pyramid     = false;   // tester: OFF
bool FDiggerFeatureFlags::bEnableBrush_Icosphere   = false;   // tester: OFF
bool FDiggerFeatureFlags::bEnableBrush_Stairs      = false;
bool FDiggerFeatureFlags::bEnableBrush_Custom      = false;
bool FDiggerFeatureFlags::bEnableBrush_Smooth      = true;
bool FDiggerFeatureFlags::bEnableBrush_Noise       = true;    // Sharp brush
bool FDiggerFeatureFlags::bEnableBrush_Light       = false;   // tester: OFF
bool FDiggerFeatureFlags::bEnableBrush_Debug       = true;    // tester: ON (diagnostics)



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

	// No config found - keep defaults (which already match tester release)
	if (FoundPath.IsEmpty())
	{
		UE_LOG(LogTemp, Log, TEXT("Digger: FeatureFlags.ini not found - using built-in defaults."));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("Digger: Loading FeatureFlags from %s"), *FoundPath);

	// Read the INI file directly instead of relying on GConfig.
	// GConfig doesn't automatically know about custom plugin config files,
	// so the previous GConfig->GetBool calls were silently failing and
	// leaving all flags at their compiled-in defaults.
	FConfigFile CustomConfig;
	CustomConfig.Read(*FoundPath);

	// Helper: read a bool from the loaded config. Falls back to the
	// current (default) value if the key is missing or malformed.
	auto ReadBool = [&](const TCHAR* Key, bool& OutValue)
	{
		FString ValueStr;
		if (CustomConfig.GetString(Section, Key, ValueStr))
		{
			ValueStr.TrimStartAndEndInline();
			OutValue = ValueStr.Equals(TEXT("true"), ESearchCase::IgnoreCase)
			        || ValueStr.Equals(TEXT("1"));
		}
	};

	// Core feature flags
	ReadBool(TEXT("bEnableBrushTools"),        bEnableBrushTools);
	ReadBool(TEXT("bEnableBrushShapes"),       bEnableBrushShapes);
	ReadBool(TEXT("bEnableCustomBrushes"),     bEnableCustomBrushes);
	ReadBool(TEXT("bEnableEnvironment"),       bEnableEnvironment);
	ReadBool(TEXT("bEnableNavigation"),        bEnableNavigation);
	ReadBool(TEXT("bEnableWorklight"),         bEnableWorklight);
	ReadBool(TEXT("bEnableAdditionalTools"),   bEnableAdditionalTools);
	ReadBool(TEXT("bEnableIslands"),           bEnableIslands);
	ReadBool(TEXT("bEnableMaterialManager"),   bEnableMaterialManager);
	ReadBool(TEXT("bEnableCaveImporter"),      bEnableCaveImporter);
	ReadBool(TEXT("bEnableDMM"),              bEnableDMM);
	ReadBool(TEXT("bEnableExportData"),        bEnableExportData);
	ReadBool(TEXT("bEnableBuild"),             bEnableBuild);
	ReadBool(TEXT("bEnableDeveloperSettings"), bEnableDeveloperSettings);
	ReadBool(TEXT("bEnableGenerationSection"), bEnableGenerationSection);

	// Per-brush flags
	ReadBool(TEXT("bEnableBrush_Sphere"),     bEnableBrush_Sphere);
	ReadBool(TEXT("bEnableBrush_Cube"),       bEnableBrush_Cube);
	ReadBool(TEXT("bEnableBrush_Cylinder"),   bEnableBrush_Cylinder);
	ReadBool(TEXT("bEnableBrush_Capsule"),    bEnableBrush_Capsule);
	ReadBool(TEXT("bEnableBrush_Cone"),       bEnableBrush_Cone);
	ReadBool(TEXT("bEnableBrush_Torus"),      bEnableBrush_Torus);
	ReadBool(TEXT("bEnableBrush_Pyramid"),    bEnableBrush_Pyramid);
	ReadBool(TEXT("bEnableBrush_Icosphere"),  bEnableBrush_Icosphere);
	ReadBool(TEXT("bEnableBrush_Stairs"),     bEnableBrush_Stairs);
	ReadBool(TEXT("bEnableBrush_Custom"),     bEnableBrush_Custom);
	ReadBool(TEXT("bEnableBrush_Smooth"),     bEnableBrush_Smooth);
	ReadBool(TEXT("bEnableBrush_Noise"),      bEnableBrush_Noise);
	ReadBool(TEXT("bEnableBrush_Light"),      bEnableBrush_Light);
	ReadBool(TEXT("bEnableBrush_Debug"),      bEnableBrush_Debug);

	UE_LOG(LogTemp, Log, TEXT("Digger: FeatureFlags loaded. ExportData=%d Islands=%d MatMgr=%d DevSettings=%d Debug=%d"),
		bEnableExportData, bEnableIslands, bEnableMaterialManager, bEnableDeveloperSettings, bEnableBrush_Debug);
}
