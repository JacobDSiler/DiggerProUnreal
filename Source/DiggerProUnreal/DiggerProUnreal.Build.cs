// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DiggerProUnreal : ModuleRules
{
	public DiggerProUnreal(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(new string[] { "GeometryFramework" });
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "HeadMountedDisplay", "EnhancedInput", "ProceduralMeshComponent", "Landscape" });
	}
}
