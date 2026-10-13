import { describe, expect, it } from 'vitest';
import { parseLayerName, tagScale } from '../src/main/tags';

describe('parseLayerName', () => {
  it('leaves plain names alone', () => {
    expect(parseLayerName('Btn_Play')).toEqual({ cleanName: 'Btn_Play', tags: {} });
  });

  it('parses the tags from the architecture doc', () => {
    expect(parseLayerName('[img] Frame_Border')).toEqual({ cleanName: 'Frame_Border', tags: { img: '' } });
    expect(parseLayerName('[skip] Notes')).toEqual({ cleanName: 'Notes', tags: { skip: '' } });
  });

  it('supports scale shorthands', () => {
    expect(parseLayerName('[img 3x] Logo').tags).toEqual({ img: '', scale: '3' });
    expect(parseLayerName('[img@1.5x] Logo').tags).toEqual({ img: '', scale: '1.5' });
    expect(parseLayerName('[img:Background@4x] Hero').tags).toEqual({ img: 'Background', scale: '4' });
  });

  it('supports key=value and key:value, case-insensitive keys, aliases', () => {
    const { cleanName, tags } = parseLayerName('Btn_Back [Action=Back, FOCUS] [btn]');
    expect(cleanName).toBe('Btn_Back');
    expect(tags).toEqual({ action: 'Back', focus: '', button: '' });
    expect(parseLayerName('[image slice=16] Panel').tags).toEqual({ img: '', slice: '16' });
  });

  it('collapses whitespace left behind by tags', () => {
    expect(parseLayerName('  Main  [var]   Panel ').cleanName).toBe('Main Panel');
  });

  it('returns an empty clean name for tag-only layers', () => {
    expect(parseLayerName('[skip]').cleanName).toBe('');
  });
});

describe('tagScale', () => {
  it('clamps to sane values', () => {
    expect(tagScale({ scale: '3' })).toBe(3);
    expect(tagScale({ scale: '0' })).toBeUndefined();
    expect(tagScale({ scale: '20' })).toBeUndefined();
    expect(tagScale({})).toBeUndefined();
  });
});
