using System.IO;
using UnrealBuildTool;

public class DiggerProUnreal : ModuleRules
{
    public DiggerProUnreal(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicIncludePaths.AddRange(new string[] {
            Path.Combine(ModuleDirectory, "Public"),
        });

        PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Private"));

        // Core runtime dependencies
        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "HeadMountedDisplay",
            "EnhancedInput",
            "ProceduralMeshComponent",
            "Landscape",
            "Foliage",
            "GeometryCore",
            "GeometryFramework",
            "MeshDescription",
            "StaticMeshDescription",
            "RenderCore",
            "RHI",
            "AssetRegistry",
            "MeshUtilitiesCommon",
            "SocketIOClient",
            "SocketIOLib",
            "SIOJson",
        });

        // Editor-only deps (only when building the Editor target)
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(new string[] {
                // If runtime code really needs editor APIs (avoid unless necessary):
                "UnrealEd",
                // Depend on your editor module rather than hard-adding its include paths:
                //"DiggerEditor",

                "AssetTools",
                "EditorStyle",
                "MeshBuilder",
                "MeshUtilities",
                "MaterialUtilities",
                "AssetRegistry"
            });

            // DO NOT add raw include paths into the editor module from runtime.
            // Rely on the module dependency above instead.
            // (Commented out on purpose)
            // PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "../DiggerEditor/Private"));
            // PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "../DiggerEditor/Public"));
        }

        PublicDefinitions.Add("WITH_SOCKETIO=1");
    }
}
