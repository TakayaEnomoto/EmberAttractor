using UnrealBuildTool;

public class EmberAttractor : ModuleRules
{
    public EmberAttractor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicIncludePaths.Add(ModuleDirectory);
        PublicDependencyModuleNames.AddRange(new[] {
            "Core", "CoreUObject", "Engine", "InputCore", "Niagara"
        });
        PrivateDependencyModuleNames.AddRange(new[] { "RenderCore", "RHI", "Slate", "SlateCore" });
    }
}
