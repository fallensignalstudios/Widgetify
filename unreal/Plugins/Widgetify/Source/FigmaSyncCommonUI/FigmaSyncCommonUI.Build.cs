// FigmaSyncCommonUI: maps Btn/* components to CommonUI buttons (with per-variant style assets), Act_* layers to
// input action widgets and screens to activatable widgets.
//
// CommonUI is reached through reflection (class paths and property names), so this module has no link-time
// dependency on the CommonUI plugin: projects without CommonUI still compile and simply get plain UMG buttons.

using UnrealBuildTool;

public class FigmaSyncCommonUI : ModuleRules
{
	public FigmaSyncCommonUI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"SlateCore",
			"Slate",
			"UMG",
			"UMGEditor",
			"UnrealEd",
			"Kismet",
			"KismetCompiler",
			"BlueprintGraph",
			"AssetRegistry",
			"FigmaSyncCore",
			"FigmaSyncEditor",
		});
	}
}
