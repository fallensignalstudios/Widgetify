#include "FigmaSyncEditorModule.h"
#include "ContentBrowserMenuContexts.h"
#include "CoreGlobals.h"
#include "FigmaBundle.h"
#include "FigmaImporter.h"
#include "FigmaLiveServer.h"
#include "FigmaSyncBlueprintExtension.h"
#include "FigmaSyncCoreModule.h"
#include "FigmaSyncSettings.h"
#include "FigmaSyncStyle.h"
#include "FigmaSyncStylePreset.h"
#include "FigmaWidgetMapper.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Logging/MessageLog.h"
#include "MessageLogInitializationOptions.h"
#include "MessageLogModule.h"
#include "Misc/ScopedSlowTask.h"
#include "Styling/AppStyle.h"
#include "Textures/SlateIcon.h"
#include "ToolMenus.h"
#include "Toolkits/AssetEditorToolkitMenuContext.h"
#include "UI/SFigmaSyncPanel.h"
#include "WidgetBlueprint.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "FigmaSync"

namespace
{
	const FName PanelTabName(TEXT("FigmaSyncPanel"));
	const FName MessageLogName(TEXT("FigmaSync"));

	FSlateIcon PluginIcon()
	{
		return FSlateIcon(FFigmaSyncStyle::GetStyleSetName(), "FigmaSync.Icon", "FigmaSync.Icon.Small");
	}

	void ShowNotification(const FText& Message, bool bSuccess)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = bSuccess ? 5.f : 8.f;
		Info.bUseSuccessFailIcons = true;
		Info.Hyperlink = FSimpleDelegate::CreateLambda([]() { FMessageLog(MessageLogName).Open(EMessageSeverity::Info, true); });
		Info.HyperlinkText = LOCTEXT("ShowLog", "Show log");
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}

	UWidgetBlueprint* GetEditedWidgetBlueprint(FToolMenuSection& Section)
	{
		if (const UAssetEditorToolkitMenuContext* Context = Section.FindContext<UAssetEditorToolkitMenuContext>())
		{
			for (UObject* Object : Context->GetEditingObjects())
			{
				if (UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(Object))
				{
					return Blueprint;
				}
			}
		}
		return nullptr;
	}
}

class FFigmaSyncEditorModule : public IFigmaSyncEditorModule
{
public:
	virtual void StartupModule() override
	{
		FFigmaSyncStyle::Initialize();

		FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>(TEXT("MessageLog"));
		FMessageLogInitializationOptions LogOptions;
		LogOptions.bShowPages = true;
		LogOptions.bAllowClear = true;
		LogOptions.bShowFilters = true;
		MessageLogModule.RegisterLogListing(MessageLogName, LOCTEXT("MessageLogLabel", "Figma Sync"), LogOptions);

		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(PanelTabName, FOnSpawnTab::CreateRaw(this, &FFigmaSyncEditorModule::SpawnPanel))
			.SetDisplayName(LOCTEXT("PanelTitle", "Figma Sync"))
			.SetTooltipText(LOCTEXT("PanelTooltip", "Import Figma frames as Widget Blueprints and keep them in sync."))
			.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
			.SetIcon(PluginIcon());

		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FFigmaSyncEditorModule::RegisterMenus));

		SettingsChangedHandle = GetMutableDefault<UFigmaSyncSettings>()->OnSettingChanged().AddRaw(this, &FFigmaSyncEditorModule::OnSettingsChanged);
		UpdateLiveServer();
	}

	virtual void ShutdownModule() override
	{
		if (LiveServer.IsValid())
		{
			LiveServer->Stop();
			LiveServer.Reset();
		}
		if (UObjectInitialized() && SettingsChangedHandle.IsValid())
		{
			GetMutableDefault<UFigmaSyncSettings>()->OnSettingChanged().Remove(SettingsChangedHandle);
		}
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
		if (FSlateApplication::IsInitialized())
		{
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(PanelTabName);
		}
		if (FModuleManager::Get().IsModuleLoaded(TEXT("MessageLog")))
		{
			FModuleManager::GetModuleChecked<FMessageLogModule>(TEXT("MessageLog")).UnregisterLogListing(MessageLogName);
		}
		FFigmaSyncStyle::Shutdown();
		Mappers.Reset();
	}

	// ------------------------------------------------------------------------------------------------ IFigmaSyncEditorModule

	virtual void RegisterWidgetMapper(const TSharedRef<IFigmaWidgetMapper>& Mapper) override
	{
		Mappers.AddUnique(Mapper);
	}

	virtual void UnregisterWidgetMapper(const TSharedRef<IFigmaWidgetMapper>& Mapper) override
	{
		Mappers.Remove(Mapper);
	}

	virtual TArray<TSharedRef<IFigmaWidgetMapper>> GetWidgetMappers() const override
	{
		return Mappers;
	}

	virtual void OpenPanel(const FString& BundlePath, UWidgetBlueprint* Target) override
	{
		FGlobalTabmanager::Get()->TryInvokeTab(PanelTabName);
		if (TSharedPtr<SFigmaSyncPanel> Panel = PanelWidget.Pin())
		{
			if (Target)
			{
				Panel->SetTarget(Target);
			}
			if (!BundlePath.IsEmpty())
			{
				Panel->LoadBundle(BundlePath);
			}
		}
	}

	virtual bool ResyncBlueprint(UWidgetBlueprint* Blueprint) override
	{
		const UFigmaSyncBlueprintExtension* State = UFigmaSyncBlueprintExtension::Find(Blueprint);
		if (!State || State->SourcePath.IsEmpty())
		{
			// Never synced: open the panel so the user can pick a bundle for it.
			OpenPanel(FString(), Blueprint);
			return false;
		}

		FScopedSlowTask Progress(1.f, FText::Format(LOCTEXT("Resyncing", "Re-syncing {0} from Figma..."), FText::FromString(Blueprint->GetName())));
		Progress.MakeDialog();
		Progress.EnterProgressFrame(1.f);
		const FFigmaImportResult Result = FFigmaImporter::Resync(Blueprint);
		ShowNotification(Result.bSuccess
			? FText::Format(LOCTEXT("ResyncOk", "{0} re-synced. {1}"), FText::FromString(Blueprint->GetName()), Result.GetSummary())
			: FText::Format(LOCTEXT("ResyncFail", "{0}: re-sync failed. {1}"), FText::FromString(Blueprint->GetName()), Result.GetSummary()),
			Result.bSuccess);
		if (TSharedPtr<SFigmaSyncPanel> Panel = PanelWidget.Pin())
		{
			Panel->ShowResult(Result);
		}
		return Result.bSuccess;
	}

private:
	TSharedRef<SDockTab> SpawnPanel(const FSpawnTabArgs& Args)
	{
		TSharedRef<SFigmaSyncPanel> Panel = SNew(SFigmaSyncPanel);
		PanelWidget = Panel;
		return SNew(SDockTab).TabRole(ETabRole::NomadTab)[Panel];
	}

	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);

		// Tools > Figma Sync
		if (UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools")))
		{
			FToolMenuSection& Section = ToolsMenu->AddSection(TEXT("Widgetify"), LOCTEXT("ToolsSection", "Widgetify"));
			Section.AddMenuEntry(TEXT("OpenFigmaSync"), LOCTEXT("OpenPanel", "Figma Sync"),
				LOCTEXT("OpenPanelTip", "Import Figma frames as Widget Blueprints and keep them in sync."), PluginIcon(),
				FUIAction(FExecuteAction::CreateLambda([this]() { OpenPanel(FString(), nullptr); })));
		}

		// Widget Designer toolbar: one-click re-sync of the open blueprint.
		if (UToolMenu* Toolbar = UToolMenus::Get()->ExtendMenu(TEXT("AssetEditor.WidgetBlueprintEditor.ToolBar")))
		{
			FToolMenuSection& Section = Toolbar->FindOrAddSection(TEXT("FigmaSync"));
			Section.AddDynamicEntry(TEXT("FigmaSyncToolbar"), FNewToolMenuSectionDelegate::CreateLambda([this](FToolMenuSection& InSection)
			{
				UWidgetBlueprint* Blueprint = GetEditedWidgetBlueprint(InSection);
				if (!Blueprint)
				{
					return;
				}
				const TWeakObjectPtr<UWidgetBlueprint> Weak = Blueprint;
				InSection.AddEntry(FToolMenuEntry::InitToolBarButton(TEXT("FigmaResync"),
					FUIAction(FExecuteAction::CreateLambda([this, Weak]()
					{
						if (UWidgetBlueprint* Target = Weak.Get())
						{
							ResyncBlueprint(Target);
						}
					})),
					LOCTEXT("ToolbarResync", "Figma Sync"),
					LOCTEXT("ToolbarResyncTip", "Re-sync this Widget Blueprint from its Figma bundle (opens the Figma Sync panel if it was never synced)."),
					PluginIcon()));
				InSection.AddEntry(FToolMenuEntry::InitToolBarButton(TEXT("FigmaOpenPanel"),
					FUIAction(FExecuteAction::CreateLambda([this, Weak]() { OpenPanel(FString(), Weak.Get()); })),
					LOCTEXT("ToolbarPanel", "Figma Panel"),
					LOCTEXT("ToolbarPanelTip", "Open the Figma Sync panel for this Widget Blueprint (lock list, log, presets)."),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Settings")));
			}));
		}

		// Content Browser: right-click a Widget Blueprint > Figma Sync.
		if (UToolMenu* AssetMenu = UToolMenus::Get()->ExtendMenu(TEXT("ContentBrowser.AssetContextMenu.WidgetBlueprint")))
		{
			FToolMenuSection& Section = AssetMenu->FindOrAddSection(TEXT("GetAssetActions"));
			Section.AddDynamicEntry(TEXT("FigmaSyncAssetActions"), FNewToolMenuSectionDelegate::CreateLambda([this](FToolMenuSection& InSection)
			{
				const UContentBrowserAssetContextMenuContext* Context = InSection.FindContext<UContentBrowserAssetContextMenuContext>();
				if (!Context || Context->SelectedAssets.Num() == 0)
				{
					return;
				}
				const TArray<FAssetData> Assets = Context->SelectedAssets;
				InSection.AddMenuEntry(TEXT("FigmaResyncAssets"), LOCTEXT("ResyncAssets", "Figma Sync: Re-sync"),
					LOCTEXT("ResyncAssetsTip", "Re-sync the selected Widget Blueprints from their Figma bundles."), PluginIcon(),
					FUIAction(FExecuteAction::CreateLambda([this, Assets]()
					{
						for (const FAssetData& Asset : Assets)
						{
							if (UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(Asset.GetAsset()))
							{
								ResyncBlueprint(Blueprint);
							}
						}
					})));
				InSection.AddMenuEntry(TEXT("FigmaOpenPanelForAsset"), LOCTEXT("OpenPanelForAsset", "Figma Sync: Open Panel"),
					LOCTEXT("OpenPanelForAssetTip", "Open the Figma Sync panel targeting this Widget Blueprint."), PluginIcon(),
					FUIAction(FExecuteAction::CreateLambda([this, Assets]()
					{
						OpenPanel(FString(), Cast<UWidgetBlueprint>(Assets[0].GetAsset()));
					})));
			}));
		}
	}

	void OnSettingsChanged(UObject* Settings, FPropertyChangedEvent& Event)
	{
		UpdateLiveServer();
	}

	void UpdateLiveServer()
	{
		const UFigmaSyncSettings& Settings = UFigmaSyncSettings::Get();
		const uint32 WantedPort = static_cast<uint32>(Settings.LivePushPort);
		// Commandlets (cooking, automation) load editor modules too; they must not grab the port.
		if (!Settings.bEnableLivePush || IsRunningCommandlet())
		{
			if (LiveServer.IsValid())
			{
				LiveServer->Stop();
				LiveServer.Reset();
			}
			return;
		}
		if (LiveServer.IsValid() && LiveServer->IsRunning() && LiveServer->GetPort() == WantedPort)
		{
			return;
		}
		if (!LiveServer.IsValid())
		{
			LiveServer = MakeShared<FFigmaLiveServer>();
			LiveServer->OnBundleReceived.BindRaw(this, &FFigmaSyncEditorModule::HandleLivePush);
		}
		FString Error;
		if (!LiveServer->Start(WantedPort, Error))
		{
			UE_LOG(LogFigmaSync, Warning, TEXT("Live push disabled: %s"), *Error);
			ShowNotification(FText::Format(LOCTEXT("LiveFailed", "Figma live push could not start: {0}"), FText::FromString(Error)), false);
		}
	}

	void HandleLivePush(const FString& SavedPath, const FString& FrameId)
	{
		const UFigmaSyncSettings& Settings = UFigmaSyncSettings::Get();
		UWidgetBlueprint* Bound = nullptr;
		if (const FSoftObjectPath* Path = FrameId.IsEmpty() ? nullptr : UFigmaSyncUserState::Get().FrameBindings.Find(FrameId))
		{
			Bound = Cast<UWidgetBlueprint>(Path->TryLoad());
		}

		if (!Bound || !Settings.bAutoApplyPushes)
		{
			OpenPanel(SavedPath, Bound);
			ShowNotification(LOCTEXT("PushReview", "Figma push received: review it in the Figma Sync panel."), true);
			return;
		}

		FString Error;
		TSharedPtr<FFigmaBundle> Bundle = FFigmaBundle::Open(SavedPath, Error);
		if (!Bundle.IsValid())
		{
			ShowNotification(FText::Format(LOCTEXT("PushBad", "Figma push could not be read: {0}"), FText::FromString(Error)), false);
			return;
		}
		const UFigmaSyncBlueprintExtension* State = UFigmaSyncBlueprintExtension::Find(Bound);
		FFigmaImportOptions Options;
		Options.Target = Bound;
		Options.SourcePath = SavedPath;
		Options.bSave = Settings.bSaveAfterImport;
		Options.StylePreset = State ? State->StylePreset.LoadSynchronous() : nullptr;
		const FFigmaImportResult Result = FFigmaImporter::Import(*Bundle, Options);
		ShowNotification(FText::Format(LOCTEXT("PushApplied", "Figma push applied to {0}. {1}"), FText::FromString(Bound->GetName()), Result.GetSummary()), Result.bSuccess);
		if (TSharedPtr<SFigmaSyncPanel> Panel = PanelWidget.Pin())
		{
			Panel->ShowResult(Result);
		}
	}

	TArray<TSharedRef<IFigmaWidgetMapper>> Mappers;
	TWeakPtr<SFigmaSyncPanel> PanelWidget;
	TSharedPtr<FFigmaLiveServer> LiveServer;
	FDelegateHandle SettingsChangedHandle;
};

IMPLEMENT_MODULE(FFigmaSyncEditorModule, FigmaSyncEditor);

#undef LOCTEXT_NAMESPACE
