#pragma once

#include "CoreMinimal.h"
#include "FigmaBundle.h"
#include "FigmaImportLog.h"
#include "UObject/WeakObjectPtrTemplates.h"

class FFigmaFontResolver;
class FFigmaTextureImporter;
class IFigmaWidgetMapper;
class UFigmaSyncSettings;
class UFigmaSyncStylePreset;
class UPackage;
class UUserWidget;
class UWidgetBlueprint;

struct FIGMASYNCEDITOR_API FFigmaImportOptions
{
	/** Widget Blueprint to update. When unset, one is created (or reused) at PackagePath/AssetName. */
	TWeakObjectPtr<UWidgetBlueprint> Target;

	/** Folder for a new blueprint, e.g. "/Game/UI/Figma". Defaults to the project setting. */
	FString PackagePath;

	/** Name for a new blueprint. Defaults to prefix + frame name ("WBP_MainMenu"). */
	FString AssetName;

	/** Parent class for a new blueprint. Unset = mappers (CommonUI) or UUserWidget decide. */
	TWeakObjectPtr<UClass> ParentClass;

	/** Look overrides. Unset = the blueprint's last preset, then the project default. */
	TWeakObjectPtr<UFigmaSyncStylePreset> StylePreset;

	bool bSave = true;
	bool bOpenEditor = false;
	bool bShowMessageLog = true;

	/** Where the bundle came from; stored on the blueprint for one-click re-sync. */
	FString SourcePath;
};

struct FFigmaImportStats
{
	int32 WidgetsCreated = 0;
	int32 WidgetsUpdated = 0;
	int32 WidgetsKept = 0;
	int32 WidgetsRemoved = 0;
	int32 TexturesImported = 0;
	int32 TexturesReused = 0;
};

struct FIGMASYNCEDITOR_API FFigmaImportResult
{
	bool bSuccess = false;
	bool bCreatedBlueprint = false;
	TWeakObjectPtr<UWidgetBlueprint> Blueprint;
	FFigmaImportStats Stats;
	TSharedRef<FFigmaImportLog> Log = MakeShared<FFigmaImportLog>();

	FText GetSummary() const;
};

/** Everything an import needs, passed to the builder, the mappers and the linter. */
class FIGMASYNCEDITOR_API FFigmaImportContext
{
public:
	FFigmaImportContext(const FFigmaBundle& InBundle, const FFigmaImportOptions& InOptions, const TSharedRef<FFigmaImportLog>& InLog);
	~FFigmaImportContext();

	const FFigmaBundle& Bundle;
	const FFigmaDocument& Document;
	FFigmaImportOptions Options;
	const UFigmaSyncSettings& Settings;
	TSharedRef<FFigmaImportLog> Log;

	UFigmaSyncStylePreset* Preset = nullptr;
	/** Created by SetPreset() (the preset's font table is part of font resolution). */
	TSharedPtr<FFigmaFontResolver> Fonts;
	TSharedRef<FFigmaTextureImporter> Textures;
	TArray<TSharedRef<IFigmaWidgetMapper>> Mappers;

	/** The screen blueprint being built. */
	UWidgetBlueprint* Blueprint = nullptr;

	/** Package folder for this screen's textures ("/Game/UI/Figma/Textures/MainMenu"). */
	FString TextureFolder;

	/** Package folder for assets shared between screens ("/Game/UI/Figma/Components"). */
	FString SharedFolder;

	FFigmaImportStats Stats;

	/** Packages to save at the end. */
	TSet<TWeakObjectPtr<UPackage>> TouchedPackages;

	/** Layer tagged [focus]: the widget that should get initial gamepad/keyboard focus. */
	FName FocusWidgetName;

	/** [action=...] tags that could not be bound to an input action, as "Widget: Action". */
	TArray<FString> UnboundActions;

	/** Figma node id -> named widget, for every node built during this import. */
	TMap<FString, FName> BuiltWidgets;

	void TouchPackage(UPackage* Package);

	/** Sets the style preset and (re)creates the font resolver with its font table. */
	void SetPreset(UFigmaSyncStylePreset* InPreset);

	/** Lower-cased node tag lookup helpers shared by builder and mappers. */
	bool IsButtonNode(const FFigmaNode& Node) const;
	bool IsButtonComponent(const FFigmaNode& Node) const;
	bool IsActionWidgetNode(const FFigmaNode& Node) const;
	bool ShouldBeVariable(const FFigmaNode& Node) const;
};
