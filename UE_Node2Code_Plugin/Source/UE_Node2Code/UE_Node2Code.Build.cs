using UnrealBuildTool;

public class UE_Node2Code : ModuleRules
{
	public UE_Node2Code(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Niagara"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"BlueprintGraph",
				"DesktopPlatform",
				"InputCore",
				"LevelEditor",
				"NiagaraEditor",
				"Projects",
				"Slate",
				"SlateCore",
				"UnrealEd"
			}
		);
	}
}
