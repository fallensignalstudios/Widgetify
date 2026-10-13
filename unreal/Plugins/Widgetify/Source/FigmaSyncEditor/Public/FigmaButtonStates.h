#pragma once

#include "CoreMinimal.h"
#include "FigmaDocument.h"

enum class EFigmaButtonState : uint8
{
	Normal,
	Hovered,
	Pressed,
	Disabled,
	Selected,
	SelectedHovered,
	SelectedPressed,
	Focused,
	Unknown,
};

/**
 * Reads button states out of Figma variants: "State=Hover" in a component set, or "Btn/Primary/Hover" naming.
 * A set may have other properties too (Size=Large); states are matched within the same combination of those.
 */
struct FIGMASYNCEDITOR_API FFigmaButtonStates
{
	static EFigmaButtonState Classify(const FString& Value);

	/** The variant property that holds the state ("State", "Status"...), or empty when there is none. */
	static FString FindStateProperty(const FFigmaComponentSet& Set);

	/** Variant properties without the state property: identifies a style group ("Size=Large"). */
	static TMap<FString, FString> WithoutState(const TMap<FString, FString>& Properties, const FString& StateProperty);

	/** Stable key for a style group, e.g. "Size=Large,Type=Ghost" (empty for sets with only a state property). */
	static FString GroupKey(const TMap<FString, FString>& Properties, const FString& StateProperty);

	/** Variant for a state within a style group, or null. */
	static const FFigmaVariant* FindVariant(const FFigmaComponentSet& Set, const FString& StateProperty, const FString& Group, EFigmaButtonState State);

	/** Style groups in declaration order. */
	static TArray<FString> GetGroups(const FFigmaComponentSet& Set, const FString& StateProperty);
};
