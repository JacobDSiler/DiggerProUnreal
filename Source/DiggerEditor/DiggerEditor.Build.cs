// DiggerEditor.Build.cs

using UnrealBuildTool;

public class DiggerEditor : ModuleRules
{
    public DiggerEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        
        PublicIncludePaths.AddRange(
            new string[] {
                "DiggerEditor/Public"
            });

        PrivateIncludePaths.AddRange(
            new string[] {
                "DiggerEditor/Private"
            });

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
                "PropertyEditor",  // <-- NEW
                "Projects"         // <-- NEW
            }
        );

    }
}
