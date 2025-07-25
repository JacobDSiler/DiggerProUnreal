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
			"GeometryCore",          // For UDynamicMesh
			"GeometryFramework",    // For dynamic mesh components
			"MeshDescription",
			"StaticMeshDescription",
			"RenderCore",
			"RHI",
			"AssetRegistry",
			"MeshUtilitiesCommon",
			"GeometryCore",
			"GeometryFramework",
			"SocketIOClient",
			"SocketIOLib",
			"SIOJson",              // Required for JSON operations
		});

		// Editor-only dependencies (for runtime module)
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] {
				"DiggerEditor",
				"UnrealEd",
				"AssetTools",
				"EditorStyle",
				"MeshBuilder", // Needed for mesh creation
				"MeshUtilities", // Provides mesh building utilities
				"MaterialUtilities", // For material handling
				"AssetTools", // For FAssetRegistryModule
				"AssetRegistry" // For asset creation notifications
			});
			
			// Add path to editor headers
			PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "../DiggerEditor/Private"));
			PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "../DiggerEditor/Public"));
		}

		PublicDefinitions.Add("WITH_SOCKETIO=1");
	}
}