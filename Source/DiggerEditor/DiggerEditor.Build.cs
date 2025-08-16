using System.IO;
using UnrealBuildTool;

public class DiggerEditor : ModuleRules
{
	public DiggerEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(new string[] {
			Path.Combine(ModuleDirectory, "Public"),
			Path.Combine(ModuleDirectory, "../DiggerProUnreal/Public") // Access runtime headers
		});

		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Private"));

		// Core editor dependencies
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"EditorFramework",
			"LevelEditor",
			"Slate",
			"SlateCore",
			"ToolMenus",
			"EditorInteractiveToolsFramework",
			"ProceduralMeshComponent",
			"SocketIOClient",
			"GeometryCore",          // For mesh tools
			"ContentBrowser",         // For asset thumbnails
			"DesktopPlatform",
			"EditorStyle",
			"ToolMenus",
			"EditorWidgets",
			"DiggerProUnreal",
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"DiggerProUnreal",       // Editor → Runtime link
			"EditorStyle",
			"EditorSubsystem",
			"AssetTools",
			"ContentBrowserData",
			"MeshDescription",
			"StaticMeshDescription",
			"PropertyEditor",
			"Projects",
			"AppFramework",
			"AssetRegistry",
			"RenderCore",
			"RHI",
			"HTTP",
			"WebSockets",
			"ApplicationCore",
			"InputCore",
			"ToolWidgets",           // For editor widgets
			"ContentBrowser",  // Provides FAssetThumbnail
			"AssetTools",      // Additional asset functionality
		});

		// For SComboBox, SEditableTextBox etc.
		PrivateDependencyModuleNames.AddRange(new string[] {
			"EditorStyle",
			"EditorWidgets", 
			"UnrealEd",
			"ToolMenus",
			"Slate",
			"SlateCore",
			"PropertyEditor"
		});
	}
}