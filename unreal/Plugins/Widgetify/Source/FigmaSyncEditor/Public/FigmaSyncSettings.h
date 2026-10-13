#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "Engine/Texture.h"
#include "Engine/TextureDefines.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"
#include "FigmaSyncSettings.generated.h"

class UFont;
class UFigmaSyncStylePreset;

/** Maps a Figma font (family + style) to an Unreal font asset and typeface. */
USTRUCT(BlueprintType)
struct FIGMASYNCEDITOR_API FFigmaFontMapping
{
	GENERATED_BODY()

	/** Figma font family, e.g. "Cinzel". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Font")
	FString FigmaFamily;

	/** Figma style, e.g. "SemiBold". Leave empty to match every style of the family. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Font")
	FString FigmaStyle;

	/** Unreal font asset (a composite font with typefaces). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Font")
	TSoftObjectPtr<UFont> Font;

	/** Typeface inside the font asset ("Bold"). Leave empty to pick one from the Figma style and weight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Font")
	FName Typeface;
};

/** Texture settings applied to newly imported PNGs. Choose one per layer with [img:PresetName]. */
USTRUCT(BlueprintType)
struct FIGMASYNCEDITOR_API FFigmaTexturePreset
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture")
	FName Name = TEXT("UI");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture")
	TEnumAsByte<TextureCompressionSettings> CompressionSettings = TC_EditorIcon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture")
	bool bSRGB = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture")
	TEnumAsByte<TextureGroup> LODGroup = TEXTUREGROUP_UI;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture")
	TEnumAsByte<TextureMipGenSettings> MipGenSettings = TMGS_NoMipmaps;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture")
	TEnumAsByte<TextureFilter> Filter = TF_Default;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture")
	bool bNeverStream = true;
};

/**
 * Project-wide Widgetify settings (Project Settings > Plugins > Widgetify (Figma Sync)).
 * Stored in Config/DefaultEditor.ini so the whole team shares them.
 */
UCLASS(Config = Editor, DefaultConfig, meta = (DisplayName = "Widgetify (Figma Sync)"))
class FIGMASYNCEDITOR_API UFigmaSyncSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UFigmaSyncSettings();

	static const UFigmaSyncSettings& Get() { return *GetDefault<UFigmaSyncSettings>(); }

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	// ---------------------------------------------------------------- Import

	/** Where new Widget Blueprints are created. */
	UPROPERTY(Config, EditAnywhere, Category = "Import", meta = (ContentDir, LongPackageName))
	FDirectoryPath DestinationFolder;

	/** Prefix for new Widget Blueprints: "WBP_" + frame name. */
	UPROPERTY(Config, EditAnywhere, Category = "Import")
	FString WidgetBlueprintPrefix = TEXT("WBP_");

	/** Sub-folder (inside the destination) for imported textures. Each screen gets its own folder below it. */
	UPROPERTY(Config, EditAnywhere, Category = "Import")
	FString TextureFolderName = TEXT("Textures");

	UPROPERTY(Config, EditAnywhere, Category = "Import")
	FString TexturePrefix = TEXT("T_");

	/** Sub-folder for assets shared between screens (generated CommonUI buttons and styles). */
	UPROPERTY(Config, EditAnywhere, Category = "Import")
	FString SharedFolderName = TEXT("Components");

	UPROPERTY(Config, EditAnywhere, Category = "Import")
	bool bSaveAfterImport = true;

	UPROPERTY(Config, EditAnywhere, Category = "Import")
	bool bOpenAfterImport = true;

	/** The resolution the Figma frames are designed at. Used for preview size and lint. */
	UPROPERTY(Config, EditAnywhere, Category = "Import")
	FIntPoint DesignResolution = FIntPoint(1920, 1080);

	/**
	 * Figma font sizes are pixels; UMG font sizes are points at 96 DPI. 0.75 makes a 24 px Figma label match a
	 * size 18 UMG font, which renders at the same pixel height at 1.0 DPI scale.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Import", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float FontSizeScale = 0.75f;

	// ---------------------------------------------------------------- Naming

	/** Layers starting with these prefixes become Blueprint variables (e.g. Btn_Play, Txt_PlayerName). */
	UPROPERTY(Config, EditAnywhere, Category = "Naming")
	TArray<FString> VariablePrefixes;

	/** Layers starting with these prefixes become buttons. */
	UPROPERTY(Config, EditAnywhere, Category = "Naming")
	TArray<FString> ButtonPrefixes;

	/** Component sets starting with these prefixes become buttons, with one style per variant (Btn/Primary/Hover). */
	UPROPERTY(Config, EditAnywhere, Category = "Naming")
	TArray<FString> ButtonComponentPrefixes;

	/** Layers starting with these prefixes become CommonUI input action widgets (input glyphs). */
	UPROPERTY(Config, EditAnywhere, Category = "Naming")
	TArray<FString> ActionWidgetPrefixes;

	// ---------------------------------------------------------------- Fonts

	/** One-time table that maps Figma family + style to Unreal fonts. Unmapped fonts show up in the lint report. */
	UPROPERTY(Config, EditAnywhere, Category = "Fonts")
	TArray<FFigmaFontMapping> FontMappings;

	/** Look for project font assets whose name contains the Figma family ("Font_Cinzel") when no mapping matches. */
	UPROPERTY(Config, EditAnywhere, Category = "Fonts")
	bool bAutoMatchFonts = true;

	/** Used when nothing else matches, so text never disappears. */
	UPROPERTY(Config, EditAnywhere, Category = "Fonts")
	TSoftObjectPtr<UFont> FallbackFont;

	// ---------------------------------------------------------------- Textures

	UPROPERTY(Config, EditAnywhere, Category = "Textures")
	TArray<FFigmaTexturePreset> TexturePresets;

	/** Preset used when a layer doesn't name one. */
	UPROPERTY(Config, EditAnywhere, Category = "Textures")
	FName DefaultTexturePreset = TEXT("UI");

	// ---------------------------------------------------------------- Re-sync

	/** Keep widgets whose Figma layer was deleted (and list them in the lint report) instead of removing them. */
	UPROPERTY(Config, EditAnywhere, Category = "Re-sync")
	bool bKeepRemovedWidgets = true;

	/** Only touch decorative widget visibility; widgets you made interactive in Unreal keep their hit-testing. */
	UPROPERTY(Config, EditAnywhere, Category = "Re-sync")
	bool bMakeDecorationsHitTestInvisible = true;

	// ---------------------------------------------------------------- CommonUI

	/** Use CommonUI classes when the CommonUI plugin is enabled. */
	UPROPERTY(Config, EditAnywhere, Category = "CommonUI")
	bool bEnableCommonUI = true;

	/** Parent class for imported screens when CommonUI is used. */
	UPROPERTY(Config, EditAnywhere, Category = "CommonUI", meta = (MetaClass = "/Script/UMG.UserWidget", EditCondition = "bEnableCommonUI"))
	FSoftClassPath ScreenParentClass;

	/** Parent class for generated button widgets (your own CommonButtonBase subclass works too). */
	UPROPERTY(Config, EditAnywhere, Category = "CommonUI", meta = (MetaClass = "/Script/UMG.UserWidget", EditCondition = "bEnableCommonUI"))
	FSoftClassPath ButtonBaseClass;

	/**
	 * Text property that receives each button instance's label. If the button base class doesn't have it,
	 * generated buttons get a ButtonText variable plus a tiny PreConstruct graph that applies it.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "CommonUI", meta = (EditCondition = "bEnableCommonUI"))
	FName ButtonTextPropertyName = TEXT("ButtonText");

	/** Data table with CommonUI input action rows; [action=Back] binds the row named "Back". */
	UPROPERTY(Config, EditAnywhere, Category = "CommonUI", meta = (AllowedClasses = "/Script/Engine.DataTable", EditCondition = "bEnableCommonUI"))
	FSoftObjectPath InputActionTable;

	/** Look for Enhanced Input actions named Prefix + action ("IA_Back") when the data table has no matching row. */
	UPROPERTY(Config, EditAnywhere, Category = "CommonUI", meta = (EditCondition = "bEnableCommonUI"))
	FString EnhancedInputActionPrefix = TEXT("IA_");

	// ---------------------------------------------------------------- Live push

	/** Run a local server so the Figma plugin's "Push to Unreal" button can send bundles straight into the editor. */
	UPROPERTY(Config, EditAnywhere, Category = "Live Push")
	bool bEnableLivePush = false;

	/** Must match the port in the Figma plugin's manifest.json (networkAccess.devAllowedDomains). */
	UPROPERTY(Config, EditAnywhere, Category = "Live Push", meta = (ClampMin = "1024", ClampMax = "65535", EditCondition = "bEnableLivePush"))
	int32 LivePushPort = 7820;

	/** Re-sync the bound Widget Blueprint as soon as a push arrives. Otherwise the bundle opens in the Figma Sync panel. */
	UPROPERTY(Config, EditAnywhere, Category = "Live Push", meta = (EditCondition = "bEnableLivePush"))
	bool bAutoApplyPushes = true;

	// ---------------------------------------------------------------- Style

	/** Applied to every import unless the Figma Sync panel picks another one. */
	UPROPERTY(Config, EditAnywhere, Category = "Style")
	TSoftObjectPtr<UFigmaSyncStylePreset> DefaultStylePreset;

	const FFigmaTexturePreset& FindTexturePreset(FName PresetName) const;
	FString GetDestinationFolder() const;
};

/** Per-user state: which Figma frame feeds which Widget Blueprint (for live push), and recent sources. */
UCLASS(Config = EditorPerProjectUserSettings)
class FIGMASYNCEDITOR_API UFigmaSyncUserState : public UObject
{
	GENERATED_BODY()

public:
	static UFigmaSyncUserState& Get() { return *GetMutableDefault<UFigmaSyncUserState>(); }

	UPROPERTY(Config)
	TMap<FString, FSoftObjectPath> FrameBindings;

	UPROPERTY(Config)
	TArray<FString> RecentSources;

	void BindFrame(const FString& FrameId, const FSoftObjectPath& Blueprint);
	void AddRecentSource(FString Path);
};
