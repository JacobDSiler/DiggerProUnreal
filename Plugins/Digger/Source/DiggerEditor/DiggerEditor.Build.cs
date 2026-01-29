using System.IO;
using UnrealBuildTool;

public class DiggerEditor : ModuleRules
{
    public DiggerEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // Public includes (exposed headers)
        PublicIncludePaths.AddRange(new[]
        {
            Path.Combine(ModuleDirectory, "Public/Core"),
            Path.Combine(ModuleDirectory, "Public/UI"),
            Path.Combine(ModuleDirectory, "Public/Tools"),
            Path.Combine(ModuleDirectory, "Public/Assets"),
            Path.Combine(ModuleDirectory, "Public/Settings"),
        });

        // Private includes (internal implementation)
        PrivateIncludePaths.AddRange(new[]
        {
            Path.Combine(ModuleDirectory, "Private/Core"),
            Path.Combine(ModuleDirectory, "Private/UI"),
            Path.Combine(ModuleDirectory, "Private/Tools"),
            Path.Combine(ModuleDirectory, "Private/Assets"),
            Path.Combine(ModuleDirectory, "Private/Settings"),
        });

        // Link to runtime module
        PrivateDependencyModuleNames.Add("Digger");

        // Core engine dependencies
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "RenderCore",
            "RHI",
        });

        // Editor framework
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "UnrealEd",
            "LevelEditor",
            "EditorFramework",
            "EditorStyle",
            "EditorWidgets",
            "EditorSubsystem",
            "InteractiveToolsFramework",
            "EditorInteractiveToolsFramework",
            "DeveloperSettings",
        });

        // Slate UI
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "Slate",
            "SlateCore",
            "ToolMenus",
            "ToolWidgets",
            "AppFramework",
        });

        // Asset and content management
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "AssetRegistry",
            "AssetTools",
            "ContentBrowser",
            "ContentBrowserData",
            "PropertyEditor",
            "Projects",
            "DesktopPlatform",
        });

        // Mesh and geometry
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "ProceduralMeshComponent",
            "MeshDescription",
            "StaticMeshDescription",
            "GeometryCore",
        });

        // Networking (editor-only tools like DiggerConnect)
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "SocketIOClient",
            "HTTP",
            "WebSockets",
            "ApplicationCore",
        });
    }
}
