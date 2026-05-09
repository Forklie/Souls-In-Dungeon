// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Soul_and_dungeon : ModuleRules
{
	public Soul_and_dungeon(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"NavigationSystem",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"Soul_and_dungeon",
			"Soul_and_dungeon/Variant_Platforming",
			"Soul_and_dungeon/Variant_Platforming/Animation",
			"Soul_and_dungeon/Variant_Combat",
			"Soul_and_dungeon/Variant_Combat/AI",
			"Soul_and_dungeon/Variant_Combat/Animation",
			"Soul_and_dungeon/Variant_Combat/Gameplay",
			"Soul_and_dungeon/Variant_Combat/Interfaces",
			"Soul_and_dungeon/Variant_Combat/UI",
			"Soul_and_dungeon/Variant_SideScrolling",
			"Soul_and_dungeon/Variant_SideScrolling/AI",
			"Soul_and_dungeon/Variant_SideScrolling/Gameplay",
			"Soul_and_dungeon/Variant_SideScrolling/Interfaces",
			"Soul_and_dungeon/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
