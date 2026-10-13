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
			// Public headers include FigmaSyncBlueprintExtension.h (a UWidgetBlueprintExtension), whose engine
			// headers pull in UnrealEd and the Kismet compiler.
			"UnrealEd",
			"KismetCompiler",
			"BlueprintGraph",
			"FigmaSyncCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Kismet",
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
