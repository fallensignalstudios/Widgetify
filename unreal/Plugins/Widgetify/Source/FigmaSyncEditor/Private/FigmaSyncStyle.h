#pragma once

#include "CoreMinimal.h"
#include "Styling/ISlateStyle.h"

class FSlateStyleSet;

/** Icons for the Figma Sync tab, menus and toolbar. */
class FFigmaSyncStyle
{
public:
	static void Initialize();
	static void Shutdown();
	static FName GetStyleSetName();

private:
	static TSharedPtr<FSlateStyleSet> StyleSet;
};
