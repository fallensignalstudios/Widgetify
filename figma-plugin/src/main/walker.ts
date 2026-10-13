import {
  BUNDLE_FORMAT,
  BUNDLE_IMAGE_DIR,
  BUNDLE_VERSION,
  type AssetDef,
  type AutoLayout,
  type Bundle,
  type ChildLayout,
  type ComponentRef,
  type ComponentSetDef,
  type Constraint,
  type ExportOptions,
  type ExportWarning,
  type FNode,
  type NodeStyle,
  type NodeType,
  type RasterReason,
  type RasterRef,
  type Rect,
  type SizeLimits,
  type TextInfo,
  type TextSegment,
} from '../shared/bundle';
import type { BundleFile } from '../shared/messages';
import { isAxisAligned, layoutBoxFromTransform, rectRelativeTo, relativeTransform, round2, type Mat } from './geometry';
import { bytesEqual, hashBytes, pngSize } from './hash';
import { classifyStyle, color, compositeSolids, firstSolidColor, isVisiblePaint, serializeEffects, serializePaints } from './paint';
import { parseLayerName, tagScale } from './tags';

export const GENERATOR_NAME = 'widgetify-figma';
export const GENERATOR_VERSION = '0.1.0';

/**
 * Everything the walker needs from Figma beyond plain node properties. The real implementation lives in
 * code.ts; tests pass a fake so the walker can run under Node.
 */
export interface ExportHost {
  styleName(id: string): Promise<string | undefined>;
  mainComponent(node: InstanceNode): Promise<ComponentNode | null>;
  /** Renders the node (with children) to PNG. absBounds is the absolute rect the PNG covers. */
  exportPng(node: SceneNode, scale: number): Promise<{ bytes: Uint8Array; absBounds: Rect | null }>;
  /** Renders only the node's own fills/strokes/effects. bounds is relative to the node's layout box. */
  exportBackgroundPng(node: SceneNode, scale: number): Promise<{ bytes: Uint8Array; bounds: Rect }>;
  ensurePageLoaded(node: BaseNode): Promise<void>;
  progress(done: number, total: number, label: string): void;
}

export interface ExportMeta {
  fileName: string;
  pageName: string;
  /** Override for reproducible output (tests, samples). */
  exportedAt?: string;
}

export interface ExportResult {
  bundle: Bundle;
  json: string;
  files: BundleFile[];
  stats: { nodes: number; images: number };
}

const VECTOR_TYPES = new Set(['VECTOR', 'STAR', 'POLYGON', 'BOOLEAN_OPERATION', 'TEXT_PATH']);
const KNOWN_TYPES = new Set<string>([
  'FRAME',
  'GROUP',
  'COMPONENT',
  'COMPONENT_SET',
  'INSTANCE',
  'SECTION',
  'RECTANGLE',
  'ELLIPSE',
  'LINE',
  'TEXT',
  'VECTOR',
  'STAR',
  'POLYGON',
  'BOOLEAN_OPERATION',
]);
const NEVER_EXPORT = new Set(['SLICE', 'CONNECTOR', 'STICKY', 'STAMP', 'WIDGET', 'EMBED', 'LINK_UNFURL']);
const STATE_WORDS = new Set(['default', 'normal', 'rest', 'idle', 'hover', 'hovered', 'pressed', 'down', 'active', 'disabled', 'selected', 'focused', 'focus']);

interface WalkCtx {
  parentAbs: Mat;
  parentAutoLayout: boolean;
  inGroup: boolean;
  path: string;
}

const TEXT_SEGMENT_FIELDS = ['fontName', 'fontSize', 'fontWeight', 'fills', 'letterSpacing', 'lineHeight', 'textCase', 'textDecoration', 'textStyleId'] as const;
type TextSeg = Pick<StyledTextSegment, (typeof TEXT_SEGMENT_FIELDS)[number] | 'characters' | 'start' | 'end'>;

/** Narrow property access that tolerates node types without the property and figma.mixed values. */
function prop<T>(node: SceneNode, key: string): T | undefined {
  if (!(key in node)) return undefined;
  const value = (node as unknown as Record<string, unknown>)[key];
  return typeof value === 'symbol' ? undefined : (value as T);
}

function hasChildren(node: SceneNode): node is SceneNode & ChildrenMixin {
  return 'children' in node && Array.isArray((node as unknown as ChildrenMixin).children);
}

function sanitizeFileName(name: string): string {
  const cleaned = name
    .normalize('NFKD')
    .replace(/[^\w-]+/g, '_')
    .replace(/_+/g, '_')
    .replace(/^_|_$/g, '');
  return cleaned.length > 0 ? cleaned.slice(0, 64) : 'image';
}

class AssetCollector {
  readonly files: BundleFile[] = [];
  readonly assets: AssetDef[] = [];
  private readonly byHash = new Map<string, { def: AssetDef; bytes: Uint8Array }[]>();
  private readonly usedNames = new Set<string>();

  add(bytes: Uint8Array, baseName: string, scale: number): AssetDef {
    const hash = hashBytes(bytes);
    const candidates = this.byHash.get(hash) ?? [];
    for (const c of candidates) {
      if (c.def.scale === scale && bytesEqual(c.bytes, bytes)) return c.def;
    }
    let name = sanitizeFileName(baseName);
    let suffix = 2;
    while (this.usedNames.has(name.toLowerCase())) {
      name = `${sanitizeFileName(baseName)}_${suffix++}`;
    }
    this.usedNames.add(name.toLowerCase());
    const size = pngSize(bytes) ?? { width: 0, height: 0 };
    const def: AssetDef = {
      id: `img${this.assets.length + 1}`,
      path: `${BUNDLE_IMAGE_DIR}/${name}.png`,
      width: size.width,
      height: size.height,
      scale,
      hash,
      bytes: bytes.length,
    };
    this.assets.push(def);
    this.files.push({ path: def.path, data: bytes });
    candidates.push({ def, bytes });
    this.byHash.set(hash, candidates);
    return def;
  }
}

export class FrameExporter {
  private readonly assets = new AssetCollector();
  private readonly warnings: ExportWarning[] = [];
  private readonly components: Record<string, ComponentSetDef> = {};
  private readonly componentsInProgress = new Set<string>();
  private readonly styleNames = new Map<string, string | undefined>();
  private done = 0;
  private total = 1;
  private nodeCount = 0;

  constructor(
    private readonly options: ExportOptions,
    private readonly host: ExportHost,
  ) {}

  async run(root: SceneNode, meta: ExportMeta): Promise<ExportResult> {
    this.total = Math.max(1, this.countNodes(root));
    const rootAbs = root.absoluteTransform as Mat;
    const rootCtx: WalkCtx = { parentAbs: rootAbs, parentAutoLayout: false, inGroup: false, path: '' };
    const rootNode = await this.serialize(root, rootCtx, true);
    if (!rootNode) {
      throw new Error('The selected layer is hidden or tagged [skip], so there is nothing to export.');
    }
    const bundle: Bundle = {
      format: BUNDLE_FORMAT,
      version: BUNDLE_VERSION,
      generator: { name: GENERATOR_NAME, version: GENERATOR_VERSION },
      exportedAt: meta.exportedAt ?? new Date().toISOString(),
      source: { fileName: meta.fileName, pageName: meta.pageName, frameId: root.id, frameName: rootNode.cleanName },
      options: this.options,
      root: rootNode,
      components: this.components,
      assets: this.assets.assets,
      warnings: this.warnings,
    };
    return {
      bundle,
      json: JSON.stringify(bundle, null, 2),
      files: this.assets.files,
      stats: { nodes: this.nodeCount, images: this.assets.assets.length },
    };
  }

  private countNodes(node: SceneNode): number {
    const { tags } = parseLayerName(node.name);
    if ('skip' in tags) return 0;
    if (!node.visible && !this.options.includeHidden) return 0;
    let n = 1;
    if (hasChildren(node) && !('img' in tags)) {
      for (const c of node.children) n += this.countNodes(c);
    }
    return n;
  }

  private warn(ctxPath: string, node: SceneNode | undefined, message: string): void {
    this.warnings.push({ nodeId: node?.id, nodePath: ctxPath || node?.name, message });
  }

  private async resolveStyle(id: unknown): Promise<string | undefined> {
    if (typeof id !== 'string' || id.length === 0) return undefined;
    if (!this.styleNames.has(id)) {
      try {
        this.styleNames.set(id, await this.host.styleName(id));
      } catch {
        this.styleNames.set(id, undefined);
      }
    }
    return this.styleNames.get(id);
  }

  private async serialize(node: SceneNode, ctx: WalkCtx, isRoot = false): Promise<FNode | null> {
    if (NEVER_EXPORT.has(node.type)) return null;
    const { cleanName, tags } = parseLayerName(node.name);
    if ('skip' in tags) return null;
    if (!node.visible && !this.options.includeHidden) return null;

    const path = ctx.path ? `${ctx.path}/${cleanName || node.name}` : cleanName || node.name;
    this.done++;
    this.nodeCount++;
    this.host.progress(Math.min(this.done, this.total), this.total, path);

    const width = prop<number>(node, 'width') ?? 0;
    const height = prop<number>(node, 'height') ?? 0;
    const rel = relativeTransform(ctx.parentAbs, node.absoluteTransform as Mat);
    const box = layoutBoxFromTransform(rel, width, height);
    const type: NodeType = KNOWN_TYPES.has(node.type) ? (node.type as NodeType) : 'OTHER';

    const out: FNode = {
      id: node.id,
      name: node.name,
      cleanName: cleanName || `${node.type.toLowerCase()}_${node.id.replace(/[^\w]+/g, '_')}`,
      tags,
      type,
      visible: node.visible,
      opacity: round2(prop<number>(node, 'opacity') ?? 1),
      rotation: box.rotation,
      x: box.x,
      y: box.y,
      width: box.width,
      height: box.height,
    };

    if (!isRoot) {
      this.applyPlacement(node, ctx, out);
    }
    const limits = this.sizeLimits(node);
    if (limits) out.sizeLimits = limits;
    const clips = prop<boolean>(node, 'clipsContent');
    if (clips !== undefined && hasChildren(node)) out.clipsContent = clips;

    const refs = prop<Record<string, string> | null>(node, 'componentPropertyReferences');
    if (refs) {
      const cleaned: Record<string, string> = {};
      for (const [k, v] of Object.entries(refs)) if (typeof v === 'string') cleaned[k] = stripPropId(v);
      if (Object.keys(cleaned).length > 0) out.propRefs = cleaned;
    }
    if (prop<boolean>(node, 'isMask')) out.isMask = true;

    if (node.type === 'INSTANCE' || node.type === 'COMPONENT' || node.type === 'COMPONENT_SET') {
      out.component = await this.componentRef(node, path);
    }

    if (node.type === 'TEXT') {
      out.text = await this.textInfo(node, path);
    } else if ('fills' in node || 'strokes' in node) {
      out.style = await this.nodeStyle(node);
    }

    if (box.flipped) {
      this.warn(path, node, 'Layer is mirrored; UMG render transforms cannot flip, so it was rasterized.');
    }

    // Decide what (if anything) becomes a PNG.
    const rasterReason = this.wholeNodeRasterReason(node, out, tags, box.flipped, isRoot);
    if (rasterReason) {
      await this.rasterizeWhole(node, out, ctx, tags, rasterReason, path);
      return out;
    }

    if (out.style && !out.style.simple && hasChildren(node)) {
      if (this.options.rasterizeUnsupported) {
        await this.rasterizeBackground(node, out, tags, path);
      } else {
        this.warn(path, node, `Background uses ${this.styleProblem(out.style)}; it will be approximated with a flat color.`);
      }
    } else if (out.style && !out.style.simple && !this.options.rasterizeUnsupported) {
      this.warn(path, node, `Uses ${this.styleProblem(out.style)}; it will be approximated with a flat color.`);
    }

    if (hasChildren(node)) {
      const layoutMode = prop<string>(node, 'layoutMode');
      const auto = layoutMode === 'HORIZONTAL' || layoutMode === 'VERTICAL';
      if (auto) {
        out.layout = this.autoLayout(node);
      } else if (layoutMode === 'GRID') {
        this.warn(path, node, 'Grid auto-layout is not supported yet; children keep their absolute positions.');
      }
      const childCtx: WalkCtx = {
        parentAbs: node.absoluteTransform as Mat,
        parentAutoLayout: auto,
        inGroup: node.type === 'GROUP' || node.type === 'BOOLEAN_OPERATION',
        path,
      };
      const children: FNode[] = [];
      for (const child of node.children) {
        if (prop<boolean>(child, 'isMask') && isRoot) {
          this.warn(path, child, 'Masks directly inside the exported frame are not supported; the mask shape was skipped.');
          continue;
        }
        const c = await this.serialize(child, childCtx);
        if (c) children.push(c);
      }
      if (node.type === 'GROUP' && !isRoot && out.constraints && !ctx.inGroup) {
        out.constraints = groupConstraints(node.children);
      }
      out.children = children;
    }
    return out;
  }

  private applyPlacement(node: SceneNode, ctx: WalkCtx, out: FNode): void {
    const positioning = prop<'AUTO' | 'ABSOLUTE'>(node, 'layoutPositioning') ?? 'AUTO';
    if (ctx.parentAutoLayout) {
      out.child = this.childLayout(node, positioning);
    }
    if (!ctx.parentAutoLayout || positioning === 'ABSOLUTE') {
      if (ctx.inGroup) {
        // Group children scale with their group, like resizing a group in Figma.
        out.constraints = { horizontal: 'SCALE', vertical: 'SCALE' };
      } else {
        const c = prop<Constraints>(node, 'constraints');
        out.constraints = c ? { horizontal: c.horizontal as Constraint, vertical: c.vertical as Constraint } : { horizontal: 'MIN', vertical: 'MIN' };
      }
    }
  }

  private childLayout(node: SceneNode, positioning: 'AUTO' | 'ABSOLUTE'): ChildLayout {
    const grow = prop<number>(node, 'layoutGrow') ?? 0;
    const align = prop<string>(node, 'layoutAlign') ?? 'INHERIT';
    const sizingH = prop<'FIXED' | 'HUG' | 'FILL'>(node, 'layoutSizingHorizontal') ?? 'FIXED';
    const sizingV = prop<'FIXED' | 'HUG' | 'FILL'>(node, 'layoutSizingVertical') ?? 'FIXED';
    return { positioning, sizingH, sizingV, grow: round2(grow), alignSelf: align === 'STRETCH' ? 'STRETCH' : 'INHERIT' };
  }

  private sizeLimits(node: SceneNode): SizeLimits | undefined {
    const limits: SizeLimits = {};
    for (const key of ['minWidth', 'maxWidth', 'minHeight', 'maxHeight'] as const) {
      const v = prop<number | null>(node, key);
      if (typeof v === 'number') limits[key] = round2(v);
    }
    return Object.keys(limits).length > 0 ? limits : undefined;
  }

  private autoLayout(node: SceneNode): AutoLayout {
    return {
      mode: prop<'HORIZONTAL' | 'VERTICAL'>(node, 'layoutMode') ?? 'HORIZONTAL',
      wrap: prop<string>(node, 'layoutWrap') === 'WRAP',
      padding: {
        left: round2(prop<number>(node, 'paddingLeft') ?? 0),
        right: round2(prop<number>(node, 'paddingRight') ?? 0),
        top: round2(prop<number>(node, 'paddingTop') ?? 0),
        bottom: round2(prop<number>(node, 'paddingBottom') ?? 0),
      },
      itemSpacing: round2(prop<number>(node, 'itemSpacing') ?? 0),
      counterAxisSpacing: round2(prop<number | null>(node, 'counterAxisSpacing') ?? 0),
      primaryAlign: prop<AutoLayout['primaryAlign']>(node, 'primaryAxisAlignItems') ?? 'MIN',
      counterAlign: prop<AutoLayout['counterAlign']>(node, 'counterAxisAlignItems') ?? 'MIN',
      reverseZIndex: prop<boolean>(node, 'itemReverseZIndex') ?? false,
    };
  }

  private async nodeStyle(node: SceneNode): Promise<NodeStyle> {
    const fills = serializePaints(prop<ReadonlyArray<Paint>>(node, 'fills'));
    const strokes = serializePaints(prop<ReadonlyArray<Paint>>(node, 'strokes'));
    const effects = serializeEffects(prop<ReadonlyArray<Effect>>(node, 'effects'));
    const strokeWeight = prop<number>(node, 'strokeWeight');
    const sides = {
      top: prop<number>(node, 'strokeTopWeight'),
      right: prop<number>(node, 'strokeRightWeight'),
      bottom: prop<number>(node, 'strokeBottomWeight'),
      left: prop<number>(node, 'strokeLeftWeight'),
    };
    const sideValues = [sides.top, sides.right, sides.bottom, sides.left].filter((v): v is number => typeof v === 'number');
    const uniform = sideValues.length < 4 || sideValues.every((v) => Math.abs(v - sideValues[0]) < 0.001);
    const dash = prop<ReadonlyArray<number>>(node, 'dashPattern');
    const dashed = !!dash && dash.length > 0;
    const blendMode = prop<string>(node, 'blendMode') ?? 'PASS_THROUGH';

    const style: NodeStyle = {
      fills,
      strokes,
      strokeWeight: round2(strokeWeight ?? (sideValues.length ? Math.max(...sideValues) : 0)),
      strokeAlign: prop<NodeStyle['strokeAlign']>(node, 'strokeAlign') ?? 'INSIDE',
      dashed,
      cornerRadius: cornerRadii(node),
      cornerSmoothing: round2(prop<number>(node, 'cornerSmoothing') ?? 0),
      effects,
      blendMode,
      simple: false,
    };
    if (!uniform && sideValues.length === 4) {
      style.strokeWeights = { top: round2(sides.top!), right: round2(sides.right!), bottom: round2(sides.bottom!), left: round2(sides.left!) };
    }
    style.fillStyle = await this.resolveStyle(prop<string>(node, 'fillStyleId'));
    style.strokeStyle = await this.resolveStyle(prop<string>(node, 'strokeStyleId'));
    style.effectStyle = await this.resolveStyle(prop<string>(node, 'effectStyleId'));
    for (const key of ['fillStyle', 'strokeStyle', 'effectStyle'] as const) {
      if (style[key] === undefined) delete style[key];
    }

    const verdict = classifyStyle({ fills, strokes, effects, uniformStroke: uniform, dashed, blendMode });
    style.simple = verdict.simple;
    if (verdict.simple) {
      // Pre-composite stacked solid fills so the importer only ever sees one.
      const solid = compositeSolids(fills);
      style.fills = solid ? [{ type: 'SOLID', visible: true, opacity: solid.opacity, blendMode: 'NORMAL', color: solid.color }] : [];
      style.strokes = strokes.filter(isVisiblePaint);
    }
    return style;
  }

  private styleProblem(style: NodeStyle): string {
    const verdict = classifyStyle({
      fills: style.fills,
      strokes: style.strokes,
      effects: style.effects,
      uniformStroke: !style.strokeWeights,
      dashed: style.dashed,
      blendMode: style.blendMode,
    });
    return verdict.detail ?? 'an unsupported effect';
  }

  private async textInfo(node: TextNode, path: string): Promise<TextInfo> {
    let segments: TextSeg[] = [];
    try {
      segments = node.getStyledTextSegments([...TEXT_SEGMENT_FIELDS]);
    } catch {
      segments = [];
    }
    let dominant: TextSeg | undefined;
    for (const s of segments) {
      if (!dominant || s.end - s.start > dominant.end - dominant.start) dominant = s;
    }
    const fontName = dominant?.fontName ?? (typeof node.fontName === 'symbol' ? { family: 'Inter', style: 'Regular' } : node.fontName);
    const fills = serializePaints(dominant?.fills ?? prop<ReadonlyArray<Paint>>(node, 'fills'));
    const textColor = firstSolidColor(fills) ?? color(0, 0, 0, 1);
    if (fills.some((p) => p.visible && p.type !== 'SOLID')) {
      this.warn(path, node, 'Text uses a gradient or image fill; using its first color. Tag it [img] to keep the exact look.');
    }

    const letterSpacing = dominant?.letterSpacing ?? prop<LetterSpacing>(node, 'letterSpacing') ?? { value: 0, unit: 'PIXELS' };
    const lineHeight = dominant?.lineHeight ?? prop<LineHeight>(node, 'lineHeight') ?? { unit: 'AUTO' };
    const info: TextInfo = {
      characters: node.characters,
      fontFamily: fontName.family,
      fontStyle: fontName.style,
      fontWeight: dominant?.fontWeight ?? prop<number>(node, 'fontWeight') ?? 400,
      italic: /italic|oblique/i.test(fontName.style),
      fontSize: round2(dominant?.fontSize ?? prop<number>(node, 'fontSize') ?? 16),
      color: textColor,
      alignH: node.textAlignHorizontal,
      alignV: node.textAlignVertical,
      autoResize: node.textAutoResize,
      truncate: prop<string>(node, 'textTruncation') === 'ENDING' || node.textAutoResize === 'TRUNCATE',
      maxLines: prop<number | null>(node, 'maxLines') ?? 0,
      letterSpacing: { value: round2(letterSpacing.value), unit: letterSpacing.unit },
      lineHeight: lineHeight.unit === 'AUTO' ? { value: 0, unit: 'AUTO' } : { value: round2(lineHeight.value), unit: lineHeight.unit },
      textCase: (dominant?.textCase ?? prop<TextCase>(node, 'textCase') ?? 'ORIGINAL') as TextInfo['textCase'],
      decoration: (dominant?.textDecoration ?? prop<TextDecoration>(node, 'textDecoration') ?? 'NONE') as TextInfo['decoration'],
      mixed: segments.length > 1,
    };
    const textStyle = await this.resolveStyle(dominant?.textStyleId ?? prop<string>(node, 'textStyleId'));
    if (textStyle) info.textStyle = textStyle;

    const strokes = serializePaints(prop<ReadonlyArray<Paint>>(node, 'strokes')).filter(isVisiblePaint);
    const strokeWeight = prop<number>(node, 'strokeWeight') ?? 0;
    const strokeColor = firstSolidColor(strokes);
    if (strokeColor && strokeWeight > 0) info.stroke = { color: strokeColor, weight: round2(strokeWeight) };

    const effects = serializeEffects(prop<ReadonlyArray<Effect>>(node, 'effects')).filter((e) => e.visible);
    const shadow = effects.find((e) => e.type === 'DROP_SHADOW');
    if (shadow && shadow.color && shadow.offset) info.shadow = { color: shadow.color, offset: shadow.offset, radius: shadow.radius };
    if (effects.some((e) => e.type !== 'DROP_SHADOW')) {
      this.warn(path, node, 'Text blur / inner shadow effects are not supported by UMG text and were dropped.');
    }

    if (info.mixed) {
      info.segments = segments.map(
        (s): TextSegment => ({
          start: s.start,
          end: s.end,
          characters: s.characters,
          fontFamily: s.fontName.family,
          fontStyle: s.fontName.style,
          fontWeight: s.fontWeight,
          fontSize: round2(s.fontSize),
          color: firstSolidColor(serializePaints(s.fills)) ?? textColor,
        }),
      );
      this.warn(path, node, `Text has ${segments.length} differently styled runs; Unreal gets the dominant style (${fontName.family} ${fontName.style}).`);
    }
    return info;
  }

  private async componentRef(node: InstanceNode | ComponentNode | ComponentSetNode, path: string): Promise<ComponentRef> {
    if (node.type === 'COMPONENT_SET') {
      return { kind: 'COMPONENT_SET', componentSetId: node.id, componentSetName: node.name };
    }
    if (node.type === 'COMPONENT') {
      const ref: ComponentRef = { kind: 'COMPONENT', componentId: node.id, componentName: node.name };
      const parent = node.parent;
      if (parent && parent.type === 'COMPONENT_SET') {
        ref.componentSetId = parent.id;
        ref.componentSetName = parent.name;
        if (node.variantProperties) ref.variant = { ...node.variantProperties };
      }
      return ref;
    }
    const ref: ComponentRef = { kind: 'INSTANCE' };
    try {
      const props = node.componentProperties;
      const cleaned: Record<string, { type: string; value: string | boolean }> = {};
      for (const [key, value] of Object.entries(props ?? {})) {
        cleaned[stripPropId(key)] = { type: value.type, value: value.value };
      }
      if (Object.keys(cleaned).length > 0) ref.properties = cleaned;
    } catch {
      // componentProperties can throw on broken instances; it's optional.
    }
    let main: ComponentNode | null = null;
    try {
      main = await this.host.mainComponent(node);
    } catch {
      main = null;
    }
    if (!main) {
      this.warn(path, node, 'Instance has no main component (deleted or inaccessible); exported as a plain frame.');
      return ref;
    }
    ref.componentId = main.id;
    ref.componentName = main.name;
    ref.remote = main.remote;
    let set: ComponentSetNode | null = null;
    try {
      set = main.parent && main.parent.type === 'COMPONENT_SET' ? main.parent : null;
    } catch {
      set = null;
    }
    if (set) {
      ref.componentSetId = set.id;
      ref.componentSetName = set.name;
      try {
        if (main.variantProperties) ref.variant = { ...main.variantProperties };
      } catch {
        // Remote variants may hide their properties.
      }
      if (this.matchesVariantPrefix(set.name)) await this.collectComponentSet(set, path);
    } else if (this.matchesVariantPrefix(main.name)) {
      const virtual = virtualSetKey(main.name);
      if (virtual) {
        ref.componentSetId = `virtual:${virtual.key}`;
        ref.componentSetName = virtual.key;
        ref.variant = { State: virtual.state };
        await this.collectVirtualSet(main, virtual.key, path);
      }
    }
    return ref;
  }

  private matchesVariantPrefix(name: string): boolean {
    return this.options.variantPrefixes.some((p) => p.length > 0 && name.toLowerCase().startsWith(p.toLowerCase()));
  }

  private async collectComponentSet(set: ComponentSetNode, path: string): Promise<void> {
    if (this.components[set.id] || this.componentsInProgress.has(set.id)) return;
    this.componentsInProgress.add(set.id);
    try {
      await this.host.ensurePageLoaded(set);
      let propertyNames: string[] = [];
      try {
        propertyNames = Object.entries(set.componentPropertyDefinitions)
          .filter(([, def]) => def.type === 'VARIANT')
          .map(([name]) => name);
      } catch {
        propertyNames = [];
      }
      const def: ComponentSetDef = { id: set.id, name: set.name, remote: set.remote, propertyNames, variants: [] };
      for (const child of set.children) {
        if (child.type !== 'COMPONENT') continue;
        const variantCtx: WalkCtx = { parentAbs: child.absoluteTransform as Mat, parentAutoLayout: false, inGroup: false, path: `${set.name}/${child.name}` };
        const node = await this.serialize(child, variantCtx, true);
        if (node) def.variants.push({ id: child.id, name: child.name, properties: { ...(child.variantProperties ?? {}) }, node });
      }
      this.components[set.id] = def;
    } catch (e) {
      this.warn(path, undefined, `Could not read variants of "${set.name}" (${String(e)}); button states will be derived in Unreal.`);
    } finally {
      this.componentsInProgress.delete(set.id);
    }
  }

  /** "Btn/Primary/Hover" style naming without a component set: siblings sharing the prefix become variants. */
  private async collectVirtualSet(main: ComponentNode, key: string, path: string): Promise<void> {
    const id = `virtual:${key}`;
    if (this.components[id] || this.componentsInProgress.has(id)) return;
    this.componentsInProgress.add(id);
    try {
      await this.host.ensurePageLoaded(main);
      const siblings = main.parent && 'children' in main.parent ? main.parent.children : [main];
      const def: ComponentSetDef = { id, name: key, remote: main.remote, propertyNames: ['State'], variants: [] };
      for (const sibling of siblings) {
        if (sibling.type !== 'COMPONENT') continue;
        const v = virtualSetKey(sibling.name);
        if (!v || v.key !== key) continue;
        const ctx: WalkCtx = { parentAbs: sibling.absoluteTransform as Mat, parentAutoLayout: false, inGroup: false, path: sibling.name };
        const node = await this.serialize(sibling, ctx, true);
        if (node) def.variants.push({ id: sibling.id, name: sibling.name, properties: { State: v.state }, node });
      }
      this.components[id] = def;
    } catch (e) {
      this.warn(path, undefined, `Could not collect states for "${key}" (${String(e)}).`);
    } finally {
      this.componentsInProgress.delete(id);
    }
  }

  private wholeNodeRasterReason(node: SceneNode, out: FNode, tags: Record<string, string>, flipped: boolean, isRoot: boolean): RasterReason | undefined {
    if ('img' in tags) return 'tag';
    if (isRoot) return undefined;
    if (flipped) return 'flip';
    if (VECTOR_TYPES.has(node.type)) return 'vector';
    if (out.type === 'OTHER') return hasChildren(node) ? undefined : 'unsupported';
    if (node.type === 'ELLIPSE') {
      const arc = prop<ArcData>(node, 'arcData');
      const fullCircle = !arc || (Math.abs(arc.endingAngle - arc.startingAngle - Math.PI * 2) < 0.001 && arc.innerRadius === 0);
      const circle = Math.abs(out.width - out.height) < 0.5;
      if (!fullCircle || !circle) return 'vector';
    }
    if (node.type === 'LINE') {
      const strokes = out.style?.strokes.filter(isVisiblePaint) ?? [];
      if (!isAxisAligned(out.rotation) || strokes.length !== 1 || strokes[0].type !== 'SOLID' || out.style?.dashed) return 'vector';
    }
    if (hasChildren(node) && node.children.some((c) => prop<boolean>(c, 'isMask'))) return 'mask';
    if (!hasChildren(node) && out.style && !out.style.simple && this.options.rasterizeUnsupported) {
      const verdict = classifyStyle({
        fills: out.style.fills,
        strokes: out.style.strokes,
        effects: out.style.effects,
        uniformStroke: !out.style.strokeWeights,
        dashed: out.style.dashed,
        blendMode: out.style.blendMode,
      });
      return verdict.reason ?? 'unsupported';
    }
    return undefined;
  }

  private async rasterizeWhole(node: SceneNode, out: FNode, ctx: WalkCtx, tags: Record<string, string>, reason: RasterReason, path: string): Promise<void> {
    const scale = tagScale(tags) ?? this.options.scale;
    try {
      const { bytes, absBounds } = await this.host.exportPng(node, scale);
      const asset = this.assets.add(bytes, out.cleanName, scale);
      const bbAbs = prop<Rect | null>(node, 'absoluteBoundingBox') ?? null;
      let bounds: Rect = { x: 0, y: 0, width: out.width, height: out.height };
      if (out.rotation !== 0 || reason === 'flip') {
        // The PNG is axis-aligned with the rotation baked in, so the widget becomes an unrotated box.
        if (bbAbs) {
          const bb = rectRelativeTo(ctx.parentAbs, bbAbs);
          out.x = bb.x;
          out.y = bb.y;
          out.width = bb.width;
          out.height = bb.height;
        }
        out.rotation = 0;
      }
      if (absBounds) {
        const rb = rectRelativeTo(ctx.parentAbs, absBounds);
        bounds = { x: round2(rb.x - out.x), y: round2(rb.y - out.y), width: rb.width, height: rb.height };
      }
      out.image = { asset: asset.id, bounds, reason } satisfies RasterRef;
      delete out.children;
      delete out.layout;
    } catch (e) {
      this.warn(path, node, `PNG export failed (${String(e)}); the layer was exported without its image.`);
    }
  }

  private async rasterizeBackground(node: SceneNode, out: FNode, tags: Record<string, string>, path: string): Promise<void> {
    const scale = tagScale(tags) ?? this.options.scale;
    try {
      const { bytes, bounds } = await this.host.exportBackgroundPng(node, scale);
      const asset = this.assets.add(bytes, `${out.cleanName}_Bg`, scale);
      out.background = { asset: asset.id, bounds, reason: this.backgroundReason(out.style!) };
    } catch (e) {
      this.warn(path, node, `Background export failed (${String(e)}); it will be approximated with a flat color.`);
    }
  }

  private backgroundReason(style: NodeStyle): RasterReason {
    return (
      classifyStyle({
        fills: style.fills,
        strokes: style.strokes,
        effects: style.effects,
        uniformStroke: !style.strokeWeights,
        dashed: style.dashed,
        blendMode: style.blendMode,
      }).reason ?? 'unsupported'
    );
  }
}

function cornerRadii(node: SceneNode): [number, number, number, number] {
  const tl = prop<number>(node, 'topLeftRadius');
  if (typeof tl === 'number') {
    return [
      round2(tl),
      round2(prop<number>(node, 'topRightRadius') ?? 0),
      round2(prop<number>(node, 'bottomRightRadius') ?? 0),
      round2(prop<number>(node, 'bottomLeftRadius') ?? 0),
    ];
  }
  const r = round2(prop<number>(node, 'cornerRadius') ?? 0);
  return [r, r, r, r];
}

function stripPropId(name: string): string {
  const hash = name.lastIndexOf('#');
  return hash > 0 ? name.slice(0, hash) : name;
}

/** "Btn/Primary/Hover" -> { key: "Btn/Primary", state: "Hover" } when the last segment is a state word. */
export function virtualSetKey(name: string): { key: string; state: string } | null {
  const parts = name.split('/').map((p) => p.trim());
  if (parts.length < 3) return null;
  const state = parts[parts.length - 1];
  if (!STATE_WORDS.has(state.toLowerCase())) return null;
  return { key: parts.slice(0, -1).join('/'), state };
}

/** Groups don't have constraints in Figma; derive them from children when they all agree. */
function groupConstraints(children: readonly SceneNode[]): { horizontal: Constraint; vertical: Constraint } {
  let h: Constraint | undefined;
  let v: Constraint | undefined;
  let uniformH = true;
  let uniformV = true;
  for (const child of children) {
    const c = prop<Constraints>(child, 'constraints');
    if (!c) continue;
    if (h === undefined) h = c.horizontal as Constraint;
    else if (h !== c.horizontal) uniformH = false;
    if (v === undefined) v = c.vertical as Constraint;
    else if (v !== c.vertical) uniformV = false;
  }
  return { horizontal: uniformH && h ? h : 'MIN', vertical: uniformV && v ? v : 'MIN' };
}
