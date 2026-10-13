#include "FigmaSyncStyle.h"
#include "Brushes/SlateImageBrush.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"

TSharedPtr<FSlateStyleSet> FFigmaSyncStyle::StyleSet;

FName FFigmaSyncStyle::GetStyleSetName()
{
	static const FName Name(TEXT("FigmaSyncStyle"));
	return Name;
}

void FFigmaSyncStyle::Initialize()
{
	if (StyleSet.IsValid())
	{
		return;
	}
	StyleSet = MakeShared<FSlateStyleSet>(GetStyleSetName());
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Widgetify"));
	const FString Resources = Plugin.IsValid() ? Plugin->GetBaseDir() / TEXT("Resources") : FString();
	StyleSet->SetContentRoot(Resources);
	StyleSet->Set("FigmaSync.Icon", new FSlateImageBrush(Resources / TEXT("Icon40.png"), FVector2D(40.0, 40.0)));
	StyleSet->Set("FigmaSync.Icon.Small", new FSlateImageBrush(Resources / TEXT("Icon16.png"), FVector2D(16.0, 16.0)));
	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet);
}

void FFigmaSyncStyle::Shutdown()
{
	if (StyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet);
		StyleSet.Reset();
	}
}
