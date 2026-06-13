// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MutableSidekicksPluginEditor : ModuleRules
{
	public MutableSidekicksPluginEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"GameplayTags",
				"MutableSidekicksPlugin"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AppFramework",
				"AssetRegistry",
				"AssetTools",
				"ContentBrowser",
				"CustomizableObject",
				"DesktopPlatform",
				"InputCore",
				"Json",
				"JsonUtilities",
				"Slate",
				"SlateCore",
				"ToolMenus",
				"UnrealEd"
			}
		);
	}
}
