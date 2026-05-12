#pragma once

#include "CoreMinimal.h"

/**
 * FDiggerFeatureFlags
 *
 * Static read-only feature flags loaded once from:
 *   Plugins/Digger/Config/FeatureFlags.ini
 *
 * The config section is expected to be:
 * [/Script/DiggerEditor.FeatureFlags]
 * bEnableBrushTools=true
 * bEnableCustomBrushes=true
 * bEnableEnvironment=true
 * bEnableIslands=false
 * bEnableAdditionalTools=false
 * bEnableDMM=false
 * bEnableExportData=true
 * bEnableDeveloperSettings=false
 *
 * Loading is performed once by calling LoadFlagsFromPluginConfig().
 * If the .ini is missing or keys are missing, sensible defaults are used
 * and no crash occurs.
 */
struct FDiggerFeatureFlags
{
	// Whether the flags have already been loaded; ensures single-load behavior.
	static bool bLoaded;

	// Feature flags (read-only after LoadFlagsFromPluginConfig runs)
	static bool bEnableBrushTools;
	static bool bEnableBrushShapes;
	static bool bEnableCustomBrushes;
	static bool bEnableEnvironment;
	static bool bEnableNavigation;
    static bool bEnableWorklight;
	static bool bEnableAdditionalTools;
    static bool bEnableIslands;
    static bool bEnableMaterialManager;
	static bool bEnableCaveImporter;
	static bool bEnableDMM;
	static bool bEnableExportData;
	static bool bEnableBuild;
	static bool bEnableDeveloperSettings;

	// Per-brush flags (defaults chosen: true for working brushes, false for unfinished)
	static bool bEnableBrush_Sphere;
	static bool bEnableBrush_Cube;
	static bool bEnableBrush_Cylinder;
	static bool bEnableBrush_Capsule;
	static bool bEnableSplineBrush;
	static bool bEnableBrush_Cone;
	static bool bEnableBrush_Torus;
	static bool bEnableBrush_Pyramid;
	static bool bEnableBrush_Icosphere;
	static bool bEnableBrush_Stairs;
	static bool bEnableBrush_Custom;
	static bool bEnableBrush_Smooth;
	static bool bEnableBrush_Noise;
	static bool bEnableBrush_Light;
	static bool bEnableBrush_Debug;
	static bool  bEnableGenerationSection;

	/** Load flags from Plugins/Digger/Config/FeatureFlags.ini (only runs once). */
	static void LoadFlagsFromPluginConfig();
};