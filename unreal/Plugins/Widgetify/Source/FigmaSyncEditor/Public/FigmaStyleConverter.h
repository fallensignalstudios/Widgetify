#pragma once

#include "CoreMinimal.h"
#include "FigmaDocument.h"
#include "Layout/Margin.h"
#include "Styling/SlateBrush.h"

class UTexture2D;
class UFigmaSyncStylePreset;

/** Converts Figma paint into Slate brushes, colors and margins. */
struct FIGMASYNCEDITOR_API FFigmaStyleConverter
{
	/** True when the node's own style draws anything (a visible fill or stroke). */
	static bool HasVisibleBox(const FFigmaNode& Node);

	/**
	 * Rounded-box brush for a simple style: fill as tint, stroke as outline, corner radii (TL, TR, BR, BL).
	 * Ellipses use half-height rounding. Returns a NoDrawType brush when nothing is visible.
	 * A preset paint-style brush (by Figma fill style name) wins when present.
	 */
	static FSlateBrush MakeShapeBrush(const FFigmaNode& Node, const UFigmaSyncStylePreset* Preset = nullptr);

	/** Solid brush for a Figma line (its stroke color), sized length x weight. */
	static FSlateBrush MakeLineBrush(const FFigmaNode& Node);

	/** Image brush for an imported PNG, sized in Slate units. SliceMargin > 0 draws as a 9-slice box. */
	static FSlateBrush MakeImageBrush(UTexture2D* Texture, const FVector2D& SizeInSlateUnits, float SliceMargin = 0.f);

	/** The brush with its tint and outline scaled (hover/pressed) and its opacity multiplied (disabled). */
	static FSlateBrush ScaleBrush(const FSlateBrush& Brush, float Brightness, float OpacityScale = 1.f);

	static FMargin ToMargin(const FFigmaPadding& Padding);
};
