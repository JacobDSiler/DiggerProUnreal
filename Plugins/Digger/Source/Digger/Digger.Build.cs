using UnrealBuildTool;
using System.IO;

public class Digger : ModuleRules
{
	public Digger(ReadOnlyTargetRules Target) : base(Target)
	{
				bUseUnity = false; // <--- ADD THIS LINE TEMPORARILY
		
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