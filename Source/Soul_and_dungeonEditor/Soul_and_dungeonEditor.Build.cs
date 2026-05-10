// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Soul_and_dungeonEditor : ModuleRules
{
	public Soul_and_dungeonEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"Soul_and_dungeon",
			"AIModule",
			"NavigationSystem",
			"LearningAgents",
			"LearningAgentsTraining",
			"LearningTraining",
			"Json",
			"JsonUtilities",
			"HTTP"
		});
	}
}
