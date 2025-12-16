using UnrealBuildTool;

public class DiggerUnreal : ModuleRules
{
    public DiggerUnreal(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // Core dependencies
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore" });

        // LINK YOUR PLUGIN HERE
        // This ensures the project forces the plugin to compile
        PublicDependencyModuleNames.AddRange(new string[] { "Digger" });
    }
}