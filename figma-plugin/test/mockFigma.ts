/**
 * A tiny fake of the Figma node graph: just enough properties for FrameExporter. Nodes are plain objects
 * cast to SceneNode, with absolute transforms computed from parent-relative positions like Figma does.
 */
import type { Rect } from '../src/shared/bundle';
import type { ExportHost } from '../src/main/walker';
import { parseLayerName } from '../src/main/tags';
import { makePng, type Rgba } from './png';

type Mat = [[number, number, number], [number, number, number]];

export interface MockNode {
  id: string;
  name: string;
  type: string;
  visible: boolean;
  opacity: number;
  width: number;
  height: number;
  absoluteTransform: Mat;
  absoluteBoundingBox: Rect;
  absoluteRenderBounds: Rect;
  parent: MockNode | null;
  children?: MockNode[];
  [key: string]: unknown;
}

export interface Common {
  x?: number;
  y?: number;
  w: number;
  h: number;
  rotation?: number;
  visible?: boolean;
  opacity?: number;
  [key: string]: unknown;
}

let nextId = 1;
export function resetIds(): void {
  nextId = 1;
}

export function solid(hexColor: string, opacity = 1): Paint {
  const v = hexColor.replace('#', '');
  return {
    type: 'SOLID',
    visible: true,
    opacity,
    blendMode: 'NORMAL',
    color: { r: parseInt(v.slice(0, 2), 16) / 255, g: parseInt(v.slice(2, 4), 16) / 255, b: parseInt(v.slice(4, 6), 16) / 255 },
  } as SolidPaint;
}

export function linear(from: string, to: string): Paint {
  const c = (h: string) => {
    const v = h.replace('#', '');
    return { r: parseInt(v.slice(0, 2), 16) / 255, g: parseInt(v.slice(2, 4), 16) / 255, b: parseInt(v.slice(4, 6), 16) / 255, a: 1 };
  };
  return {
    type: 'GRADIENT_LINEAR',
    visible: true,
    opacity: 1,
    blendMode: 'NORMAL',
    gradientTransform: [
      [1, 0, 0],
      [0, 1, 0],
    ],
    gradientStops: [
      { position: 0, color: c(from) },
      { position: 1, color: c(to) },
    ],
  } as GradientPaint;
}

export function dropShadow(alpha = 0.5, radius = 24, y = 8): Effect {
  return { type: 'DROP_SHADOW', visible: true, radius, spread: 0, offset: { x: 0, y }, color: { r: 0, g: 0, b: 0, a: alpha }, blendMode: 'NORMAL', showShadowBehindNode: false } as DropShadowEffect;
}

const DEFAULTS: Record<string, unknown> = {
  fills: [],
  strokes: [],
  effects: [],
  strokeWeight: 1,
  strokeAlign: 'INSIDE',
  dashPattern: [],
  cornerRadius: 0,
  cornerSmoothing: 0,
  blendMode: 'PASS_THROUGH',
  isMask: false,
  constraints: { horizontal: 'MIN', vertical: 'MIN' },
  layoutPositioning: 'AUTO',
  layoutGrow: 0,
  layoutAlign: 'INHERIT',
  layoutSizingHorizontal: 'FIXED',
  layoutSizingVertical: 'FIXED',
  componentPropertyReferences: null,
  fillStyleId: '',
  strokeStyleId: '',
  effectStyleId: '',
};

function make(type: string, name: string, c: Common, children?: MockNode[], extra: Record<string, unknown> = {}): MockNode {
  const { x = 0, y = 0, w, h, rotation = 0, visible = true, opacity = 1, ...rest } = c;
  const node: MockNode = {
    ...DEFAULTS,
    ...extra,
    ...rest,
    id: `${nextId++}:1`,
    name,
    type,
    visible,
    opacity,
    width: w,
    height: h,
    // Filled in by place().
    absoluteTransform: [
      [1, 0, 0],
      [0, 1, 0],
    ],
    absoluteBoundingBox: { x: 0, y: 0, width: w, height: h },
    absoluteRenderBounds: { x: 0, y: 0, width: w, height: h },
    parent: null,
    _local: { x, y, rotation },
  };
  if (children) {
    node.children = children;
    for (const child of children) child.parent = node;
  }
  return node;
}

export const frame = (name: string, c: Common, children: MockNode[] = [], extra: Record<string, unknown> = {}) =>
  make('FRAME', name, c, children, { layoutMode: 'NONE', clipsContent: true, paddingLeft: 0, paddingRight: 0, paddingTop: 0, paddingBottom: 0, itemSpacing: 0, counterAxisSpacing: 0, primaryAxisAlignItems: 'MIN', counterAxisAlignItems: 'MIN', layoutWrap: 'NO_WRAP', itemReverseZIndex: false, minWidth: null, maxWidth: null, minHeight: null, maxHeight: null, ...extra });
export const group = (name: string, c: Common, children: MockNode[]) => make('GROUP', name, c, children);
export const rect = (name: string, c: Common) => make('RECTANGLE', name, c);
export const ellipse = (name: string, c: Common) => make('ELLIPSE', name, c, undefined, { arcData: { startingAngle: 0, endingAngle: Math.PI * 2, innerRadius: 0 } });
export const vector = (name: string, c: Common) => make('VECTOR', name, c);
export const line = (name: string, c: Common) => make('LINE', name, c);

export interface TextOpts extends Common {
  characters: string;
  family?: string;
  style?: string;
  weight?: number;
  size?: number;
  color?: string;
  align?: 'LEFT' | 'CENTER' | 'RIGHT' | 'JUSTIFIED';
  valign?: 'TOP' | 'CENTER' | 'BOTTOM';
  autoResize?: 'NONE' | 'WIDTH_AND_HEIGHT' | 'HEIGHT' | 'TRUNCATE';
  textCase?: string;
  letterSpacing?: { value: number; unit: 'PIXELS' | 'PERCENT' };
}

export function text(name: string, o: TextOpts): MockNode {
  const fills = [solid(o.color ?? '#FFFFFF')];
  const fontName = { family: o.family ?? 'Inter', style: o.style ?? 'Regular' };
  const node = make('TEXT', name, o, undefined, {
    characters: o.characters,
    fontName,
    fontSize: o.size ?? 16,
    fontWeight: o.weight ?? 400,
    fills,
    textAlignHorizontal: o.align ?? 'LEFT',
    textAlignVertical: o.valign ?? 'TOP',
    textAutoResize: o.autoResize ?? 'WIDTH_AND_HEIGHT',
    textTruncation: 'DISABLED',
    maxLines: null,
    letterSpacing: o.letterSpacing ?? { value: 0, unit: 'PIXELS' },
    lineHeight: { unit: 'AUTO' },
    textCase: o.textCase ?? 'ORIGINAL',
    textDecoration: 'NONE',
    textStyleId: '',
  });
  node.getStyledTextSegments = () => [
    {
      characters: o.characters,
      start: 0,
      end: o.characters.length,
      fontName,
      fontSize: o.size ?? 16,
      fontWeight: o.weight ?? 400,
      fills,
      letterSpacing: o.letterSpacing ?? { value: 0, unit: 'PIXELS' },
      lineHeight: { unit: 'AUTO' },
      textCase: o.textCase ?? 'ORIGINAL',
      textDecoration: 'NONE',
      textStyleId: '',
    },
  ];
  return node;
}

/** A component (optionally inside a set built with componentSet()). */
export function component(name: string, c: Common, children: MockNode[] = [], extra: Record<string, unknown> = {}): MockNode {
  const node = frame(name, c, children, extra);
  node.type = 'COMPONENT';
  node.remote = false;
  node.variantProperties = null;
  return node;
}

export function componentSet(name: string, c: Common, variants: MockNode[]): MockNode {
  const set = make('COMPONENT_SET', name, c, variants, { layoutMode: 'NONE', clipsContent: false, remote: false });
  const definitions: Record<string, { type: string; defaultValue: string; variantOptions: string[] }> = {};
  for (const v of variants) {
    const props: Record<string, string> = {};
    for (const part of v.name.split(',')) {
      const [k, val] = part.split('=').map((s) => s.trim());
      if (k && val) {
        props[k] = val;
        definitions[k] = definitions[k] ?? { type: 'VARIANT', defaultValue: val, variantOptions: [] };
        definitions[k].variantOptions.push(val);
      }
    }
    v.variantProperties = props;
  }
  set.componentPropertyDefinitions = definitions;
  return set;
}

/** An instance: a deep copy of the main component's subtree with instance ids, like Figma creates. */
export function instance(name: string, main: MockNode, c: Common, overrides: Record<string, string> = {}): MockNode {
  const cloneChildren = (n: MockNode, prefix: string): MockNode[] =>
    (n.children ?? []).map((child) => {
      const copy: MockNode = { ...child, id: `I${prefix};${child.id}`, parent: null };
      if (child.type === 'TEXT' && overrides[child.name] !== undefined) {
        const chars = overrides[child.name];
        copy.characters = chars;
        const segs = (child.getStyledTextSegments as () => Array<Record<string, unknown>>)();
        copy.getStyledTextSegments = () => segs.map((s) => ({ ...s, characters: chars, end: chars.length }));
      }
      if (child.children) {
        copy.children = cloneChildren(child, prefix);
        for (const cc of copy.children) cc.parent = copy;
      }
      return copy;
    });
  const node = make('INSTANCE', name, c, undefined, {
    ...Object.fromEntries(Object.entries(main).filter(([k]) => !['id', 'name', 'type', 'children', 'parent', 'absoluteTransform', 'width', 'height', '_local'].includes(k))),
  });
  node.children = cloneChildren(main, node.id);
  for (const child of node.children) child.parent = node;
  node.componentProperties = Object.fromEntries(Object.entries(overrides).map(([k, v]) => [`${k}#${node.id}`, { type: 'TEXT', value: v }]));
  node._main = main;
  return node;
}

/** Computes absolute transforms/bounds for a tree, starting at a page origin. */
export function place(root: MockNode, origin = { x: 0, y: 0 }): MockNode {
  const visit = (node: MockNode, parentAbs: Mat) => {
    const local = node._local as { x: number; y: number; rotation: number };
    // Figma rotation is counter-clockwise; a CCW rotation by r in y-down space is [[cos, sin], [-sin, cos]].
    const r = (local.rotation * Math.PI) / 180;
    const cos = Math.cos(r);
    const sin = Math.sin(r);
    const localMat: Mat = [
      [cos, sin, local.x],
      [-sin, cos, local.y],
    ];
    const abs: Mat = [
      [
        parentAbs[0][0] * localMat[0][0] + parentAbs[0][1] * localMat[1][0],
        parentAbs[0][0] * localMat[0][1] + parentAbs[0][1] * localMat[1][1],
        parentAbs[0][0] * localMat[0][2] + parentAbs[0][1] * localMat[1][2] + parentAbs[0][2],
      ],
      [
        parentAbs[1][0] * localMat[0][0] + parentAbs[1][1] * localMat[1][0],
        parentAbs[1][0] * localMat[0][1] + parentAbs[1][1] * localMat[1][1],
        parentAbs[1][0] * localMat[0][2] + parentAbs[1][1] * localMat[1][2] + parentAbs[1][2],
      ],
    ];
    node.absoluteTransform = abs;
    const corners = [
      [0, 0],
      [node.width, 0],
      [0, node.height],
      [node.width, node.height],
    ].map(([x, y]) => ({ x: abs[0][0] * x + abs[0][1] * y + abs[0][2], y: abs[1][0] * x + abs[1][1] * y + abs[1][2] }));
    const minX = Math.min(...corners.map((p) => p.x));
    const minY = Math.min(...corners.map((p) => p.y));
    const bb = { x: minX, y: minY, width: Math.max(...corners.map((p) => p.x)) - minX, height: Math.max(...corners.map((p) => p.y)) - minY };
    node.absoluteBoundingBox = bb;
    const shadow = ((node.effects as Effect[]) ?? []).find((e) => e.type === 'DROP_SHADOW' && e.visible) as DropShadowEffect | undefined;
    node.absoluteRenderBounds = shadow
      ? {
          x: bb.x - shadow.radius + shadow.offset.x,
          y: bb.y - shadow.radius + shadow.offset.y,
          width: bb.width + shadow.radius * 2,
          height: bb.height + shadow.radius * 2,
        }
      : bb;
    for (const child of node.children ?? []) visit(child, abs);
  };
  visit(root, [
    [1, 0, origin.x],
    [0, 1, origin.y],
  ]);
  return root;
}

export function asScene(node: MockNode): SceneNode {
  return node as unknown as SceneNode;
}

export interface MockHostOptions {
  styles?: Record<string, string>;
  /** Custom art for rasterized nodes, keyed by layer name. */
  art?: Record<string, (x: number, y: number, w: number, h: number) => Rgba>;
}

export function mockHost(opts: MockHostOptions = {}): ExportHost & { exported: string[] } {
  const exported: string[] = [];
  const paint = (rawName: string, w: number, h: number) => {
    const suffix = rawName.endsWith('#bg') ? '#bg' : '';
    const art = opts.art?.[parseLayerName(rawName.slice(0, rawName.length - suffix.length)).cleanName + suffix];
    return makePng(Math.max(1, Math.round(w)), Math.max(1, Math.round(h)), (x, y) => (art ? art(x, y, w, h) : [200, 160, 80, 255]));
  };
  return {
    exported,
    async styleName(id) {
      return opts.styles?.[id];
    },
    async mainComponent(node) {
      return ((node as unknown as MockNode)._main as unknown as ComponentNode) ?? null;
    },
    async exportPng(node, scale) {
      const n = node as unknown as MockNode;
      exported.push(n.name);
      const rb = n.absoluteRenderBounds;
      return { bytes: paint(n.name, rb.width * scale, rb.height * scale), absBounds: rb };
    },
    async exportBackgroundPng(node, scale) {
      const n = node as unknown as MockNode;
      exported.push(`${n.name}#bg`);
      const bb = n.absoluteBoundingBox;
      const rb = n.absoluteRenderBounds;
      return {
        bytes: paint(`${n.name}#bg`, rb.width * scale, rb.height * scale),
        bounds: { x: rb.x - bb.x, y: rb.y - bb.y, width: rb.width, height: rb.height },
      };
    },
    async ensurePageLoaded() {},
    progress() {},
  };
}
