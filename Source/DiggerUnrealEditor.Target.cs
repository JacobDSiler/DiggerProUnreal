using UnrealBuildTool;
using System.Collections.Generic;

public class DiggerUnrealEditorTarget : TargetRules
{
    public DiggerUnrealEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;

        DefaultBuildSettings = BuildSettingsVersion.V2;

        ExtraModuleNames.Add("HostModule");
    }
}
