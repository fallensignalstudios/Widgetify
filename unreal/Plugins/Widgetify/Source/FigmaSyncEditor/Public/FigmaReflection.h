#pragma once

#include "CoreMinimal.h"
#include "UObject/Class.h"

class FProperty;
class UScriptStruct;

/**
 * Sets UPROPERTYs by name, checking types at runtime. Used for CommonUI (whose classes are MinimalAPI and optional)
 * and for widget properties whose setters only exist in newer engine versions. Every setter returns false instead
 * of asserting when the property is missing or has an unexpected type, so callers can log and move on.
 */
struct FIGMASYNCEDITOR_API FFigmaReflection
{
	static FProperty* Find(const UStruct* Struct, FName PropertyName);
	static bool Has(const UObject* Object, FName PropertyName);

	static bool SetBool(UObject* Object, FName PropertyName, bool bValue);
	static bool SetFloat(UObject* Object, FName PropertyName, double Value);
	static bool SetInt(UObject* Object, FName PropertyName, int64 Value);
	static bool SetName(UObject* Object, FName PropertyName, FName Value);
	static bool SetString(UObject* Object, FName PropertyName, const FString& Value);
	static bool SetText(UObject* Object, FName PropertyName, const FText& Value);
	/** Enum value by short name ("Ellipsis") or full name ("ETextOverflowPolicy::Ellipsis"). */
	static bool SetEnum(UObject* Object, FName PropertyName, const FString& ValueName);
	/** Object property; the value must be compatible with the property class. */
	static bool SetObject(UObject* Object, FName PropertyName, UObject* Value);
	/** TSubclassOf / class property; the value must derive from the meta class. */
	static bool SetClass(UObject* Object, FName PropertyName, UClass* Value);

	/** Pointer to a struct property's value when its type is exactly ExpectedStruct, otherwise null. */
	static void* GetStructPtr(UObject* Object, FName PropertyName, const UScriptStruct* ExpectedStruct);

	template <typename T>
	static bool SetStruct(UObject* Object, FName PropertyName, const UScriptStruct* ExpectedStruct, const T& Value)
	{
		if (T* Ptr = static_cast<T*>(GetStructPtr(Object, PropertyName, ExpectedStruct)))
		{
			*Ptr = Value;
			return true;
		}
		return false;
	}

	/** Appends an element to a TArray<Struct> property. */
	template <typename T>
	static bool AddToStructArray(UObject* Object, FName PropertyName, const UScriptStruct* ExpectedStruct, const T& Value, bool bReplaceExisting)
	{
		return AddToStructArrayRaw(Object, PropertyName, ExpectedStruct, &Value, bReplaceExisting);
	}

	static bool AddToStructArrayRaw(UObject* Object, FName PropertyName, const UScriptStruct* ExpectedStruct, const void* Value, bool bReplaceExisting);

	/** Loads a class by path ("/Script/CommonUI.CommonButtonBase") without asserting when its module isn't loaded. */
	static UClass* LoadClass(const FString& ClassPath);
};
