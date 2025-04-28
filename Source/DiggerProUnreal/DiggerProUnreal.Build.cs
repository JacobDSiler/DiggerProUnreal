// DiggerProUnreal.Build.cs

using UnrealBuildTool;

public class DiggerProUnreal : ModuleRules
{
    public DiggerProUnreal(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] { 
            "Core", 
            "CoreUObject", 
            "Engine", 
            "InputCore", 
            "HeadMountedDisplay", 
            "EnhancedInput", 
            "ProceduralMeshComponent", 
            "Landscape", 
            "Foliage"
        });

        // Only add editor-only dependencies in PrivateDependencyModuleNames and only if building for editor
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(new string[] {
                "UnrealEd",
                "EditorStyle",
                "LevelEditor",
                "Slate",
                "SlateCore",
                "GeometryFramework",
                "EditorScriptingUtilities",
                "DiggerEditor" 
            });

            // DO NOT add "DiggerEditor" here!
        }
    }
}
