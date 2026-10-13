#include "FigmaStyleConverter.h"
#include "Engine/Texture2D.h"
#include "FigmaSyncStylePreset.h"

namespace
{
	FLinearColor ScaleColor(const FLinearColor& Color, float Brightness, float OpacityScale)
	{
		return FLinearColor(
			FMath::Clamp(Color.R * Brightness, 0.f, 1.f),
			FMath::Clamp(Color.G * Brightness, 0.f, 1.f),
			FMath::Clamp(Color.B * Brightness, 0.f, 1.f),
			FMath::Clamp(Color.A * OpacityScale, 0.f, 1.f));
	}
}

bool FFigmaStyleConverter::HasVisibleBox(const FFigmaNode& Node)
{
	return Node.bHasStyle && (Node.Style.HasVisibleFill() || Node.Style.HasVisibleStroke());
}

FSlateBrush FFigmaStyleConverter::MakeShapeBrush(const FFigmaNode& Node, const UFigmaSyncStylePreset* Preset)
{
	if (Preset && Node.bHasStyle)
	{
		if (const FSlateBrush* Override = Preset->FindPaintStyle(Node.Style.FillStyle))
		{
			FSlateBrush Brush = *Override;
			Brush.SetImageSize(FVector2D(FMath::Max(Node.Width, 1.f), FMath::Max(Node.Height, 1.f)));
			return Brush;
		}
	}

	FSlateBrush Brush;
	const FFigmaStyle& Style = Node.Style;
	const FFigmaPaint* Fill = Node.bHasStyle ? Style.FindVisibleFill() : nullptr;
	const FFigmaPaint* Stroke = Node.bHasStyle && Style.HasVisibleStroke() ? Style.FindVisibleStroke() : nullptr;
	if (!Fill && !Stroke)
	{
		Brush.DrawAs = ESlateBrushDrawType::NoDrawType;
		return Brush;
	}

	Brush.SetImageSize(FVector2D(FMath::Max(Node.Width, 1.f), FMath::Max(Node.Height, 1.f)));
	Brush.TintColor = FSlateColor(Fill ? Fill->ToLinearColor() : FLinearColor::Transparent);

	const bool bEllipse = Node.Type == EFigmaNodeType::Ellipse;
	if (bEllipse || Style.HasCornerRadius() || Stroke)
	{
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.OutlineSettings.RoundingType = bEllipse ? ESlateBrushRoundingType::HalfHeightRadius : ESlateBrushRoundingType::FixedRadius;
		Brush.OutlineSettings.CornerRadii = Style.CornerRadius;
		if (Stroke)
		{
			Brush.OutlineSettings.Color = FSlateColor(Stroke->ToLinearColor());
			Brush.OutlineSettings.Width = Style.StrokeWeight;
		}
		else
		{
			Brush.OutlineSettings.Color = FSlateColor(FLinearColor::Transparent);
			Brush.OutlineSettings.Width = 0.f;
		}
	}
	else
	{
		// A resource-less Image brush draws a flat quad in the tint color: cheaper than a rounded box.
		Brush.DrawAs = ESlateBrushDrawType::Image;
	}
	return Brush;
}

FSlateBrush FFigmaStyleConverter::MakeLineBrush(const FFigmaNode& Node)
{
	FSlateBrush Brush;
	const FFigmaPaint* Stroke = Node.bHasStyle ? Node.Style.FindVisibleStroke() : nullptr;
	if (!Stroke)
	{
		Brush.DrawAs = ESlateBrushDrawType::NoDrawType;
		return Brush;
	}
	Brush.DrawAs = ESlateBrushDrawType::Image;
	Brush.TintColor = FSlateColor(Stroke->ToLinearColor());
	Brush.SetImageSize(FVector2D(FMath::Max(Node.Width, 1.f), FMath::Max(Node.Style.StrokeWeight, 1.f)));
	return Brush;
}

FSlateBrush FFigmaStyleConverter::MakeImageBrush(UTexture2D* Texture, const FVector2D& SizeInSlateUnits, float SliceMargin)
{
	FSlateBrush Brush;
	Brush.SetResourceObject(Texture);
	Brush.SetImageSize(FVector2D(FMath::Max(SizeInSlateUnits.X, 1.0), FMath::Max(SizeInSlateUnits.Y, 1.0)));
	Brush.TintColor = FSlateColor(FLinearColor::White);
	if (SliceMargin > 0.f && SizeInSlateUnits.X > 0 && SizeInSlateUnits.Y > 0)
	{
		// Box margins are in UV space: a fraction of the image size.
		const float U = FMath::Clamp(SliceMargin / static_cast<float>(SizeInSlateUnits.X), 0.f, 0.5f);
		const float V = FMath::Clamp(SliceMargin / static_cast<float>(SizeInSlateUnits.Y), 0.f, 0.5f);
		Brush.DrawAs = ESlateBrushDrawType::Box;
		Brush.Margin = FMargin(U, V, U, V);
	}
	else
	{
		Brush.DrawAs = ESlateBrushDrawType::Image;
	}
	return Brush;
}

FSlateBrush FFigmaStyleConverter::ScaleBrush(const FSlateBrush& Brush, float Brightness, float OpacityScale)
{
	FSlateBrush Out = Brush;
	Out.TintColor = FSlateColor(ScaleColor(Brush.TintColor.GetSpecifiedColor(), Brightness, OpacityScale));
	if (Out.DrawAs == ESlateBrushDrawType::RoundedBox)
	{
		Out.OutlineSettings.Color = FSlateColor(ScaleColor(Brush.OutlineSettings.Color.GetSpecifiedColor(), Brightness, OpacityScale));
	}
	return Out;
}

FMargin FFigmaStyleConverter::ToMargin(const FFigmaPadding& Padding)
{
	return FMargin(Padding.Left, Padding.Top, Padding.Right, Padding.Bottom);
}
