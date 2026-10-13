# Naming & tags

Widgetify reads intent from layer names, so designers never have to touch Unreal. There are two mechanisms:

- **Prefixes** say what a layer *is*, e.g. `Btn_Play`. They become the widget name that programmers reference.
- **Tags** in square brackets say *how to export it*, e.g. `[img 3x] Logo`. Tags are removed from the widget name.

You can put tags anywhere in the name, and one name can hold several: `[img 2x, preset=Background] Hero_Art`.
Tag names are case-insensitive.

## Prefixes

These are the defaults. You can change them in **Project Settings → Plugins → Widgetify (Figma Sync) → Naming**.

| Prefix | Meaning |
| --- | --- |
| `Btn_` | Clickable. The frame becomes a `Button`: its fill, stroke and radius become the button style, and its children become the content. Exposed as a variable. |
| `Txt_` | Text that code changes, e.g. `Txt_PlayerName`. Exposed as a variable. |
| `Img_`, `Ico_` | Images and icons that code swaps. Exposed as variables. |
| `Pnl_`, `Box_`, `Lst_`, `Bar_`, `Inp_` | Panels, boxes, lists, progress bars and inputs that code reaches. Exposed as variables. |
| `Act_` | An input glyph: a `CommonActionWidget` (needs CommonUI) bound with `[action=...]`. |

Layers without a known prefix are still built. They just aren't exposed as variables, which keeps the
Blueprint's variable list clean.

Widget names are sanitized for Unreal: spaces and symbols become `_`, and duplicates get `_2`, `_3`, and so on. Once
a widget exists it keeps its name, even if the layer is renamed in Figma, so graph references don't break.

## Tags

| Tag | Effect |
| --- | --- |
| `[img]` | Rasterize this layer and its children into one PNG, shown as an `Image`. Use it for art that UMG can't express. |
| `[img 3x]` (also `[img@3x]`) | Rasterize at 3× instead of the export scale. The scale is limited to 8×. |
| `[img:Preset]` / `[preset=Preset]` | Import the texture with a named texture preset (sRGB, compression, mips, LOD group). The defaults are `UI`, `Background` and `Linear`. |
| `[slice=16]` | Draw the image as a 9-slice box with 16 px margins, so panels stretch without distortion. |
| `[skip]` (also `[ignore]`) | Leave the layer out completely. Use it for designer notes, guides and redlines. |
| `[focus]` | This widget gets initial gamepad/keyboard focus. Sets the screen's Desired Focus Widget. |
| `[action=Back]` | Bind a CommonUI input action, either a row in the input action data table or an Enhanced Input action named `IA_Back`. |
| `[var]` / `[novar]` | Always / never tick *Is Variable*. |
| `[button]` / `[nobutton]` | Make a frame a button even without the `Btn_` prefix, or stop it from becoming one. |
| `[overlay]` | Use an `Overlay` for the children (stacked on top of each other) instead of a canvas or box. |
| `[canvas]` | Use a `CanvasPanel` even when the frame has auto-layout. |
| `[widget]` (on the exported frame) | Create a plain `UserWidget` instead of a `CommonActivatableWidget` screen. Use it for HUD pieces and reusable parts. |
| `[actionwidget]` | Make the layer an action widget without the `Act_` prefix. |

### What gets rasterized automatically

When **Rasterize gradients, shadows, masks** is on (the default), the exporter rasterizes anything UMG can't draw
natively: vectors and boolean shapes, gradients, image fills, drop shadows and blurs, masks, and flipped layers.

A frame whose *own background* needs rasterizing (for example, a panel with a gradient) only rasterizes the
background. Its children are still built as real widgets.

## Buttons from components

A component set whose name starts with `Btn/` or `Button/` is treated as a **button family**. Both the Figma
exporter and Project Settings control these prefixes.

```
Btn/Primary            ← component set
  State=Default        ← variants
  State=Hover
  State=Pressed
  State=Disabled
  State=Selected       (optional)
```

Widgetify reads the state from a variant property named `State` (or `Status`, `Interaction` …), with values like
`Default`, `Normal`, `Rest`, `Hover`, `Hovered`, `Pressed`, `Down`, `Active`, `Disabled`, `Selected` and `Focused`.
Other properties, such as `Size=Large` or `Type=Ghost`, define **style groups**: each group gets its own style asset.

Plain components named `Btn/Primary/Hover`, `Btn/Primary/Pressed` … work too. They are grouped into one family.

With CommonUI enabled, each family generates:

| Asset | Contents |
| --- | --- |
| `WBP_Btn_Primary` | A `CommonButtonBase` Widget Blueprint whose content is built from the Normal variant. The label is exposed. |
| `BS_Btn_Primary[_Group]` | A `CommonButtonStyle`: normal, hovered, pressed, disabled and selected brushes from the variants, plus padding. |
| `TS_Btn_Primary[_Group]_Normal` … | `CommonTextStyle`s for the label in each state. |

The generated assets go to `/Game/UI/Figma/Components` (`Styles/` and `Textures/` subfolders). Every screen
that uses the family shares them.

Each instance on a screen gets the right style for its group. It also gets its label text: from a text component
property such as `Label`, or else from the instance's text layer. A `State=Disabled` instance is created disabled.

When a state variant is missing, it's derived from Normal: hover is brighter, pressed is darker and disabled is
faded. You can tune these amounts in a style preset. If you'd rather hand-author a style, map the family to your own
`CommonButtonStyle` in the preset's **Button Styles**.

Without CommonUI, `Btn_*` frames and `Btn/*` instances become plain UMG `Button`s, styled from the layer.

## Input actions

`[action=Confirm]` is resolved in this order:

1. A row named `Confirm` in the **Input Action Table** (Project Settings → Widgetify → CommonUI). This is a CommonUI
   input action data table.
2. An Enhanced Input action asset named `IA_Confirm`. The prefix can be changed in Project Settings.

When neither exists, the lint report lists the action as unbound. Nothing fails silently.

## Fonts

Figma fonts are matched in this order:

1. The style preset's font table.
2. The project's font table (Project Settings → Widgetify → Fonts).
3. A font asset whose name matches the family. The lint report suggests adding an explicit mapping.
4. The fallback font. Logged as "unmapped".

Typefaces are picked by style name ("SemiBold Italic") or by weight. Font sizes are converted from px to Slate
points (× 0.75).
