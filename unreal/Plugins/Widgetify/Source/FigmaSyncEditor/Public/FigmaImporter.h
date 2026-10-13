#pragma once

#include "CoreMinimal.h"
#include "FigmaImportContext.h"

class FFigmaBundle;
class UWidgetBlueprint;

/**
 * One import or re-sync, start to finish:
 *  1. resolve (or create) the Widget Blueprint,
 *  2. let mappers prepare shared assets (CommonUI buttons and styles),
 *  3. import textures,
 *  4. build the widget tree inside an undoable transaction,
 *  5. compile, set class defaults (preview size, initial focus),
 *  6. lint, save, and write the import log.
 */
class FIGMASYNCEDITOR_API FFigmaImporter
{
public:
	static FFigmaImportResult Import(const FFigmaBundle& Bundle, const FFigmaImportOptions& Options);

	/** Re-imports from the bundle a blueprint was last synced from. */
	static FFigmaImportResult Resync(UWidgetBlueprint* Blueprint);

	/** "WBP_" + frame name. */
	static FString GetDefaultAssetName(const FFigmaDocument& Document);

	/** Saved/FigmaSync/Logs */
	static FString GetLogDirectory();
};
