import type { Rect } from '../shared/bundle';

/**
 * Figma affine transform: [[m00, m01, m02], [m10, m11, m12]]
 *   x' = m00 * x + m01 * y + m02
 *   y' = m10 * x + m11 * y + m12
 */
export type Mat = [[number, number, number], [number, number, number]];

export const IDENTITY: Mat = [
  [1, 0, 0],
  [0, 1, 0],
];

export function invert(m: Mat): Mat {
  const [[a, c, e], [b, d, f]] = m;
  const det = a * d - b * c;
  if (Math.abs(det) < 1e-12) {
    // Degenerate (zero-size) transform: fall back to a pure translation inverse.
    return [
      [1, 0, -e],
      [0, 1, -f],
    ];
  }
  const ia = d / det;
  const ic = -c / det;
  const ib = -b / det;
  const id = a / det;
  return [
    [ia, ic, -(ia * e + ic * f)],
    [ib, id, -(ib * e + id * f)],
  ];
}

/** Returns a ∘ b (apply b first, then a). */
export function multiply(a: Mat, b: Mat): Mat {
  return [
    [
      a[0][0] * b[0][0] + a[0][1] * b[1][0],
      a[0][0] * b[0][1] + a[0][1] * b[1][1],
      a[0][0] * b[0][2] + a[0][1] * b[1][2] + a[0][2],
    ],
    [
      a[1][0] * b[0][0] + a[1][1] * b[1][0],
      a[1][0] * b[0][1] + a[1][1] * b[1][1],
      a[1][0] * b[0][2] + a[1][1] * b[1][2] + a[1][2],
    ],
  ];
}

export function apply(m: Mat, x: number, y: number): { x: number; y: number } {
  return { x: m[0][0] * x + m[0][1] * y + m[0][2], y: m[1][0] * x + m[1][1] * y + m[1][2] };
}

/** Child transform expressed in the parent's local space. */
export function relativeTransform(parentAbs: Mat, childAbs: Mat): Mat {
  return multiply(invert(parentAbs), childAbs);
}

export interface LayoutBox {
  x: number;
  y: number;
  width: number;
  height: number;
  /** Clockwise degrees, rotation about the box center (UMG convention). */
  rotation: number;
  flipped: boolean;
}

/**
 * Converts a node transform (in parent space) into a UMG-style layout box: an unrotated box whose
 * center matches the node's center, plus a rotation about that center.
 */
export function layoutBoxFromTransform(rel: Mat, width: number, height: number): LayoutBox {
  const [[a, c, e], [b, d, f]] = rel;
  const cx = a * (width / 2) + c * (height / 2) + e;
  const cy = b * (width / 2) + d * (height / 2) + f;
  const det = a * d - b * c;
  const rotation = normalizeAngle((Math.atan2(b, a) * 180) / Math.PI);
  return {
    x: round2(cx - width / 2),
    y: round2(cy - height / 2),
    width: round2(width),
    height: round2(height),
    rotation: round2(rotation),
    flipped: det < 0,
  };
}

/** Axis-aligned bounds, in the parent's local space, of an absolute-space rect. */
export function rectRelativeTo(parentAbs: Mat, abs: Rect): Rect {
  const inv = invert(parentAbs);
  const corners = [
    apply(inv, abs.x, abs.y),
    apply(inv, abs.x + abs.width, abs.y),
    apply(inv, abs.x, abs.y + abs.height),
    apply(inv, abs.x + abs.width, abs.y + abs.height),
  ];
  const xs = corners.map((p) => p.x);
  const ys = corners.map((p) => p.y);
  const minX = Math.min(...xs);
  const minY = Math.min(...ys);
  return {
    x: round2(minX),
    y: round2(minY),
    width: round2(Math.max(...xs) - minX),
    height: round2(Math.max(...ys) - minY),
  };
}

/** Maps an angle to (-180, 180]. */
export function normalizeAngle(deg: number): number {
  let r = deg % 360;
  if (r <= -180) r += 360;
  if (r > 180) r -= 360;
  return r;
}

/** Rounds to 2 decimals and normalizes -0, so exports are stable and diff-friendly. */
export function round2(v: number): number {
  const r = Math.round(v * 100) / 100;
  return Object.is(r, -0) ? 0 : r;
}

export function isAxisAligned(rotation: number): boolean {
  const r = Math.abs(normalizeAngle(rotation));
  return r < 0.01 || Math.abs(r - 90) < 0.01 || Math.abs(r - 180) < 0.01;
}
