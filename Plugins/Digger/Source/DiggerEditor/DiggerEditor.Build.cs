using System;
using System.IO;
using UnrealBuildTool;

public class DiggerEditor : ModuleRules
{
    public DiggerEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // Depend on runtime module
        PublicDependencyModuleNames.AddRange(new string[] { "Digger" });
        PrivateDependencyModuleNames.AddRange(new string[] { "Digger" });

        // Public includes (editor-only headers)
        PublicIncludePaths.AddRange(new[]
        {
            Path.Combine(ModuleDirectory, "Public/Core"),
            Path.Combine(ModuleDirectory, "Public/UI"),
            Path.Combine(ModuleDirectory, "Public/Tools"),
            Path.Combine(ModuleDirectory, "Public/Assets"),
            Path.Combine(ModuleDirectory, "Public/Settings"),
            // Removed: Public/VoxelEngine (that folder belongs to runtime module)
        });

        // Private includes (editor implementation)
        PrivateIncludePaths.AddRange(new[]
        {
            Path.Combine(ModuleDirectory, "Private/Core"),
            Path.Combine(ModuleDirectory, "Private/UI"),
            Path.Combine(ModuleDirectory, "Private/Tools"),
            Path.Combine(ModuleDirectory, "Private/Assets"),
            Path.Combine(ModuleDirectory, "Private/Settings"),
            // Removed: Private/VoxelEngine (no such folder in editor module)
        });

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "DeveloperSettings"   // ← REQUIRED FOR UHT TO GENERATE THE HEADER
        });

        
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
            "Landscape",
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
