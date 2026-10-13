import { describe, expect, it } from 'vitest';
import type { Paint } from '../src/shared/bundle';
import { classifyStyle, compositeSolids, firstSolidColor } from '../src/main/paint';

const solid = (r: number, g: number, b: number, opacity = 1): Paint => ({ type: 'SOLID', visible: true, opacity, blendMode: 'NORMAL', color: { r, g, b, a: 1 } });
const base = { strokes: [] as Paint[], effects: [], uniformStroke: true, dashed: false, blendMode: 'PASS_THROUGH' };

describe('classifyStyle', () => {
  it('accepts a flat rounded box', () => {
    expect(classifyStyle({ ...base, fills: [solid(1, 0, 0)], strokes: [solid(0, 0, 0)] }).simple).toBe(true);
  });

  it('flags gradients, image fills, effects and odd strokes', () => {
    const gradient: Paint = { type: 'GRADIENT_LINEAR', visible: true, opacity: 1, blendMode: 'NORMAL', stops: [] };
    expect(classifyStyle({ ...base, fills: [gradient] })).toMatchObject({ simple: false, reason: 'gradient' });
    expect(classifyStyle({ ...base, fills: [{ type: 'IMAGE', visible: true, opacity: 1, blendMode: 'NORMAL' }] })).toMatchObject({ reason: 'imageFill' });
    expect(classifyStyle({ ...base, fills: [], effects: [{ type: 'DROP_SHADOW', visible: true, radius: 4 }] })).toMatchObject({ reason: 'effects' });
    expect(classifyStyle({ ...base, fills: [], strokes: [solid(0, 0, 0)], uniformStroke: false })).toMatchObject({ reason: 'unsupported' });
    expect(classifyStyle({ ...base, fills: [], strokes: [solid(0, 0, 0)], dashed: true })).toMatchObject({ reason: 'unsupported' });
  });

  it('ignores hidden paints and effects', () => {
    const hiddenGradient: Paint = { type: 'GRADIENT_LINEAR', visible: false, opacity: 1, blendMode: 'NORMAL', stops: [] };
    expect(classifyStyle({ ...base, fills: [hiddenGradient], effects: [{ type: 'DROP_SHADOW', visible: false, radius: 4 }] }).simple).toBe(true);
  });
});

describe('compositeSolids', () => {
  it('composites bottom to top', () => {
    const out = compositeSolids([solid(1, 1, 1), solid(0, 0, 0, 0.5)]);
    expect(out?.opacity).toBe(1);
    expect(out?.color.r).toBeCloseTo(0.5);
  });

  it('accumulates alpha', () => {
    const out = compositeSolids([solid(1, 0, 0, 0.5), solid(1, 0, 0, 0.5)]);
    expect(out?.opacity).toBeCloseTo(0.75);
    expect(out?.color.r).toBeCloseTo(1);
  });

  it('returns null for nothing visible', () => {
    expect(compositeSolids([])).toBeNull();
    expect(compositeSolids([{ ...solid(1, 0, 0), visible: false }])).toBeNull();
  });
});

describe('firstSolidColor', () => {
  it('uses the top-most visible solid and folds opacity into alpha', () => {
    expect(firstSolidColor([solid(1, 0, 0), solid(0, 1, 0, 0.5)])).toEqual({ r: 0, g: 1, b: 0, a: 0.5 });
  });
});
