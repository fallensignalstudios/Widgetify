#pragma once

#include "CoreMinimal.h"
#include "FigmaDocument.h"
#include "FigmaWidgetMapper.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UWidgetBlueprint;

/**
 * CommonUI mapping:
 *  - Btn/* component sets -> one CommonButtonBase Widget Blueprint per set (built from the Normal variant),
 *    one CommonButtonStyle per variant group (Normal/Hovered/Pressed/Disabled/Selected brushes from the variants)
 *    and CommonTextStyles for the label in each state;
 *  - Act_* layers -> CommonActionWidget (input glyph), bound with [action=...];
 *  - screens -> CommonActivatableWidget (configurable).
 *
 * Everything goes through class paths and property names, so the plugin compiles and degrades gracefully when the
 * CommonUI plugin is not enabled.
 */
class FFigmaCommonUIMapper : public IFigmaWidgetMapper
{
public:
	virtual FName GetMapperName() const override { return TEXT("CommonUI"); }
	virtual void PrepareImport(FFigmaImportContext& Context) override;
	virtual UClass* ChooseScreenParentClass(FFigmaImportContext& Context) override;
	virtual UClass* ChooseLeafClass(const FFigmaNode& Node, FFigmaImportContext& Context) override;
	virtual UClass* ChooseTextClass(const FFigmaNode& Node, FFigmaImportContext& Context) override;
	virtual void ConfigureWidget(UWidget* Widget, const FFigmaNode& Node, FFigmaImportContext& Context) override;
	virtual void Lint(UWidgetBlueprint* Blueprint, FFigmaImportContext& Context) override;

private:
	struct FButtonSet
	{
		FString Name;
		FString StateProperty;
		FString LabelComponentProperty;
		FName LabelProperty;
		TWeakObjectPtr<UWidgetBlueprint> Blueprint;
		TWeakObjectPtr<UClass> WidgetClass;
		TMap<FString, TWeakObjectPtr<UClass>> StyleByGroup;
		bool bDerivedStates = false;
	};

	bool LoadCommonUI(FFigmaImportContext& Context);
	void PrepareButtonSet(const FFigmaNode& Instance, FFigmaImportContext& Context);
	UClass* MakeTextStyle(const FString& Folder, const FString& AssetName, const FFigmaNode& TextNode, FFigmaImportContext& Context);
	UClass* MakeButtonStyle(const FString& Folder, const FString& AssetName, const FFigmaComponentSet& Set, const FButtonSet& Info, const FString& Group, FFigmaImportContext& Context);
	UWidgetBlueprint* MakeButtonWidget(const FString& Folder, const FString& AssetName, const FFigmaNode& ContentRoot, FButtonSet& Info, UClass* LabelStyle, FFigmaImportContext& Context);
	bool AddLabelGraph(UWidgetBlueprint* Blueprint, FName LabelWidget, FName TextVariable, FFigmaImportContext& Context);
	bool BindInputAction(UObject* Widget, const FString& Action, bool bActionWidget, FFigmaImportContext& Context);
	UClass* FindOrCreateBlueprintClass(const FString& Folder, const FString& AssetName, UClass* ParentClass, FFigmaImportContext& Context);
	const FButtonSet* FindSetForNode(const FFigmaNode& Node) const;

	static FString GetSetId(const FFigmaNode& Node);
	static const FFigmaNode* FindLabelNode(const FFigmaNode& Root, const FString& PropertyName);

	bool bAvailable = false;
	TWeakObjectPtr<UClass> ButtonBaseClass;
	TWeakObjectPtr<UClass> ButtonStyleClass;
	TWeakObjectPtr<UClass> TextStyleClass;
	TWeakObjectPtr<UClass> TextBlockClass;
	TWeakObjectPtr<UClass> ActionWidgetClass;

	TMap<FString, FButtonSet> Sets;
	TMap<FString, FFigmaComponentSet> SyntheticSets;

	/** While a button's content is being built: its label layer gets this text style. */
	bool bBuildingButton = false;
	FString CurrentLabelNodeId;
	TWeakObjectPtr<UClass> CurrentLabelStyle;
};
