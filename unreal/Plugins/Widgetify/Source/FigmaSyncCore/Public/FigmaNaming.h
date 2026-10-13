#pragma once

#include "CoreMinimal.h"

/** Turns Figma layer names into names Unreal accepts for widgets, Blueprint variables and assets. */
struct FIGMASYNCCORE_API FFigmaNaming
{
	/**
	 * ASCII letters, digits and underscores only, never starting with a digit, never "None".
	 * Characters Unreal rejects become single underscores; underscores the designer typed are kept.
	 */
	static FString SanitizeIdentifier(const FString& In, const FString& Fallback = TEXT("Widget"));

	/** Base, Base_2, Base_3... until IsTaken returns false. */
	static FString MakeUnique(const FString& Base, TFunctionRef<bool(const FString&)> IsTaken);

	/** Case-insensitive prefix test against a list ("Btn_", "Txt_"...). */
	static bool StartsWithAny(const FString& Name, const TArray<FString>& Prefixes);

	/** "Btn/Primary" -> "Btn_Primary" (component paths to asset names). */
	static FString PathToIdentifier(const FString& Path);
};
