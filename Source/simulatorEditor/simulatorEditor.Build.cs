using UnrealBuildTool;

public class simulatorEditor : ModuleRules
{
	public simulatorEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject", "Engine", "Slate", "SlateCore", "InputCore", "UnrealEd", "LevelEditor",
				"ToolMenus", "Projects", "simulator", "PropertyEditor", "AssetRegistry", "Json", "DesktopPlatform",
				"ApplicationCore"
		});
	}
}
