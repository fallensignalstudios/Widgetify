#include "FigmaSyncBlueprintExtension.h"
#include "FigmaSyncStylePreset.h"
#include "WidgetBlueprint.h"

UFigmaSyncBlueprintExtension* UFigmaSyncBlueprintExtension::FindOrAdd(UWidgetBlueprint* Blueprint)
{
	if (!Blueprint)
	{
		return nullptr;
	}
	if (UFigmaSyncBlueprintExtension* Existing = UWidgetBlueprintExtension::GetExtension<UFigmaSyncBlueprintExtension>(Blueprint))
	{
		return Existing;
	}
	UFigmaSyncBlueprintExtension* Extension = UWidgetBlueprintExtension::RequestExtension<UFigmaSyncBlueprintExtension>(Blueprint);
	if (Extension)
	{
		// RequestExtension creates the object without flags; make it part of undo/redo like the rest of the blueprint.
		Extension->SetFlags(RF_Transactional);
	}
	return Extension;
}

UFigmaSyncBlueprintExtension* UFigmaSyncBlueprintExtension::Find(const UWidgetBlueprint* Blueprint)
{
	return Blueprint ? UWidgetBlueprintExtension::GetExtension<UFigmaSyncBlueprintExtension>(Blueprint) : nullptr;
}

const FFigmaSyncLockRule* UFigmaSyncBlueprintExtension::FindLock(FName WidgetName) const
{
	return Locks.FindByPredicate([WidgetName](const FFigmaSyncLockRule& Rule) { return Rule.Widget == WidgetName; });
}

bool UFigmaSyncBlueprintExtension::IsManagedWidget(FName WidgetName) const
{
	for (const TPair<FString, FFigmaSyncNodeRecord>& Pair : Nodes)
	{
		if (Pair.Value.Widget == WidgetName)
		{
			return true;
		}
		for (const TPair<FName, FName>& Helper : Pair.Value.Helpers)
		{
			if (Helper.Value == WidgetName)
			{
				return true;
			}
		}
	}
	return false;
}

TSet<FName> UFigmaSyncBlueprintExtension::GetManagedWidgetNames() const
{
	TSet<FName> Names;
	for (const TPair<FString, FFigmaSyncNodeRecord>& Pair : Nodes)
	{
		Names.Add(Pair.Value.Widget);
		for (const TPair<FName, FName>& Helper : Pair.Value.Helpers)
		{
			Names.Add(Helper.Value);
		}
	}
	return Names;
}
