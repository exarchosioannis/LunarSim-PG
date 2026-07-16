// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class simulator : ModuleRules
{
	public simulator(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		bEnableExceptions = true;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", 
			"TempoROS", "rclcpp", "RHI", "RenderCore", "UnrealGT", "ChaosVehicles", "ImageWrapper"});

		PrivateDependencyModuleNames.AddRange(new string[] { "Landscape" });

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
