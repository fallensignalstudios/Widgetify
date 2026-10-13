# Widgetify: the Figma plugin

This plugin exports one Figma frame as a **figmasync bundle** (`figmasync.json` + PNGs) for the Widgetify Unreal
plugin. It can also push the bundle straight into a running Unreal Editor.

Everything happens inside Figma through the Plugin API: no REST calls, no tokens, no rate limits.

## Install (development plugin)

1. Build it. Skip this step if `dist/` already exists.

   ```bash
   npm install
   npm run build
   ```

2. In the **Figma desktop app**, open **Plugins → Development → Import plugin from manifest…** and pick this folder's
   `manifest.json`.
3. Select a frame, then run **Plugins → Development → Widgetify (Figma → UMG)**.

## Use

1. **Select one frame.** This is the screen. Components, instances, sections and groups work too.
2. Check the options. The plugin remembers them:

   | Option | Default | What it does |
   | --- | --- | --- |
   | Image scale | 2x | Resolution of rasterized art. 2x stays crisp at 1080p, and on 4K with UI scaling. |
   | Export hidden layers | on | Hidden layers arrive as *Collapsed* widgets that code can show later. |
   | Rasterize gradients, shadows, masks | on | When it's off, unsupported paint is approximated with a flat color, and the exporter warns. |
   | Button component prefixes | `Btn/, Button/` | Component sets with these prefixes export every variant, so Unreal can build hover, pressed and disabled styles. |

3. Then either:
   - **Export bundle (.zip)**: saves `<Frame>.figmasync.zip`. Open it from Unreal's **Tools → Figma Sync** panel.
   - **Push to Unreal**: sends the bundle to the editor on `http://127.0.0.1:7820`. The status dot shows whether
     Unreal is listening. Turn this on in Unreal under **Project Settings → Plugins → Widgetify (Figma Sync) → Live
     Push**.

Export warnings (things that were approximated or skipped) are listed in the plugin, and they are also written to
the bundle so the Unreal import log shows them.

Layer naming and `[tags]` are covered in [../docs/NAMING.md](../docs/NAMING.md).

### Live push notes

- Figma only allows localhost for **development** plugins, through `networkAccess.devAllowedDomains` in
  `manifest.json`. A published plugin would need the editor address in `allowedDomains`.
- Newer Chromium-based builds may ask once for permission to reach devices on your **local network**. Allow it,
  or push won't connect.
- To change the port, edit it in Unreal's Project Settings, in `manifest.json` (`devAllowedDomains`) and in
  `DEFAULT_LIVE_PORT` in `src/shared/messages.ts`. Then rebuild.

## Develop

```bash
npm run watch      # rebuild dist/ on change, then re-run the plugin in Figma
npm run check      # typecheck + tests + build
npm test           # vitest only
npm run sample     # regenerate ../samples/MainMenu.figmasync.zip (also copied into the Unreal plugin)
```

| Path | |
| --- | --- |
| `src/shared/bundle.ts` | The bundle format: the single source of truth. Change it here first, then update `FigmaSyncCore` (C++) and `schema/`. |
| `src/main/` | The plugin's main thread (Figma API). `walker.ts` holds the exporter logic and is unit-tested through the `ExportHost` interface. `code.ts` is the real host. |
| `src/ui/` | The plugin's UI iframe: options, zip building, download, live push. |
| `test/` | vitest suites, the mock Figma document and the sample bundle generator. |
| `build.mjs` | esbuild. It bundles `src/main/code.ts` → `dist/code.js`, and inlines the UI script into `dist/ui.html`. |

The `dist/` folder is committed, so designers can import the plugin without installing Node. Run `npm run build`
before committing changes to `src/`.
