// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DiggerProUnreal : ModuleRules
{
    public DiggerProUnreal(ReadOnlyTargetRules target) : base(target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20; // This sets the C++ standard to C++20

        // Combine all dependencies into a single list
        PublicDependencyModuleNames.AddRange(new string[] { 
            "Core", 
            "CoreUObject", 
            "Engine", 
            "InputCore", 
            "UnrealEd", 
            "EditorStyle", 
            "LevelEditor", 
            "Slate", 
            "SlateCore", 
            "GeometryFramework",
            "HeadMountedDisplay", 
            "EnhancedInput", 
            "ProceduralMeshComponent", 
            "Landscape", 
            "Foliage"
        });
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.Add("DiggerEditor");
        }

    }
}
