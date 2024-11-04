// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DiggerProUnreal : ModuleRules
{
	public DiggerProUnreal(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(new string[] { "GeometryFramework" });
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20; // This sets the C++ standard to C++20


		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "HeadMountedDisplay", "EnhancedInput", "ProceduralMeshComponent", "Landscape", "Foliage" });
	}
}
