#pragma once

#include "CoreMinimal.h"
#include "FigmaDocument.h"
#include "UObject/WeakObjectPtrTemplates.h"

class FFigmaImportContext;
class UTexture2D;

/**
 * Turns bundle PNGs into UTexture2D assets. New textures get the chosen preset (UI compression, no mips, sRGB...).
 * Changed images update the existing asset in place, so every brush that references it stays valid and texture
 * settings tweaked by hand survive. The PNG's MD5 is stored in the texture's import data, so unchanged images are
 * skipped on re-sync.
 */
class FIGMASYNCEDITOR_API FFigmaTextureImporter
{
public:
	explicit FFigmaTextureImporter(FFigmaImportContext& InContext);

	/** Texture for a bundle asset in Folder (default: the screen's texture folder), importing it if needed. */
	UTexture2D* GetOrImport(const FFigmaAsset& Asset, FName PresetName, const FString& Folder = FString());

	/** Size of an asset in Slate units (PNG pixels / export scale). */
	static FVector2D GetSlateSize(const FFigmaAsset& Asset);

	/** Texture preset for a layer: [img:Name] / [preset=Name] tag, then the style preset, then the project default. */
	static FName GetPresetForNode(const FFigmaNode& Node, const class UFigmaSyncStylePreset* StylePreset);

private:
	UTexture2D* ImportPng(const TArray<uint8>& PngBytes, const FString& PackagePath, const FString& AssetName, FName PresetName, UTexture2D* Existing, FString& OutError);

	FFigmaImportContext& Context;
	TMap<FString, TWeakObjectPtr<UTexture2D>> Cache;
	/** "Folder/AssetName" -> id of the bundle asset that uses it in this import. */
	TMap<FString, FString> ClaimedNames;
};
