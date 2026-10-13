#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "FigmaDocument.h"
#include "FigmaImportLog.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STreeView.h"
#include "Widgets/Views/SListView.h"

class FFigmaBundle;
class FFigmaImportContext;
class UFigmaSyncStylePreset;
class UWidgetBlueprint;
struct FFigmaImportResult;

/**
 * The Figma Sync panel: pick an exported bundle, preview the node tree (and what each layer becomes),
 * create or update a Widget Blueprint, and read the import log and lint report.
 */
class SFigmaSyncPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFigmaSyncPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SFigmaSyncPanel() override;

	/** Loads a bundle (zip, folder or figmasync.json) into the preview. */
	void LoadBundle(const FString& Path);

	/** Targets an existing Widget Blueprint (and loads its last bundle if none is loaded). */
	void SetTarget(UWidgetBlueprint* Blueprint);

	/** Shows the result of an import that happened elsewhere (toolbar re-sync, live push). */
	void ShowResult(const FFigmaImportResult& Result);

	// Drop a bundle .zip or folder from the OS file browser onto the panel.
	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;

private:
	// Source
	FReply OnBrowseZip();
	FReply OnBrowseFolder();
	FReply OnLoadSample();
	FText GetSourceText() const;
	FText GetSourceInfoText() const;

	// Target
	FString GetTargetPath() const;
	void OnTargetChanged(const FAssetData& Asset);
	const UClass* GetParentClass() const;
	void OnParentClassChanged(const UClass* NewClass);
	FString GetPresetPath() const;
	void OnPresetChanged(const FAssetData& Asset);
	EVisibility GetNewAssetVisibility() const;

	// Actions
	FReply OnImport();
	FReply OnResync();
	FReply OnOpenBlueprint();
	FReply OnSaveLookAsPreset();
	FReply OnOpenLogs();
	FReply OnOpenSettings();
	bool HasBundle() const;
	bool HasResyncTarget() const;
	FText GetImportButtonText() const;

	// Preview tree
	TSharedRef<ITableRow> OnGenerateNodeRow(TSharedPtr<FFigmaNode> Node, const TSharedRef<STableViewBase>& OwnerTable);
	void OnGetNodeChildren(TSharedPtr<FFigmaNode> Node, TArray<TSharedPtr<FFigmaNode>>& OutChildren);

	// Log
	TSharedRef<ITableRow> OnGenerateLogRow(TSharedPtr<FFigmaLogEntry> Entry, const TSharedRef<STableViewBase>& OwnerTable);
	void SetLogEntries(const TArray<TSharedPtr<FFigmaLogEntry>>& Entries);

	void SetBundle(const TSharedPtr<FFigmaBundle>& NewBundle, const FString& Path);
	void Notify(const FText& Message, bool bSuccess) const;

	TSharedPtr<FFigmaBundle> Bundle;
	TSharedPtr<FFigmaImportContext> PreviewContext;
	FString SourcePath;
	FString LoadError;

	TArray<TSharedPtr<FFigmaNode>> RootNodes;
	TSharedPtr<STreeView<TSharedPtr<FFigmaNode>>> NodeTree;
	TArray<TSharedPtr<FFigmaLogEntry>> LogEntries;
	TSharedPtr<SListView<TSharedPtr<FFigmaLogEntry>>> LogList;

	TWeakObjectPtr<UWidgetBlueprint> Target;
	TWeakObjectPtr<UClass> ParentClass;
	TWeakObjectPtr<UFigmaSyncStylePreset> Preset;
	FString NewAssetFolder;
	FString NewAssetName;
	bool bSaveAfterImport = true;
	bool bOpenAfterImport = true;
	FText Summary;
};
