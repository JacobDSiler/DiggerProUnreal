using UnrealBuildTool;
using System.Collections.Generic;

public class DiggerUnrealTarget : TargetRules
{
    public DiggerUnrealTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;

        DefaultBuildSettings = BuildSettingsVersion.V2;

        ExtraModuleNames.Add("HostModule");
    }
}
