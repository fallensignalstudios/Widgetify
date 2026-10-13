// FigmaSyncCore: bundle reading (zip or folder), JSON parsing and the intermediate node model.
// Deliberately free of editor and Slate dependencies so it stays easy to unit test.

using UnrealBuildTool;

public class FigmaSyncCore : ModuleRules
{
	public FigmaSyncCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"Json",
		});
	}
}
