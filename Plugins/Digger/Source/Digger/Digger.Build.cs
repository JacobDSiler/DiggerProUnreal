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
            "MeshDescription",
            "StaticMeshDescription",
            "AssetRegistry",

            // Runtime networking
            "SocketIOClient",
            "SocketIOLib",
            "SIOJson",
        });

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
            });
        }

        PublicDefinitions.Add("WITH_SOCKETIO=1");
    }
}
