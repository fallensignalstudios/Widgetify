import { describe, expect, it } from 'vitest';
import { invert, layoutBoxFromTransform, multiply, rectRelativeTo, relativeTransform, type Mat } from '../src/main/geometry';

const translate = (x: number, y: number): Mat => [
  [1, 0, x],
  [0, 1, y],
];

/** Figma-style counter-clockwise rotation by `deg` with origin at (x, y). */
const rotateCcw = (deg: number, x = 0, y = 0): Mat => {
  const r = (deg * Math.PI) / 180;
  return [
    [Math.cos(r), Math.sin(r), x],
    [-Math.sin(r), Math.cos(r), y],
  ];
};

describe('affine helpers', () => {
  it('inverts and multiplies', () => {
    const m = multiply(translate(10, 20), rotateCcw(30));
    const id = multiply(invert(m), m);
    expect(id[0][0]).toBeCloseTo(1);
    expect(id[0][1]).toBeCloseTo(0);
    expect(id[1][1]).toBeCloseTo(1);
    expect(id[0][2]).toBeCloseTo(0);
    expect(id[1][2]).toBeCloseTo(0);
  });
});

describe('layoutBoxFromTransform', () => {
  it('passes through unrotated boxes', () => {
    const rel = relativeTransform(translate(100, 100), translate(150, 130));
    expect(layoutBoxFromTransform(rel, 40, 20)).toEqual({ x: 50, y: 30, width: 40, height: 20, rotation: 0, flipped: false });
  });

  it('converts Figma corner rotation into UMG center rotation', () => {
    // A 100x20 bar rotated 90deg counter-clockwise around its top-left corner at (200, 200).
    const box = layoutBoxFromTransform(rotateCcw(90, 200, 200), 100, 20);
    // Figma CCW 90 == UMG -90 (clockwise-positive).
    expect(box.rotation).toBe(-90);
    // The bar's center ends up at (210, 150); the unrotated layout box is centered there.
    expect(box.x + box.width / 2).toBeCloseTo(210);
    expect(box.y + box.height / 2).toBeCloseTo(150);
    expect(box.width).toBe(100);
  });

  it('detects mirroring', () => {
    const flipped: Mat = [
      [-1, 0, 50],
      [0, 1, 0],
    ];
    expect(layoutBoxFromTransform(flipped, 10, 10).flipped).toBe(true);
  });
});

describe('rectRelativeTo', () => {
  it('maps absolute rects into parent space', () => {
    expect(rectRelativeTo(translate(10, 10), { x: 20, y: 30, width: 5, height: 6 })).toEqual({ x: 10, y: 20, width: 5, height: 6 });
  });
});
