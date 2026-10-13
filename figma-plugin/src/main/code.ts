import type { ExportOptions, Rect } from '../shared/bundle';
import { DEFAULT_LIVE_PORT, DEFAULT_OPTIONS, type MainToUi, type SelectionInfo, type UiPrefs, type UiToMain } from '../shared/messages';
import { FrameExporter, type ExportHost } from './walker';

const PREFS_KEY = 'widgetify.prefs.v1';
const EXPORTABLE_ROOTS = new Set(['FRAME', 'COMPONENT', 'COMPONENT_SET', 'INSTANCE', 'SECTION', 'GROUP']);

figma.showUI(__html__, { width: 380, height: 600, themeColors: true, title: 'Widgetify — Figma → UMG' });

function post(msg: MainToUi): void {
  figma.ui.postMessage(msg);
}

function countLayers(node: SceneNode, limit = 5000): number {
  let n = 1;
  if ('children' in node) {
    for (const c of node.children) {
      n += countLayers(c, limit - n);
      if (n >= limit) return n;
    }
  }
  return n;
}

function describeSelection(): SelectionInfo {
  const sel = figma.currentPage.selection;
  if (sel.length === 0) return { ok: false, reason: 'Select a frame to export.' };
  if (sel.length > 1) return { ok: false, reason: 'Select a single frame (one screen per bundle).' };
  const node = sel[0];
  if (!EXPORTABLE_ROOTS.has(node.type)) {
    return { ok: false, reason: `A ${node.type.toLowerCase().replace(/_/g, ' ')} can't be a screen. Select its parent frame.` };
  }
  return {
    ok: true,
    id: node.id,
    name: node.name,
    type: node.type,
    width: Math.round(node.width),
    height: Math.round(node.height),
    layerCount: countLayers(node),
  };
}

async function loadPrefs(): Promise<UiPrefs> {
  try {
    const stored = (await figma.clientStorage.getAsync(PREFS_KEY)) as Partial<UiPrefs> | undefined;
    return {
      options: { ...DEFAULT_OPTIONS, ...(stored?.options ?? {}) },
      livePort: stored?.livePort ?? DEFAULT_LIVE_PORT,
    };
  } catch {
    return { options: { ...DEFAULT_OPTIONS }, livePort: DEFAULT_LIVE_PORT };
  }
}

function pageOf(node: BaseNode): PageNode | null {
  let current: BaseNode | null = node;
  while (current && current.type !== 'PAGE') current = current.parent;
  return current && current.type === 'PAGE' ? current : null;
}

function copyBackground(source: SceneNode, target: RectangleNode): void {
  const s = source as SceneNode & Partial<GeometryMixin & IndividualStrokesMixin & RectangleCornerMixin & CornerMixin & BlendMixin>;
  if (s.fills !== undefined && typeof s.fills !== 'symbol') target.fills = s.fills;
  if (s.strokes !== undefined) target.strokes = s.strokes;
  if (s.strokeAlign !== undefined) target.strokeAlign = s.strokeAlign;
  if (typeof s.strokeWeight === 'number') {
    target.strokeWeight = s.strokeWeight;
  } else if (typeof s.strokeTopWeight === 'number') {
    target.strokeTopWeight = s.strokeTopWeight;
    target.strokeRightWeight = s.strokeRightWeight ?? 0;
    target.strokeBottomWeight = s.strokeBottomWeight ?? 0;
    target.strokeLeftWeight = s.strokeLeftWeight ?? 0;
  }
  if (s.dashPattern !== undefined) target.dashPattern = s.dashPattern;
  if (typeof s.topLeftRadius === 'number') {
    target.topLeftRadius = s.topLeftRadius;
    target.topRightRadius = s.topRightRadius ?? 0;
    target.bottomRightRadius = s.bottomRightRadius ?? 0;
    target.bottomLeftRadius = s.bottomLeftRadius ?? 0;
  } else if (typeof s.cornerRadius === 'number') {
    target.cornerRadius = s.cornerRadius;
  }
  if (typeof s.cornerSmoothing === 'number') target.cornerSmoothing = s.cornerSmoothing;
  if (s.effects !== undefined) target.effects = s.effects;
}

const host: ExportHost = {
  async styleName(id) {
    const style = await figma.getStyleByIdAsync(id);
    return style?.name;
  },
  async mainComponent(node) {
    return node.getMainComponentAsync();
  },
  async exportPng(node, scale) {
    const wasVisible = node.visible;
    if (!wasVisible) node.visible = true;
    try {
      const bytes = await node.exportAsync({ format: 'PNG', constraint: { type: 'SCALE', value: scale } });
      const absBounds = (('absoluteRenderBounds' in node ? node.absoluteRenderBounds : null) ?? node.absoluteBoundingBox) as Rect | null;
      return { bytes, absBounds };
    } finally {
      if (!wasVisible) node.visible = false;
    }
  },
  async exportBackgroundPng(node, scale) {
    // Render the node's own paint without its children by copying it onto a throwaway rectangle.
    const rect = figma.createRectangle();
    try {
      rect.name = '__widgetify_background__';
      rect.resize(Math.max(node.width, 0.01), Math.max(node.height, 0.01));
      copyBackground(node, rect);
      const bytes = await rect.exportAsync({ format: 'PNG', constraint: { type: 'SCALE', value: scale } });
      const t = rect.absoluteTransform;
      const rb = rect.absoluteRenderBounds ?? { x: t[0][2], y: t[1][2], width: node.width, height: node.height };
      return { bytes, bounds: { x: rb.x - t[0][2], y: rb.y - t[1][2], width: rb.width, height: rb.height } };
    } finally {
      rect.remove();
    }
  },
  async ensurePageLoaded(node) {
    const page = pageOf(node);
    if (page && page !== figma.currentPage) await page.loadAsync();
  },
  progress(done, total, label) {
    post({ type: 'progress', done, total, label });
  },
};

let exporting = false;

async function runExport(options: ExportOptions, purpose: 'download' | 'push'): Promise<void> {
  if (exporting) return;
  const selection = describeSelection();
  if (!selection.ok) {
    post({ type: 'export-error', message: selection.reason ?? 'Nothing to export.' });
    return;
  }
  exporting = true;
  try {
    const root = figma.currentPage.selection[0];
    const exporter = new FrameExporter(options, host);
    const result = await exporter.run(root, { fileName: figma.root.name, pageName: figma.currentPage.name });
    const safeName = result.bundle.source.frameName.replace(/[^\w-]+/g, '_') || 'Screen';
    post({
      type: 'bundle',
      purpose,
      fileName: `${safeName}.figmasync.zip`,
      frameName: result.bundle.source.frameName,
      frameId: result.bundle.source.frameId,
      json: result.json,
      files: result.files,
      warnings: result.bundle.warnings,
      stats: result.stats,
    });
  } catch (e) {
    post({ type: 'export-error', message: e instanceof Error ? e.message : String(e) });
  } finally {
    exporting = false;
  }
}

figma.ui.onmessage = async (msg: UiToMain) => {
  switch (msg.type) {
    case 'ready':
      post({ type: 'init', prefs: await loadPrefs(), selection: describeSelection() });
      break;
    case 'export':
      await runExport(msg.options, msg.purpose);
      break;
    case 'save-prefs':
      await figma.clientStorage.setAsync(PREFS_KEY, msg.prefs);
      break;
    case 'notify':
      figma.notify(msg.text, { error: msg.error === true });
      break;
  }
};

figma.on('selectionchange', () => post({ type: 'selection', selection: describeSelection() }));
