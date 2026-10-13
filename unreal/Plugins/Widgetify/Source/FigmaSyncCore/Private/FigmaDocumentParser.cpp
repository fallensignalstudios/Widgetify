#include "FigmaDocumentParser.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	using FJsonObjectPtr = TSharedPtr<FJsonObject>;
	using FJsonArray = TArray<TSharedPtr<FJsonValue>>;

	constexpr int32 MaxDepth = 256;

	FString Str(const FJsonObjectPtr& Object, const TCHAR* Key, const FString& Default = FString())
	{
		FString Value;
		return Object.IsValid() && Object->TryGetStringField(Key, Value) ? Value : Default;
	}

	float Num(const FJsonObjectPtr& Object, const TCHAR* Key, float Default = 0.f)
	{
		double Value = 0.0;
		return Object.IsValid() && Object->TryGetNumberField(Key, Value) ? static_cast<float>(Value) : Default;
	}

	bool Bool(const FJsonObjectPtr& Object, const TCHAR* Key, bool Default = false)
	{
		bool Value = false;
		return Object.IsValid() && Object->TryGetBoolField(Key, Value) ? Value : Default;
	}

	FJsonObjectPtr Obj(const FJsonObjectPtr& Object, const TCHAR* Key)
	{
		const FJsonObjectPtr* Value = nullptr;
		return Object.IsValid() && Object->TryGetObjectField(Key, Value) && Value ? *Value : FJsonObjectPtr();
	}

	const FJsonArray& Arr(const FJsonObjectPtr& Object, const TCHAR* Key)
	{
		static const FJsonArray Empty;
		const FJsonArray* Value = nullptr;
		return Object.IsValid() && Object->TryGetArrayField(Key, Value) && Value ? *Value : Empty;
	}

	bool HasArray(const FJsonObjectPtr& Object, const TCHAR* Key)
	{
		const FJsonArray* Value = nullptr;
		return Object.IsValid() && Object->TryGetArrayField(Key, Value) && Value;
	}

	void StringMap(const FJsonObjectPtr& Object, TMap<FString, FString>& Out)
	{
		if (!Object.IsValid())
		{
			return;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
		{
			FString Value;
			if (Pair.Value.IsValid() && Pair.Value->TryGetString(Value))
			{
				Out.Add(Pair.Key, Value);
			}
		}
	}

	template <typename EnumType>
	struct TEnumEntry
	{
		const TCHAR* Name;
		EnumType Value;
	};

	template <typename EnumType, int32 N>
	EnumType Lookup(const FString& Value, const TEnumEntry<EnumType> (&Table)[N], EnumType Default)
	{
		for (const TEnumEntry<EnumType>& Entry : Table)
		{
			if (Value.Equals(Entry.Name, ESearchCase::IgnoreCase))
			{
				return Entry.Value;
			}
		}
		return Default;
	}

	EFigmaNodeType ParseNodeType(const FString& Value)
	{
		static const TEnumEntry<EFigmaNodeType> Table[] = {
			{ TEXT("FRAME"), EFigmaNodeType::Frame },
			{ TEXT("GROUP"), EFigmaNodeType::Group },
			{ TEXT("COMPONENT"), EFigmaNodeType::Component },
			{ TEXT("COMPONENT_SET"), EFigmaNodeType::ComponentSet },
			{ TEXT("INSTANCE"), EFigmaNodeType::Instance },
			{ TEXT("SECTION"), EFigmaNodeType::Section },
			{ TEXT("RECTANGLE"), EFigmaNodeType::Rectangle },
			{ TEXT("ELLIPSE"), EFigmaNodeType::Ellipse },
			{ TEXT("LINE"), EFigmaNodeType::Line },
			{ TEXT("TEXT"), EFigmaNodeType::Text },
			{ TEXT("VECTOR"), EFigmaNodeType::Vector },
			{ TEXT("STAR"), EFigmaNodeType::Star },
			{ TEXT("POLYGON"), EFigmaNodeType::Polygon },
			{ TEXT("BOOLEAN_OPERATION"), EFigmaNodeType::BooleanOperation },
		};
		return Lookup(Value, Table, EFigmaNodeType::Other);
	}

	EFigmaConstraint ParseConstraint(const FString& Value)
	{
		static const TEnumEntry<EFigmaConstraint> Table[] = {
			{ TEXT("MIN"), EFigmaConstraint::Min },
			{ TEXT("MAX"), EFigmaConstraint::Max },
			{ TEXT("CENTER"), EFigmaConstraint::Center },
			{ TEXT("STRETCH"), EFigmaConstraint::Stretch },
			{ TEXT("SCALE"), EFigmaConstraint::Scale },
		};
		return Lookup(Value, Table, EFigmaConstraint::Min);
	}

	EFigmaAlign ParseAlign(const FString& Value)
	{
		static const TEnumEntry<EFigmaAlign> Table[] = {
			{ TEXT("MIN"), EFigmaAlign::Min },
			{ TEXT("CENTER"), EFigmaAlign::Center },
			{ TEXT("MAX"), EFigmaAlign::Max },
			{ TEXT("SPACE_BETWEEN"), EFigmaAlign::SpaceBetween },
			{ TEXT("BASELINE"), EFigmaAlign::Baseline },
		};
		return Lookup(Value, Table, EFigmaAlign::Min);
	}

	EFigmaSizing ParseSizing(const FString& Value)
	{
		static const TEnumEntry<EFigmaSizing> Table[] = {
			{ TEXT("FIXED"), EFigmaSizing::Fixed },
			{ TEXT("HUG"), EFigmaSizing::Hug },
			{ TEXT("FILL"), EFigmaSizing::Fill },
		};
		return Lookup(Value, Table, EFigmaSizing::Fixed);
	}

	EFigmaUnit ParseUnit(const FString& Value)
	{
		static const TEnumEntry<EFigmaUnit> Table[] = {
			{ TEXT("AUTO"), EFigmaUnit::Auto },
			{ TEXT("PIXELS"), EFigmaUnit::Pixels },
			{ TEXT("PERCENT"), EFigmaUnit::Percent },
		};
		return Lookup(Value, Table, EFigmaUnit::Pixels);
	}

	EFigmaPaintType ParsePaintType(const FString& Value)
	{
		static const TEnumEntry<EFigmaPaintType> Table[] = {
			{ TEXT("SOLID"), EFigmaPaintType::Solid },
			{ TEXT("GRADIENT_LINEAR"), EFigmaPaintType::GradientLinear },
			{ TEXT("GRADIENT_RADIAL"), EFigmaPaintType::GradientRadial },
			{ TEXT("GRADIENT_ANGULAR"), EFigmaPaintType::GradientAngular },
			{ TEXT("GRADIENT_DIAMOND"), EFigmaPaintType::GradientDiamond },
			{ TEXT("IMAGE"), EFigmaPaintType::Image },
			{ TEXT("VIDEO"), EFigmaPaintType::Video },
			{ TEXT("PATTERN"), EFigmaPaintType::Pattern },
		};
		return Lookup(Value, Table, EFigmaPaintType::Other);
	}

	EFigmaEffectType ParseEffectType(const FString& Value)
	{
		static const TEnumEntry<EFigmaEffectType> Table[] = {
			{ TEXT("DROP_SHADOW"), EFigmaEffectType::DropShadow },
			{ TEXT("INNER_SHADOW"), EFigmaEffectType::InnerShadow },
			{ TEXT("LAYER_BLUR"), EFigmaEffectType::LayerBlur },
			{ TEXT("BACKGROUND_BLUR"), EFigmaEffectType::BackgroundBlur },
		};
		return Lookup(Value, Table, EFigmaEffectType::Other);
	}

	FFigmaColor ParseColor(const FJsonObjectPtr& Object, const FFigmaColor& Default = FFigmaColor())
	{
		if (!Object.IsValid())
		{
			return Default;
		}
		FFigmaColor Color;
		Color.R = Num(Object, TEXT("r"));
		Color.G = Num(Object, TEXT("g"));
		Color.B = Num(Object, TEXT("b"));
		Color.A = Num(Object, TEXT("a"), 1.f);
		return Color;
	}

	FFigmaRect ParseRect(const FJsonObjectPtr& Object)
	{
		FFigmaRect Rect;
		Rect.X = Num(Object, TEXT("x"));
		Rect.Y = Num(Object, TEXT("y"));
		Rect.Width = Num(Object, TEXT("width"));
		Rect.Height = Num(Object, TEXT("height"));
		return Rect;
	}

	FFigmaPadding ParseSides(const FJsonObjectPtr& Object)
	{
		FFigmaPadding Sides;
		Sides.Left = Num(Object, TEXT("left"));
		Sides.Top = Num(Object, TEXT("top"));
		Sides.Right = Num(Object, TEXT("right"));
		Sides.Bottom = Num(Object, TEXT("bottom"));
		return Sides;
	}

	FFigmaPaint ParsePaint(const FJsonObjectPtr& Object)
	{
		FFigmaPaint Paint;
		Paint.Type = ParsePaintType(Str(Object, TEXT("type")));
		Paint.bVisible = Bool(Object, TEXT("visible"), true);
		Paint.Opacity = Num(Object, TEXT("opacity"), 1.f);
		Paint.BlendMode = Str(Object, TEXT("blendMode"), TEXT("NORMAL"));
		Paint.Color = ParseColor(Obj(Object, TEXT("color")));
		Paint.ScaleMode = Str(Object, TEXT("scaleMode"));
		for (const TSharedPtr<FJsonValue>& StopValue : Arr(Object, TEXT("stops")))
		{
			const FJsonObjectPtr Stop = StopValue.IsValid() ? StopValue->AsObject() : nullptr;
			if (Stop.IsValid())
			{
				FFigmaGradientStop& Out = Paint.Stops.AddDefaulted_GetRef();
				Out.Position = Num(Stop, TEXT("position"));
				Out.Color = ParseColor(Obj(Stop, TEXT("color")));
			}
		}
		return Paint;
	}

	void ParsePaints(const FJsonArray& Values, TArray<FFigmaPaint>& Out)
	{
		for (const TSharedPtr<FJsonValue>& Value : Values)
		{
			const FJsonObjectPtr Object = Value.IsValid() ? Value->AsObject() : nullptr;
			if (Object.IsValid())
			{
				Out.Add(ParsePaint(Object));
			}
		}
	}

	FFigmaEffect ParseEffect(const FJsonObjectPtr& Object)
	{
		FFigmaEffect Effect;
		Effect.Type = ParseEffectType(Str(Object, TEXT("type")));
		Effect.bVisible = Bool(Object, TEXT("visible"), true);
		Effect.Radius = Num(Object, TEXT("radius"));
		Effect.Color = ParseColor(Obj(Object, TEXT("color")));
		if (const FJsonObjectPtr Offset = Obj(Object, TEXT("offset")))
		{
			Effect.Offset = FVector2D(Num(Offset, TEXT("x")), Num(Offset, TEXT("y")));
		}
		Effect.Spread = Num(Object, TEXT("spread"));
		return Effect;
	}

	void ParseStyle(const FJsonObjectPtr& Object, FFigmaStyle& Style)
	{
		ParsePaints(Arr(Object, TEXT("fills")), Style.Fills);
		ParsePaints(Arr(Object, TEXT("strokes")), Style.Strokes);
		Style.StrokeWeight = Num(Object, TEXT("strokeWeight"));
		if (const FJsonObjectPtr Weights = Obj(Object, TEXT("strokeWeights")))
		{
			Style.bHasStrokeWeights = true;
			Style.StrokeWeights = ParseSides(Weights);
		}
		const FString Align = Str(Object, TEXT("strokeAlign"), TEXT("INSIDE"));
		Style.StrokeAlign = Align.Equals(TEXT("OUTSIDE"), ESearchCase::IgnoreCase) ? EFigmaStrokeAlign::Outside
			: Align.Equals(TEXT("CENTER"), ESearchCase::IgnoreCase) ? EFigmaStrokeAlign::Center
			: EFigmaStrokeAlign::Inside;
		Style.bDashed = Bool(Object, TEXT("dashed"));
		const FJsonArray& Radii = Arr(Object, TEXT("cornerRadius"));
		if (Radii.Num() == 4)
		{
			double R[4] = { 0, 0, 0, 0 };
			for (int32 Index = 0; Index < 4; ++Index)
			{
				if (Radii[Index].IsValid())
				{
					Radii[Index]->TryGetNumber(R[Index]);
				}
			}
			Style.CornerRadius = FVector4(R[0], R[1], R[2], R[3]);
		}
		Style.CornerSmoothing = Num(Object, TEXT("cornerSmoothing"));
		for (const TSharedPtr<FJsonValue>& Value : Arr(Object, TEXT("effects")))
		{
			const FJsonObjectPtr Effect = Value.IsValid() ? Value->AsObject() : nullptr;
			if (Effect.IsValid())
			{
				Style.Effects.Add(ParseEffect(Effect));
			}
		}
		Style.BlendMode = Str(Object, TEXT("blendMode"), TEXT("PASS_THROUGH"));
		Style.FillStyle = Str(Object, TEXT("fillStyle"));
		Style.StrokeStyle = Str(Object, TEXT("strokeStyle"));
		Style.EffectStyle = Str(Object, TEXT("effectStyle"));
		Style.bSimple = Bool(Object, TEXT("simple"));
	}

	void ParseText(const FJsonObjectPtr& Object, FFigmaText& Text)
	{
		Text.Characters = Str(Object, TEXT("characters"));
		Text.FontFamily = Str(Object, TEXT("fontFamily"), TEXT("Inter"));
		Text.FontStyle = Str(Object, TEXT("fontStyle"), TEXT("Regular"));
		Text.FontWeight = FMath::RoundToInt(Num(Object, TEXT("fontWeight"), 400.f));
		Text.bItalic = Bool(Object, TEXT("italic"));
		Text.FontSize = Num(Object, TEXT("fontSize"), 16.f);
		Text.Color = ParseColor(Obj(Object, TEXT("color")));

		static const TEnumEntry<EFigmaTextAlignH> AlignHTable[] = {
			{ TEXT("LEFT"), EFigmaTextAlignH::Left },
			{ TEXT("CENTER"), EFigmaTextAlignH::Center },
			{ TEXT("RIGHT"), EFigmaTextAlignH::Right },
			{ TEXT("JUSTIFIED"), EFigmaTextAlignH::Justified },
		};
		static const TEnumEntry<EFigmaTextAlignV> AlignVTable[] = {
			{ TEXT("TOP"), EFigmaTextAlignV::Top },
			{ TEXT("CENTER"), EFigmaTextAlignV::Center },
			{ TEXT("BOTTOM"), EFigmaTextAlignV::Bottom },
		};
		static const TEnumEntry<EFigmaTextAutoResize> ResizeTable[] = {
			{ TEXT("NONE"), EFigmaTextAutoResize::None },
			{ TEXT("WIDTH_AND_HEIGHT"), EFigmaTextAutoResize::WidthAndHeight },
			{ TEXT("HEIGHT"), EFigmaTextAutoResize::Height },
			{ TEXT("TRUNCATE"), EFigmaTextAutoResize::Truncate },
		};
		static const TEnumEntry<EFigmaTextCase> CaseTable[] = {
			{ TEXT("ORIGINAL"), EFigmaTextCase::Original },
			{ TEXT("UPPER"), EFigmaTextCase::Upper },
			{ TEXT("LOWER"), EFigmaTextCase::Lower },
			{ TEXT("TITLE"), EFigmaTextCase::Title },
			{ TEXT("SMALL_CAPS"), EFigmaTextCase::SmallCaps },
			{ TEXT("SMALL_CAPS_FORCED"), EFigmaTextCase::SmallCaps },
		};
		static const TEnumEntry<EFigmaTextDecoration> DecorationTable[] = {
			{ TEXT("NONE"), EFigmaTextDecoration::None },
			{ TEXT("UNDERLINE"), EFigmaTextDecoration::Underline },
			{ TEXT("STRIKETHROUGH"), EFigmaTextDecoration::Strikethrough },
		};
		Text.AlignH = Lookup(Str(Object, TEXT("alignH")), AlignHTable, EFigmaTextAlignH::Left);
		Text.AlignV = Lookup(Str(Object, TEXT("alignV")), AlignVTable, EFigmaTextAlignV::Top);
		Text.AutoResize = Lookup(Str(Object, TEXT("autoResize")), ResizeTable, EFigmaTextAutoResize::WidthAndHeight);
		Text.bTruncate = Bool(Object, TEXT("truncate"));
		Text.MaxLines = FMath::RoundToInt(Num(Object, TEXT("maxLines")));
		if (const FJsonObjectPtr Spacing = Obj(Object, TEXT("letterSpacing")))
		{
			Text.LetterSpacing = Num(Spacing, TEXT("value"));
			Text.LetterSpacingUnit = ParseUnit(Str(Spacing, TEXT("unit"), TEXT("PIXELS")));
		}
		if (const FJsonObjectPtr LineHeight = Obj(Object, TEXT("lineHeight")))
		{
			Text.LineHeight = Num(LineHeight, TEXT("value"));
			Text.LineHeightUnit = ParseUnit(Str(LineHeight, TEXT("unit"), TEXT("AUTO")));
		}
		Text.TextCase = Lookup(Str(Object, TEXT("textCase")), CaseTable, EFigmaTextCase::Original);
		Text.Decoration = Lookup(Str(Object, TEXT("decoration")), DecorationTable, EFigmaTextDecoration::None);
		Text.TextStyle = Str(Object, TEXT("textStyle"));
		if (const FJsonObjectPtr Stroke = Obj(Object, TEXT("stroke")))
		{
			Text.bHasStroke = true;
			Text.StrokeColor = ParseColor(Obj(Stroke, TEXT("color")));
			Text.StrokeWeight = Num(Stroke, TEXT("weight"));
		}
		if (const FJsonObjectPtr Shadow = Obj(Object, TEXT("shadow")))
		{
			Text.bHasShadow = true;
			Text.ShadowColor = ParseColor(Obj(Shadow, TEXT("color")));
			if (const FJsonObjectPtr Offset = Obj(Shadow, TEXT("offset")))
			{
				Text.ShadowOffset = FVector2D(Num(Offset, TEXT("x")), Num(Offset, TEXT("y")));
			}
			Text.ShadowRadius = Num(Shadow, TEXT("radius"));
		}
		Text.bMixed = Bool(Object, TEXT("mixed"));
		for (const TSharedPtr<FJsonValue>& Value : Arr(Object, TEXT("segments")))
		{
			const FJsonObjectPtr Segment = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!Segment.IsValid())
			{
				continue;
			}
			FFigmaTextSegment& Out = Text.Segments.AddDefaulted_GetRef();
			Out.Start = FMath::RoundToInt(Num(Segment, TEXT("start")));
			Out.End = FMath::RoundToInt(Num(Segment, TEXT("end")));
			Out.Characters = Str(Segment, TEXT("characters"));
			Out.FontFamily = Str(Segment, TEXT("fontFamily"));
			Out.FontStyle = Str(Segment, TEXT("fontStyle"));
			Out.FontWeight = FMath::RoundToInt(Num(Segment, TEXT("fontWeight"), 400.f));
			Out.FontSize = Num(Segment, TEXT("fontSize"), Text.FontSize);
			Out.Color = ParseColor(Obj(Segment, TEXT("color")), Text.Color);
		}
	}

	void ParseComponent(const FJsonObjectPtr& Object, FFigmaComponentRef& Ref)
	{
		const FString Kind = Str(Object, TEXT("kind"));
		Ref.Kind = Kind.Equals(TEXT("INSTANCE"), ESearchCase::IgnoreCase) ? EFigmaComponentKind::Instance
			: Kind.Equals(TEXT("COMPONENT"), ESearchCase::IgnoreCase) ? EFigmaComponentKind::Component
			: Kind.Equals(TEXT("COMPONENT_SET"), ESearchCase::IgnoreCase) ? EFigmaComponentKind::ComponentSet
			: EFigmaComponentKind::None;
		Ref.ComponentId = Str(Object, TEXT("componentId"));
		Ref.ComponentName = Str(Object, TEXT("componentName"));
		Ref.ComponentSetId = Str(Object, TEXT("componentSetId"));
		Ref.ComponentSetName = Str(Object, TEXT("componentSetName"));
		StringMap(Obj(Object, TEXT("variant")), Ref.Variant);
		Ref.bRemote = Bool(Object, TEXT("remote"));
		if (const FJsonObjectPtr Properties = Obj(Object, TEXT("properties")))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Properties->Values)
			{
				const FJsonObjectPtr Property = Pair.Value.IsValid() ? Pair.Value->AsObject() : nullptr;
				if (!Property.IsValid())
				{
					continue;
				}
				const FString Type = Str(Property, TEXT("type"));
				if (Type.Equals(TEXT("BOOLEAN"), ESearchCase::IgnoreCase))
				{
					Ref.BoolProperties.Add(Pair.Key, Bool(Property, TEXT("value")));
				}
				else if (Type.Equals(TEXT("TEXT"), ESearchCase::IgnoreCase))
				{
					Ref.TextProperties.Add(Pair.Key, Str(Property, TEXT("value")));
				}
			}
		}
	}

	FFigmaRasterRef ParseRaster(const FJsonObjectPtr& Object)
	{
		FFigmaRasterRef Ref;
		if (Object.IsValid())
		{
			Ref.AssetId = Str(Object, TEXT("asset"));
			Ref.Bounds = ParseRect(Obj(Object, TEXT("bounds")));
			Ref.Reason = Str(Object, TEXT("reason"));
		}
		return Ref;
	}

	TSharedPtr<FFigmaNode> ParseNode(const FJsonObjectPtr& Object, FFigmaNode* Parent, int32 Depth)
	{
		if (!Object.IsValid() || Depth > MaxDepth)
		{
			return nullptr;
		}

		TSharedPtr<FFigmaNode> Node = MakeShared<FFigmaNode>();
		Node->Parent = Parent;
		Node->Id = Str(Object, TEXT("id"));
		Node->Name = Str(Object, TEXT("name"));
		Node->CleanName = Str(Object, TEXT("cleanName"), Node->Name);
		StringMap(Obj(Object, TEXT("tags")), Node->Tags);
		Node->Type = ParseNodeType(Str(Object, TEXT("type")));
		Node->bVisible = Bool(Object, TEXT("visible"), true);
		Node->Opacity = Num(Object, TEXT("opacity"), 1.f);
		Node->Rotation = Num(Object, TEXT("rotation"));
		Node->X = Num(Object, TEXT("x"));
		Node->Y = Num(Object, TEXT("y"));
		Node->Width = Num(Object, TEXT("width"));
		Node->Height = Num(Object, TEXT("height"));

		if (const FJsonObjectPtr Constraints = Obj(Object, TEXT("constraints")))
		{
			Node->bHasConstraints = true;
			Node->ConstraintH = ParseConstraint(Str(Constraints, TEXT("horizontal")));
			Node->ConstraintV = ParseConstraint(Str(Constraints, TEXT("vertical")));
		}

		if (const FJsonObjectPtr Layout = Obj(Object, TEXT("layout")))
		{
			const FString Mode = Str(Layout, TEXT("mode"));
			Node->Layout.Mode = Mode.Equals(TEXT("HORIZONTAL"), ESearchCase::IgnoreCase) ? EFigmaLayoutMode::Horizontal
				: Mode.Equals(TEXT("VERTICAL"), ESearchCase::IgnoreCase) ? EFigmaLayoutMode::Vertical
				: EFigmaLayoutMode::None;
			Node->bHasLayout = Node->Layout.Mode != EFigmaLayoutMode::None;
			Node->Layout.bWrap = Bool(Layout, TEXT("wrap"));
			Node->Layout.Padding = ParseSides(Obj(Layout, TEXT("padding")));
			Node->Layout.ItemSpacing = Num(Layout, TEXT("itemSpacing"));
			Node->Layout.CounterAxisSpacing = Num(Layout, TEXT("counterAxisSpacing"));
			Node->Layout.PrimaryAlign = ParseAlign(Str(Layout, TEXT("primaryAlign"), TEXT("MIN")));
			Node->Layout.CounterAlign = ParseAlign(Str(Layout, TEXT("counterAlign"), TEXT("MIN")));
			Node->Layout.bReverseZIndex = Bool(Layout, TEXT("reverseZIndex"));
		}

		if (const FJsonObjectPtr Child = Obj(Object, TEXT("child")))
		{
			Node->bHasChildLayout = true;
			Node->Child.bAbsolute = Str(Child, TEXT("positioning")).Equals(TEXT("ABSOLUTE"), ESearchCase::IgnoreCase);
			Node->Child.SizingH = ParseSizing(Str(Child, TEXT("sizingH"), TEXT("FIXED")));
			Node->Child.SizingV = ParseSizing(Str(Child, TEXT("sizingV"), TEXT("FIXED")));
			Node->Child.Grow = Num(Child, TEXT("grow"));
			Node->Child.bStretch = Str(Child, TEXT("alignSelf")).Equals(TEXT("STRETCH"), ESearchCase::IgnoreCase);
		}

		if (const FJsonObjectPtr Limits = Obj(Object, TEXT("sizeLimits")))
		{
			double Value = 0.0;
			if (Limits->TryGetNumberField(TEXT("minWidth"), Value)) { Node->SizeLimits.MinWidth = static_cast<float>(Value); }
			if (Limits->TryGetNumberField(TEXT("maxWidth"), Value)) { Node->SizeLimits.MaxWidth = static_cast<float>(Value); }
			if (Limits->TryGetNumberField(TEXT("minHeight"), Value)) { Node->SizeLimits.MinHeight = static_cast<float>(Value); }
			if (Limits->TryGetNumberField(TEXT("maxHeight"), Value)) { Node->SizeLimits.MaxHeight = static_cast<float>(Value); }
		}

		Node->bClipsContent = Bool(Object, TEXT("clipsContent"));

		if (const FJsonObjectPtr Style = Obj(Object, TEXT("style")))
		{
			Node->bHasStyle = true;
			ParseStyle(Style, Node->Style);
		}
		if (const FJsonObjectPtr Text = Obj(Object, TEXT("text")))
		{
			Node->bHasText = true;
			ParseText(Text, Node->Text);
		}
		if (const FJsonObjectPtr Component = Obj(Object, TEXT("component")))
		{
			ParseComponent(Component, Node->Component);
		}
		Node->Image = ParseRaster(Obj(Object, TEXT("image")));
		Node->Background = ParseRaster(Obj(Object, TEXT("background")));
		StringMap(Obj(Object, TEXT("propRefs")), Node->PropRefs);
		Node->bIsMask = Bool(Object, TEXT("isMask"));

		if (HasArray(Object, TEXT("children")))
		{
			Node->bHasChildrenArray = true;
			for (const TSharedPtr<FJsonValue>& Value : Arr(Object, TEXT("children")))
			{
				const FJsonObjectPtr ChildObject = Value.IsValid() ? Value->AsObject() : nullptr;
				if (TSharedPtr<FFigmaNode> ChildNode = ParseNode(ChildObject, Node.Get(), Depth + 1))
				{
					Node->Children.Add(ChildNode);
				}
			}
		}
		return Node;
	}
}

bool FFigmaDocumentParser::Parse(const FString& JsonText, FFigmaDocument& OutDocument, FString& OutError)
{
	OutDocument = FFigmaDocument();

	FJsonObjectPtr Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = FString::Printf(TEXT("figmasync.json is not valid JSON (%s)."), *Reader->GetErrorMessage());
		return false;
	}

	const FString Format = Str(Root, TEXT("format"));
	if (!Format.Equals(TEXT("figmasync")))
	{
		OutError = TEXT("This JSON is not a figmasync bundle (missing \"format\": \"figmasync\"). Export it with the Widgetify Figma plugin.");
		return false;
	}

	OutDocument.Version = FMath::RoundToInt(Num(Root, TEXT("version")));
	if (OutDocument.Version > FigmaSync::SupportedBundleVersion)
	{
		OutError = FString::Printf(TEXT("This bundle uses format version %d, but this plugin reads up to version %d. Update the Widgetify plugin in Unreal."),
			OutDocument.Version, FigmaSync::SupportedBundleVersion);
		return false;
	}

	const FJsonObjectPtr Generator = Obj(Root, TEXT("generator"));
	OutDocument.GeneratorName = Str(Generator, TEXT("name"));
	OutDocument.GeneratorVersion = Str(Generator, TEXT("version"));
	OutDocument.ExportedAt = Str(Root, TEXT("exportedAt"));

	const FJsonObjectPtr Source = Obj(Root, TEXT("source"));
	OutDocument.FileName = Str(Source, TEXT("fileName"));
	OutDocument.PageName = Str(Source, TEXT("pageName"));
	OutDocument.FrameId = Str(Source, TEXT("frameId"));
	OutDocument.FrameName = Str(Source, TEXT("frameName"));

	const FJsonObjectPtr Options = Obj(Root, TEXT("options"));
	OutDocument.Options.Scale = Num(Options, TEXT("scale"), 2.f);
	OutDocument.Options.bIncludeHidden = Bool(Options, TEXT("includeHidden"), true);
	OutDocument.Options.bRasterizeUnsupported = Bool(Options, TEXT("rasterizeUnsupported"), true);
	for (const TSharedPtr<FJsonValue>& Value : Arr(Options, TEXT("variantPrefixes")))
	{
		FString Prefix;
		if (Value.IsValid() && Value->TryGetString(Prefix))
		{
			OutDocument.Options.VariantPrefixes.Add(Prefix);
		}
	}

	OutDocument.Root = ParseNode(Obj(Root, TEXT("root")), nullptr, 0);
	if (!OutDocument.Root.IsValid())
	{
		OutError = TEXT("The bundle has no root node.");
		return false;
	}
	if (OutDocument.FrameName.IsEmpty())
	{
		OutDocument.FrameName = OutDocument.Root->CleanName;
	}
	if (OutDocument.FrameId.IsEmpty())
	{
		OutDocument.FrameId = OutDocument.Root->Id;
	}

	if (const FJsonObjectPtr Components = Obj(Root, TEXT("components")))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Components->Values)
		{
			const FJsonObjectPtr SetObject = Pair.Value.IsValid() ? Pair.Value->AsObject() : nullptr;
			if (!SetObject.IsValid())
			{
				continue;
			}
			FFigmaComponentSet Set;
			Set.Id = Str(SetObject, TEXT("id"), Pair.Key);
			Set.Name = Str(SetObject, TEXT("name"));
			Set.bRemote = Bool(SetObject, TEXT("remote"));
			for (const TSharedPtr<FJsonValue>& NameValue : Arr(SetObject, TEXT("propertyNames")))
			{
				FString PropertyName;
				if (NameValue.IsValid() && NameValue->TryGetString(PropertyName))
				{
					Set.PropertyNames.Add(PropertyName);
				}
			}
			for (const TSharedPtr<FJsonValue>& VariantValue : Arr(SetObject, TEXT("variants")))
			{
				const FJsonObjectPtr VariantObject = VariantValue.IsValid() ? VariantValue->AsObject() : nullptr;
				if (!VariantObject.IsValid())
				{
					continue;
				}
				FFigmaVariant Variant;
				Variant.Id = Str(VariantObject, TEXT("id"));
				Variant.Name = Str(VariantObject, TEXT("name"));
				StringMap(Obj(VariantObject, TEXT("properties")), Variant.Properties);
				Variant.Node = ParseNode(Obj(VariantObject, TEXT("node")), nullptr, 0);
				if (Variant.Node.IsValid())
				{
					Set.Variants.Add(MoveTemp(Variant));
				}
			}
			OutDocument.Components.Add(Pair.Key, MoveTemp(Set));
		}
	}

	for (const TSharedPtr<FJsonValue>& Value : Arr(Root, TEXT("assets")))
	{
		const FJsonObjectPtr AssetObject = Value.IsValid() ? Value->AsObject() : nullptr;
		if (!AssetObject.IsValid())
		{
			continue;
		}
		FFigmaAsset& Asset = OutDocument.Assets.AddDefaulted_GetRef();
		Asset.Id = Str(AssetObject, TEXT("id"));
		Asset.Path = Str(AssetObject, TEXT("path"));
		Asset.Width = FMath::RoundToInt(Num(AssetObject, TEXT("width")));
		Asset.Height = FMath::RoundToInt(Num(AssetObject, TEXT("height")));
		Asset.Scale = Num(AssetObject, TEXT("scale"), 1.f);
		Asset.Hash = Str(AssetObject, TEXT("hash"));
		Asset.Bytes = static_cast<int64>(Num(AssetObject, TEXT("bytes")));
	}

	for (const TSharedPtr<FJsonValue>& Value : Arr(Root, TEXT("warnings")))
	{
		const FJsonObjectPtr WarningObject = Value.IsValid() ? Value->AsObject() : nullptr;
		if (WarningObject.IsValid())
		{
			FFigmaWarning& Warning = OutDocument.Warnings.AddDefaulted_GetRef();
			Warning.NodeId = Str(WarningObject, TEXT("nodeId"));
			Warning.NodePath = Str(WarningObject, TEXT("nodePath"));
			Warning.Message = Str(WarningObject, TEXT("message"));
		}
	}
	return true;
}
