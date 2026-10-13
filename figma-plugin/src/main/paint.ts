import type { Color, Effect as BEffect, Paint as BPaint, PaintType, RasterReason } from '../shared/bundle';
import { round2 } from './geometry';

const NORMAL_BLENDS = new Set(['NORMAL', 'PASS_THROUGH']);
const GRADIENTS = new Set(['GRADIENT_LINEAR', 'GRADIENT_RADIAL', 'GRADIENT_ANGULAR', 'GRADIENT_DIAMOND']);
const KNOWN_PAINTS = new Set<string>(['SOLID', ...GRADIENTS, 'IMAGE', 'VIDEO', 'PATTERN']);

function round4(v: number): number {
  return Math.round(v * 10000) / 10000;
}

export function color(r: number, g: number, b: number, a = 1): Color {
  return { r: round4(r), g: round4(g), b: round4(b), a: round4(a) };
}

/** Serializes Figma paints. `figma.mixed` (a symbol) or undefined yields an empty list. */
export function serializePaints(paints: ReadonlyArray<Paint> | symbol | undefined): BPaint[] {
  if (!paints || typeof paints === 'symbol') return [];
  return paints.map(serializePaint);
}

export function serializePaint(p: Paint): BPaint {
  const type = (KNOWN_PAINTS.has(p.type) ? p.type : 'OTHER') as PaintType;
  const out: BPaint = {
    type,
    visible: p.visible !== false,
    opacity: round4(p.opacity ?? 1),
    blendMode: p.blendMode ?? 'NORMAL',
  };
  if (p.type === 'SOLID') {
    out.color = color(p.color.r, p.color.g, p.color.b, 1);
  } else if (GRADIENTS.has(p.type)) {
    const g = p as GradientPaint;
    out.stops = g.gradientStops.map((s) => ({ position: round4(s.position), color: color(s.color.r, s.color.g, s.color.b, s.color.a) }));
  } else if (p.type === 'IMAGE') {
    out.scaleMode = (p as ImagePaint).scaleMode;
  }
  return out;
}

export function serializeEffects(effects: ReadonlyArray<Effect> | undefined): BEffect[] {
  if (!effects) return [];
  return effects.map((e) => {
    switch (e.type) {
      case 'DROP_SHADOW':
      case 'INNER_SHADOW':
        return {
          type: e.type,
          visible: e.visible !== false,
          radius: round2(e.radius),
          color: color(e.color.r, e.color.g, e.color.b, e.color.a),
          offset: { x: round2(e.offset.x), y: round2(e.offset.y) },
          spread: round2(e.spread ?? 0),
        };
      case 'LAYER_BLUR':
      case 'BACKGROUND_BLUR':
        return { type: e.type, visible: e.visible !== false, radius: round2(e.radius) };
      default:
        return { type: 'OTHER', visible: (e as { visible?: boolean }).visible !== false, radius: 0 };
    }
  });
}

export function isVisiblePaint(p: BPaint): boolean {
  return p.visible && p.opacity > 0.0005;
}

/**
 * Source-over composite of solid paints, bottom (index 0) to top, in sRGB like Figma does.
 * Returns null when there is nothing to draw.
 */
export function compositeSolids(paints: BPaint[]): { color: Color; opacity: number } | null {
  let r = 0;
  let g = 0;
  let b = 0;
  let a = 0;
  let any = false;
  for (const p of paints) {
    if (p.type !== 'SOLID' || !p.color || !isVisiblePaint(p)) continue;
    any = true;
    const sa = p.opacity * p.color.a;
    const outA = sa + a * (1 - sa);
    if (outA <= 0) continue;
    r = (p.color.r * sa + r * a * (1 - sa)) / outA;
    g = (p.color.g * sa + g * a * (1 - sa)) / outA;
    b = (p.color.b * sa + b * a * (1 - sa)) / outA;
    a = outA;
  }
  if (!any || a <= 0) return null;
  return { color: color(r, g, b, 1), opacity: round4(a) };
}

export interface StyleVerdict {
  simple: boolean;
  /** Why the look can't be a rounded-box brush. */
  reason?: RasterReason;
  detail?: string;
}

/**
 * Decides whether a fill/stroke/effect combination maps exactly onto a Slate rounded-box brush.
 */
export function classifyStyle(input: {
  fills: BPaint[];
  strokes: BPaint[];
  effects: BEffect[];
  uniformStroke: boolean;
  dashed: boolean;
  blendMode: string;
}): StyleVerdict {
  const fills = input.fills.filter(isVisiblePaint);
  const strokes = input.strokes.filter(isVisiblePaint);
  const effects = input.effects.filter((e) => e.visible);

  for (const p of [...fills, ...strokes]) {
    if (p.type === 'IMAGE' || p.type === 'VIDEO' || p.type === 'PATTERN') {
      return { simple: false, reason: 'imageFill', detail: `${p.type.toLowerCase()} paint` };
    }
    if (GRADIENTS.has(p.type)) {
      return { simple: false, reason: 'gradient', detail: p.type.toLowerCase().replace('_', ' ') };
    }
    if (p.type === 'OTHER') {
      return { simple: false, reason: 'unsupported', detail: 'unknown paint type' };
    }
    if (!NORMAL_BLENDS.has(p.blendMode)) {
      return { simple: false, reason: 'unsupported', detail: `paint blend mode ${p.blendMode}` };
    }
  }
  if (strokes.length > 1) return { simple: false, reason: 'unsupported', detail: 'multiple strokes' };
  if (strokes.length === 1 && !input.uniformStroke) return { simple: false, reason: 'unsupported', detail: 'per-side stroke weights' };
  if (strokes.length === 1 && input.dashed) return { simple: false, reason: 'unsupported', detail: 'dashed stroke' };
  if (effects.length > 0) {
    return { simple: false, reason: 'effects', detail: effects.map((e) => e.type.toLowerCase().replace('_', ' ')).join(', ') };
  }
  if (!NORMAL_BLENDS.has(input.blendMode)) return { simple: false, reason: 'unsupported', detail: `layer blend mode ${input.blendMode}` };
  return { simple: true };
}

/** First visible solid paint as a color with paint opacity folded into alpha (used for text). */
export function firstSolidColor(paints: BPaint[]): Color | null {
  for (let i = paints.length - 1; i >= 0; i--) {
    const p = paints[i];
    if (p.type === 'SOLID' && p.color && isVisiblePaint(p)) {
      return color(p.color.r, p.color.g, p.color.b, p.color.a * p.opacity);
    }
  }
  // Fall back to the first gradient stop so text never silently turns black.
  for (const p of paints) {
    if (p.stops && p.stops.length > 0 && isVisiblePaint(p)) {
      const c = p.stops[0].color;
      return color(c.r, c.g, c.b, c.a * p.opacity);
    }
  }
  return null;
}
