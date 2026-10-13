#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

class IFigmaWidgetMapper;
class UWidgetBlueprint;

/** Public entry points of the Widgetify editor module. */
class IFigmaSyncEditorModule : public IModuleInterface
{
public:
	static IFigmaSyncEditorModule& Get()
	{
		return FModuleManager::LoadModuleChecked<IFigmaSyncEditorModule>(TEXT("FigmaSyncEditor"));
	}

	static bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded(TEXT("FigmaSyncEditor"));
	}

	/** Adds a custom Figma -> UMG mapping (see IFigmaWidgetMapper). */
	virtual void RegisterWidgetMapper(const TSharedRef<IFigmaWidgetMapper>& Mapper) = 0;
	virtual void UnregisterWidgetMapper(const TSharedRef<IFigmaWidgetMapper>& Mapper) = 0;
	virtual TArray<TSharedRef<IFigmaWidgetMapper>> GetWidgetMappers() const = 0;

	/** Opens the Figma Sync panel, optionally pre-filled with a bundle and/or a target blueprint. */
	virtual void OpenPanel(const FString& BundlePath = FString(), UWidgetBlueprint* Target = nullptr) = 0;

	/** Re-syncs a blueprint from the bundle it was last synced from and shows the result. */
	virtual bool ResyncBlueprint(UWidgetBlueprint* Blueprint) = 0;
};
