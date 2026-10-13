#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"
#include "WidgetBlueprintExtension.h"
#include "FigmaSyncBlueprintExtension.generated.h"

class UFigmaSyncStylePreset;

/** What Widgetify remembers about one Figma layer in one Widget Blueprint. */
USTRUCT()
struct FIGMASYNCEDITOR_API FFigmaSyncNodeRecord
{
	GENERATED_BODY()

	/** The widget named after the layer (what designers and Blueprints reference). */
	UPROPERTY(VisibleAnywhere, Category = "Figma")
	FName Widget;

	/** Helper widgets Widgetify created around it, keyed by role (Size, Layout, Stack, Bg, Flow, Abs, Spacer1...). */
	UPROPERTY(VisibleAnywhere, Category = "Figma")
	TMap<FName, FName> Helpers;

	/** Class of the named widget when it was last synced (used to relink widgets renamed in Unreal). */
	UPROPERTY(VisibleAnywhere, Category = "Figma")
	FSoftClassPath WidgetClass;

	/** Figma layer name at the last sync. */
	UPROPERTY(VisibleAnywhere, Category = "Figma")
	FString LayerName;

	UPROPERTY(VisibleAnywhere, Category = "Figma")
	FString ParentId;

	/** True when the layer disappeared from Figma but the widget was kept. */
	UPROPERTY(VisibleAnywhere, Category = "Figma")
	bool bRemovedInFigma = false;
};

/** A widget (or parts of it) that re-sync must leave alone. */
USTRUCT(BlueprintType)
struct FIGMASYNCEDITOR_API FFigmaSyncLockRule
{
	GENERATED_BODY()

	/** Widget name in this Widget Blueprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lock")
	FName Widget;

	/** Position, size, padding and alignment. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lock")
	bool bLockLayout = true;

	/** Brushes, colors, fonts, images, opacity and visibility. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lock")
	bool bLockAppearance = true;

	/** Text content. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lock")
	bool bLockText = true;

	/** Don't add, remove or reorder this widget's children. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lock")
	bool bLockChildren = false;
};

/**
 * Figma sync state stored inside a Widget Blueprint (editor-only data; never cooked).
 * Maps Figma node ids to widgets so re-sync updates the look without touching graphs, bindings or animations.
 */
UCLASS()
class FIGMASYNCEDITOR_API UFigmaSyncBlueprintExtension : public UWidgetBlueprintExtension
{
	GENERATED_BODY()

public:
	/** Last bundle (zip or folder) this blueprint was synced from. */
	UPROPERTY(VisibleAnywhere, Category = "Source")
	FString SourcePath;

	UPROPERTY(VisibleAnywhere, Category = "Source")
	FString FigmaFileName;

	UPROPERTY(VisibleAnywhere, Category = "Source")
	FString FrameId;

	UPROPERTY(VisibleAnywhere, Category = "Source")
	FString FrameName;

	UPROPERTY(VisibleAnywhere, Category = "Source")
	FDateTime LastSyncTime;

	UPROPERTY(EditAnywhere, Category = "Source")
	TSoftObjectPtr<UFigmaSyncStylePreset> StylePreset;

	/** Widgets and properties re-sync must not touch. */
	UPROPERTY(EditAnywhere, Category = "Lock List")
	TArray<FFigmaSyncLockRule> Locks;

	/** Figma node id -> record. */
	UPROPERTY()
	TMap<FString, FFigmaSyncNodeRecord> Nodes;

	/** Finds (or creates) the extension. Must not be called while the blueprint is compiling. */
	static UFigmaSyncBlueprintExtension* FindOrAdd(UWidgetBlueprint* Blueprint);
	static UFigmaSyncBlueprintExtension* Find(const UWidgetBlueprint* Blueprint);

	const FFigmaSyncLockRule* FindLock(FName WidgetName) const;
	bool IsManagedWidget(FName WidgetName) const;
	/** Every widget name Widgetify owns in this blueprint (named widgets and helpers). */
	TSet<FName> GetManagedWidgetNames() const;
};
