using UnrealBuildTool;
using System.Collections.Generic;

public class DiggerEditor : ModuleRules
{
	public DiggerEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;


		// ✅ Remove this: SupportedPlatforms is deprecated and not needed for standard modules
		// SupportedPlatforms = new List<UnrealPlatform> { UnrealPlatform.Windows, UnrealPlatform.Win64 };

/*		PublicIncludePaths.AddRange(new string[] {
			"DiggerEditor/Public"
		});

		PrivateIncludePaths.AddRange(new string[] {
			"DiggerEditor/Private"
		});

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"UnrealEd",
			"LevelEditor",
			"Slate",
			"SlateCore",
			"EditorStyle",
			"EditorSubsystem",
			"AssetTools",
			"ContentBrowser"
		});*/
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"UnrealEd",
				"Slate",
				"SlateCore",
				"EditorFramework", // ← ✅ Needed for FEditorModeInfo
				"LevelEditor",     // ← Often needed for Editor Modes
				"InputCore",       // ← Needed for input handling
				"ToolMenus",        // ← Optional, but useful for custom menus
				"ProceduralMeshComponent"
			});


		/*PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore",
			"EditorStyle", "UnrealEd", "Slate", "SlateCore",
			"EditorWidgets", "LevelEditor", "ToolMenus"
		});*/

		// Optional: For ToolMenus, ContentBrowser, etc.
		// DynamicallyLoadedModuleNames.AddRange(new string[] { "AssetTools" });
	}
}