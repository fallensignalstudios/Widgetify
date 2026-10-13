import { BUNDLE_JSON_NAME, type ExportOptions, type ExportWarning } from '../shared/bundle';
import { DEFAULT_LIVE_PORT, DEFAULT_OPTIONS, type MainToUi, type SelectionInfo, type UiPrefs, type UiToMain } from '../shared/messages';
import { buildZip } from './zip';

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;

const els = {
  selName: $<HTMLDivElement>('selName'),
  selMeta: $<HTMLDivElement>('selMeta'),
  scale: $<HTMLSelectElement>('scale'),
  includeHidden: $<HTMLInputElement>('includeHidden'),
  rasterize: $<HTMLInputElement>('rasterize'),
  prefixes: $<HTMLInputElement>('prefixes'),
  download: $<HTMLButtonElement>('download'),
  push: $<HTMLButtonElement>('push'),
  liveDot: $<HTMLSpanElement>('liveDot'),
  liveStatus: $<HTMLSpanElement>('liveStatus'),
  progress: $<HTMLDivElement>('progress'),
  bar: $<HTMLDivElement>('bar'),
  progressLabel: $<HTMLDivElement>('progressLabel'),
  log: $<HTMLDivElement>('log'),
};

let prefs: UiPrefs = { options: { ...DEFAULT_OPTIONS }, livePort: DEFAULT_LIVE_PORT };
let selection: SelectionInfo = { ok: false, reason: 'Loading…' };
let busy = false;
let unrealConnected = false;

function send(msg: UiToMain): void {
  parent.postMessage({ pluginMessage: msg }, '*');
}

function readOptions(): ExportOptions {
  return {
    scale: Number(els.scale.value) || 2,
    includeHidden: els.includeHidden.checked,
    rasterizeUnsupported: els.rasterize.checked,
    variantPrefixes: els.prefixes.value
      .split(',')
      .map((s) => s.trim())
      .filter((s) => s.length > 0),
  };
}

function writeOptions(options: ExportOptions): void {
  els.scale.value = String(options.scale);
  els.includeHidden.checked = options.includeHidden;
  els.rasterize.checked = options.rasterizeUnsupported;
  els.prefixes.value = options.variantPrefixes.join(', ');
}

let saveTimer: number | undefined;
function savePrefsSoon(): void {
  prefs = { ...prefs, options: readOptions() };
  window.clearTimeout(saveTimer);
  saveTimer = window.setTimeout(() => send({ type: 'save-prefs', prefs }), 300);
}

function refreshButtons(): void {
  els.download.disabled = busy || !selection.ok;
  els.push.disabled = busy || !selection.ok || !unrealConnected;
}

function showSelection(sel: SelectionInfo): void {
  selection = sel;
  if (sel.ok) {
    els.selName.textContent = sel.name ?? '';
    els.selMeta.textContent = `${sel.width} × ${sel.height} · ${sel.layerCount}${(sel.layerCount ?? 0) >= 5000 ? '+' : ''} layers`;
    els.selName.classList.remove('muted');
  } else {
    els.selName.textContent = sel.reason ?? 'Nothing selected';
    els.selMeta.textContent = 'Tip: one top-level frame = one Widget Blueprint.';
    els.selName.classList.add('muted');
  }
  refreshButtons();
}

function setBusy(value: boolean, label = ''): void {
  busy = value;
  els.progress.hidden = !value;
  if (value) {
    els.bar.style.width = '0%';
    els.progressLabel.textContent = label;
  }
  refreshButtons();
}

function logLine(text: string, kind: 'info' | 'warn' | 'error' | 'ok' = 'info'): void {
  const line = document.createElement('div');
  line.className = `line ${kind}`;
  line.textContent = text;
  els.log.prepend(line);
}

function clearLog(): void {
  els.log.textContent = '';
}

function showWarnings(warnings: ExportWarning[]): void {
  for (const w of warnings.slice().reverse()) {
    logLine(`${w.nodePath ? `${w.nodePath}: ` : ''}${w.message}`, 'warn');
  }
}

function downloadBytes(bytes: Uint8Array, fileName: string): void {
  const blob = new Blob([bytes as BlobPart], { type: 'application/zip' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = fileName;
  document.body.appendChild(a);
  a.click();
  a.remove();
  window.setTimeout(() => URL.revokeObjectURL(url), 10_000);
}

function liveUrl(path: string): string {
  return `http://127.0.0.1:${prefs.livePort}${path}`;
}

async function pushBytes(bytes: Uint8Array, frameName: string, frameId: string): Promise<void> {
  const query = `?frame=${encodeURIComponent(frameName)}&id=${encodeURIComponent(frameId)}`;
  const response = await fetch(liveUrl(`/figmasync/push${query}`), {
    method: 'POST',
    headers: { 'Content-Type': 'application/octet-stream' },
    body: bytes as BodyInit,
  });
  const text = await response.text();
  if (!response.ok) throw new Error(text || `HTTP ${response.status}`);
  let message = 'Sent to Unreal.';
  try {
    const parsed = JSON.parse(text) as { message?: string };
    if (parsed.message) message = parsed.message;
  } catch {
    // Plain-text reply is fine.
  }
  logLine(message, 'ok');
  send({ type: 'notify', text: `Widgetify: ${message}` });
}

async function pingUnreal(): Promise<void> {
  const controller = new AbortController();
  const timer = window.setTimeout(() => controller.abort(), 1500);
  try {
    const response = await fetch(liveUrl('/figmasync/ping'), { signal: controller.signal });
    const info = (await response.json()) as { project?: string; engine?: string };
    unrealConnected = response.ok;
    els.liveStatus.textContent = `Unreal connected${info.project ? ` · ${info.project}` : ''}${info.engine ? ` (UE ${info.engine})` : ''}`;
  } catch {
    unrealConnected = false;
    els.liveStatus.textContent = `Unreal not detected on :${prefs.livePort} (enable Live Push in Figma Sync settings)`;
  } finally {
    window.clearTimeout(timer);
    els.liveDot.classList.toggle('on', unrealConnected);
    refreshButtons();
  }
}

function startExport(purpose: 'download' | 'push'): void {
  if (busy || !selection.ok) return;
  clearLog();
  setBusy(true, 'Walking layers…');
  send({ type: 'export', options: readOptions(), purpose });
}

window.onmessage = async (event: MessageEvent) => {
  const msg = (event.data as { pluginMessage?: MainToUi }).pluginMessage;
  if (!msg) return;
  switch (msg.type) {
    case 'init':
      prefs = msg.prefs;
      writeOptions(prefs.options);
      showSelection(msg.selection);
      void pingUnreal();
      window.setInterval(() => void pingUnreal(), 4000);
      break;
    case 'selection':
      showSelection(msg.selection);
      break;
    case 'progress':
      els.bar.style.width = `${Math.round((msg.done / Math.max(1, msg.total)) * 100)}%`;
      els.progressLabel.textContent = msg.label;
      break;
    case 'bundle': {
      try {
        const encoder = new TextEncoder();
        const zip = buildZip([{ path: BUNDLE_JSON_NAME, data: encoder.encode(msg.json) }, ...msg.files]);
        const sizeKb = Math.round(zip.length / 1024);
        if (msg.purpose === 'download') {
          downloadBytes(zip, msg.fileName);
          logLine(`Saved ${msg.fileName} (${msg.stats.nodes} layers, ${msg.stats.images} images, ${sizeKb} KB).`, 'ok');
        } else {
          await pushBytes(zip, msg.frameName, msg.frameId);
        }
        showWarnings(msg.warnings);
        if (msg.warnings.length === 0) logLine('No warnings. Everything mapped cleanly.', 'ok');
      } catch (e) {
        logLine(`Export failed: ${e instanceof Error ? e.message : String(e)}`, 'error');
      } finally {
        setBusy(false);
      }
      break;
    }
    case 'export-error':
      logLine(msg.message, 'error');
      setBusy(false);
      break;
  }
};

for (const el of [els.scale, els.includeHidden, els.rasterize, els.prefixes]) {
  el.addEventListener('change', savePrefsSoon);
}
els.download.addEventListener('click', () => startExport('download'));
els.push.addEventListener('click', () => startExport('push'));

send({ type: 'ready' });
