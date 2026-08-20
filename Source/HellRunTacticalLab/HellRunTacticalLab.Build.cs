using UnrealBuildTool;

public class HellRunTacticalLab : ModuleRules
{
    public HellRunTacticalLab(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core", "CoreUObject", "Engine", "AIModule"
        });
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "Json", "JsonUtilities"
        });
    }
}
