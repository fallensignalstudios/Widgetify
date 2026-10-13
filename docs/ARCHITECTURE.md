# Architecture

Widgetify has two halves joined by a file format:

```
Figma plugin (TypeScript)  ──►  figmasync bundle (zip / folder / HTTP push)  ──►  Unreal editor plugin (C++)
   reads the design              figmasync.json + images/*.png                     builds Widget Blueprints
```

**Rule:** Figma owns the look; Unreal owns the behavior. Everything below follows from that. Imports write the look
(layout, brushes, fonts, text, art) and never write graphs, bindings or animations.

**In scope (v1):** layout, text, colors, borders, images, buttons, naming-based bindings, CommonUI hooks, re-sync,
lint and live push.

**Out of scope (v1):** animations, prototyping flows, complex vector effects (they are rasterized instead) and gameplay
logic.

## 1. Figma plugin (`figma-plugin/`)

The plugin uses Figma's Plugin API from inside the editor, never the REST API, so there are no tokens and no rate
limits. It runs with `documentAccess: "dynamic-page"` and only uses the async APIs (`getMainComponentAsync`,
`getStyleByIdAsync`, `loadAsync`).

| File | Role |
| --- | --- |
| `src/shared/bundle.ts` | **The bundle format.** This is the single source of truth: the C++ model mirrors it field for field, and the JSON Schema documents it. |
| `src/shared/messages.ts` | Messages between the plugin's main thread and its UI iframe; export defaults. |
| `src/main/walker.ts` | `FrameExporter`: walks the frame and decides what to rasterize, writes nodes, and collects component variants. Figma access goes through the `ExportHost` interface, so the whole exporter runs under unit tests with a mock host. |
| `src/main/code.ts` | The real `ExportHost`: `exportAsync` PNGs, background-only rasters, visibility toggling, remembered options. |
| `src/main/tags.ts`, `geometry.ts`, `paint.ts`, `hash.ts` | Tag parsing, affine math (rotation and flips into box + angle), paint classification, image dedupe. |
| `src/ui/` | The UI: builds the zip (stored, CRC-checked), downloads it, or POSTs it to the editor; pings the editor every 4 s to show whether live push is available. |

### Export decisions

- **Geometry.** Each node gets its unrotated layout box relative to its parent, plus a clockwise rotation about the box
  center. That is exactly a UMG render transform with pivot (0.5, 0.5), so rotated layers land where Figma shows them.
- **Rasterize or rebuild?** A node is rasterized when it is tagged `[img]`, or when UMG can't draw it natively:
  vectors and boolean shapes, gradients, image fills, effects, masks, and flips. When only a container's *own* paint
  is unsupported (say, a gradient panel), just its background is rasterized: the exporter renders a temporary
  rectangle with the same paints and corner radii. Its children stay real widgets.
- **Simple styles stay vector.** Stacked solid fills are pre-composited. Anything expressible as one Slate rounded box
  is marked `simple: true` and becomes a brush instead of a PNG.
- **Images are deduped** by content hash plus bytes, so repeated art ships once.
- **Variants.** For component sets whose name starts with a button prefix (`Btn/`, `Button/`), every variant is
  exported, so Unreal can build hover/pressed/disabled styles. Plain components named `Btn/Primary/Hover` are grouped
  into a *virtual* set.

## 2. The bundle (`figmasync.json` + `images/`)

The bundle is a ZIP (stored entries; the Unreal reader also accepts deflate), a folder, or the same zip POSTed
to the editor. See [SCHEMA.md](SCHEMA.md) and [`schema/figmasync.schema.json`](../schema/figmasync.schema.json).

Conventions: lengths in Figma px (1 px = 1 Slate unit at the design resolution), colors in sRGB 0..1, and angles
clockwise. Each node carries its Figma id, which is the key for re-sync.

## 3. Unreal plugin (`unreal/Plugins/Widgetify/`)

All three modules are `Editor` modules: nothing is cooked or shipped.

```
FigmaSyncCore         Core, Json                    bundle model, JSON parser, zip reader, naming helpers, tests
   ▲
FigmaSyncEditor       UMG, UMGEditor, UnrealEd, …   importer, widget builder, re-sync, textures, fonts, lint,
   ▲                                                Figma Sync panel, toolbar/menus, live push server
FigmaSyncCommonUI     (no CommonUI link dep)        IFigmaWidgetMapper for CommonUI buttons, styles, action widgets
```

### FigmaSyncCore

This module has no editor dependencies, so it can be unit-tested (`FigmaSync.Core.*` automation tests).

- `FFigmaDocument` and friends: the in-memory model. Colors convert sRGB → linear on demand.
- `FFigmaDocumentParser`: tolerant JSON → model. It warns on unknown values and never asserts.
- `FFigmaZipReader`: central-directory zip reader. It handles stored and raw-deflate entries and checks CRCs.
- `FFigmaBundle`: opens a zip, a folder, or a bare `figmasync.json`, and reads images by bundle-relative path. Paths
  that escape the bundle are rejected.

### FigmaSyncEditor

The import pipeline (`FFigmaImporter::Import`):

1. **Target.** Use the given Widget Blueprint, or the one at `<Destination>/WBP_<Frame>`, or create it.
2. **Look.** Pick the style preset: the explicit one, else the Blueprint's last one, else the project default.
3. **Shared assets.** Mappers prepare first (CommonUI buttons and styles), because the screen references them.
4. **Create** the Blueprint if needed. The parent class comes from the mappers (`CommonActivatableWidget`) or is
   `UserWidget`.
5. **Textures** are imported outside the undo transaction. A PNG whose MD5 matches the one stored in the texture's
   `AssetImportData` is skipped.
6. **Widget tree** is built inside one `FScopedTransaction` by `FFigmaWidgetBuilder`.
7. **Compile**, then set class defaults (Desired Focus Widget from `[focus]`).
8. **Lint** (`FFigmaLinter` plus each mapper's `Lint`).
9. **Save**, then write the log to `Saved/FigmaSync/Logs` and mirror it to the *Figma Sync* Message Log page.

### Re-sync design

The re-sync state lives inside the Widget Blueprint, in `UFigmaSyncBlueprintExtension` (a
`UWidgetBlueprintExtension`, editor-only data). It holds:

- the source bundle path, frame id, last sync time and style preset;
- the **lock list**;
- `Nodes`: a map from **Figma node id → `FFigmaSyncNodeRecord`**. A record holds:
  - the *named widget*, the one designers and code reference;
  - its *helper widgets* by role.

| Role | Helper | When |
| --- | --- | --- |
| `Size` | `SizeBox` | Fixed sizes inside boxes, size limits, aligned fixed-height text. |
| `Layout` | Panel inside the `Border` | Frames with their own fill, stroke or radius. |
| `Stack` + `Bg` | `Overlay` + `Image` | A rasterized background that overflows the box (shadows). |
| `Flow` + `Abs` | Box + `CanvasPanel` | An auto-layout frame with absolutely positioned children. |
| `SpacerN` | `Spacer` | Space-between distribution. |

Helpers get deterministic names (`Pnl_Menu_Layout`, `Btn_Play_Size` …). Because helpers are tracked separately from
the named widget, the frame's structure can change without anything that references the named widget breaking:

- **Wrapper adoption.** A frame that gains a fill keeps its panel, which is renamed `_Layout`, and a new `Border`
  takes over the name. A frame that loses its fill hands the name back to its panel.
- **Class changes.** A layer whose class changes (for example, a Text that becomes an Image) gets a new widget with
  the same name. The old one is renamed `_Replaced` and removed. Hand-added children move to the new panel.
- **Relink.** If someone renamed a widget in Unreal, the record's stored class and parent are used to find it again.
- **Lookup** is done with `FindObject<UWidget>(WidgetTree, Name)`, because `UWidgetTree::FindWidget` only walks from
  the root, which misses widgets that are mid-reparent.
- **Deletion** means `RemoveWidget`, then renaming the widget into the transient package so its name is free again.
- **Hand-added widgets** are kept, ordered after the Figma children of their panel. Widgets for layers deleted in
  Figma are kept and flagged, unless *Keep Removed Widgets* is off.
- **Never touched:** event graphs, function graphs, property bindings, animations, and "Is Variable" on widgets that
  someone exposed by hand.

### Editor UX

- **Figma Sync panel** (Tools → Figma Sync, a nomad tab):
  - open a `.zip` or folder, drag & drop, or **Try sample**;
  - a layer → widget preview tree;
  - pick the target or create a new Blueprint, with parent class and style preset;
  - re-sync from the last bundle;
  - **Save look as preset**;
  - the import log and lint report.
- **Widget Blueprint editor toolbar:** *Figma Sync* (re-sync) and *Figma Panel*. These are added through UToolMenus
  `AssetEditor.WidgetBlueprintEditor.ToolBar`, because the Widget Designer ignores `FAssetEditorExtender` delegates.
- **Content Browser:** right-click a Widget Blueprint → *Figma Sync: Re-sync* / *Open Panel*.
- **Style presets** (`UFigmaSyncStylePreset` data assets) hold "the game's look" once:
  - font mappings;
  - Figma paint-style → brush;
  - Figma text-style → font/color/CommonTextStyle;
  - button style overrides;
  - hover/pressed/disabled factors;
  - parent classes and the texture preset.

### FigmaSyncCommonUI

CommonUI's classes are `MinimalAPI` and the plugin is optional, so this module **never links against CommonUI**. It
works through class paths (`/Script/CommonUI.CommonButtonStyle`) and reflected property names
(`NormalBase`, `NormalHovered`, `TriggeringInputAction` …). Projects without CommonUI compile fine and get plain
UMG output.

It registers an `IFigmaWidgetMapper`, the plugin's extension point:

| Hook | CommonUI behavior |
| --- | --- |
| `PrepareImport` | For each `Btn/*` family on the screen:<br>• builds `WBP_<Family>` (a `CommonButtonBase` whose content comes from the Normal variant);<br>• builds a `BS_` style per variant group (brushes from Hover/Pressed/Disabled/Selected variants, or derived from Normal);<br>• builds `TS_` text styles per state;<br>• wires a `ButtonText` variable into the label (a small PreConstruct graph, marked "Widgetify", added once). |
| `ChooseScreenParentClass` | `CommonActivatableWidget` (configurable; `[widget]` opts out). |
| `ChooseLeafClass` | The generated button class for `Btn/*` instances; `CommonActionWidget` for `Act_*`. |
| `ChooseTextClass` | `CommonTextBlock` for button labels, and for text styles that the preset maps to a `CommonTextStyle`. |
| `ConfigureWidget` | Per-instance style group, label text, disabled state, `[action=...]` binding (a data-table row or an Enhanced Input `IA_*` asset). |
| `Lint` | Families whose states had to be derived; labels that can't be applied. |

Studios can register their own mapper, for example to use project-specific button classes, with
`IFigmaSyncEditorModule::Get().RegisterWidgetMapper(...)`.

### Live push (optional)

`FFigmaLiveServer` uses the engine's HttpServer module on `localhost:<LivePushPort>` (default 7820):

- `GET /figmasync/ping`: the plugin uses it to show "Unreal is listening".
- `POST /figmasync/push?frame=<name>&id=<frameId>`: the body is the bundle zip.
- `OPTIONS` handlers and CORS headers on both routes, because the plugin UI runs on a `null`/figma.com origin.

Pushes are saved to `Saved/FigmaSync/Inbox` and handled one per editor tick, on the game thread. If the frame id is
bound to a Widget Blueprint (the binding is made the first time you import that frame) and *Auto Apply Pushes* is on,
the push re-syncs that Blueprint at once. Otherwise it opens in the panel for review.

On the Figma side, localhost is allowed through `networkAccess.devAllowedDomains`, which only works for
development plugins. A published plugin would need `allowedDomains`.

## 4. Engine compatibility (UE 5.3–5.7)

Version differences are handled at compile time with `UE_VERSION_OLDER_THAN`, or by reflection where a setter
doesn't exist in older engines:

| Difference | Handling |
| --- | --- |
| HttpServer `FHttpRequestHandler` is a `TFunction` up to 5.3 and a `TDelegate` from 5.4. | Gated in `FigmaLiveServer.cpp`. |
| `UWidgetBlueprint::OnVariableAdded`/`OnVariableRemoved` and `WidgetVariableNameToGuidMap` exist only from 5.6. | Gated in `FigmaWidgetBuilder.cpp`. |
| CommonUI only falls back to Desired Focus Widget on activatable widgets from 5.5. | The linter tells you to implement *Get Desired Focus Target* before 5.5. |
| `UTextBlock` `WrapTextAt` / `LineHeightPercentage` setters exist only from 5.4. | Set by reflection. |
| `FCompression::UncompressMemory` takes `int32` sizes in 5.3/5.4 and `int64` from 5.5. | `int32` arguments, which convert either way. |

## 5. Decisions on the original open questions

| Question | Decision |
| --- | --- |
| UE version | 5.3–5.7 from one codebase, with version gates. |
| C++ vs Blueprint project | The plugin is C++, so a Blueprint-only project needs one C++ class to compile it. Everything it generates is Blueprint. |
| CommonUI base | Optional and auto-detected. Screens are `CommonActivatableWidget`s and buttons `CommonButtonBase`s; both are configurable, and both fall back to UMG when CommonUI is absent. |
| Target resolution | Design at **1920×1080**: 1 px = 1 Slate unit. Configurable; the linter flags frames that don't match. |
| Figma plan | Any. Development plugins run from a local manifest in the desktop app, and no REST API is used. |

## 6. Testing

- `figma-plugin`: vitest suites for:
  - tags;
  - geometry (rotation and flips);
  - paint classification;
  - the zip writer and reader;
  - the whole exporter against a mock Figma document;
  - plus a deterministic sample bundle (`samples/MainMenu.figmasync.zip`, also copied into the Unreal plugin's
    `Resources/Samples`).

  Run `npm run check`.
- `FigmaSyncCore`: automation tests (`FigmaSync.Core.*`) for:
  - parsing the format;
  - colors;
  - naming;
  - zip reading, with embedded stored and deflated archives cross-checked against the TypeScript writer and Python's
    `zipfile`;
  - path-traversal rejection.
