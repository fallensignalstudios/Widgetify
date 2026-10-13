#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "Fonts/SlateFontInfo.h"

class UFigmaSyncSettings;
class UFigmaSyncStylePreset;
struct FFigmaFontMapping;
struct FFigmaText;

struct FIGMASYNCEDITOR_API FFigmaResolvedFont
{
	const UObject* Font = nullptr;
	FName Typeface;
	/** Matched an explicit mapping (preset or project table). */
	bool bMapped = false;
	/** Found by name in the project (no mapping). */
	bool bAutoMatched = false;
	/** Nothing matched; the fallback font is used. */
	bool bFallback = false;

	FString Describe() const;
};

/** Maps Figma family/style/weight to Unreal font assets: preset table, project table, name search, then fallback. */
class FIGMASYNCEDITOR_API FFigmaFontResolver
{
public:
	FFigmaFontResolver(const UFigmaSyncSettings& InSettings, const UFigmaSyncStylePreset* InPreset);

	FFigmaResolvedFont Resolve(const FString& Family, const FString& Style, int32 Weight, bool bItalic);

	/** Font info for a Figma text run: resolved font, scaled size, letter spacing, outline. */
	FSlateFontInfo MakeFontInfo(const FFigmaText& Text);

	/** "Family Style" -> resolution, for every font that had no explicit mapping in this import. */
	const TMap<FString, FFigmaResolvedFont>& GetUnmappedFonts() const { return Unmapped; }

	static FString Normalize(const FString& Name);

private:
	const FFigmaFontMapping* FindMapping(const TArray<FFigmaFontMapping>& Mappings, const FString& Family, const FString& Style) const;
	FName PickTypeface(const UObject* Font, const FString& Style, int32 Weight, bool bItalic) const;
	const UObject* AutoMatch(const FString& Family, const FString& Style);

	const UFigmaSyncSettings& Settings;
	const UFigmaSyncStylePreset* Preset = nullptr;
	TArray<FAssetData> ProjectFonts;
	bool bIndexedFonts = false;
	TMap<FString, FFigmaResolvedFont> Cache;
	TMap<FString, FFigmaResolvedFont> Unmapped;
};
