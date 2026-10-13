// FigmaSyncEditor: texture import, widget tree building, re-sync, lint, the Figma Sync panel and live push.

using UnrealBuildTool;

public class FigmaSyncEditor : ModuleRules
{
	public FigmaSyncEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateIncludePaths.Add(System.IO.Path.Combine(ModuleDirectory, "Private"));

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings",
			"SlateCore",
			"Slate",
			"UMG",
			"UMGEditor",
			"FigmaSyncCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"Kismet",
			"KismetCompiler",
			"BlueprintGraph",
			"InputCore",
			"AssetTools",
			"AssetRegistry",
			"ContentBrowser",
			"DesktopPlatform",
			"ImageWrapper",
			"Json",
			"MessageLog",
			"Projects",
			"PropertyEditor",
			"Settings",
			"ToolMenus",
			"WorkspaceMenuStructure",
			"HTTPServer",
		});
	}
}
