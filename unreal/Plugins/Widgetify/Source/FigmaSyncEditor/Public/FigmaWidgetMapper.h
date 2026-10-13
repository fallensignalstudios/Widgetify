#pragma once

#include "CoreMinimal.h"

class FFigmaImportContext;
class UClass;
class UWidget;
class UWidgetBlueprint;
struct FFigmaNode;

/**
 * Extension point for custom Figma -> UMG mappings. FigmaSyncCommonUI registers one to turn Btn/* components into
 * CommonUI buttons; studios can register their own (e.g. to use project-specific widget classes).
 *
 * Register with IFigmaSyncEditorModule::Get().RegisterWidgetMapper().
 */
class IFigmaWidgetMapper
{
public:
	virtual ~IFigmaWidgetMapper() = default;

	virtual FName GetMapperName() const = 0;

	/** Runs before anything is built. Create shared assets (button classes, styles) here. */
	virtual void PrepareImport(FFigmaImportContext& Context) {}

	/** Parent class for a newly created screen blueprint, or null to let others decide. */
	virtual UClass* ChooseScreenParentClass(FFigmaImportContext& Context) { return nullptr; }

	/** A widget class that represents this node as a single leaf (its children are not built), or null. */
	virtual UClass* ChooseLeafClass(const FFigmaNode& Node, FFigmaImportContext& Context) { return nullptr; }

	/** Widget class for a text node (must derive from UTextBlock), or null for UTextBlock. */
	virtual UClass* ChooseTextClass(const FFigmaNode& Node, FFigmaImportContext& Context) { return nullptr; }

	/** Called after default configuration of a widget whose class this mapper chose. */
	virtual void ConfigureWidget(UWidget* Widget, const FFigmaNode& Node, FFigmaImportContext& Context) {}

	/** Runs after the screen blueprint compiled; class defaults can be edited here. */
	virtual void FinalizeBlueprint(UWidgetBlueprint* Blueprint, FFigmaImportContext& Context) {}

	/** Adds findings to the lint report. */
	virtual void Lint(UWidgetBlueprint* Blueprint, FFigmaImportContext& Context) {}
};
