using UnrealBuildTool;
using System.IO;

public class Digger : ModuleRules
{
    public Digger(ReadOnlyTargetRules Target) : base(Target)
    {
        // Unity ON so all cpp (including VoxelConversion.cpp) are compiled together
        bUseUnity = true;
        MinFilesUsingPrecompiledHeaderOverride = 1;

        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicIncludePaths.AddRange(new string[]
        {
            Path.Combine(ModuleDirectory, "Public")
        });

        // Eigen is a sibling plugin at Plugins/Eigen/
        PublicIncludePaths.Add(Path.Combine(PluginDirectory, "..", "Eigen"));

        // Public headers (only paths that actually exist)
        PublicIncludePaths.AddRange(
            new string[] {
                Path.Combine(ModuleDirectory, "Public/Core"),
                Path.Combine(ModuleDirectory, "Public/VoxelEngine"),
                Path.Combine(ModuleDirectory, "Public/Meshing"),
                Path.Combine(ModuleDirectory, "Public/Tools"),
                Path.Combine(ModuleDirectory, "Public/Tools/Shapes"),
                Path.Combine(ModuleDirectory, "Public/Gameplay"),
                Path.Combine(ModuleDirectory, "Public/Data"),
                // Removed: Public/Data/Materials (warning: directory does not exist)
                Path.Combine(ModuleDirectory, "Public/Utils"),
            }
        );

        PrivateIncludePaths.AddRange(new string[]
        {
            Path.Combine(ModuleDirectory, "Private"),
            Path.Combine(ModuleDirectory, "Private/VoxelEngine"),
        });

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "DeveloperSettings",
            "InputCore",
            "ProceduralMeshComponent",
            "RenderCore",
            "RHI",
            "Landscape",
            "Foliage",
            "GeometryCore",
            "GeometryFramework",
            "Niagara",
            "MeshDescription",
            "StaticMeshDescription",
            "AssetRegistry",

        });

        // Runtime networking — optional SocketIO dependency
        // Only link if the plugin is present (testers may not have it)
        string SocketIOPluginDir = Path.Combine(PluginDirectory, "..", "SocketIOClient");
        bool bHasSocketIO = Directory.Exists(SocketIOPluginDir);
        if (bHasSocketIO)
        {
            PublicDependencyModuleNames.AddRange(new[]
            {
                "SocketIOClient",
                "SocketIOLib",
                "SIOJson",
            });
            PublicDefinitions.Add("WITH_SOCKETIO=1");
        }
        else
        {
            PublicDefinitions.Add("WITH_SOCKETIO=0");
        }

        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(new[]
            {
                "UnrealEd",
                "AssetTools",
                "EditorStyle",
                "MeshBuilder",
                "MeshUtilities",
                "MaterialUtilities",
                "MeshConversion",
            });
        }
    }
}
