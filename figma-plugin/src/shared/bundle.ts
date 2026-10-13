/**
 * figmasync bundle format, version 1.
 *
 * A bundle is a ZIP archive (stored, no compression) or a plain folder containing:
 *   figmasync.json   - this document
 *   images/*.png     - rasterized art referenced by AssetDef.path
 *
 * This file is the single source of truth for the format. The Unreal importer
 * (FigmaSyncCore/FigmaDocument.h) mirrors these field names exactly, and
 * schema/figmasync.schema.json documents them for other tools.
 *
 * Conventions:
 *  - All lengths are Figma pixels (1 px == 1 Slate unit at the design resolution).
 *  - Colors are sRGB floats in 0..1. Alpha is the color's own alpha; paint opacity is separate.
 *  - Angles are degrees, clockwise-positive (the UMG render-transform convention).
 *  - Node x/y/width/height describe the unrotated layout box, relative to the parent node's box.
 *    Rotation is applied around the box center, exactly like a UMG render transform with pivot (0.5, 0.5).
 */

export const BUNDLE_FORMAT = 'figmasync';
export const BUNDLE_VERSION = 1;
export const BUNDLE_JSON_NAME = 'figmasync.json';
export const BUNDLE_IMAGE_DIR = 'images';

export interface Bundle {
  format: typeof BUNDLE_FORMAT;
  version: typeof BUNDLE_VERSION;
  generator: { name: string; version: string };
  exportedAt: string;
  source: BundleSource;
  options: ExportOptions;
  root: FNode;
  /** Component sets (or standalone components) whose variants were exported, keyed by id. */
  components: Record<string, ComponentSetDef>;
  assets: AssetDef[];
  warnings: ExportWarning[];
}

export interface BundleSource {
  fileName: string;
  pageName: string;
  frameId: string;
  frameName: string;
}

export interface ExportOptions {
  /** PNG export scale for rasterized art. 2 is a good default for 1080p UI. */
  scale: number;
  /** Export hidden layers as collapsed widgets instead of dropping them. */
  includeHidden: boolean;
  /** Rasterize gradients, shadows, blurs, image fills and masks instead of approximating them. */
  rasterizeUnsupported: boolean;
  /** Component-set name prefixes whose variants are exported in full (for button styles). */
  variantPrefixes: string[];
}

export interface ExportWarning {
  nodeId?: string;
  nodePath?: string;
  message: string;
}

export interface AssetDef {
  id: string;
  /** Path inside the bundle, always forward slashes, e.g. "images/Frame_Border.png". */
  path: string;
  /** Pixel size of the PNG. */
  width: number;
  height: number;
  /** Export scale the PNG was rendered at. width / scale == size in Slate units. */
  scale: number;
  /** Content hash of the PNG bytes (hex). Used to skip unchanged textures on re-sync. */
  hash: string;
  bytes: number;
}

export type NodeType =
  | 'FRAME'
  | 'GROUP'
  | 'COMPONENT'
  | 'COMPONENT_SET'
  | 'INSTANCE'
  | 'SECTION'
  | 'RECTANGLE'
  | 'ELLIPSE'
  | 'LINE'
  | 'TEXT'
  | 'VECTOR'
  | 'STAR'
  | 'POLYGON'
  | 'BOOLEAN_OPERATION'
  | 'OTHER';

export type Constraint = 'MIN' | 'MAX' | 'CENTER' | 'STRETCH' | 'SCALE';

export interface Rect {
  x: number;
  y: number;
  width: number;
  height: number;
}

export interface FNode {
  /** Figma node id. Stable across edits; the key for re-sync. */
  id: string;
  /** Raw layer name, tags included. */
  name: string;
  /** Layer name with [tags] removed and whitespace trimmed. Drives the Unreal widget name. */
  cleanName: string;
  /** Parsed [tags]; keys are lower-case. "[img 3x]" -> { img: "", scale: "3" }. */
  tags: Record<string, string>;
  type: NodeType;
  visible: boolean;
  opacity: number;
  rotation: number;
  x: number;
  y: number;
  width: number;
  height: number;
  /** Present when the parent is not an auto-layout frame (or this child is absolutely positioned). */
  constraints?: { horizontal: Constraint; vertical: Constraint };
  /** Present on auto-layout containers. */
  layout?: AutoLayout;
  /** Present when the parent is an auto-layout frame. */
  child?: ChildLayout;
  sizeLimits?: SizeLimits;
  clipsContent?: boolean;
  style?: NodeStyle;
  text?: TextInfo;
  component?: ComponentRef;
  /** The whole node (and its subtree) was rasterized to one image; children are omitted. */
  image?: RasterRef;
  /** Only the container's own background (fills/strokes/effects) was rasterized; children follow normally. */
  background?: RasterRef;
  /** componentPropertyReferences, e.g. { characters: "Label" } (the "#id" suffix is stripped). */
  propRefs?: Record<string, string>;
  /** True when this node is a mask for its following siblings. */
  isMask?: boolean;
  children?: FNode[];
}

export interface SizeLimits {
  minWidth?: number;
  maxWidth?: number;
  minHeight?: number;
  maxHeight?: number;
}

export interface AutoLayout {
  mode: 'HORIZONTAL' | 'VERTICAL';
  wrap: boolean;
  padding: { left: number; right: number; top: number; bottom: number };
  itemSpacing: number;
  counterAxisSpacing: number;
  primaryAlign: 'MIN' | 'CENTER' | 'MAX' | 'SPACE_BETWEEN';
  counterAlign: 'MIN' | 'CENTER' | 'MAX' | 'BASELINE';
  reverseZIndex: boolean;
}

export interface ChildLayout {
  positioning: 'AUTO' | 'ABSOLUTE';
  sizingH: 'FIXED' | 'HUG' | 'FILL';
  sizingV: 'FIXED' | 'HUG' | 'FILL';
  grow: number;
  alignSelf: 'INHERIT' | 'STRETCH';
}

export interface Color {
  r: number;
  g: number;
  b: number;
  a: number;
}

export type PaintType =
  | 'SOLID'
  | 'GRADIENT_LINEAR'
  | 'GRADIENT_RADIAL'
  | 'GRADIENT_ANGULAR'
  | 'GRADIENT_DIAMOND'
  | 'IMAGE'
  | 'VIDEO'
  | 'PATTERN'
  | 'OTHER';

export interface Paint {
  type: PaintType;
  visible: boolean;
  opacity: number;
  blendMode: string;
  color?: Color;
  stops?: { position: number; color: Color }[];
  scaleMode?: string;
}

export type EffectType = 'DROP_SHADOW' | 'INNER_SHADOW' | 'LAYER_BLUR' | 'BACKGROUND_BLUR' | 'OTHER';

export interface Effect {
  type: EffectType;
  visible: boolean;
  radius: number;
  color?: Color;
  offset?: { x: number; y: number };
  spread?: number;
}

export interface NodeStyle {
  fills: Paint[];
  strokes: Paint[];
  strokeWeight: number;
  /** Present only when the four sides differ. */
  strokeWeights?: { top: number; right: number; bottom: number; left: number };
  strokeAlign: 'INSIDE' | 'OUTSIDE' | 'CENTER';
  dashed: boolean;
  /** Top-left, top-right, bottom-right, bottom-left (the Slate CornerRadii order). */
  cornerRadius: [number, number, number, number];
  cornerSmoothing: number;
  effects: Effect[];
  blendMode: string;
  /** Figma style names, when the layer uses shared styles. */
  fillStyle?: string;
  strokeStyle?: string;
  effectStyle?: string;
  /**
   * True when the look is exactly expressible as a Slate rounded-box brush:
   * at most one solid fill (stacked solids are pre-composited into it), at most one uniform solid stroke,
   * and no visible effects.
   */
  simple: boolean;
}

export interface TextInfo {
  characters: string;
  fontFamily: string;
  /** Figma style name, e.g. "Bold", "SemiBold Italic". */
  fontStyle: string;
  fontWeight: number;
  italic: boolean;
  fontSize: number;
  /** Effective text color: first visible solid fill with paint opacity folded into alpha. */
  color: Color;
  alignH: 'LEFT' | 'CENTER' | 'RIGHT' | 'JUSTIFIED';
  alignV: 'TOP' | 'CENTER' | 'BOTTOM';
  autoResize: 'NONE' | 'WIDTH_AND_HEIGHT' | 'HEIGHT' | 'TRUNCATE';
  truncate: boolean;
  /** 0 means unlimited. */
  maxLines: number;
  letterSpacing: { value: number; unit: 'PIXELS' | 'PERCENT' };
  lineHeight: { value: number; unit: 'AUTO' | 'PIXELS' | 'PERCENT' };
  textCase: 'ORIGINAL' | 'UPPER' | 'LOWER' | 'TITLE' | 'SMALL_CAPS' | 'SMALL_CAPS_FORCED';
  decoration: 'NONE' | 'UNDERLINE' | 'STRIKETHROUGH';
  textStyle?: string;
  stroke?: { color: Color; weight: number };
  shadow?: { color: Color; offset: { x: number; y: number }; radius: number };
  /** More than one styled run. The dominant run's style is used above; runs are listed in segments. */
  mixed: boolean;
  segments?: TextSegment[];
}

export interface TextSegment {
  start: number;
  end: number;
  characters: string;
  fontFamily: string;
  fontStyle: string;
  fontWeight: number;
  fontSize: number;
  color: Color;
}

export interface ComponentRef {
  kind: 'INSTANCE' | 'COMPONENT' | 'COMPONENT_SET';
  componentId?: string;
  /** Main component name. For variants this is e.g. "State=Hover"; for plain components "Btn/Primary/Hover". */
  componentName?: string;
  componentSetId?: string;
  componentSetName?: string;
  variant?: Record<string, string>;
  /** Instance component properties (TEXT/BOOLEAN/INSTANCE_SWAP/VARIANT), "#id" suffixes stripped. */
  properties?: Record<string, { type: string; value: string | boolean }>;
  remote?: boolean;
}

export type RasterReason = 'tag' | 'vector' | 'effects' | 'gradient' | 'imageFill' | 'mask' | 'flip' | 'unsupported';

export interface RasterRef {
  asset: string;
  /** Where the PNG sits relative to the node's layout box. Can extend past it (shadows, outside strokes). */
  bounds: Rect;
  reason: RasterReason;
}

export interface ComponentSetDef {
  id: string;
  /** e.g. "Btn/Primary" */
  name: string;
  remote: boolean;
  /** Property names in declaration order, e.g. ["State", "Size"]. */
  propertyNames: string[];
  variants: VariantDef[];
}

export interface VariantDef {
  id: string;
  name: string;
  properties: Record<string, string>;
  node: FNode;
}
