#include "FigmaImporter.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Editor.h"
#include "FigmaAssetUtils.h"
#include "FigmaBundle.h"
#include "FigmaLinter.h"
#include "FigmaNaming.h"
#include "FigmaSyncBlueprintExtension.h"
#include "FigmaSyncSettings.h"
#include "FigmaSyncStylePreset.h"
#include "FigmaTextureImporter.h"
#include "FigmaWidgetBuilder.h"
#include "FigmaWidgetMapper.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"

#define LOCTEXT_NAMESPACE "FigmaSync"

namespace
{
	void ForEachRaster(const FFigmaNode& Node, TFunctionRef<void(const FFigmaNode&, const FFigmaRasterRef&)> Visitor)
	{
		if (Node.Image.IsSet())
		{
			Visitor(Node, Node.Image);
		}
		if (Node.Background.IsSet())
		{
			Visitor(Node, Node.Background);
		}
		for (const TSharedPtr<FFigmaNode>& Child : Node.Children)
		{
			if (Child.IsValid())
			{
				ForEachRaster(*Child, Visitor);
			}
		}
	}

	/** Textures are imported before the undo transaction opens: asset creation doesn't belong in widget undo history. */
	void PreImportTextures(FFigmaImportContext& Context)
	{
		auto Import = [&Context](const FFigmaNode& Node, const FFigmaRasterRef& Ref)
		{
			if (const FFigmaAsset* Asset = Context.Document.FindAsset(Ref.AssetId))
			{
				Context.Textures->GetOrImport(*Asset, FFigmaTextureImporter::GetPresetForNode(Node, Context.Preset));
			}
		};
		if (!Context.Document.Root.IsValid())
		{
			return;
		}
		ForEachRaster(*Context.Document.Root, Import);

		// Plain UMG buttons draw their hover/pressed/disabled brushes from the component's variants.
		TSet<FString> VisitedSets;
		Context.Document.ForEachNode([&](const FFigmaNode& Node)
		{
			if (Node.Component.ComponentSetId.IsEmpty() || VisitedSets.Contains(Node.Component.ComponentSetId))
			{
				return;
			}
			if (FFigmaWidgetBuilder::Classify(Node, Context) != EFigmaWidgetKind::Button)
			{
				return;
			}
			VisitedSets.Add(Node.Component.ComponentSetId);
			if (const FFigmaComponentSet* Set = Context.Document.FindComponentSet(Node.Component.ComponentSetId))
			{
				for (const FFigmaVariant& Variant : Set->Variants)
				{
					if (Variant.Node.IsValid())
					{
						ForEachRaster(*Variant.Node, Import);
					}
				}
			}
		});
	}

	void FinalizeClassDefaults(FFigmaImportContext& Context, UWidgetBlueprint* Blueprint)
	{
		UUserWidget* Defaults = Blueprint->GeneratedClass ? Cast<UUserWidget>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
		if (!Defaults || !Context.Document.Root.IsValid())
		{
			return;
		}
		Defaults->Modify();

		const FFigmaNode& Root = *Context.Document.Root;
		const FIntPoint Design = Context.Settings.DesignResolution;
		const bool bFullScreen = Root.Width >= Design.X * 0.9f && Root.Height >= Design.Y * 0.9f;
		Defaults->DesignSizeMode = bFullScreen ? EDesignPreviewSizeMode::FillScreen : EDesignPreviewSizeMode::Desired;
		Defaults->DesignTimeSize = FVector2D(Root.Width, Root.Height);

		if (!Context.FocusWidgetName.IsNone())
		{
			Defaults->SetDesiredFocusWidget(Context.FocusWidgetName);
		}
		Blueprint->MarkPackageDirty();
	}
}

FString FFigmaImporter::GetDefaultAssetName(const FFigmaDocument& Document)
{
	const UFigmaSyncSettings& Settings = UFigmaSyncSettings::Get();
	const FString Base = FFigmaNaming::SanitizeIdentifier(Document.FrameName, TEXT("Screen"));
	return Base.StartsWith(Settings.WidgetBlueprintPrefix) ? Base : Settings.WidgetBlueprintPrefix + Base;
}

FString FFigmaImporter::GetLogDirectory()
{
	return FPaths::ProjectSavedDir() / TEXT("FigmaSync") / TEXT("Logs");
}

FFigmaImportResult FFigmaImporter::Import(const FFigmaBundle& Bundle, const FFigmaImportOptions& Options)
{
	FFigmaImportResult Result;
	const FFigmaDocument& Document = Bundle.GetDocument();
	if (!Document.Root.IsValid())
	{
		Result.Log->Error(TEXT("The bundle has no root frame."));
		return Result;
	}

	FFigmaImportContext Context(Bundle, Options, Result.Log);
	const UFigmaSyncSettings& Settings = Context.Settings;
	for (const FFigmaWarning& Warning : Document.Warnings)
	{
		Context.Log->AddForPath(EFigmaLogSeverity::Info, EFigmaLogCategory::Export, Warning.Message, Warning.NodeId, Warning.NodePath);
	}

	// 1. Target blueprint.
	UWidgetBlueprint* Blueprint = Options.Target.Get();
	FString PackagePath = FFigmaAssetUtils::SanitizePackagePath(Options.PackagePath.IsEmpty() ? Settings.GetDestinationFolder() : Options.PackagePath);
	FString AssetName = Options.AssetName.IsEmpty() ? GetDefaultAssetName(Document) : FFigmaNaming::SanitizeIdentifier(Options.AssetName, TEXT("WBP_Screen"));
	if (Blueprint)
	{
		PackagePath = FPackageName::GetLongPackagePath(Blueprint->GetPackage()->GetName());
		AssetName = Blueprint->GetName();
	}
	else
	{
		if (PackagePath.IsEmpty())
		{
			Context.Log->Error(FString::Printf(TEXT("'%s' is not a valid content folder (it must start with /Game/)."), *Options.PackagePath));
			return Result;
		}
		Blueprint = Cast<UWidgetBlueprint>(FFigmaAssetUtils::FindAsset(PackagePath, AssetName, UWidgetBlueprint::StaticClass()));
		if (!Blueprint && FFigmaAssetUtils::DoesAssetExist(PackagePath, AssetName))
		{
			Context.Log->Error(FString::Printf(TEXT("%s/%s already exists and is not a Widget Blueprint. Pick another name."), *PackagePath, *AssetName));
			return Result;
		}
	}

	UFigmaSyncBlueprintExtension* ExistingState = Blueprint ? UFigmaSyncBlueprintExtension::Find(Blueprint) : nullptr;
	if (ExistingState && !ExistingState->FrameId.IsEmpty() && ExistingState->FrameId != Document.FrameId)
	{
		Context.Log->Warning(FString::Printf(TEXT("%s was synced from Figma frame '%s'; this bundle is '%s'. Widgets are matched by layer id, so most will be recreated."),
			*AssetName, *ExistingState->FrameName, *Document.FrameName));
	}

	// 2. Look.
	UFigmaSyncStylePreset* Preset = Options.StylePreset.Get();
	if (!Preset && ExistingState)
	{
		Preset = ExistingState->StylePreset.LoadSynchronous();
	}
	if (!Preset)
	{
		Preset = Settings.DefaultStylePreset.LoadSynchronous();
	}
	Context.SetPreset(Preset);

	const FString ScreenFolder = FFigmaNaming::SanitizeIdentifier(Document.FrameName, TEXT("Screen"));
	Context.TextureFolder = PackagePath / Settings.TextureFolderName / ScreenFolder;
	Context.SharedFolder = Settings.GetDestinationFolder() / Settings.SharedFolderName;

	// 3. Shared assets (CommonUI buttons and styles) come first: the screen references them.
	for (const TSharedRef<IFigmaWidgetMapper>& Mapper : Context.Mappers)
	{
		Mapper->PrepareImport(Context);
	}

	// 4. Create the blueprint if needed.
	if (!Blueprint)
	{
		UClass* ParentClass = Options.ParentClass.Get();
		if (!ParentClass && Preset && Preset->ScreenParentClass.IsValid())
		{
			ParentClass = Preset->ScreenParentClass.TryLoadClass<UUserWidget>();
		}
		for (int32 Index = 0; !ParentClass && Index < Context.Mappers.Num(); ++Index)
		{
			ParentClass = Context.Mappers[Index]->ChooseScreenParentClass(Context);
		}
		FString Error;
		Blueprint = FFigmaAssetUtils::CreateWidgetBlueprint(PackagePath, AssetName, ParentClass ? ParentClass : UUserWidget::StaticClass(), Error);
		if (!Blueprint)
		{
			Context.Log->Error(Error);
			return Result;
		}
		Result.bCreatedBlueprint = true;
		Context.Log->Info(FString::Printf(TEXT("Created %s (parent class %s)."), *Blueprint->GetPathName(), *Blueprint->ParentClass->GetName()));
	}
	Context.Blueprint = Blueprint;
	Result.Blueprint = Blueprint;

	// 5. Textures, outside the undo transaction.
	PreImportTextures(Context);

	// 6. Widget tree, inside one undoable transaction.
	UFigmaSyncBlueprintExtension* State = UFigmaSyncBlueprintExtension::FindOrAdd(Blueprint);
	bool bBuilt = false;
	{
		FScopedTransaction Transaction(LOCTEXT("FigmaSyncTransaction", "Figma Sync"));
		if (State)
		{
			State->Modify();
		}
		FFigmaWidgetBuilder Builder(Context, Blueprint, State);
		bBuilt = Builder.Build(*Document.Root);
		if (State)
		{
			State->SourcePath = Options.SourcePath.IsEmpty() ? Bundle.GetSourcePath() : Options.SourcePath;
			State->FigmaFileName = Document.FileName;
			State->FrameId = Document.FrameId;
			State->FrameName = Document.FrameName;
			State->LastSyncTime = FDateTime::Now();
			if (Preset)
			{
				State->StylePreset = Preset;
			}
		}
	}
	if (!bBuilt)
	{
		Context.Log->Error(TEXT("The widget tree could not be built. Nothing was saved."));
		Result.Stats = Context.Stats;
		Result.Log->FlushToMessageLog(LOCTEXT("ImportFailedPage", "Figma Sync (failed)"), Options.bShowMessageLog);
		return Result;
	}

	// 7. Compile, then class defaults (they live on the generated class).
	FFigmaAssetUtils::CompileBlueprint(Blueprint, &Context.Log.Get());
	FinalizeClassDefaults(Context, Blueprint);
	for (const TSharedRef<IFigmaWidgetMapper>& Mapper : Context.Mappers)
	{
		Mapper->FinalizeBlueprint(Blueprint, Context);
	}

	// 8. Lint.
	FFigmaLinter::Run(Context, Blueprint);

	// 9. Remember, save, report.
	UFigmaSyncUserState::Get().BindFrame(Document.FrameId, FSoftObjectPath(Blueprint));
	UFigmaSyncUserState::Get().AddRecentSource(State ? State->SourcePath : Bundle.GetSourcePath());
	Context.TouchPackage(Blueprint->GetPackage());
	if (Options.bSave && !FFigmaAssetUtils::SavePackages(Context.TouchedPackages))
	{
		Context.Log->Warning(TEXT("Some assets could not be saved; save them manually."));
	}

	Result.Stats = Context.Stats;
	Result.bSuccess = !Result.Log->HasErrors();

	const FString LogPath = GetLogDirectory() / (Blueprint->GetName() + TEXT(".log"));
	IFileManager::Get().MakeDirectory(*GetLogDirectory(), true);
	Result.Log->SaveToFile(LogPath, FString::Printf(TEXT("Widgetify import of '%s' (%s) into %s at %s\n%s"),
		*Document.FrameName, *Bundle.GetSourcePath(), *Blueprint->GetPathName(), *FDateTime::Now().ToString(), *Result.GetSummary().ToString()));
	Result.Log->FlushToMessageLog(FText::Format(LOCTEXT("ImportPage", "Figma Sync: {0}"), FText::FromString(Blueprint->GetName())), false);

	if (Options.bOpenEditor && GEditor)
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Blueprint);
	}
	return Result;
}

FFigmaImportResult FFigmaImporter::Resync(UWidgetBlueprint* Blueprint)
{
	FFigmaImportResult Result;
	UFigmaSyncBlueprintExtension* State = UFigmaSyncBlueprintExtension::Find(Blueprint);
	if (!Blueprint || !State || State->SourcePath.IsEmpty())
	{
		Result.Log->Error(TEXT("This Widget Blueprint has no Figma source yet. Import it once from the Figma Sync panel."));
		return Result;
	}

	FString Error;
	TSharedPtr<FFigmaBundle> Bundle = FFigmaBundle::Open(State->SourcePath, Error);
	if (!Bundle.IsValid())
	{
		Result.Log->Error(FString::Printf(TEXT("Could not open the last Figma bundle (%s): %s"), *State->SourcePath, *Error));
		return Result;
	}

	FFigmaImportOptions Options;
	Options.Target = Blueprint;
	Options.SourcePath = State->SourcePath;
	Options.StylePreset = State->StylePreset.LoadSynchronous();
	Options.bSave = UFigmaSyncSettings::Get().bSaveAfterImport;
	return Import(*Bundle, Options);
}

#undef LOCTEXT_NAMESPACE
