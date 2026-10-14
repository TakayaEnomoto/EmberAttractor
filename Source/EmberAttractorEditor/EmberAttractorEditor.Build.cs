using UnrealBuildTool;

public class EmberAttractorEditor : ModuleRules
{
    public EmberAttractorEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] {
            "Core", "CoreUObject", "Engine", "EmberAttractor", "UnrealEd",
            "Niagara", "NiagaraCore", "NiagaraEditor", "AssetTools", "AssetRegistry",
            "Projects", "RenderCore", "VectorVM", "Slate", "SlateCore"
        });
    }
}
