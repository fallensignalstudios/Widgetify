#include "FigmaButtonStates.h"

EFigmaButtonState FFigmaButtonStates::Classify(const FString& InValue)
{
	const FString Value = InValue.TrimStartAndEnd().ToLower().Replace(TEXT(" "), TEXT("")).Replace(TEXT("-"), TEXT("")).Replace(TEXT("_"), TEXT(""));
	if (Value == TEXT("default") || Value == TEXT("normal") || Value == TEXT("rest") || Value == TEXT("idle") || Value == TEXT("enabled") || Value == TEXT("base"))
	{
		return EFigmaButtonState::Normal;
	}
	if (Value == TEXT("hover") || Value == TEXT("hovered") || Value == TEXT("highlighted") || Value == TEXT("highlight"))
	{
		return EFigmaButtonState::Hovered;
	}
	if (Value == TEXT("pressed") || Value == TEXT("down") || Value == TEXT("active") || Value == TEXT("clicked") || Value == TEXT("press"))
	{
		return EFigmaButtonState::Pressed;
	}
	if (Value == TEXT("disabled") || Value == TEXT("inactive") || Value == TEXT("locked"))
	{
		return EFigmaButtonState::Disabled;
	}
	if (Value == TEXT("selected") || Value == TEXT("checked") || Value == TEXT("on") || Value == TEXT("toggled"))
	{
		return EFigmaButtonState::Selected;
	}
	if (Value == TEXT("selectedhover") || Value == TEXT("selectedhovered") || Value == TEXT("checkedhover"))
	{
		return EFigmaButtonState::SelectedHovered;
	}
	if (Value == TEXT("selectedpressed") || Value == TEXT("checkedpressed"))
	{
		return EFigmaButtonState::SelectedPressed;
	}
	if (Value == TEXT("focus") || Value == TEXT("focused"))
	{
		return EFigmaButtonState::Focused;
	}
	return EFigmaButtonState::Unknown;
}

FString FFigmaButtonStates::FindStateProperty(const FFigmaComponentSet& Set)
{
	static const TCHAR* PreferredNames[] = { TEXT("State"), TEXT("Status"), TEXT("Interaction"), TEXT("Mode") };
	for (const TCHAR* Preferred : PreferredNames)
	{
		for (const FString& Name : Set.PropertyNames)
		{
			if (Name.Equals(Preferred, ESearchCase::IgnoreCase))
			{
				return Name;
			}
		}
	}
	// Otherwise: the first property whose values look like states.
	for (const FString& Name : Set.PropertyNames)
	{
		for (const FFigmaVariant& Variant : Set.Variants)
		{
			const FString* Value = Variant.Properties.Find(Name);
			if (Value && Classify(*Value) != EFigmaButtonState::Unknown && Classify(*Value) != EFigmaButtonState::Normal)
			{
				return Name;
			}
		}
	}
	return Set.Variants.Num() > 0 && Set.Variants[0].Properties.Contains(TEXT("State")) ? FString(TEXT("State")) : FString();
}

TMap<FString, FString> FFigmaButtonStates::WithoutState(const TMap<FString, FString>& Properties, const FString& StateProperty)
{
	TMap<FString, FString> Out = Properties;
	if (!StateProperty.IsEmpty())
	{
		Out.Remove(StateProperty);
	}
	return Out;
}

FString FFigmaButtonStates::GroupKey(const TMap<FString, FString>& Properties, const FString& StateProperty)
{
	TArray<FString> Parts;
	for (const TPair<FString, FString>& Pair : Properties)
	{
		if (!Pair.Key.Equals(StateProperty))
		{
			Parts.Add(FString::Printf(TEXT("%s=%s"), *Pair.Key, *Pair.Value));
		}
	}
	Parts.Sort();
	return FString::Join(Parts, TEXT(","));
}

const FFigmaVariant* FFigmaButtonStates::FindVariant(const FFigmaComponentSet& Set, const FString& StateProperty, const FString& Group, EFigmaButtonState State)
{
	for (const FFigmaVariant& Variant : Set.Variants)
	{
		if (GroupKey(Variant.Properties, StateProperty) != Group)
		{
			continue;
		}
		const FString* Value = StateProperty.IsEmpty() ? nullptr : Variant.Properties.Find(StateProperty);
		const EFigmaButtonState VariantState = Value ? Classify(*Value) : EFigmaButtonState::Normal;
		if (VariantState == State)
		{
			return &Variant;
		}
	}
	return nullptr;
}

TArray<FString> FFigmaButtonStates::GetGroups(const FFigmaComponentSet& Set, const FString& StateProperty)
{
	TArray<FString> Groups;
	for (const FFigmaVariant& Variant : Set.Variants)
	{
		Groups.AddUnique(GroupKey(Variant.Properties, StateProperty));
	}
	return Groups;
}
