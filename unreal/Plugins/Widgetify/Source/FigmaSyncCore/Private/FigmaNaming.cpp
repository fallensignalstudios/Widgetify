#include "FigmaNaming.h"

namespace
{
	constexpr int32 MaxIdentifierLength = 96;

	bool IsIdentifierChar(TCHAR C)
	{
		return (C >= TEXT('A') && C <= TEXT('Z')) || (C >= TEXT('a') && C <= TEXT('z')) || (C >= TEXT('0') && C <= TEXT('9')) || C == TEXT('_');
	}
}

FString FFigmaNaming::SanitizeIdentifier(const FString& In, const FString& Fallback)
{
	FString Out;
	Out.Reserve(In.Len());
	bool bLastWasReplacement = false;
	for (const TCHAR C : In)
	{
		if (IsIdentifierChar(C))
		{
			Out.AppendChar(C);
			bLastWasReplacement = false;
		}
		else if (!bLastWasReplacement && Out.Len() > 0)
		{
			Out.AppendChar(TEXT('_'));
			bLastWasReplacement = true;
		}
	}
	if (bLastWasReplacement)
	{
		Out.LeftChopInline(1);
	}
	if (Out.IsEmpty())
	{
		Out = Fallback.IsEmpty() ? FString(TEXT("Widget")) : Fallback;
	}
	if (FChar::IsDigit(Out[0]))
	{
		Out = TEXT("W_") + Out;
	}
	if (Out.Equals(TEXT("None"), ESearchCase::IgnoreCase))
	{
		Out += TEXT("_");
	}
	if (Out.Len() > MaxIdentifierLength)
	{
		Out.LeftInline(MaxIdentifierLength);
	}
	return Out;
}

FString FFigmaNaming::MakeUnique(const FString& Base, TFunctionRef<bool(const FString&)> IsTaken)
{
	if (!IsTaken(Base))
	{
		return Base;
	}
	for (int32 Suffix = 2; Suffix < 100000; ++Suffix)
	{
		const FString Candidate = FString::Printf(TEXT("%s_%d"), *Base, Suffix);
		if (!IsTaken(Candidate))
		{
			return Candidate;
		}
	}
	return Base + TEXT("_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
}

bool FFigmaNaming::StartsWithAny(const FString& Name, const TArray<FString>& Prefixes)
{
	for (const FString& Prefix : Prefixes)
	{
		if (!Prefix.IsEmpty() && Name.StartsWith(Prefix, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

FString FFigmaNaming::PathToIdentifier(const FString& Path)
{
	return SanitizeIdentifier(Path.Replace(TEXT("/"), TEXT("_")), TEXT("Component"));
}
