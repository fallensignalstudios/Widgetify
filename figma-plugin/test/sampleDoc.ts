/**
 * A Sovereign Call style main menu built with the mock Figma API. Exercises canvas anchoring, auto-layout,
 * component variants, text, rasterized art, background rasters, hidden layers, rotation and tags.
 */
import { componentSet, component, dropShadow, ellipse, frame, group, instance, line, linear, place, rect, resetIds, solid, text, vector, type MockNode } from './mockFigma';
import { hex, mix, type Rgba } from './png';

const GOLD = '#E8C27A';

function smoothstep(e0: number, e1: number, x: number): number {
  const t = Math.min(1, Math.max(0, (x - e0) / (e1 - e0)));
  return t * t * (3 - 2 * t);
}

export const sampleArt: Record<string, (x: number, y: number, w: number, h: number) => Rgba> = {
  Bg_Gradient: (x, y, w, h) => {
    const t = y / h;
    const base = mix(hex('#1B2536'), hex('#05070B'), t);
    const dx = (x - w / 2) / (w / 2);
    const dy = (y - h * 0.45) / (h / 2);
    const vignette = smoothstep(0.4, 1.4, Math.sqrt(dx * dx + dy * dy));
    return mix(base, hex('#000000'), vignette * 0.7);
  },
  Logo: (x, y, w, h) => {
    const cx = w / 2;
    const cy = h / 2;
    const r = Math.min(w, h) * 0.42;
    const d = Math.hypot(x - cx, y - cy);
    const ring = 1 - smoothstep(r * 0.04, r * 0.07, Math.abs(d - r));
    const diamond = 1 - smoothstep(r * 0.5, r * 0.53, Math.abs(x - cx) + Math.abs(y - cy) * 1.3);
    const a = Math.max(ring, diamond * 0.9);
    const shade = mix(hex(GOLD), hex('#FFF4D6'), 1 - y / h);
    return [shade[0], shade[1], shade[2], Math.round(a * 255)];
  },
  Img_Glow: (x, y, w, h) => {
    const d = Math.hypot(x - w / 2, y - h / 2) / (w / 2);
    const a = (1 - smoothstep(0, 1, d)) * 0.55;
    const c = hex(GOLD);
    return [c[0], c[1], c[2], Math.round(a * 255)];
  },
  Ico_Back: (x, y, w, h) => {
    // A chevron pointing left.
    const u = x / w;
    const v = y / h;
    const dist = Math.abs(u - (0.38 + Math.abs(v - 0.5) * 0.6));
    const a = (1 - smoothstep(0.06, 0.1, dist)) * (v > 0.15 && v < 0.85 ? 1 : 0);
    return [242, 230, 204, Math.round(a * 255)];
  },
  Ico_Star: (x, y, w, h) => {
    const a = Math.atan2(y - h / 2, x - w / 2);
    const rr = (0.32 + 0.16 * Math.cos(5 * a)) * w;
    const inside = 1 - smoothstep(rr - 1, rr + 1, Math.hypot(x - w / 2, y - h / 2));
    const c = hex(GOLD);
    return [c[0], c[1], c[2], Math.round(inside * 255)];
  },
  'Pnl_Menu#bg': (x, y, w, _h) => {
    // Shadowed rounded panel: 24 px shadow margin around a 400 x 368 box, offset 8 px down (at 2x scale).
    const s = w / (400 + 48);
    const px = x / s - 24;
    const py = y / s - 16;
    const rx = Math.max(0, Math.abs(px - 200) - (200 - 16));
    const ry = Math.max(0, Math.abs(py - 184) - (184 - 16));
    const sd = Math.hypot(rx, ry) - 16;
    const qx = Math.max(0, Math.abs(px - 200) - (200 - 16));
    const qy = Math.max(0, Math.abs(py + 8 - 184) - (184 - 16));
    const shadow = (1 - smoothstep(-8, 24, Math.hypot(qx, qy) - 16)) * 0.55;
    const body = 1 - smoothstep(-0.5, 0.5, sd);
    const border = (1 - smoothstep(0, 1.2, Math.abs(sd + 0.5))) * 0.45;
    const fill = mix(hex('#121826', Math.round(255 * 0.92)), hex(GOLD, 255), border);
    const outA = body * (fill[3] / 255) + shadow * (1 - body);
    if (outA <= 0) return [0, 0, 0, 0];
    const rgb = [0, 1, 2].map((i) => Math.round((fill[i] * body * (fill[3] / 255)) / outA));
    return [rgb[0], rgb[1], rgb[2], Math.round(outA * 255)];
  },
};

export interface SampleDoc {
  root: MockNode;
  buttonSet: MockNode;
}

export function buildSampleDoc(): SampleDoc {
  resetIds();

  const label = (fill: string) =>
    text('Label', {
      w: 120,
      h: 32,
      characters: 'BUTTON',
      family: 'Cinzel',
      style: 'SemiBold',
      weight: 600,
      size: 24,
      color: fill,
      align: 'CENTER',
      textCase: 'UPPER',
      letterSpacing: { value: 10, unit: 'PERCENT' },
      componentPropertyReferences: { characters: 'Label#0:1' },
      layoutSizingHorizontal: 'HUG',
      layoutSizingVertical: 'HUG',
    });

  const variant = (state: string, bg: string, bgOpacity: number, strokeOpacity: number, textColor: string, x: number) =>
    component(
      `State=${state}`,
      {
        x,
        y: 0,
        w: 400,
        h: 64,
        fills: [solid(bg, bgOpacity)],
        strokes: [solid(GOLD, strokeOpacity)],
        strokeWeight: 1.5,
        cornerRadius: 8,
      },
      [label(textColor)],
      { layoutMode: 'HORIZONTAL', paddingLeft: 24, paddingRight: 24, paddingTop: 16, paddingBottom: 16, itemSpacing: 12, primaryAxisAlignItems: 'CENTER', counterAxisAlignItems: 'CENTER' },
    );

  const buttonSet = componentSet('Btn/Primary', { w: 1700, h: 64 }, [
    variant('Default', '#1E2738', 1, 0.6, '#F2E6CC', 0),
    variant('Hover', '#2A3550', 1, 1, '#FFFFFF', 430),
    variant('Pressed', '#151B27', 1, 1, '#E8C27A', 860),
    variant('Disabled', '#1E2738', 0.5, 0.2, '#6B7280', 1290),
  ]);
  place(buttonSet, { x: 4000, y: 0 });

  const [vDefault, , , vDisabled] = buttonSet.children!;
  const menuButton = (name: string, main: MockNode, chars: string, y: number) =>
    instance(name, main, { x: 32, y, w: 336, h: 64, layoutSizingHorizontal: 'FILL', layoutSizingVertical: 'FIXED' }, { Label: chars });

  const root = frame(
    'MainMenu',
    { w: 1920, h: 1080, fills: [solid('#0B0F17')] },
    [
      rect('[img 1x] Bg_Gradient', { w: 1920, h: 1080, fills: [linear('#1B2536', '#05070B')], constraints: { horizontal: 'STRETCH', vertical: 'STRETCH' } }),
      ellipse('Img_Glow', { x: 760, y: 40, w: 400, h: 400, fills: [linear(GOLD, '#000000')], constraints: { horizontal: 'CENTER', vertical: 'MIN' } }),
      group('[img 3x] Logo', { x: 885, y: 96, w: 150, h: 150 }, [
        ellipse('Ring', { w: 150, h: 150, strokes: [solid(GOLD)], strokeWeight: 6 }),
        vector('Diamond', { x: 40, y: 30, w: 70, h: 90, fills: [solid(GOLD)] }),
      ]),
      text('Txt_Title', {
        x: 650,
        y: 262,
        w: 620,
        h: 86,
        characters: 'Sovereign Call',
        family: 'Cinzel',
        style: 'Bold',
        weight: 700,
        size: 64,
        color: GOLD,
        align: 'CENTER',
        textCase: 'UPPER',
        letterSpacing: { value: 8, unit: 'PERCENT' },
        constraints: { horizontal: 'CENTER', vertical: 'MIN' },
        effects: [dropShadow(0.6, 12, 4)],
      }),
      line('Img_Divider', { x: 760, y: 372, w: 400, h: 0, strokes: [solid(GOLD, 0.5)], strokeWeight: 2, constraints: { horizontal: 'CENTER', vertical: 'MIN' } }),
      frame(
        'Pnl_Menu',
        {
          x: 760,
          y: 420,
          w: 400,
          h: 368,
          fills: [solid('#121826', 0.92)],
          strokes: [solid(GOLD, 0.45)],
          cornerRadius: 16,
          effects: [dropShadow(0.55, 24, 8)],
          constraints: { horizontal: 'CENTER', vertical: 'CENTER' },
        },
        [
          menuButton('[focus] Btn_Play', vDefault, 'Play', 32),
          menuButton('Btn_Continue', vDisabled, 'Continue', 112),
          menuButton('Btn_Settings', vDefault, 'Settings', 192),
          menuButton('Btn_Quit', vDefault, 'Quit', 272),
        ],
        { layoutMode: 'VERTICAL', paddingLeft: 32, paddingRight: 32, paddingTop: 32, paddingBottom: 32, itemSpacing: 16, counterAxisAlignItems: 'CENTER', layoutSizingVertical: 'HUG' },
      ),
      rect('Img_Seal', { x: 1640, y: 120, w: 120, h: 120, rotation: 15, fills: [solid('#7A1F2B')], strokes: [solid(GOLD)], strokeWeight: 3, cornerRadius: 12, constraints: { horizontal: 'MAX', vertical: 'MIN' } }),
      vector('Ico_Star', { x: 1676, y: 156, w: 48, h: 48, fills: [solid(GOLD)], constraints: { horizontal: 'MAX', vertical: 'MIN' } }),
      vector('Ico_Star', { x: 160, y: 156, w: 48, h: 48, fills: [solid(GOLD)], constraints: { horizontal: 'MIN', vertical: 'MIN' } }),
      text('Txt_Error', { x: 760, y: 820, w: 400, h: 24, characters: 'Connection lost. Retrying…', size: 18, color: '#FF5A5A', align: 'CENTER', autoResize: 'HEIGHT', visible: false, constraints: { horizontal: 'CENTER', vertical: 'CENTER' } }),
      frame(
        'Pnl_Footer',
        { x: 0, y: 1000, w: 1920, h: 80, fills: [solid('#000000', 0.45)], constraints: { horizontal: 'STRETCH', vertical: 'MAX' } },
        [
          text('Txt_Version', { x: 48, y: 29, w: 190, h: 22, characters: 'v0.1.0 · Early Access', size: 16, color: '#8A93A6', layoutSizingHorizontal: 'HUG', layoutSizingVertical: 'HUG' }),
          frame(
            'Box_Prompts',
            { x: 1560, y: 20, w: 312, h: 40, layoutSizingHorizontal: 'HUG', layoutSizingVertical: 'HUG' },
            [
              frame(
                '[action=Confirm] Act_Confirm',
                { x: 0, y: 4, w: 32, h: 32, layoutSizingHorizontal: 'FIXED', layoutSizingVertical: 'FIXED' },
                [
                  ellipse('Glyph_Bg', { w: 32, h: 32, fills: [solid('#F2E6CC')] }),
                  text('Glyph_Key', { x: 10, y: 5, w: 12, h: 22, characters: 'A', size: 16, style: 'Bold', weight: 700, color: '#0B0F17', align: 'CENTER' }),
                ],
              ),
              text('Txt_ConfirmLabel', { x: 44, y: 9, w: 70, h: 22, characters: 'Select', size: 16, color: '#F2E6CC', layoutSizingHorizontal: 'HUG', layoutSizingVertical: 'HUG' }),
              frame(
                '[action=Back] Btn_Back',
                { x: 150, y: 0, w: 162, h: 40, fills: [solid('#1E2738')], strokes: [solid(GOLD, 0.4)], cornerRadius: 20, layoutSizingHorizontal: 'HUG', layoutSizingVertical: 'FIXED' },
                [
                  vector('Ico_Back', { x: 16, y: 8, w: 24, h: 24, fills: [solid('#F2E6CC')], layoutSizingHorizontal: 'FIXED', layoutSizingVertical: 'FIXED' }),
                  text('Txt_BackLabel', { x: 48, y: 9, w: 98, h: 22, characters: 'Back', size: 16, color: '#F2E6CC', layoutSizingHorizontal: 'HUG', layoutSizingVertical: 'HUG' }),
                ],
                { layoutMode: 'HORIZONTAL', paddingLeft: 16, paddingRight: 20, paddingTop: 8, paddingBottom: 8, itemSpacing: 8, counterAxisAlignItems: 'CENTER' },
              ),
            ],
            { layoutMode: 'HORIZONTAL', itemSpacing: 12, counterAxisAlignItems: 'CENTER' },
          ),
        ],
        { layoutMode: 'HORIZONTAL', paddingLeft: 48, paddingRight: 48, paddingTop: 20, paddingBottom: 20, primaryAxisAlignItems: 'SPACE_BETWEEN', counterAxisAlignItems: 'CENTER' },
      ),
      frame('[skip] Notes', { x: 40, y: 40, w: 300, h: 120, fills: [solid('#FFF3A0')] }, [text('Note', { x: 12, y: 12, w: 276, h: 96, characters: 'Remember: hover states live in the Btn/Primary set.', color: '#333333' })]),
    ],
  );
  place(root);
  return { root, buttonSet };
}
