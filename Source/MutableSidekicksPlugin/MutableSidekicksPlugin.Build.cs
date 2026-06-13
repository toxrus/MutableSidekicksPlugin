// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MutableSidekicksPlugin : ModuleRules
{
	public MutableSidekicksPlugin(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"GameplayTags",
				"CustomizableObject",
				"DeveloperSettings",
				"Json",
				"JsonUtilities"
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
				"SlateCore"
			}
			);

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"AnimationCore",
					"AssetRegistry",
					"MeshDescription",
					"RenderCore",
					"SkeletalMeshDescription",
					"SkeletalMeshModifiers",
					"UnrealEd"
				}
				);
		}
	}
}
