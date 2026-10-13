import { deflateSync } from 'node:zlib';
import { crc32 } from '../src/ui/zip';

export type Rgba = [number, number, number, number];

/** Encodes an RGBA8 PNG. `pixel(x, y)` returns 0..255 channels. */
export function makePng(width: number, height: number, pixel: (x: number, y: number) => Rgba): Uint8Array {
  const raw = Buffer.alloc((width * 4 + 1) * height);
  let o = 0;
  for (let y = 0; y < height; y++) {
    raw[o++] = 0; // filter: none
    for (let x = 0; x < width; x++) {
      const [r, g, b, a] = pixel(x, y);
      raw[o++] = r;
      raw[o++] = g;
      raw[o++] = b;
      raw[o++] = a;
    }
  }
  const chunk = (type: string, data: Buffer) => {
    const len = Buffer.alloc(4);
    len.writeUInt32BE(data.length);
    const td = Buffer.concat([Buffer.from(type, 'ascii'), data]);
    const crc = Buffer.alloc(4);
    crc.writeUInt32BE(crc32(new Uint8Array(td)));
    return Buffer.concat([len, td, crc]);
  };
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0);
  ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8; // bit depth
  ihdr[9] = 6; // RGBA
  ihdr[10] = 0;
  ihdr[11] = 0;
  ihdr[12] = 0;
  const png = Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', ihdr),
    chunk('IDAT', deflateSync(raw, { level: 9 })),
    chunk('IEND', Buffer.alloc(0)),
  ]);
  return new Uint8Array(png);
}

export function hex(color: string, alpha = 255): Rgba {
  const v = color.replace('#', '');
  return [parseInt(v.slice(0, 2), 16), parseInt(v.slice(2, 4), 16), parseInt(v.slice(4, 6), 16), alpha];
}

export function mix(a: Rgba, b: Rgba, t: number): Rgba {
  return [0, 1, 2, 3].map((i) => Math.round(a[i] + (b[i] - a[i]) * t)) as Rgba;
}
