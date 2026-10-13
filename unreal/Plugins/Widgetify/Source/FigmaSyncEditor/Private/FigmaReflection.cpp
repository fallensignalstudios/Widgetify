#include "FigmaReflection.h"
#include "UObject/EnumProperty.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"

FProperty* FFigmaReflection::Find(const UStruct* Struct, FName PropertyName)
{
	return Struct ? FindFProperty<FProperty>(Struct, PropertyName) : nullptr;
}

bool FFigmaReflection::Has(const UObject* Object, FName PropertyName)
{
	return Object && Find(Object->GetClass(), PropertyName) != nullptr;
}

bool FFigmaReflection::SetBool(UObject* Object, FName PropertyName, bool bValue)
{
	FBoolProperty* Property = Object ? CastField<FBoolProperty>(Find(Object->GetClass(), PropertyName)) : nullptr;
	if (!Property)
	{
		return false;
	}
	// Handles bitfield bools (uint8 bFoo:1) as well as plain bools.
	Property->SetPropertyValue_InContainer(Object, bValue);
	return true;
}

bool FFigmaReflection::SetFloat(UObject* Object, FName PropertyName, double Value)
{
	FProperty* Property = Object ? Find(Object->GetClass(), PropertyName) : nullptr;
	if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
	{
		FloatProperty->SetPropertyValue_InContainer(Object, static_cast<float>(Value));
		return true;
	}
	if (FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(Property))
	{
		DoubleProperty->SetPropertyValue_InContainer(Object, Value);
		return true;
	}
	return false;
}

bool FFigmaReflection::SetInt(UObject* Object, FName PropertyName, int64 Value)
{
	FNumericProperty* Property = Object ? CastField<FNumericProperty>(Find(Object->GetClass(), PropertyName)) : nullptr;
	if (!Property || !Property->IsInteger() || Property->IsEnum())
	{
		return false;
	}
	Property->SetIntPropertyValue(Property->ContainerPtrToValuePtr<void>(Object), Value);
	return true;
}

bool FFigmaReflection::SetName(UObject* Object, FName PropertyName, FName Value)
{
	FNameProperty* Property = Object ? CastField<FNameProperty>(Find(Object->GetClass(), PropertyName)) : nullptr;
	if (!Property)
	{
		return false;
	}
	Property->SetPropertyValue_InContainer(Object, Value);
	return true;
}

bool FFigmaReflection::SetString(UObject* Object, FName PropertyName, const FString& Value)
{
	FStrProperty* Property = Object ? CastField<FStrProperty>(Find(Object->GetClass(), PropertyName)) : nullptr;
	if (!Property)
	{
		return false;
	}
	Property->SetPropertyValue_InContainer(Object, Value);
	return true;
}

bool FFigmaReflection::SetText(UObject* Object, FName PropertyName, const FText& Value)
{
	FTextProperty* Property = Object ? CastField<FTextProperty>(Find(Object->GetClass(), PropertyName)) : nullptr;
	if (!Property)
	{
		return false;
	}
	Property->SetPropertyValue_InContainer(Object, Value);
	return true;
}

bool FFigmaReflection::SetEnum(UObject* Object, FName PropertyName, const FString& ValueName)
{
	FProperty* Property = Object ? Find(Object->GetClass(), PropertyName) : nullptr;
	UEnum* Enum = nullptr;
	FNumericProperty* Underlying = nullptr;
	if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
	{
		Enum = EnumProperty->GetEnum();
		Underlying = EnumProperty->GetUnderlyingProperty();
	}
	else if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
	{
		Enum = ByteProperty->Enum;
		Underlying = ByteProperty;
	}
	if (!Enum || !Underlying)
	{
		return false;
	}

	int64 Value = Enum->GetValueByNameString(ValueName);
	if (Value == INDEX_NONE)
	{
		Value = Enum->GetValueByNameString(Enum->GenerateFullEnumName(*ValueName));
	}
	if (Value == INDEX_NONE)
	{
		return false;
	}
	Underlying->SetIntPropertyValue(Property->ContainerPtrToValuePtr<void>(Object), Value);
	return true;
}

bool FFigmaReflection::SetObject(UObject* Object, FName PropertyName, UObject* Value)
{
	FObjectPropertyBase* Property = Object ? CastField<FObjectPropertyBase>(Find(Object->GetClass(), PropertyName)) : nullptr;
	if (!Property || CastField<FClassProperty>(Property))
	{
		return false;
	}
	if (Value && !Value->IsA(Property->PropertyClass))
	{
		return false;
	}
	Property->SetObjectPropertyValue_InContainer(Object, Value);
	return true;
}

bool FFigmaReflection::SetClass(UObject* Object, FName PropertyName, UClass* Value)
{
	FClassProperty* Property = Object ? CastField<FClassProperty>(Find(Object->GetClass(), PropertyName)) : nullptr;
	if (!Property)
	{
		return false;
	}
	if (Value && Property->MetaClass && !Value->IsChildOf(Property->MetaClass))
	{
		return false;
	}
	Property->SetObjectPropertyValue_InContainer(Object, Value);
	return true;
}

void* FFigmaReflection::GetStructPtr(UObject* Object, FName PropertyName, const UScriptStruct* ExpectedStruct)
{
	FStructProperty* Property = Object ? CastField<FStructProperty>(Find(Object->GetClass(), PropertyName)) : nullptr;
	if (!Property || !ExpectedStruct || Property->Struct != ExpectedStruct)
	{
		return nullptr;
	}
	return Property->ContainerPtrToValuePtr<void>(Object);
}

bool FFigmaReflection::AddToStructArrayRaw(UObject* Object, FName PropertyName, const UScriptStruct* ExpectedStruct, const void* Value, bool bReplaceExisting)
{
	FArrayProperty* ArrayProperty = Object ? CastField<FArrayProperty>(Find(Object->GetClass(), PropertyName)) : nullptr;
	FStructProperty* Inner = ArrayProperty ? CastField<FStructProperty>(ArrayProperty->Inner) : nullptr;
	if (!Inner || Inner->Struct != ExpectedStruct || !Value)
	{
		return false;
	}
	FScriptArrayHelper Helper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(Object));
	if (bReplaceExisting)
	{
		Helper.EmptyValues();
	}
	const int32 Index = Helper.AddValue();
	Inner->Struct->CopyScriptStruct(Helper.GetRawPtr(Index), Value);
	return true;
}

UClass* FFigmaReflection::LoadClass(const FString& ClassPath)
{
	if (ClassPath.IsEmpty())
	{
		return nullptr;
	}
	const FSoftClassPath SoftPath(ClassPath);
	if (UClass* Loaded = SoftPath.ResolveClass())
	{
		return Loaded;
	}
	// Script classes resolve only when their module is loaded; asset classes can be loaded on demand.
	if (ClassPath.StartsWith(TEXT("/Script/")))
	{
		return nullptr;
	}
	return SoftPath.TryLoadClass<UObject>();
}
