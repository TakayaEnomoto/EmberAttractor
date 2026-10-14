using UnrealBuildTool;

public class EmberAttractorTarget : TargetRules
{
    public EmberAttractorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V6;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
        ExtraModuleNames.Add("EmberAttractor");
    }
}
