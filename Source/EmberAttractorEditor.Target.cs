using UnrealBuildTool;

public class EmberAttractorEditorTarget : TargetRules
{
    public EmberAttractorEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V6;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
        ExtraModuleNames.AddRange(new[] { "EmberAttractor", "EmberAttractorEditor" });
    }
}
