#include "FigmaSyncSettings.h"
#include "Engine/Font.h"
#include "FigmaSyncStylePreset.h"

UFigmaSyncSettings::UFigmaSyncSettings()
{
	DestinationFolder.Path = TEXT("/Game/UI/Figma");

	VariablePrefixes = { TEXT("Btn_"), TEXT("Txt_"), TEXT("Img_"), TEXT("Ico_"), TEXT("Pnl_"), TEXT("Box_"), TEXT("Lst_"), TEXT("Bar_"), TEXT("Inp_"), TEXT("Act_") };
	ButtonPrefixes = { TEXT("Btn_") };
	ButtonComponentPrefixes = { TEXT("Btn/"), TEXT("Button/") };
	ActionWidgetPrefixes = { TEXT("Act_") };

	FallbackFont = TSoftObjectPtr<UFont>(FSoftObjectPath(TEXT("/Engine/EngineFonts/Roboto.Roboto")));

	FFigmaTexturePreset UiPreset;
	UiPreset.Name = TEXT("UI");
	TexturePresets.Add(UiPreset);

	FFigmaTexturePreset Background;
	Background.Name = TEXT("Background");
	Background.CompressionSettings = TC_Default;
	Background.MipGenSettings = TMGS_NoMipmaps;
	TexturePresets.Add(Background);

	FFigmaTexturePreset Linear;
	Linear.Name = TEXT("Linear");
	Linear.bSRGB = false;
	TexturePresets.Add(Linear);

	ScreenParentClass = FSoftClassPath(TEXT("/Script/CommonUI.CommonActivatableWidget"));
	ButtonBaseClass = FSoftClassPath(TEXT("/Script/CommonUI.CommonButtonBase"));
}

const FFigmaTexturePreset& UFigmaSyncSettings::FindTexturePreset(FName PresetName) const
{
	const FName Wanted = PresetName.IsNone() ? DefaultTexturePreset : PresetName;
	for (const FFigmaTexturePreset& Preset : TexturePresets)
	{
		if (Preset.Name == Wanted)
		{
			return Preset;
		}
	}
	for (const FFigmaTexturePreset& Preset : TexturePresets)
	{
		if (Preset.Name == DefaultTexturePreset)
		{
			return Preset;
		}
	}
	static const FFigmaTexturePreset BuiltIn;
	return TexturePresets.Num() > 0 ? TexturePresets[0] : BuiltIn;
}

FString UFigmaSyncSettings::GetDestinationFolder() const
{
	FString Folder = DestinationFolder.Path.IsEmpty() ? FString(TEXT("/Game/UI/Figma")) : DestinationFolder.Path;
	Folder.RemoveFromEnd(TEXT("/"));
	return Folder;
}

void UFigmaSyncUserState::BindFrame(const FString& FrameId, const FSoftObjectPath& Blueprint)
{
	if (FrameId.IsEmpty())
	{
		return;
	}
	FrameBindings.Add(FrameId, Blueprint);
	SaveConfig();
}

void UFigmaSyncUserState::AddRecentSource(FString Path)
{
	if (Path.IsEmpty())
	{
		return;
	}
	RecentSources.Remove(Path);
	RecentSources.Insert(Path, 0);
	if (RecentSources.Num() > 10)
	{
		RecentSources.SetNum(10);
	}
	SaveConfig();
}
