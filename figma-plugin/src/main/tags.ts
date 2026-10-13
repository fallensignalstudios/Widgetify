/**
 * Layer-name tags. Designers steer the export with bracketed tags anywhere in a layer name:
 *
 *   [img] Frame_Border          rasterize this layer (and its children) to a PNG
 *   [img 3x] Logo               ...at 3x instead of the export default
 *   [img:Background] Hero        ...using the "Background" texture preset in Unreal
 *   [img slice=16] Panel_Frame   ...as a 9-slice brush with a 16 px margin
 *   [skip] Notes                 leave this layer (and its children) out entirely
 *   [focus] Btn_Play             initial gamepad/keyboard focus for the screen
 *   [action=Back] Btn_Back       bind a CommonUI input action
 *
 * Grammar: each [...] group holds tokens separated by spaces or commas. A token is
 *   key | key=value | key:value | Nx | @Nx | key@Nx
 * Keys are case-insensitive and stored lower-case. "Nx" sets scale=N.
 */

export interface ParsedName {
  cleanName: string;
  tags: Record<string, string>;
}

const TAG_GROUP = /\[([^\[\]]*)\]/g;
const SCALE_TOKEN = /^@?(\d+(?:\.\d+)?)x$/i;
const KEY_WITH_SCALE = /^([A-Za-z_][\w-]*)@(\d+(?:\.\d+)?)x$/i;
const VALUE_WITH_SCALE = /^(.*)@(\d+(?:\.\d+)?)x$/i;

const KEY_ALIASES: Record<string, string> = {
  image: 'img',
  raster: 'img',
  ignore: 'skip',
  btn: 'button',
  variable: 'var',
  '9slice': 'slice',
};

export function normalizeTagKey(key: string): string {
  const lower = key.toLowerCase();
  return KEY_ALIASES[lower] ?? lower;
}

export function parseLayerName(raw: string): ParsedName {
  const tags: Record<string, string> = {};
  const withoutTags = raw.replace(TAG_GROUP, (_match, body: string) => {
    parseTagBody(body, tags);
    return ' ';
  });
  const cleanName = withoutTags.replace(/\s+/g, ' ').trim();
  return { cleanName, tags };
}

function parseTagBody(body: string, out: Record<string, string>): void {
  const tokens = body.split(/[\s,]+/).filter((t) => t.length > 0);
  for (const token of tokens) {
    const scaleOnly = SCALE_TOKEN.exec(token);
    if (scaleOnly) {
      out.scale = scaleOnly[1];
      continue;
    }
    const keyWithScale = KEY_WITH_SCALE.exec(token);
    if (keyWithScale) {
      out[normalizeTagKey(keyWithScale[1])] = out[normalizeTagKey(keyWithScale[1])] ?? '';
      out.scale = keyWithScale[2];
      continue;
    }
    const sep = token.search(/[=:]/);
    if (sep > 0) {
      const key = normalizeTagKey(token.slice(0, sep));
      let value = token.slice(sep + 1);
      const valueWithScale = VALUE_WITH_SCALE.exec(value);
      if (valueWithScale) {
        value = valueWithScale[1];
        out.scale = valueWithScale[2];
      }
      out[key] = value;
      continue;
    }
    const key = normalizeTagKey(token);
    if (!(key in out)) {
      out[key] = '';
    }
  }
}

/** Scale requested by a [img 3x] style tag, if any. */
export function tagScale(tags: Record<string, string>): number | undefined {
  if (tags.scale === undefined) return undefined;
  const value = Number(tags.scale);
  return Number.isFinite(value) && value > 0 && value <= 8 ? value : undefined;
}
