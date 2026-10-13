#include "FigmaSyncStylePreset.h"

const FFigmaTextStyleOverride* UFigmaSyncStylePreset::FindTextStyle(const FString& FigmaStyleName) const
{
	return FigmaStyleName.IsEmpty() ? nullptr : TextStyles.Find(FigmaStyleName);
}

const FSlateBrush* UFigmaSyncStylePreset::FindPaintStyle(const FString& FigmaStyleName) const
{
	return FigmaStyleName.IsEmpty() ? nullptr : PaintStyles.Find(FigmaStyleName);
}

const FFigmaButtonStyleOverride* UFigmaSyncStylePreset::FindButtonStyle(const FString& Key) const
{
	return Key.IsEmpty() ? nullptr : ButtonStyles.Find(Key);
}
