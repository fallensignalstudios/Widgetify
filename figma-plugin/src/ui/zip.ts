/**
 * Minimal ZIP writer: "stored" entries only (PNGs are already compressed), UTF-8 names, no ZIP64.
 * The Unreal importer ships a matching reader (FigmaZipReader), so bundles never need extracting.
 */

export interface ZipEntry {
  path: string;
  data: Uint8Array;
}

const CRC_TABLE: Uint32Array = (() => {
  const table = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    table[n] = c >>> 0;
  }
  return table;
})();

export function crc32(data: Uint8Array): number {
  let crc = 0xffffffff;
  for (let i = 0; i < data.length; i++) crc = CRC_TABLE[(crc ^ data[i]) & 0xff] ^ (crc >>> 8);
  return (crc ^ 0xffffffff) >>> 0;
}

function dosDateTime(date: Date): { time: number; date: number } {
  const year = Math.max(1980, date.getFullYear());
  return {
    time: (date.getHours() << 11) | (date.getMinutes() << 5) | Math.floor(date.getSeconds() / 2),
    date: ((year - 1980) << 9) | ((date.getMonth() + 1) << 5) | date.getDate(),
  };
}

export function buildZip(entries: ZipEntry[], modified: Date = new Date()): Uint8Array {
  const encoder = new TextEncoder();
  const stamp = dosDateTime(modified);
  const seen = new Set<string>();
  const prepared = entries.map((e) => {
    const path = e.path.replace(/\\/g, '/').replace(/^\/+/, '');
    if (seen.has(path)) throw new Error(`Duplicate zip entry: ${path}`);
    seen.add(path);
    return { name: encoder.encode(path), data: e.data, crc: crc32(e.data) };
  });

  let localSize = 0;
  let centralSize = 0;
  for (const p of prepared) {
    localSize += 30 + p.name.length + p.data.length;
    centralSize += 46 + p.name.length;
  }
  if (localSize + centralSize + 22 > 0xffffffff || prepared.length > 0xffff) {
    throw new Error('Bundle is too large for a non-ZIP64 archive.');
  }

  const out = new Uint8Array(localSize + centralSize + 22);
  const view = new DataView(out.buffer);
  let offset = 0;
  const localOffsets: number[] = [];

  for (const p of prepared) {
    localOffsets.push(offset);
    view.setUint32(offset, 0x04034b50, true);
    view.setUint16(offset + 4, 20, true); // version needed
    view.setUint16(offset + 6, 0x0800, true); // UTF-8 names
    view.setUint16(offset + 8, 0, true); // stored
    view.setUint16(offset + 10, stamp.time, true);
    view.setUint16(offset + 12, stamp.date, true);
    view.setUint32(offset + 14, p.crc, true);
    view.setUint32(offset + 18, p.data.length, true);
    view.setUint32(offset + 22, p.data.length, true);
    view.setUint16(offset + 26, p.name.length, true);
    view.setUint16(offset + 28, 0, true);
    out.set(p.name, offset + 30);
    out.set(p.data, offset + 30 + p.name.length);
    offset += 30 + p.name.length + p.data.length;
  }

  const centralStart = offset;
  prepared.forEach((p, i) => {
    view.setUint32(offset, 0x02014b50, true);
    view.setUint16(offset + 4, 20, true); // version made by
    view.setUint16(offset + 6, 20, true); // version needed
    view.setUint16(offset + 8, 0x0800, true);
    view.setUint16(offset + 10, 0, true);
    view.setUint16(offset + 12, stamp.time, true);
    view.setUint16(offset + 14, stamp.date, true);
    view.setUint32(offset + 16, p.crc, true);
    view.setUint32(offset + 20, p.data.length, true);
    view.setUint32(offset + 24, p.data.length, true);
    view.setUint16(offset + 28, p.name.length, true);
    view.setUint16(offset + 30, 0, true); // extra
    view.setUint16(offset + 32, 0, true); // comment
    view.setUint16(offset + 34, 0, true); // disk
    view.setUint16(offset + 36, 0, true); // internal attrs
    view.setUint32(offset + 38, 0, true); // external attrs
    view.setUint32(offset + 42, localOffsets[i], true);
    out.set(p.name, offset + 46);
    offset += 46 + p.name.length;
  });

  view.setUint32(offset, 0x06054b50, true);
  view.setUint16(offset + 4, 0, true);
  view.setUint16(offset + 6, 0, true);
  view.setUint16(offset + 8, prepared.length, true);
  view.setUint16(offset + 10, prepared.length, true);
  view.setUint32(offset + 12, offset - centralStart, true);
  view.setUint32(offset + 16, centralStart, true);
  view.setUint16(offset + 20, 0, true);
  return out;
}

/** Reads a stored-only zip produced by buildZip (used by tests and the sample generator). */
export function readStoredZip(bytes: Uint8Array): Map<string, Uint8Array> {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  let eocd = -1;
  for (let i = bytes.length - 22; i >= Math.max(0, bytes.length - 22 - 0xffff); i--) {
    if (view.getUint32(i, true) === 0x06054b50) {
      eocd = i;
      break;
    }
  }
  if (eocd < 0) throw new Error('Not a zip archive');
  const count = view.getUint16(eocd + 10, true);
  let offset = view.getUint32(eocd + 16, true);
  const decoder = new TextDecoder();
  const files = new Map<string, Uint8Array>();
  for (let i = 0; i < count; i++) {
    if (view.getUint32(offset, true) !== 0x02014b50) throw new Error('Bad central directory');
    const method = view.getUint16(offset + 10, true);
    const size = view.getUint32(offset + 20, true);
    const nameLen = view.getUint16(offset + 28, true);
    const extraLen = view.getUint16(offset + 30, true);
    const commentLen = view.getUint16(offset + 32, true);
    const local = view.getUint32(offset + 42, true);
    const name = decoder.decode(bytes.subarray(offset + 46, offset + 46 + nameLen));
    if (method !== 0) throw new Error(`Entry ${name} is compressed`);
    const localNameLen = view.getUint16(local + 26, true);
    const localExtraLen = view.getUint16(local + 28, true);
    const start = local + 30 + localNameLen + localExtraLen;
    files.set(name, bytes.subarray(start, start + size));
    offset += 46 + nameLen + extraLen + commentLen;
  }
  return files;
}
