#include "FigmaFontResolver.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Font.h"
#include "FigmaDocument.h"
#include "FigmaSyncSettings.h"
#include "FigmaSyncStylePreset.h"
#include "Fonts/CompositeFont.h"
#include "Modules/ModuleManager.h"

namespace
{
	/** Typeface names that usually correspond to a CSS-style weight. */
	const TArray<FString>& WeightNames(int32 Weight)
	{
		static const TArray<FString> Thin = { TEXT("thin"), TEXT("hairline") };
		static const TArray<FString> ExtraLight = { TEXT("extralight"), TEXT("ultralight") };
		static const TArray<FString> Light = { TEXT("light") };
		static const TArray<FString> Regular = { TEXT("regular"), TEXT("normal"), TEXT("book"), TEXT("roman"), TEXT("default") };
		static const TArray<FString> Medium = { TEXT("medium") };
		static const TArray<FString> SemiBold = { TEXT("semibold"), TEXT("demibold") };
		static const TArray<FString> Bold = { TEXT("bold") };
		static const TArray<FString> ExtraBold = { TEXT("extrabold"), TEXT("ultrabold") };
		static const TArray<FString> Black = { TEXT("black"), TEXT("heavy") };
		if (Weight <= 150) return Thin;
		if (Weight <= 250) return ExtraLight;
		if (Weight <= 350) return Light;
		if (Weight <= 450) return Regular;
		if (Weight <= 550) return Medium;
		if (Weight <= 650) return SemiBold;
		if (Weight <= 750) return Bold;
		if (Weight <= 850) return ExtraBold;
		return Black;
	}
}

FString FFigmaResolvedFont::Describe() const
{
	const FString FontName = Font ? Font->GetName() : FString(TEXT("(none)"));
	return Typeface.IsNone() ? FontName : FString::Printf(TEXT("%s (%s)"), *FontName, *Typeface.ToString());
}

FFigmaFontResolver::FFigmaFontResolver(const UFigmaSyncSettings& InSettings, const UFigmaSyncStylePreset* InPreset)
	: Settings(InSettings)
	, Preset(InPreset)
{
}

FString FFigmaFontResolver::Normalize(const FString& Name)
{
	FString Out;
	Out.Reserve(Name.Len());
	for (const TCHAR C : Name)
	{
		if (FChar::IsAlnum(C))
		{
			Out.AppendChar(FChar::ToLower(C));
		}
	}
	return Out;
}

const FFigmaFontMapping* FFigmaFontResolver::FindMapping(const TArray<FFigmaFontMapping>& Mappings, const FString& Family, const FString& Style) const
{
	const FString WantedFamily = Normalize(Family);
	const FString WantedStyle = Normalize(Style);
	const FFigmaFontMapping* AnyStyle = nullptr;
	for (const FFigmaFontMapping& Mapping : Mappings)
	{
		if (Normalize(Mapping.FigmaFamily) != WantedFamily)
		{
			continue;
		}
		if (Mapping.FigmaStyle.IsEmpty())
		{
			AnyStyle = AnyStyle ? AnyStyle : &Mapping;
		}
		else if (Normalize(Mapping.FigmaStyle) == WantedStyle)
		{
			return &Mapping;
		}
	}
	return AnyStyle;
}

FName FFigmaFontResolver::PickTypeface(const UObject* FontObject, const FString& Style, int32 Weight, bool bItalic) const
{
	const UFont* Font = Cast<UFont>(FontObject);
	if (!Font || Font->FontCacheType != EFontCacheType::Runtime)
	{
		return NAME_None;
	}
	const TArray<FTypefaceEntry>& Entries = Font->GetCompositeFont() ? Font->GetCompositeFont()->DefaultTypeface.Fonts : TArray<FTypefaceEntry>();
	if (Entries.Num() == 0)
	{
		return NAME_None;
	}

	const FString WantedStyle = Normalize(Style);
	for (const FTypefaceEntry& Entry : Entries)
	{
		if (Normalize(Entry.Name.ToString()) == WantedStyle)
		{
			return Entry.Name;
		}
	}

	// Try weight names, with and without "italic".
	for (const FString& WeightName : WeightNames(Weight))
	{
		const FString Wanted = bItalic ? (WeightName == TEXT("regular") ? FString(TEXT("italic")) : WeightName + TEXT("italic")) : WeightName;
		for (const FTypefaceEntry& Entry : Entries)
		{
			if (Normalize(Entry.Name.ToString()) == Wanted)
			{
				return Entry.Name;
			}
		}
	}
	for (const FString& WeightName : WeightNames(Weight))
	{
		for (const FTypefaceEntry& Entry : Entries)
		{
			if (Normalize(Entry.Name.ToString()) == WeightName)
			{
				return Entry.Name;
			}
		}
	}
	for (const FString& WeightName : WeightNames(400))
	{
		for (const FTypefaceEntry& Entry : Entries)
		{
			if (Normalize(Entry.Name.ToString()) == WeightName)
			{
				return Entry.Name;
			}
		}
	}
	return Entries[0].Name;
}

const UObject* FFigmaFontResolver::AutoMatch(const FString& Family, const FString& Style)
{
	if (!bIndexedFonts)
	{
		bIndexedFonts = true;
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		Registry.GetAssetsByClass(UFont::StaticClass()->GetClassPathName(), ProjectFonts, true);
	}

	const FString WantedFamily = Normalize(Family);
	const FString WantedFamilyStyle = WantedFamily + Normalize(Style);
	if (WantedFamily.IsEmpty())
	{
		return nullptr;
	}

	// Prefer exact names, then names containing family+style ("Font_Cinzel_Bold"), then names containing the family.
	const FAssetData* Best = nullptr;
	int32 BestScore = 0;
	for (const FAssetData& Asset : ProjectFonts)
	{
		if (Asset.PackageName.ToString().StartsWith(TEXT("/Engine/")))
		{
			continue;
		}
		FString AssetName = Normalize(Asset.AssetName.ToString());
		AssetName.RemoveFromStart(TEXT("font"));
		int32 Score = 0;
		if (AssetName == WantedFamily) Score = 4;
		else if (AssetName.Contains(WantedFamilyStyle)) Score = 3;
		else if (AssetName.Contains(WantedFamily)) Score = 2;
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = &Asset;
		}
	}
	return Best ? Best->GetAsset() : nullptr;
}

FFigmaResolvedFont FFigmaFontResolver::Resolve(const FString& Family, const FString& Style, int32 Weight, bool bItalic)
{
	const FString Key = FString::Printf(TEXT("%s|%s|%d|%d"), *Family, *Style, Weight, bItalic ? 1 : 0);
	if (const FFigmaResolvedFont* Cached = Cache.Find(Key))
	{
		return *Cached;
	}

	FFigmaResolvedFont Result;
	const FFigmaFontMapping* Mapping = Preset ? FindMapping(Preset->FontMappings, Family, Style) : nullptr;
	if (!Mapping)
	{
		Mapping = FindMapping(Settings.FontMappings, Family, Style);
	}
	if (Mapping)
	{
		Result.Font = Mapping->Font.LoadSynchronous();
		Result.Typeface = Mapping->Typeface.IsNone() ? PickTypeface(Result.Font, Style, Weight, bItalic) : Mapping->Typeface;
		Result.bMapped = Result.Font != nullptr;
	}
	if (!Result.Font && Settings.bAutoMatchFonts)
	{
		Result.Font = AutoMatch(Family, Style);
		Result.Typeface = PickTypeface(Result.Font, Style, Weight, bItalic);
		Result.bAutoMatched = Result.Font != nullptr;
	}
	if (!Result.Font)
	{
		Result.Font = Settings.FallbackFont.LoadSynchronous();
		Result.Typeface = PickTypeface(Result.Font, Style, Weight, bItalic);
		Result.bFallback = true;
	}

	if (!Result.bMapped)
	{
		Unmapped.Add(FString::Printf(TEXT("%s %s"), *Family, *Style), Result);
	}
	Cache.Add(Key, Result);
	return Result;
}

FSlateFontInfo FFigmaFontResolver::MakeFontInfo(const FFigmaText& Text)
{
	const FFigmaResolvedFont Resolved = Resolve(Text.FontFamily, Text.FontStyle, Text.FontWeight, Text.bItalic);
	FSlateFontInfo Info(Resolved.Font, FMath::Max(1.f, Text.FontSize * Settings.FontSizeScale), Resolved.Typeface);

	// Slate letter spacing is in thousandths of an em.
	if (Text.LetterSpacingUnit == EFigmaUnit::Percent)
	{
		Info.LetterSpacing = FMath::RoundToInt(Text.LetterSpacing * 10.f);
	}
	else if (Text.FontSize > 0.f)
	{
		Info.LetterSpacing = FMath::RoundToInt(Text.LetterSpacing / Text.FontSize * 1000.f);
	}

	if (Text.bHasStroke && Text.StrokeWeight > 0.f)
	{
		Info.OutlineSettings.OutlineSize = FMath::Max(1, FMath::RoundToInt(Text.StrokeWeight));
		Info.OutlineSettings.OutlineColor = Text.StrokeColor.ToLinear();
	}
	return Info;
}
