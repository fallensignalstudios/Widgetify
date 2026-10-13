import { describe, expect, it } from 'vitest';
import { buildZip, crc32, readStoredZip } from '../src/ui/zip';

describe('crc32', () => {
  it('matches the standard check value', () => {
    expect(crc32(new TextEncoder().encode('123456789'))).toBe(0xcbf43926);
  });
});

describe('buildZip', () => {
  it('round-trips stored entries with UTF-8 names', () => {
    const enc = new TextEncoder();
    const zip = buildZip([
      { path: 'figmasync.json', data: enc.encode('{"ok":true}') },
      { path: 'images/Ünïcode.png', data: new Uint8Array([1, 2, 3, 4]) },
      { path: 'images/empty.png', data: new Uint8Array(0) },
    ]);
    const files = readStoredZip(zip);
    expect([...files.keys()]).toEqual(['figmasync.json', 'images/Ünïcode.png', 'images/empty.png']);
    expect(new TextDecoder().decode(files.get('figmasync.json'))).toBe('{"ok":true}');
    expect([...files.get('images/Ünïcode.png')!]).toEqual([1, 2, 3, 4]);
    expect(files.get('images/empty.png')!.length).toBe(0);
  });

  it('rejects duplicate paths', () => {
    expect(() => buildZip([{ path: 'a', data: new Uint8Array() }, { path: 'a', data: new Uint8Array() }])).toThrow(/Duplicate/);
  });

  it('is readable by the system unzip tool layout (EOCD at the end)', () => {
    const zip = buildZip([{ path: 'x.txt', data: new Uint8Array([65]) }]);
    const view = new DataView(zip.buffer);
    expect(view.getUint32(zip.length - 22, true)).toBe(0x06054b50);
  });
});
