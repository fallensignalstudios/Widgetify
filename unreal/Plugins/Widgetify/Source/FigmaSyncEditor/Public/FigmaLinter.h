#pragma once

#include "CoreMinimal.h"

class FFigmaImportContext;
class UWidgetBlueprint;

/**
 * The lint report run after every import: missing focus paths, unbound input actions, unmapped fonts, layer names
 * that promise something the widget can't do, resolution mismatches, and anything mappers add.
 */
class FIGMASYNCEDITOR_API FFigmaLinter
{
public:
	static void Run(FFigmaImportContext& Context, UWidgetBlueprint* Blueprint);
};
