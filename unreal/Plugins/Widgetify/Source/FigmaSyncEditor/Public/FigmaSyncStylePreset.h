#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Fonts/SlateFontInfo.h"
#include "FigmaSyncSettings.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include "UObject/SoftObjectPath.h"
#include "FigmaSyncStylePreset.generated.h"

/** Replaces the look of every layer that uses a Figma text style. */
USTRUCT(BlueprintType)
struct FIGMASYNCEDITOR_API FFigmaTextStyleOverride
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FSlateFontInfo Font;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	bool bOverrideColor = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text", meta = (EditCondition = "bOverrideColor"))
	FLinearColor Color = FLinearColor::White;

	/** Optional CommonUI text style class, applied to CommonTextBlocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text", meta = (MetaClass = "/Script/CommonUI.CommonTextStyle"))
	FSoftClassPath CommonTextStyle;
};

/** Replaces the generated look of a button (by component set name, e.g. "Btn/Primary", or layer name). */
USTRUCT(BlueprintType)
struct FIGMASYNCEDITOR_API FFigmaButtonStyleOverride
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button")
	bool bOverrideButtonStyle = false;

	/** Used by plain UMG buttons. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button", meta = (EditCondition = "bOverrideButtonStyle"))
	FButtonStyle ButtonStyle;

	/** Used by CommonUI buttons instead of the generated style class. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button", meta = (MetaClass = "/Script/CommonUI.CommonButtonStyle"))
	FSoftClassPath CommonButtonStyle;
};

/**
 * A reusable look for a game (fonts, brushes, button states): save it once and apply it to every imported screen.
 * Figma owns the layout; the preset owns how named styles render in Unreal.
 *
 * Create one with the "Save look as preset" button in the Figma Sync panel, or as a Data Asset.
 */
UCLASS(BlueprintType)
class FIGMASYNCEDITOR_API UFigmaSyncStylePreset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Checked before the project-wide font table. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fonts")
	TArray<FFigmaFontMapping> FontMappings;

	/** Figma paint style name ("Surface/Panel") -> brush used instead of the flat Figma color. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Styles")
	TMap<FString, FSlateBrush> PaintStyles;

	/** Figma text style name ("Heading/H1") -> font and color. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Styles")
	TMap<FString, FFigmaTextStyleOverride> TextStyles;

	/** Component set ("Btn/Primary") or layer name ("Btn_Back") -> button look. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buttons")
	TMap<FString, FFigmaButtonStyleOverride> ButtonStyles;

	/** When a plain button has no Hover variant in Figma, its hovered brush is the normal one times this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buttons", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float HoverBrightness = 1.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buttons", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float PressedBrightness = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buttons", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DisabledOpacity = 0.45f;

	/** Overrides the project's screen parent class for screens imported with this preset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Classes", meta = (MetaClass = "/Script/UMG.UserWidget"))
	FSoftClassPath ScreenParentClass;

	/** Overrides the project's button base class for buttons generated with this preset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Classes", meta = (MetaClass = "/Script/UMG.UserWidget"))
	FSoftClassPath ButtonBaseClass;

	/** Texture preset used for layers that don't name one. Empty = project default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Textures")
	FName TexturePreset;

	const FFigmaTextStyleOverride* FindTextStyle(const FString& FigmaStyleName) const;
	const FSlateBrush* FindPaintStyle(const FString& FigmaStyleName) const;
	const FFigmaButtonStyleOverride* FindButtonStyle(const FString& Key) const;
};
