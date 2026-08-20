using UnrealBuildTool;

public class HellRunTacticalLabEditor : ModuleRules
{
    public HellRunTacticalLabEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[]
        {
            "Core", "CoreUObject", "Engine", "HellRunTacticalLab", "HellRunTraversalNavigation", "HellRunGOAP",
            "UnrealEd", "EditorFramework", "AssetDefinition", "AssetTools",
            "GraphEditor", "PropertyEditor", "Slate", "SlateCore", "ToolMenus",
            "ApplicationCore", "InputCore", "Json", "JsonUtilities", "ImageCore", "AIModule", "NavigationSystem"
        });
    }
}
