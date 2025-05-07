// DiggerEditor.Build.cs

using UnrealBuildTool;

public class DiggerEditor : ModuleRules
{
    public DiggerEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "UnrealEd",
                "Slate",
                "SlateCore",
                "EditorFramework",
                "LevelEditor",
                "InputCore",
                "ToolMenus",
                "ProceduralMeshComponent",
                "DiggerProUnreal", // <-- This is correct!
                "EditorInteractiveToolsFramework"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "EditorStyle",
                "EditorSubsystem",
                "AssetTools",
                "ContentBrowser",
                "MeshDescription",
                "StaticMeshDescription",
            }
        );
    }
}
