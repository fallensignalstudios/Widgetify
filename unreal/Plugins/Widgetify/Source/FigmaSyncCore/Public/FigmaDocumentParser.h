#pragma once

#include "CoreMinimal.h"
#include "FigmaDocument.h"

class FIGMASYNCCORE_API FFigmaDocumentParser
{
public:
	/**
	 * Parses figmasync.json text into OutDocument. Unknown fields are ignored and missing optional fields get
	 * defaults, so newer exporters stay readable. Returns false with a human-readable error when the text is not
	 * a usable bundle (wrong format, newer major version, no root).
	 */
	static bool Parse(const FString& JsonText, FFigmaDocument& OutDocument, FString& OutError);
};
