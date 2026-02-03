using UnrealBuildTool;
using System.IO;

public class Digger : ModuleRules
{
	public Digger(ReadOnlyTargetRules Target) : base(Target)
	{
		//	bUseUnity = false; // <--- ADD THIS LINE TEMPORARILY bUseUnity = false is helpful during development, debugging, and plugin authoring.
		
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(new string[]
		{
			Path.Combine(ModuleDirectory, "Public")
		});
		
		// Add these paths so you don't have to change #include lines in your code
		PublicIncludePaths.AddRange(
			new string[] {
				System.IO.Path.Combine(ModuleDirectory, "Public/Core"),
				System.IO.Path.Combine(ModuleDirectory, "Public/VoxelEngine"),
				System.IO.Path.Combine(ModuleDirectory, "Public/Meshing"),
				System.IO.Path.Combine(ModuleDirectory, "Public/Tools"),
				System.IO.Path.Combine(ModuleDirectory, "Public/Tools/Shapes"), // Add this for the brushes
				System.IO.Path.Combine(ModuleDirectory, "Public/Gameplay"),
				System.IO.Path.Combine(ModuleDirectory, "Public/Data"),
				System.IO.Path.Combine(ModuleDirectory, "Public/Data/Materials"),
				System.IO.Path.Combine(ModuleDirectory, "Public/Utils"),
			}
		);

		PrivateIncludePaths.AddRange(new string[]
		{
			Path.Combine(ModuleDirectory, "Private")
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

			// Runtime networking (if you need it at runtime)
			"SocketIOClient",
			"SocketIOLib",
			"SIOJson",
		});

		// Only added when building Editor targets
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