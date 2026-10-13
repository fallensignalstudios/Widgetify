#pragma once

#include "CoreMinimal.h"

/**
 * In-memory model of a figmasync bundle (format version 1).
 *
 * Mirrors figma-plugin/src/shared/bundle.ts field for field. Lengths are Figma pixels (1 px == 1 Slate unit
 * at the design resolution), colors are sRGB 0..1, angles are clockwise degrees about the box center.
 */

namespace FigmaSync
{
	/** Highest bundle version this importer understands. */
	inline constexpr int32 SupportedBundleVersion = 1;
	inline const TCHAR* BundleJsonName = TEXT("figmasync.json");
}

enum class EFigmaNodeType : uint8
{
	Frame,
	Group,
	Component,
	ComponentSet,
	Instance,
	Section,
	Rectangle,
	Ellipse,
	Line,
	Text,
	Vector,
	Star,
	Polygon,
	BooleanOperation,
	Other,
};

enum class EFigmaConstraint : uint8 { Min, Max, Center, Stretch, Scale };
enum class EFigmaLayoutMode : uint8 { None, Horizontal, Vertical };
enum class EFigmaAlign : uint8 { Min, Center, Max, SpaceBetween, Baseline };
enum class EFigmaSizing : uint8 { Fixed, Hug, Fill };
enum class EFigmaStrokeAlign : uint8 { Inside, Outside, Center };
enum class EFigmaTextAlignH : uint8 { Left, Center, Right, Justified };
enum class EFigmaTextAlignV : uint8 { Top, Center, Bottom };
enum class EFigmaTextAutoResize : uint8 { None, WidthAndHeight, Height, Truncate };
enum class EFigmaTextCase : uint8 { Original, Upper, Lower, Title, SmallCaps };
enum class EFigmaTextDecoration : uint8 { None, Underline, Strikethrough };
enum class EFigmaUnit : uint8 { Auto, Pixels, Percent };
enum class EFigmaComponentKind : uint8 { None, Instance, Component, ComponentSet };

enum class EFigmaPaintType : uint8
{
	Solid,
	GradientLinear,
	GradientRadial,
	GradientAngular,
	GradientDiamond,
	Image,
	Video,
	Pattern,
	Other,
};

enum class EFigmaEffectType : uint8 { DropShadow, InnerShadow, LayerBlur, BackgroundBlur, Other };

struct FIGMASYNCCORE_API FFigmaColor
{
	float R = 0.f;
	float G = 0.f;
	float B = 0.f;
	float A = 1.f;

	/** Converts the sRGB color to the linear color Slate expects, with an extra opacity multiplier. */
	FLinearColor ToLinear(float Opacity = 1.f) const;

	/** "#RRGGBB" or "#RRGGBBAA", handy for logs and preset keys. */
	FString ToHex() const;
};

struct FFigmaRect
{
	float X = 0.f;
	float Y = 0.f;
	float Width = 0.f;
	float Height = 0.f;
};

struct FFigmaPadding
{
	float Left = 0.f;
	float Top = 0.f;
	float Right = 0.f;
	float Bottom = 0.f;

	bool IsZero() const { return Left == 0.f && Top == 0.f && Right == 0.f && Bottom == 0.f; }
};

struct FFigmaGradientStop
{
	float Position = 0.f;
	FFigmaColor Color;
};

struct FIGMASYNCCORE_API FFigmaPaint
{
	EFigmaPaintType Type = EFigmaPaintType::Solid;
	bool bVisible = true;
	float Opacity = 1.f;
	FString BlendMode;
	FFigmaColor Color;
	TArray<FFigmaGradientStop> Stops;
	FString ScaleMode;

	bool IsVisible() const { return bVisible && Opacity > 0.0005f; }
	bool IsSolid() const { return Type == EFigmaPaintType::Solid; }

	/** Solid color with paint opacity folded into alpha, as linear. Gradients return their first stop. */
	FLinearColor ToLinearColor() const;
};

struct FFigmaEffect
{
	EFigmaEffectType Type = EFigmaEffectType::Other;
	bool bVisible = true;
	float Radius = 0.f;
	FFigmaColor Color;
	FVector2D Offset = FVector2D::ZeroVector;
	float Spread = 0.f;
};

struct FIGMASYNCCORE_API FFigmaStyle
{
	TArray<FFigmaPaint> Fills;
	TArray<FFigmaPaint> Strokes;
	float StrokeWeight = 0.f;
	bool bHasStrokeWeights = false;
	FFigmaPadding StrokeWeights;
	EFigmaStrokeAlign StrokeAlign = EFigmaStrokeAlign::Inside;
	bool bDashed = false;
	/** Top-left, top-right, bottom-right, bottom-left. */
	FVector4 CornerRadius = FVector4(0, 0, 0, 0);
	float CornerSmoothing = 0.f;
	TArray<FFigmaEffect> Effects;
	FString BlendMode;
	FString FillStyle;
	FString StrokeStyle;
	FString EffectStyle;
	/** Exactly expressible as a Slate rounded box (one solid fill, one uniform solid stroke, no effects). */
	bool bSimple = false;

	const FFigmaPaint* FindVisibleFill() const;
	const FFigmaPaint* FindVisibleStroke() const;
	bool HasVisibleFill() const { return FindVisibleFill() != nullptr; }
	bool HasVisibleStroke() const { return FindVisibleStroke() != nullptr && StrokeWeight > 0.f; }
	bool HasCornerRadius() const { return CornerRadius.X > 0 || CornerRadius.Y > 0 || CornerRadius.Z > 0 || CornerRadius.W > 0; }
	bool HasVisibleEffects() const;
	/** True when nothing would be drawn (no visible fill, stroke or effect). */
	bool IsEmpty() const { return !HasVisibleFill() && !HasVisibleStroke() && !HasVisibleEffects(); }
};

struct FFigmaAutoLayout
{
	EFigmaLayoutMode Mode = EFigmaLayoutMode::None;
	bool bWrap = false;
	FFigmaPadding Padding;
	float ItemSpacing = 0.f;
	float CounterAxisSpacing = 0.f;
	EFigmaAlign PrimaryAlign = EFigmaAlign::Min;
	EFigmaAlign CounterAlign = EFigmaAlign::Min;
	bool bReverseZIndex = false;
};

struct FFigmaChildLayout
{
	bool bAbsolute = false;
	EFigmaSizing SizingH = EFigmaSizing::Fixed;
	EFigmaSizing SizingV = EFigmaSizing::Fixed;
	float Grow = 0.f;
	bool bStretch = false;
};

struct FFigmaSizeLimits
{
	TOptional<float> MinWidth;
	TOptional<float> MaxWidth;
	TOptional<float> MinHeight;
	TOptional<float> MaxHeight;

	bool IsSet() const { return MinWidth.IsSet() || MaxWidth.IsSet() || MinHeight.IsSet() || MaxHeight.IsSet(); }
};

struct FFigmaTextSegment
{
	int32 Start = 0;
	int32 End = 0;
	FString Characters;
	FString FontFamily;
	FString FontStyle;
	int32 FontWeight = 400;
	float FontSize = 16.f;
	FFigmaColor Color;
};

struct FFigmaText
{
	FString Characters;
	FString FontFamily;
	FString FontStyle;
	int32 FontWeight = 400;
	bool bItalic = false;
	float FontSize = 16.f;
	FFigmaColor Color;
	EFigmaTextAlignH AlignH = EFigmaTextAlignH::Left;
	EFigmaTextAlignV AlignV = EFigmaTextAlignV::Top;
	EFigmaTextAutoResize AutoResize = EFigmaTextAutoResize::WidthAndHeight;
	bool bTruncate = false;
	int32 MaxLines = 0;
	float LetterSpacing = 0.f;
	EFigmaUnit LetterSpacingUnit = EFigmaUnit::Pixels;
	float LineHeight = 0.f;
	EFigmaUnit LineHeightUnit = EFigmaUnit::Auto;
	EFigmaTextCase TextCase = EFigmaTextCase::Original;
	EFigmaTextDecoration Decoration = EFigmaTextDecoration::None;
	FString TextStyle;
	bool bHasStroke = false;
	FFigmaColor StrokeColor;
	float StrokeWeight = 0.f;
	bool bHasShadow = false;
	FFigmaColor ShadowColor;
	FVector2D ShadowOffset = FVector2D::ZeroVector;
	float ShadowRadius = 0.f;
	bool bMixed = false;
	TArray<FFigmaTextSegment> Segments;
};

struct FFigmaComponentRef
{
	EFigmaComponentKind Kind = EFigmaComponentKind::None;
	FString ComponentId;
	FString ComponentName;
	FString ComponentSetId;
	FString ComponentSetName;
	TMap<FString, FString> Variant;
	/** TEXT component properties (e.g. Label -> "Play"). */
	TMap<FString, FString> TextProperties;
	TMap<FString, bool> BoolProperties;
	bool bRemote = false;

	bool IsSet() const { return Kind != EFigmaComponentKind::None; }
	/** Component set name when present, otherwise the component name ("Btn/Primary"). */
	const FString& GetFamilyName() const { return ComponentSetName.IsEmpty() ? ComponentName : ComponentSetName; }
};

struct FFigmaRasterRef
{
	FString AssetId;
	/** PNG placement relative to the node's layout box. May extend past it (shadows). */
	FFigmaRect Bounds;
	FString Reason;

	bool IsSet() const { return !AssetId.IsEmpty(); }
};

struct FIGMASYNCCORE_API FFigmaNode
{
	FString Id;
	FString Name;
	FString CleanName;
	TMap<FString, FString> Tags;
	EFigmaNodeType Type = EFigmaNodeType::Frame;
	bool bVisible = true;
	float Opacity = 1.f;
	float Rotation = 0.f;
	float X = 0.f;
	float Y = 0.f;
	float Width = 0.f;
	float Height = 0.f;

	bool bHasConstraints = false;
	EFigmaConstraint ConstraintH = EFigmaConstraint::Min;
	EFigmaConstraint ConstraintV = EFigmaConstraint::Min;

	bool bHasLayout = false;
	FFigmaAutoLayout Layout;

	bool bHasChildLayout = false;
	FFigmaChildLayout Child;

	FFigmaSizeLimits SizeLimits;
	bool bClipsContent = false;

	bool bHasStyle = false;
	FFigmaStyle Style;

	bool bHasText = false;
	FFigmaText Text;

	FFigmaComponentRef Component;
	FFigmaRasterRef Image;
	FFigmaRasterRef Background;
	TMap<FString, FString> PropRefs;
	bool bIsMask = false;

	/** True when the JSON had a children array (frames, groups, instances...), even if it is empty. */
	bool bHasChildrenArray = false;
	TArray<TSharedPtr<FFigmaNode>> Children;

	/** Back pointer, filled in by the parser. Null for roots (screen and variant roots). */
	FFigmaNode* Parent = nullptr;

	bool HasTag(const TCHAR* Key) const { return Tags.Contains(Key); }
	FString GetTag(const TCHAR* Key, const FString& Default = FString()) const;

	bool IsText() const { return Type == EFigmaNodeType::Text; }
	bool IsRasterized() const { return Image.IsSet(); }
	/** Frame-like: frames, groups, components, instances, sections. Rasterized nodes are never containers. */
	bool IsContainer() const { return bHasChildrenArray && !IsRasterized(); }
	bool IsAutoLayout() const { return bHasLayout && Layout.Mode != EFigmaLayoutMode::None; }

	/** "MainMenu/Pnl_Menu/Btn_Play" for logs. */
	FString GetPath() const;
	int32 CountDescendants() const;
};

struct FFigmaAsset
{
	FString Id;
	FString Path;
	int32 Width = 0;
	int32 Height = 0;
	float Scale = 1.f;
	FString Hash;
	int64 Bytes = 0;
};

struct FFigmaVariant
{
	FString Id;
	FString Name;
	TMap<FString, FString> Properties;
	TSharedPtr<FFigmaNode> Node;
};

struct FFigmaComponentSet
{
	FString Id;
	FString Name;
	bool bRemote = false;
	TArray<FString> PropertyNames;
	TArray<FFigmaVariant> Variants;
};

struct FFigmaWarning
{
	FString NodeId;
	FString NodePath;
	FString Message;
};

struct FFigmaExportOptions
{
	float Scale = 2.f;
	bool bIncludeHidden = true;
	bool bRasterizeUnsupported = true;
	TArray<FString> VariantPrefixes;
};

struct FIGMASYNCCORE_API FFigmaDocument
{
	int32 Version = 0;
	FString GeneratorName;
	FString GeneratorVersion;
	FString ExportedAt;

	FString FileName;
	FString PageName;
	FString FrameId;
	FString FrameName;

	FFigmaExportOptions Options;
	TSharedPtr<FFigmaNode> Root;
	TMap<FString, FFigmaComponentSet> Components;
	TArray<FFigmaAsset> Assets;
	TArray<FFigmaWarning> Warnings;

	const FFigmaAsset* FindAsset(const FString& AssetId) const;
	const FFigmaComponentSet* FindComponentSet(const FString& SetId) const;

	/** Depth-first visit of the screen tree (not component variants). */
	void ForEachNode(TFunctionRef<void(const FFigmaNode&)> Visitor) const;
	const FFigmaNode* FindNodeById(const FString& NodeId) const;
	int32 CountNodes() const;
};
