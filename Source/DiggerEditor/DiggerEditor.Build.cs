using System.IO;
using UnrealBuildTool;

public class DiggerEditor : ModuleRules
{
	public DiggerEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(new string[]
		{
			Path.Combine(ModuleDirectory, "Public")
		});

		PrivateIncludePaths.AddRange(new string[]
		{
			Path.Combine(ModuleDirectory, "Private")
		});

		// Link to runtime module for access to core Digger classes
		PrivateDependencyModuleNames.Add("DiggerProUnreal");

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// Core engine
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"RenderCore",
			"RHI",

			// Editor framework
			"UnrealEd",
			"LevelEditor",
			"EditorFramework",
			"EditorStyle",
			"EditorWidgets",
			"EditorSubsystem",
			"InteractiveToolsFramework",
			"EditorInteractiveToolsFramework",

			// Slate UI
			"Slate",
			"SlateCore",
			"ToolMenus",
			"ToolWidgets",
			"AppFramework",

			// Asset & Content management
			"AssetRegistry",
			"AssetTools",
			"ContentBrowser",
			"ContentBrowserData",
			"PropertyEditor",
			"Projects",
			"DesktopPlatform",

			// Mesh / Geometry
			"ProceduralMeshComponent",
			"MeshDescription",
			"StaticMeshDescription",
			"GeometryCore",

			// Networking (Editor-only: DiggerConnect)
			"SocketIOClient",
			"HTTP",
			"WebSockets",
			"ApplicationCore",
		});
	}
}