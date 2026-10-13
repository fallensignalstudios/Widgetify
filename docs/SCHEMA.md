# The figmasync bundle (format version 1)

A bundle is what the Figma plugin produces and the Unreal plugin consumes:

```
MainMenu.figmasync.zip
├── figmasync.json        the document described below
└── images/
    ├── Bg_Gradient.png   rasterized art, referenced from assets[]
    └── Logo.png
```

The Unreal importer accepts the zip (stored or deflated entries, optionally wrapping a single folder), an extracted
folder, or the `figmasync.json` path itself.

- **Source of truth:** [`figma-plugin/src/shared/bundle.ts`](../figma-plugin/src/shared/bundle.ts)
- **JSON Schema:** [`schema/figmasync.schema.json`](../schema/figmasync.schema.json)
- **C++ mirror:** `FigmaSyncCore/Public/FigmaDocument.h`

## Conventions

| Thing | Convention |
| --- | --- |
| Lengths | Figma px. 1 px = 1 Slate unit at the design resolution (default 1920×1080). |
| Node box | `x`, `y`, `width`, `height` describe the **unrotated** layout box, relative to the parent's box. |
| Rotation | Degrees, **clockwise**, about the box center (a UMG render transform with pivot 0.5, 0.5). |
| Colors | sRGB floats 0..1 (`{ r, g, b, a }`). Paint opacity is kept separate from color alpha. Unreal converts to linear. |
| Corner radii | `[topLeft, topRight, bottomRight, bottomLeft]`, the Slate `CornerRadii` order. |
| Ids | Figma node ids (`"38:1"`). They are stable across edits and are the re-sync key. |
| Names | `name` is the raw layer name; `cleanName` has `[tags]` removed; `tags` holds the parsed tags with lower-case keys. |

## Top level

```jsonc
{
  "format": "figmasync",
  "version": 1,
  "generator": { "name": "widgetify-figma", "version": "0.1.0" },
  "exportedAt": "2026-10-13T12:00:00.000Z",
  "source": { "fileName": "Sovereign Call UI", "pageName": "Menus", "frameId": "38:1", "frameName": "MainMenu" },
  "options": { "scale": 2, "includeHidden": true, "rasterizeUnsupported": true, "variantPrefixes": ["Btn/", "Button/"] },
  "root": { /* node: the exported frame */ },
  "components": { "9:1": { /* component set: Btn/Primary and its variants */ } },
  "assets": [ { "id": "img1", "path": "images/Bg_Gradient.png", "width": 1920, "height": 1080, "scale": 1, "hash": "c193a9072b8f652e", "bytes": 70215 } ],
  "warnings": [ { "nodeId": "41:7", "nodePath": "MainMenu/Emblem", "message": "Layer is mirrored; UMG render transforms cannot flip, so it was rasterized." } ]
}
```

An importer must reject bundles whose `version` is higher than it supports. Unknown fields are ignored, so new
optional fields don't need a version bump.

## Nodes

Every node has `id`, `name`, `cleanName`, `tags`, `type`, `visible`, `opacity`, `rotation`, `x`, `y`, `width` and
`height`. The following fields are optional; each one appears only when it applies:

| Field | When | Meaning |
| --- | --- | --- |
| `constraints` | The parent isn't auto-layout, or this child is absolutely positioned. | `{ horizontal, vertical }`: `MIN`, `MAX`, `CENTER`, `STRETCH` or `SCALE`. These become canvas anchors. Children of groups get `SCALE`. |
| `layout` | Auto-layout frames. | `mode` (`HORIZONTAL`/`VERTICAL`), `wrap`, `padding`, `itemSpacing`, `counterAxisSpacing`, `primaryAlign`, `counterAlign`, `reverseZIndex`. |
| `child` | The parent is auto-layout. | `positioning` (`AUTO`/`ABSOLUTE`), `sizingH`/`sizingV` (`FIXED`/`HUG`/`FILL`), `grow`, `alignSelf`. |
| `sizeLimits` | Min/max width or height is set. | These become a `SizeBox`. |
| `clipsContent` | Containers. | Clip children. |
| `style` | The node has paint. | `fills`, `strokes`, `strokeWeight`(`s`), `strokeAlign`, `dashed`, `cornerRadius`, `cornerSmoothing`, `effects`, `blendMode`, shared style names, and `simple` (exactly one Slate rounded box). |
| `text` | `TEXT` nodes. | `characters`, font (`fontFamily`, `fontStyle`, `fontWeight`, `italic`, `fontSize` in px), `color`, alignment, `autoResize`, `truncate`/`maxLines`, `letterSpacing`, `lineHeight`, `textCase`, `decoration`, `textStyle`, `stroke`, `shadow`, and `mixed` + `segments` for multi-style text. |
| `component` | Instances, components, component sets. | `kind`, ids and names of the main component and its set, `variant` (e.g. `{ "State": "Hover" }`), instance `properties` (`{ "Label": { "type": "TEXT", "value": "Play" } }`), `remote`. |
| `image` | The whole node was rasterized. | `{ asset, bounds, reason }`. Children are omitted. `bounds` places the PNG relative to the node box and may extend past it (shadows). |
| `background` | Only the container's own paint was rasterized. | Same shape as `image`. Children follow normally. |
| `propRefs` | The layer is bound to a component property. | e.g. `{ "characters": "Label" }`. `#id` suffixes are stripped. |
| `isMask` | The layer masks its later siblings. | Masks are rasterized with what they mask. |
| `children` | Frames, groups, instances … | Child nodes, back to front. An empty array still marks a container. |

`reason` is one of `tag`, `vector`, `effects`, `gradient`, `imageFill`, `mask`, `flip` or `unsupported`. The importer
uses it for the preview and the log.

## Component sets

`components` holds every set whose name starts with one of `options.variantPrefixes`. Every variant is exported
in full, so Unreal can build button states:

```jsonc
"9:1": {
  "id": "9:1",
  "name": "Btn/Primary",
  "remote": false,
  "propertyNames": ["State"],
  "variants": [
    { "id": "2:1", "name": "State=Default", "properties": { "State": "Default" }, "node": { /* node */ } },
    { "id": "4:1", "name": "State=Hover",   "properties": { "State": "Hover" },   "node": { /* node */ } }
  ]
}
```

Plain components named `Btn/Primary/Hover`, `Btn/Primary/Pressed` … are grouped into a **virtual set** keyed
`virtual:Btn/Primary`, with a single `State` property.

## Assets

| Field | Meaning |
| --- | --- |
| `id` | Referenced by `image.asset` / `background.asset`. |
| `path` | Bundle-relative, forward slashes (`images/Logo.png`). Importers must reject paths that escape the bundle. |
| `width`, `height` | PNG pixels. |
| `scale` | The scale the PNG was rendered at. The size in Slate units is `width / scale` × `height / scale`. |
| `hash` | Hex content hash. Unreal stores it on the texture and skips re-importing unchanged art. |
| `bytes` | File size. |

Identical images (same hash and bytes) are stored once and shared by every node that uses them.
