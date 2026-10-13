#include "UI/SFigmaSyncPanel.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/UserWidget.h"
#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "Engine/Font.h"
#include "FigmaAssetUtils.h"
#include "FigmaBundle.h"
#include "FigmaFontResolver.h"
#include "FigmaImportContext.h"
#include "FigmaImporter.h"
#include "FigmaNaming.h"
#include "FigmaStyleConverter.h"
#include "FigmaSyncBlueprintExtension.h"
#include "FigmaSyncSettings.h"
#include "FigmaSyncStyle.h"
#include "FigmaSyncStylePreset.h"
#include "FigmaWidgetBuilder.h"
#include "FileHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "IDesktopPlatform.h"
#include "ISettingsModule.h"
#include "Input/DragAndDrop.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "PropertyCustomizationHelpers.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Textures/SlateIcon.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FigmaSyncPanel"

namespace
{
	TSharedRef<SWidget> SectionLabel(const FText& Text)
	{
		return SNew(STextBlock).Text(Text).Font(FCoreStyle::GetDefaultFontStyle("Bold", 10));
	}

	TSharedRef<SWidget> FieldLabel(const FText& Text)
	{
		return SNew(SBox).MinDesiredWidth(110.f).VAlign(VAlign_Center)[SNew(STextBlock).Text(Text)];
	}

	FString TypeLabel(const FFigmaNode& Node)
	{
		FString Type;
		switch (Node.Type)
		{
		case EFigmaNodeType::Frame: Type = TEXT("Frame"); break;
		case EFigmaNodeType::Group: Type = TEXT("Group"); break;
		case EFigmaNodeType::Component: Type = TEXT("Component"); break;
		case EFigmaNodeType::ComponentSet: Type = TEXT("Component set"); break;
		case EFigmaNodeType::Instance: Type = TEXT("Instance"); break;
		case EFigmaNodeType::Section: Type = TEXT("Section"); break;
		case EFigmaNodeType::Rectangle: Type = TEXT("Rectangle"); break;
		case EFigmaNodeType::Ellipse: Type = TEXT("Ellipse"); break;
		case EFigmaNodeType::Line: Type = TEXT("Line"); break;
		case EFigmaNodeType::Text: Type = TEXT("Text"); break;
		case EFigmaNodeType::Vector: Type = TEXT("Vector"); break;
		case EFigmaNodeType::Star: Type = TEXT("Star"); break;
		case EFigmaNodeType::Polygon: Type = TEXT("Polygon"); break;
		case EFigmaNodeType::BooleanOperation: Type = TEXT("Boolean"); break;
		default: Type = TEXT("Layer"); break;
		}
		if (Node.IsAutoLayout())
		{
			Type += Node.Layout.Mode == EFigmaLayoutMode::Horizontal ? TEXT(" (auto H)") : TEXT(" (auto V)");
		}
		if (Node.Component.IsSet() && !Node.Component.GetFamilyName().IsEmpty())
		{
			Type += FString::Printf(TEXT(" of %s"), *Node.Component.GetFamilyName());
		}
		for (const TPair<FString, FString>& Tag : Node.Tags)
		{
			Type += Tag.Value.IsEmpty() ? FString::Printf(TEXT(" [%s]"), *Tag.Key) : FString::Printf(TEXT(" [%s=%s]"), *Tag.Key, *Tag.Value);
		}
		if (!Node.bVisible)
		{
			Type += TEXT(" (hidden)");
		}
		return Type;
	}

	const FSlateBrush* SeverityIcon(EFigmaLogSeverity Severity)
	{
		switch (Severity)
		{
		case EFigmaLogSeverity::Error: return FAppStyle::GetBrush("Icons.ErrorWithColor");
		case EFigmaLogSeverity::Warning: return FAppStyle::GetBrush("Icons.WarningWithColor");
		default: return FAppStyle::GetBrush("Icons.InfoWithColor");
		}
	}

	/** Builds a style preset from the fonts, text styles and paint styles a bundle uses, as a starting point to tweak. */
	UFigmaSyncStylePreset* CaptureLook(const FFigmaDocument& Document, FFigmaImportContext& Context)
	{
		const FString Folder = UFigmaSyncSettings::Get().GetDestinationFolder() / TEXT("Presets");
		const FString BaseName = TEXT("FSP_") + FFigmaNaming::SanitizeIdentifier(Document.FileName.IsEmpty() ? Document.FrameName : Document.FileName, TEXT("Look"));
		const FString AssetName = FFigmaAssetUtils::MakeUniqueAssetName(Folder, BaseName);
		UPackage* Package = CreatePackage(*(Folder / AssetName));
		UFigmaSyncStylePreset* Preset = NewObject<UFigmaSyncStylePreset>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);

		TSet<FString> SeenFonts;
		auto Visit = [&](const FFigmaNode& Node)
		{
			if (Node.bHasText)
			{
				const FString FontKey = Node.Text.FontFamily + TEXT("|") + Node.Text.FontStyle;
				if (!SeenFonts.Contains(FontKey))
				{
					SeenFonts.Add(FontKey);
					const FFigmaResolvedFont Resolved = Context.Fonts->Resolve(Node.Text.FontFamily, Node.Text.FontStyle, Node.Text.FontWeight, Node.Text.bItalic);
					FFigmaFontMapping Mapping;
					Mapping.FigmaFamily = Node.Text.FontFamily;
					Mapping.FigmaStyle = Node.Text.FontStyle;
					Mapping.Font = TSoftObjectPtr<UFont>(FSoftObjectPath(Resolved.Font));
					Mapping.Typeface = Resolved.Typeface;
					Preset->FontMappings.Add(Mapping);
				}
				if (!Node.Text.TextStyle.IsEmpty() && !Preset->TextStyles.Contains(Node.Text.TextStyle))
				{
					FFigmaTextStyleOverride Style;
					Style.Font = Context.Fonts->MakeFontInfo(Node.Text);
					Style.bOverrideColor = true;
					Style.Color = Node.Text.Color.ToLinear();
					Preset->TextStyles.Add(Node.Text.TextStyle, Style);
				}
			}
			if (Node.bHasStyle && Node.Style.bSimple && !Node.Style.FillStyle.IsEmpty() && !Preset->PaintStyles.Contains(Node.Style.FillStyle))
			{
				Preset->PaintStyles.Add(Node.Style.FillStyle, FFigmaStyleConverter::MakeShapeBrush(Node));
			}
		};
		Document.ForEachNode(Visit);
		for (const TPair<FString, FFigmaComponentSet>& Pair : Document.Components)
		{
			FFigmaButtonStyleOverride Placeholder;
			Preset->ButtonStyles.Add(Pair.Value.Name, Placeholder);
		}

		FAssetRegistryModule::AssetCreated(Preset);
		Package->MarkPackageDirty();
		UEditorLoadingAndSavingUtils::SavePackages({ Package }, true);
		return Preset;
	}
}

void SFigmaSyncPanel::Construct(const FArguments& InArgs)
{
	const UFigmaSyncSettings& Settings = UFigmaSyncSettings::Get();
	NewAssetFolder = Settings.GetDestinationFolder();
	bSaveAfterImport = Settings.bSaveAfterImport;
	bOpenAfterImport = Settings.bOpenAfterImport;
	Preset = Settings.DefaultStylePreset.LoadSynchronous();

	ChildSlot
	[
		SNew(SVerticalBox)

		// Header
		+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 8.f, 8.f, 4.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(SImage).Image(FSlateIcon(FFigmaSyncStyle::GetStyleSetName(), "FigmaSync.Icon").GetIcon())
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(LOCTEXT("Title", "Figma Sync")).Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Subtitle", "Figma owns the look; Unreal owns the behavior. Drop an exported .zip here."))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton).Text(LOCTEXT("Settings", "Settings")).OnClicked(this, &SFigmaSyncPanel::OnOpenSettings)
			]
		]

		// 1. Source
		+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 4.f)
		[
			SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(8.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[SectionLabel(LOCTEXT("SourceHeader", "1. Figma bundle"))]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 4.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(SEditableTextBox).IsReadOnly(true).Text(this, &SFigmaSyncPanel::GetSourceText)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f)
					[
						SNew(SButton).Text(LOCTEXT("OpenZip", "Open .zip...")).OnClicked(this, &SFigmaSyncPanel::OnBrowseZip)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f)
					[
						SNew(SButton).Text(LOCTEXT("OpenFolder", "Open folder...")).OnClicked(this, &SFigmaSyncPanel::OnBrowseFolder)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f)
					[
						SNew(SButton)
						.Text(LOCTEXT("Sample", "Try sample"))
						.ToolTipText(LOCTEXT("SampleTip", "Load the MainMenu sample bundle that ships with the plugin."))
						.OnClicked(this, &SFigmaSyncPanel::OnLoadSample)
					]
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(this, &SFigmaSyncPanel::GetSourceInfoText).AutoWrapText(true).ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
			]
		]

		// 2. Target
		+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 4.f)
		[
			SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(8.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[SectionLabel(LOCTEXT("TargetHeader", "2. Widget Blueprint"))]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 2.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[FieldLabel(LOCTEXT("UpdateExisting", "Update existing"))]
					+ SHorizontalBox::Slot().FillWidth(1.f)
					[
						SNew(SObjectPropertyEntryBox)
						.AllowedClass(UWidgetBlueprint::StaticClass())
						.ObjectPath(this, &SFigmaSyncPanel::GetTargetPath)
						.OnObjectChanged(this, &SFigmaSyncPanel::OnTargetChanged)
						.AllowClear(true)
						.DisplayThumbnail(false)
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
				[
					SNew(SHorizontalBox)
					.Visibility(this, &SFigmaSyncPanel::GetNewAssetVisibility)
					+ SHorizontalBox::Slot().AutoWidth()[FieldLabel(LOCTEXT("CreateIn", "or create in"))]
					+ SHorizontalBox::Slot().FillWidth(0.6f)
					[
						SNew(SEditableTextBox)
						.Text_Lambda([this]() { return FText::FromString(NewAssetFolder); })
						.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type) { NewAssetFolder = Text.ToString(); })
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f)[SNew(STextBlock).Text(LOCTEXT("Named", "named"))]
					+ SHorizontalBox::Slot().FillWidth(0.4f)
					[
						SNew(SEditableTextBox)
						.Text_Lambda([this]() { return FText::FromString(NewAssetName); })
						.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type) { NewAssetName = Text.ToString(); })
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
				[
					SNew(SHorizontalBox)
					.Visibility(this, &SFigmaSyncPanel::GetNewAssetVisibility)
					+ SHorizontalBox::Slot().AutoWidth()[FieldLabel(LOCTEXT("ParentClass", "Parent class"))]
					+ SHorizontalBox::Slot().FillWidth(1.f)
					[
						SNew(SClassPropertyEntryBox)
						.MetaClass(UUserWidget::StaticClass())
						.AllowAbstract(true)
						.AllowNone(true)
						.SelectedClass(this, &SFigmaSyncPanel::GetParentClass)
						.OnSetClass(this, &SFigmaSyncPanel::OnParentClassChanged)
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[FieldLabel(LOCTEXT("StylePreset", "Style preset"))]
					+ SHorizontalBox::Slot().FillWidth(1.f)
					[
						SNew(SObjectPropertyEntryBox)
						.AllowedClass(UFigmaSyncStylePreset::StaticClass())
						.ObjectPath(this, &SFigmaSyncPanel::GetPresetPath)
						.OnObjectChanged(this, &SFigmaSyncPanel::OnPresetChanged)
						.AllowClear(true)
						.DisplayThumbnail(false)
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 16.f, 0.f)
					[
						SNew(SCheckBox)
						.IsChecked_Lambda([this]() { return bSaveAfterImport ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
						.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bSaveAfterImport = State == ECheckBoxState::Checked; })
						[SNew(STextBlock).Text(LOCTEXT("SaveAfter", "Save assets"))]
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SCheckBox)
						.IsChecked_Lambda([this]() { return bOpenAfterImport ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
						.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bOpenAfterImport = State == ECheckBoxState::Checked; })
						[SNew(STextBlock).Text(LOCTEXT("OpenAfter", "Open when done"))]
					]
				]
			]
		]

		// 3. Actions
		+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 6.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(&FAppStyle::Get(), "PrimaryButton")
				.Text(this, &SFigmaSyncPanel::GetImportButtonText)
				.IsEnabled(this, &SFigmaSyncPanel::HasBundle)
				.OnClicked(this, &SFigmaSyncPanel::OnImport)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.Text(LOCTEXT("Resync", "Re-sync from last bundle"))
				.ToolTipText(LOCTEXT("ResyncTip", "Re-import the target from the bundle it was last synced from."))
				.IsEnabled(this, &SFigmaSyncPanel::HasResyncTarget)
				.OnClicked(this, &SFigmaSyncPanel::OnResync)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.Text(LOCTEXT("OpenBlueprint", "Open Blueprint"))
				.IsEnabled_Lambda([this]() { return Target.IsValid(); })
				.OnClicked(this, &SFigmaSyncPanel::OnOpenBlueprint)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.Text(LOCTEXT("SavePreset", "Save look as preset"))
				.ToolTipText(LOCTEXT("SavePresetTip", "Create a style preset from this bundle's fonts, text styles and paint styles, then tweak it once for every screen."))
				.IsEnabled(this, &SFigmaSyncPanel::HasBundle)
				.OnClicked(this, &SFigmaSyncPanel::OnSaveLookAsPreset)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton).Text(LOCTEXT("Logs", "Logs folder")).OnClicked(this, &SFigmaSyncPanel::OnOpenLogs)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(10.f, 0.f, 10.f, 4.f)
		[
			SNew(STextBlock).Text_Lambda([this]() { return Summary; }).AutoWrapText(true)
		]

		// Preview + log
		+ SVerticalBox::Slot().FillHeight(1.f).Padding(8.f, 4.f, 8.f, 8.f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)
			+ SSplitter::Slot().Value(0.55f)
			[
				SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(6.f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)[SectionLabel(LOCTEXT("PreviewHeader", "Layers -> widgets"))]
					+ SVerticalBox::Slot().FillHeight(1.f)
					[
						SAssignNew(NodeTree, STreeView<TSharedPtr<FFigmaNode>>)
						.TreeItemsSource(&RootNodes)
						.OnGenerateRow(this, &SFigmaSyncPanel::OnGenerateNodeRow)
						.OnGetChildren(this, &SFigmaSyncPanel::OnGetNodeChildren)
						.SelectionMode(ESelectionMode::Single)
					]
				]
			]
			+ SSplitter::Slot().Value(0.45f)
			[
				SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(6.f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)[SectionLabel(LOCTEXT("LogHeader", "Import log & lint report"))]
					+ SVerticalBox::Slot().FillHeight(1.f)
					[
						SAssignNew(LogList, SListView<TSharedPtr<FFigmaLogEntry>>)
						.ListItemsSource(&LogEntries)
						.OnGenerateRow(this, &SFigmaSyncPanel::OnGenerateLogRow)
						.SelectionMode(ESelectionMode::None)
					]
				]
			]
		]
	];

	// Copy: LoadBundle reorders the recent list.
	const TArray<FString> Recent = UFigmaSyncUserState::Get().RecentSources;
	if (Recent.Num() > 0 && FFigmaBundle::IsBundlePath(Recent[0]))
	{
		LoadBundle(Recent[0]);
	}
}

SFigmaSyncPanel::~SFigmaSyncPanel()
{
	// The preview context references the bundle; release it first.
	PreviewContext.Reset();
	Bundle.Reset();
}

// ---------------------------------------------------------------------------------------------------------------- Source

void SFigmaSyncPanel::LoadBundle(const FString& Path)
{
	FString Error;
	TSharedPtr<FFigmaBundle> NewBundle = FFigmaBundle::Open(Path, Error);
	if (!NewBundle.IsValid())
	{
		LoadError = Error;
		SetBundle(nullptr, Path);
		Notify(FText::FromString(Error), false);
		return;
	}
	LoadError.Reset();
	SetBundle(NewBundle, Path);
	UFigmaSyncUserState::Get().AddRecentSource(Path);

	const FFigmaDocument& Document = NewBundle->GetDocument();
	NewAssetName = FFigmaImporter::GetDefaultAssetName(Document);
	if (!Target.IsValid())
	{
		if (const FSoftObjectPath* Bound = UFigmaSyncUserState::Get().FrameBindings.Find(Document.FrameId))
		{
			Target = Cast<UWidgetBlueprint>(Bound->TryLoad());
		}
	}
}

void SFigmaSyncPanel::SetBundle(const TSharedPtr<FFigmaBundle>& NewBundle, const FString& Path)
{
	PreviewContext.Reset();
	Bundle = NewBundle;
	SourcePath = Path;
	RootNodes.Reset();
	Summary = FText::GetEmpty();

	if (Bundle.IsValid())
	{
		const FFigmaDocument& Document = Bundle->GetDocument();
		RootNodes.Add(Document.Root);
		PreviewContext = MakeShared<FFigmaImportContext>(*Bundle, FFigmaImportOptions(), MakeShared<FFigmaImportLog>());
		PreviewContext->SetPreset(Preset.Get());

		TSharedRef<FFigmaImportLog> ExportLog = MakeShared<FFigmaImportLog>();
		for (const FFigmaWarning& Warning : Document.Warnings)
		{
			ExportLog->AddForPath(EFigmaLogSeverity::Info, EFigmaLogCategory::Export, Warning.Message, Warning.NodeId, Warning.NodePath);
		}
		SetLogEntries(ExportLog->GetEntries());
	}
	else
	{
		SetLogEntries({});
	}

	if (NodeTree.IsValid())
	{
		NodeTree->RequestTreeRefresh();
		for (const TSharedPtr<FFigmaNode>& Root : RootNodes)
		{
			NodeTree->SetItemExpansion(Root, true);
		}
	}
}

FReply SFigmaSyncPanel::OnBrowseZip()
{
	IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
	if (!Desktop)
	{
		return FReply::Handled();
	}
	TArray<FString> Files;
	const FString DefaultDir = SourcePath.IsEmpty() ? FPaths::ProjectDir() : FPaths::GetPath(SourcePath);
	const bool bPicked = Desktop->OpenFileDialog(FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared()),
		LOCTEXT("OpenBundleTitle", "Open Figma bundle").ToString(), DefaultDir, TEXT(""),
		TEXT("Figma bundle (*.zip;*.json)|*.zip;*.json"), EFileDialogFlags::None, Files);
	if (bPicked && Files.Num() > 0)
	{
		LoadBundle(Files[0]);
	}
	return FReply::Handled();
}

FReply SFigmaSyncPanel::OnBrowseFolder()
{
	IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
	if (!Desktop)
	{
		return FReply::Handled();
	}
	FString Folder;
	const FString DefaultDir = SourcePath.IsEmpty() ? FPaths::ProjectDir() : FPaths::GetPath(SourcePath);
	if (Desktop->OpenDirectoryDialog(FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared()),
		LOCTEXT("OpenFolderTitle", "Open extracted Figma bundle folder").ToString(), DefaultDir, Folder))
	{
		LoadBundle(Folder);
	}
	return FReply::Handled();
}

FReply SFigmaSyncPanel::OnLoadSample()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Widgetify"));
	const FString SamplePath = Plugin.IsValid() ? Plugin->GetBaseDir() / TEXT("Resources/Samples/MainMenu.figmasync.zip") : FString();
	if (SamplePath.IsEmpty() || !FPaths::FileExists(SamplePath))
	{
		Notify(LOCTEXT("NoSample", "The sample bundle is missing from the plugin's Resources/Samples folder."), false);
		return FReply::Handled();
	}
	LoadBundle(SamplePath);
	return FReply::Handled();
}

FText SFigmaSyncPanel::GetSourceText() const
{
	return SourcePath.IsEmpty() ? LOCTEXT("NoSource", "No bundle loaded: export one from the Widgetify Figma plugin") : FText::FromString(SourcePath);
}

FText SFigmaSyncPanel::GetSourceInfoText() const
{
	if (!LoadError.IsEmpty())
	{
		return FText::FromString(LoadError);
	}
	if (!Bundle.IsValid())
	{
		return FText::GetEmpty();
	}
	const FFigmaDocument& Document = Bundle->GetDocument();
	const FFigmaNode& Root = *Document.Root;
	return FText::Format(LOCTEXT("SourceInfo", "{0}  ·  {1}×{2}  ·  {3} layers  ·  {4} images  ·  from \"{5} / {6}\"  ·  exported {7}"),
		FText::FromString(Document.FrameName), FText::AsNumber(FMath::RoundToInt(Root.Width)), FText::AsNumber(FMath::RoundToInt(Root.Height)),
		FText::AsNumber(Document.CountNodes()), FText::AsNumber(Document.Assets.Num()), FText::FromString(Document.FileName),
		FText::FromString(Document.PageName), FText::FromString(Document.ExportedAt.Left(16).Replace(TEXT("T"), TEXT(" "))));
}

FReply SFigmaSyncPanel::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	const TSharedPtr<FExternalDragOperation> Operation = DragDropEvent.GetOperationAs<FExternalDragOperation>();
	return Operation.IsValid() && Operation->HasFiles() ? FReply::Handled() : FReply::Unhandled();
}

FReply SFigmaSyncPanel::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	const TSharedPtr<FExternalDragOperation> Operation = DragDropEvent.GetOperationAs<FExternalDragOperation>();
	if (Operation.IsValid() && Operation->HasFiles() && Operation->GetFiles().Num() > 0)
	{
		LoadBundle(Operation->GetFiles()[0]);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

// ---------------------------------------------------------------------------------------------------------------- Target

void SFigmaSyncPanel::SetTarget(UWidgetBlueprint* Blueprint)
{
	Target = Blueprint;
	const UFigmaSyncBlueprintExtension* State = UFigmaSyncBlueprintExtension::Find(Blueprint);
	if (State)
	{
		if (UFigmaSyncStylePreset* StatePreset = State->StylePreset.LoadSynchronous())
		{
			Preset = StatePreset;
		}
		if (!State->SourcePath.IsEmpty() && FFigmaBundle::IsBundlePath(State->SourcePath) && SourcePath != State->SourcePath)
		{
			LoadBundle(State->SourcePath);
		}
	}
}

FString SFigmaSyncPanel::GetTargetPath() const
{
	return Target.IsValid() ? Target->GetPathName() : FString();
}

void SFigmaSyncPanel::OnTargetChanged(const FAssetData& Asset)
{
	SetTarget(Cast<UWidgetBlueprint>(Asset.GetAsset()));
}

const UClass* SFigmaSyncPanel::GetParentClass() const
{
	return ParentClass.Get();
}

void SFigmaSyncPanel::OnParentClassChanged(const UClass* NewClass)
{
	ParentClass = const_cast<UClass*>(NewClass);
}

FString SFigmaSyncPanel::GetPresetPath() const
{
	return Preset.IsValid() ? Preset->GetPathName() : FString();
}

void SFigmaSyncPanel::OnPresetChanged(const FAssetData& Asset)
{
	Preset = Cast<UFigmaSyncStylePreset>(Asset.GetAsset());
	if (PreviewContext.IsValid())
	{
		PreviewContext->SetPreset(Preset.Get());
	}
}

EVisibility SFigmaSyncPanel::GetNewAssetVisibility() const
{
	return Target.IsValid() ? EVisibility::Collapsed : EVisibility::Visible;
}

// ---------------------------------------------------------------------------------------------------------------- Actions

bool SFigmaSyncPanel::HasBundle() const
{
	return Bundle.IsValid();
}

bool SFigmaSyncPanel::HasResyncTarget() const
{
	const UFigmaSyncBlueprintExtension* State = UFigmaSyncBlueprintExtension::Find(Target.Get());
	return State && !State->SourcePath.IsEmpty();
}

FText SFigmaSyncPanel::GetImportButtonText() const
{
	return Target.IsValid() ? FText::Format(LOCTEXT("UpdateTarget", "Update {0}"), FText::FromString(Target->GetName())) : LOCTEXT("CreateTarget", "Create Widget Blueprint");
}

FReply SFigmaSyncPanel::OnImport()
{
	if (!Bundle.IsValid())
	{
		return FReply::Handled();
	}
	FFigmaImportOptions Options;
	Options.Target = Target;
	Options.PackagePath = NewAssetFolder;
	Options.AssetName = NewAssetName;
	Options.ParentClass = ParentClass;
	Options.StylePreset = Preset;
	Options.bSave = bSaveAfterImport;
	Options.bOpenEditor = bOpenAfterImport;
	Options.SourcePath = SourcePath;

	FScopedSlowTask Progress(1.f, LOCTEXT("Importing", "Importing from Figma..."));
	Progress.MakeDialog();
	Progress.EnterProgressFrame(1.f);
	const FFigmaImportResult Result = FFigmaImporter::Import(*Bundle, Options);
	if (Result.Blueprint.IsValid())
	{
		Target = Result.Blueprint;
	}
	ShowResult(Result);
	Notify(Result.bSuccess ? FText::Format(LOCTEXT("ImportDone", "Figma Sync: {0}"), Result.GetSummary()) : LOCTEXT("ImportFailed", "Figma Sync failed; see the log."), Result.bSuccess);
	return FReply::Handled();
}

FReply SFigmaSyncPanel::OnResync()
{
	UWidgetBlueprint* Blueprint = Target.Get();
	if (!Blueprint)
	{
		return FReply::Handled();
	}
	FScopedSlowTask Progress(1.f, LOCTEXT("Resyncing", "Re-syncing from Figma..."));
	Progress.MakeDialog();
	Progress.EnterProgressFrame(1.f);
	const FFigmaImportResult Result = FFigmaImporter::Resync(Blueprint);
	ShowResult(Result);
	Notify(Result.bSuccess ? FText::Format(LOCTEXT("ResyncDone", "Re-synced: {0}"), Result.GetSummary()) : LOCTEXT("ResyncFailed", "Re-sync failed; see the log."), Result.bSuccess);
	if (const UFigmaSyncBlueprintExtension* State = UFigmaSyncBlueprintExtension::Find(Blueprint))
	{
		if (FFigmaBundle::IsBundlePath(State->SourcePath))
		{
			FString Error;
			if (TSharedPtr<FFigmaBundle> Reloaded = FFigmaBundle::Open(State->SourcePath, Error))
			{
				SetBundle(Reloaded, State->SourcePath);
				ShowResult(Result);
			}
		}
	}
	return FReply::Handled();
}

FReply SFigmaSyncPanel::OnOpenBlueprint()
{
	if (Target.IsValid() && GEditor)
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Target.Get());
	}
	return FReply::Handled();
}

FReply SFigmaSyncPanel::OnSaveLookAsPreset()
{
	if (!Bundle.IsValid() || !PreviewContext.IsValid())
	{
		return FReply::Handled();
	}
	UFigmaSyncStylePreset* NewPreset = CaptureLook(Bundle->GetDocument(), *PreviewContext);
	if (NewPreset)
	{
		Preset = NewPreset;
		PreviewContext->SetPreset(NewPreset);
		if (GEditor)
		{
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(NewPreset);
		}
		Notify(FText::Format(LOCTEXT("PresetSaved", "Saved {0}. Tweak fonts, text styles and brushes there; every import with this preset uses them."), FText::FromString(NewPreset->GetName())), true);
	}
	return FReply::Handled();
}

FReply SFigmaSyncPanel::OnOpenLogs()
{
	const FString Directory = FFigmaImporter::GetLogDirectory();
	IFileManager::Get().MakeDirectory(*Directory, true);
	FPlatformProcess::ExploreFolder(*FPaths::ConvertRelativePathToFull(Directory));
	return FReply::Handled();
}

FReply SFigmaSyncPanel::OnOpenSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>(TEXT("Settings")))
	{
		SettingsModule->ShowViewer(TEXT("Project"), TEXT("Plugins"), TEXT("FigmaSyncSettings"));
	}
	return FReply::Handled();
}

void SFigmaSyncPanel::ShowResult(const FFigmaImportResult& Result)
{
	SetLogEntries(Result.Log->GetEntries());
	Summary = Result.GetSummary();
	if (Result.Blueprint.IsValid())
	{
		Target = Result.Blueprint;
	}
}

void SFigmaSyncPanel::Notify(const FText& Message, bool bSuccess) const
{
	FNotificationInfo Info(Message);
	Info.ExpireDuration = bSuccess ? 5.f : 8.f;
	Info.bUseSuccessFailIcons = true;
	if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
	}
}

// ---------------------------------------------------------------------------------------------------------------- Lists

TSharedRef<ITableRow> SFigmaSyncPanel::OnGenerateNodeRow(TSharedPtr<FFigmaNode> Node, const TSharedRef<STableViewBase>& OwnerTable)
{
	const FString Mapping = PreviewContext.IsValid() && Node.IsValid() ? FFigmaWidgetBuilder::Describe(*Node, *PreviewContext) : FString();
	return SNew(STableRow<TSharedPtr<FFigmaNode>>, OwnerTable)
		.ToolTipText(FText::FromString(Node.IsValid() ? Node->GetPath() : FString()))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(2.f, 1.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Node.IsValid() ? Node->CleanName : FString()))
				.ColorAndOpacity(Node.IsValid() && Node->bVisible ? FSlateColor::UseForeground() : FSlateColor::UseSubduedForeground())
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(8.f, 1.f)
			[
				SNew(STextBlock).Text(FText::FromString(Node.IsValid() ? TypeLabel(*Node) : FString())).ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Right).Padding(8.f, 1.f)
			[
				SNew(STextBlock).Text(FText::FromString(Mapping))
			]
		];
}

void SFigmaSyncPanel::OnGetNodeChildren(TSharedPtr<FFigmaNode> Node, TArray<TSharedPtr<FFigmaNode>>& OutChildren)
{
	if (Node.IsValid())
	{
		OutChildren.Append(Node->Children);
	}
}

TSharedRef<ITableRow> SFigmaSyncPanel::OnGenerateLogRow(TSharedPtr<FFigmaLogEntry> Entry, const TSharedRef<STableViewBase>& OwnerTable)
{
	const FString Text = Entry.IsValid() ? (Entry->NodePath.IsEmpty() ? Entry->Message : FString::Printf(TEXT("%s: %s"), *Entry->NodePath, *Entry->Message)) : FString();
	const FString Category = !Entry.IsValid() ? FString() : Entry->Category == EFigmaLogCategory::Lint ? FString(TEXT("Lint")) : Entry->Category == EFigmaLogCategory::Export ? FString(TEXT("Figma")) : FString(TEXT("Import"));
	return SNew(STableRow<TSharedPtr<FFigmaLogEntry>>, OwnerTable)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(2.f, 2.f)
			[
				SNew(SImage).Image(SeverityIcon(Entry.IsValid() ? Entry->Severity : EFigmaLogSeverity::Info))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(4.f, 2.f)
			[
				SNew(SBox).MinDesiredWidth(44.f)[SNew(STextBlock).Text(FText::FromString(Category)).ColorAndOpacity(FSlateColor::UseSubduedForeground())]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(4.f, 2.f)
			[
				SNew(STextBlock).Text(FText::FromString(Text)).AutoWrapText(true)
			]
		];
}

void SFigmaSyncPanel::SetLogEntries(const TArray<TSharedPtr<FFigmaLogEntry>>& Entries)
{
	// Errors first, then warnings, then the rest, keeping order within each group.
	LogEntries.Reset();
	for (const EFigmaLogSeverity Severity : { EFigmaLogSeverity::Error, EFigmaLogSeverity::Warning, EFigmaLogSeverity::Info })
	{
		for (const TSharedPtr<FFigmaLogEntry>& Entry : Entries)
		{
			if (Entry.IsValid() && Entry->Severity == Severity)
			{
				LogEntries.Add(Entry);
			}
		}
	}
	if (LogList.IsValid())
	{
		LogList->RequestListRefresh();
	}
}

#undef LOCTEXT_NAMESPACE
