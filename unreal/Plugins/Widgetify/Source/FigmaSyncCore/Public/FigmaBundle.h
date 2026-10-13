#pragma once

#include "CoreMinimal.h"
#include "FigmaDocument.h"

class FFigmaZipReader;

/**
 * A figmasync bundle opened from disk or memory: the parsed document plus access to its image files.
 *
 * Accepted sources:
 *  - MainMenu.figmasync.zip (what the Figma plugin downloads; may also wrap a single folder)
 *  - a folder containing figmasync.json and images/
 *  - the figmasync.json file itself (images are read next to it)
 */
class FIGMASYNCCORE_API FFigmaBundle
{
public:
	static TSharedPtr<FFigmaBundle> Open(const FString& Path, FString& OutError);

	/** Opens a zip held in memory, e.g. one pushed live from Figma. */
	static TSharedPtr<FFigmaBundle> OpenFromZipBytes(TArray<uint8>&& ZipBytes, const FString& DisplayName, FString& OutError);

	const FFigmaDocument& GetDocument() const { return Document; }

	/** The path the bundle was opened from (or the display name for in-memory bundles). */
	const FString& GetSourcePath() const { return SourcePath; }

	bool IsArchive() const { return Zip.IsValid(); }

	/** Reads a file by its bundle-relative path ("images/Logo.png"). Rejects paths that escape the bundle. */
	bool ReadFile(const FString& RelativePath, TArray<uint8>& OutBytes, FString& OutError) const;

	bool ReadAsset(const FFigmaAsset& Asset, TArray<uint8>& OutBytes, FString& OutError) const { return ReadFile(Asset.Path, OutBytes, OutError); }

	/** True when Path looks like something Open() accepts (cheap check for drag-and-drop and pickers). */
	static bool IsBundlePath(const FString& Path);

	~FFigmaBundle();

private:
	FFigmaBundle() = default;

	bool InitFromZip(TArray<uint8>&& ZipBytes, FString& OutError);
	bool ParseJsonBytes(const TArray<uint8>& JsonBytes, FString& OutError);

	FFigmaDocument Document;
	FString SourcePath;
	FString FolderRoot;
	FString ArchivePrefix;
	TSharedPtr<FFigmaZipReader> Zip;
};
