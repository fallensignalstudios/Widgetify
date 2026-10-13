import { beforeAll, describe, expect, it } from 'vitest';
import type { Bundle, FNode } from '../src/shared/bundle';
import { DEFAULT_OPTIONS } from '../src/shared/messages';
import { FrameExporter, virtualSetKey, type ExportResult } from '../src/main/walker';
import { asScene, mockHost } from './mockFigma';
import { buildSampleDoc, sampleArt } from './sampleDoc';

function find(node: FNode, cleanName: string): FNode | undefined {
  if (node.cleanName === cleanName) return node;
  for (const c of node.children ?? []) {
    const hit = find(c, cleanName);
    if (hit) return hit;
  }
  return undefined;
}

function get(bundle: Bundle, cleanName: string): FNode {
  const node = find(bundle.root, cleanName);
  if (!node) throw new Error(`No node named ${cleanName}`);
  return node;
}

let result: ExportResult;
let bundle: Bundle;
let host: ReturnType<typeof mockHost>;

beforeAll(async () => {
  const doc = buildSampleDoc();
  host = mockHost({ art: sampleArt });
  result = await new FrameExporter(DEFAULT_OPTIONS, host).run(asScene(doc.root), { fileName: 'Sovereign Call UI', pageName: 'Menus' });
  bundle = result.bundle;
});

describe('bundle envelope', () => {
  it('identifies itself and its source', () => {
    expect(bundle.format).toBe('figmasync');
    expect(bundle.version).toBe(1);
    expect(bundle.source).toMatchObject({ fileName: 'Sovereign Call UI', pageName: 'Menus', frameName: 'MainMenu' });
    expect(JSON.parse(result.json)).toEqual(bundle);
  });

  it('puts the root at the origin with its own size', () => {
    expect(bundle.root).toMatchObject({ x: 0, y: 0, width: 1920, height: 1080, type: 'FRAME' });
    expect(bundle.root.constraints).toBeUndefined();
  });
});

describe('layer selection', () => {
  it('drops [skip] layers entirely', () => {
    expect(find(bundle.root, 'Notes')).toBeUndefined();
    expect(find(bundle.root, 'Note')).toBeUndefined();
  });

  it('keeps hidden layers as invisible nodes', () => {
    expect(get(bundle, 'Txt_Error').visible).toBe(false);
  });

  it('can drop hidden layers instead', async () => {
    const doc = buildSampleDoc();
    const r = await new FrameExporter({ ...DEFAULT_OPTIONS, includeHidden: false }, mockHost()).run(asScene(doc.root), { fileName: 'f', pageName: 'p' });
    expect(find(r.bundle.root, 'Txt_Error')).toBeUndefined();
  });
});

describe('rasterization', () => {
  it('honors [img] tags and per-layer scale', () => {
    const bg = get(bundle, 'Bg_Gradient');
    expect(bg.image?.reason).toBe('tag');
    const asset = bundle.assets.find((a) => a.id === bg.image!.asset)!;
    expect(asset).toMatchObject({ scale: 1, width: 1920, height: 1080, path: 'images/Bg_Gradient.png' });
  });

  it('bakes tagged groups and omits their children', () => {
    const logo = get(bundle, 'Logo');
    expect(logo.image?.reason).toBe('tag');
    expect(logo.children).toBeUndefined();
    expect(bundle.assets.find((a) => a.id === logo.image!.asset)!.scale).toBe(3);
  });

  it('rasterizes vectors and gradients automatically', () => {
    expect(get(bundle, 'Ico_Back').image?.reason).toBe('vector');
    expect(get(bundle, 'Img_Glow').image?.reason).toBe('gradient');
  });

  it('dedupes identical art', () => {
    const stars = bundle.root.children!.filter((c) => c.cleanName === 'Ico_Star');
    expect(stars).toHaveLength(2);
    expect(stars[0].image!.asset).toBe(stars[1].image!.asset);
  });

  it('rasterizes only the background of shadowed containers, with overflow bounds', () => {
    const menu = get(bundle, 'Pnl_Menu');
    expect(menu.background?.reason).toBe('effects');
    expect(menu.background!.bounds.x).toBeLessThan(0);
    expect(menu.background!.bounds.width).toBeGreaterThan(menu.width);
    expect(menu.children).toHaveLength(4);
  });

  it('keeps simple shapes as brushes', () => {
    const seal = get(bundle, 'Img_Seal');
    expect(seal.image).toBeUndefined();
    expect(seal.style?.simple).toBe(true);
    expect(seal.style?.cornerRadius).toEqual([12, 12, 12, 12]);
    const divider = get(bundle, 'Img_Divider');
    expect(divider.type).toBe('LINE');
    expect(divider.image).toBeUndefined();
  });
});

describe('geometry and layout', () => {
  it('converts rotation to UMG convention around the center', () => {
    const seal = get(bundle, 'Img_Seal');
    expect(seal.rotation).toBe(-15);
    expect(seal.width).toBe(120);
  });

  it('exports constraints for canvas children', () => {
    expect(get(bundle, 'Pnl_Footer').constraints).toEqual({ horizontal: 'STRETCH', vertical: 'MAX' });
    expect(get(bundle, 'Txt_Title').constraints).toEqual({ horizontal: 'CENTER', vertical: 'MIN' });
  });

  it('exports auto-layout containers', () => {
    expect(get(bundle, 'Pnl_Menu').layout).toEqual({
      mode: 'VERTICAL',
      wrap: false,
      padding: { left: 32, right: 32, top: 32, bottom: 32 },
      itemSpacing: 16,
      counterAxisSpacing: 0,
      primaryAlign: 'MIN',
      counterAlign: 'CENTER',
      reverseZIndex: false,
    });
    expect(get(bundle, 'Pnl_Footer').layout?.primaryAlign).toBe('SPACE_BETWEEN');
  });

  it('exports child sizing for auto-layout children (and no constraints)', () => {
    const play = get(bundle, 'Btn_Play');
    expect(play.child).toEqual({ positioning: 'AUTO', sizingH: 'FILL', sizingV: 'FIXED', grow: 0, alignSelf: 'INHERIT' });
    expect(play.constraints).toBeUndefined();
    expect(play.x).toBe(32);
    expect(play.y).toBe(32);
  });
});

describe('text', () => {
  it('captures font, color, alignment and spacing', () => {
    const title = get(bundle, 'Txt_Title');
    expect(title.text).toMatchObject({
      characters: 'Sovereign Call',
      fontFamily: 'Cinzel',
      fontStyle: 'Bold',
      fontWeight: 700,
      fontSize: 64,
      alignH: 'CENTER',
      textCase: 'UPPER',
      letterSpacing: { value: 8, unit: 'PERCENT' },
      mixed: false,
    });
    expect(title.text!.color.r).toBeCloseTo(0xe8 / 255, 3);
    expect(title.text!.shadow).toMatchObject({ offset: { x: 0, y: 4 } });
  });
});

describe('components', () => {
  it('describes instances with their variant and properties', () => {
    const play = get(bundle, 'Btn_Play');
    expect(play.tags).toEqual({ focus: '' });
    expect(play.component).toMatchObject({
      kind: 'INSTANCE',
      componentSetName: 'Btn/Primary',
      componentName: 'State=Default',
      variant: { State: 'Default' },
      properties: { Label: { type: 'TEXT', value: 'Play' } },
    });
    expect(get(bundle, 'Btn_Continue').component?.variant).toEqual({ State: 'Disabled' });
  });

  it('applies text overrides inside instances', () => {
    const play = get(bundle, 'Btn_Play');
    expect(play.children![0].text!.characters).toBe('Play');
    expect(play.children![0].propRefs).toEqual({ characters: 'Label' });
  });

  it('exports every variant of matching component sets once', () => {
    const sets = Object.values(bundle.components);
    expect(sets).toHaveLength(1);
    expect(sets[0].name).toBe('Btn/Primary');
    expect(sets[0].propertyNames).toEqual(['State']);
    expect(sets[0].variants.map((v) => v.properties.State)).toEqual(['Default', 'Hover', 'Pressed', 'Disabled']);
    const hover = sets[0].variants[1].node;
    expect(hover).toMatchObject({ x: 0, y: 0, width: 400, height: 64 });
    expect(hover.style?.fills[0].color?.r).toBeCloseTo(0x2a / 255, 3);
    expect(hover.layout?.primaryAlign).toBe('CENTER');
  });

  it('parses slash-named state components', () => {
    expect(virtualSetKey('Btn/Primary/Hover')).toEqual({ key: 'Btn/Primary', state: 'Hover' });
    expect(virtualSetKey('Btn/Primary')).toBeNull();
    expect(virtualSetKey('Icons/Arrow/Left')).toBeNull();
  });
});

describe('tags and bindings', () => {
  it('keeps action tags for CommonUI', () => {
    expect(get(bundle, 'Btn_Back').tags).toEqual({ action: 'Back' });
    expect(get(bundle, 'Act_Confirm').tags).toEqual({ action: 'Confirm' });
  });
});
