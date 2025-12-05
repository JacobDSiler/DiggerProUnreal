using UnrealBuildTool;
using System.IO;

public class DiggerRuntime : ModuleRules
{
	public DiggerRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(new string[]
		{
			Path.Combine(ModuleDirectory, "Public")
		});

		PrivateIncludePaths.AddRange(new string[]
		{
			Path.Combine(ModuleDirectory, "Private")
		});

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
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