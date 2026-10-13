import type { ExportOptions, ExportWarning } from './bundle';

export const DEFAULT_OPTIONS: ExportOptions = {
  scale: 2,
  includeHidden: true,
  rasterizeUnsupported: true,
  variantPrefixes: ['Btn/', 'Button/'],
};

export const DEFAULT_LIVE_PORT = 7820;

export interface UiPrefs {
  options: ExportOptions;
  livePort: number;
}

export interface SelectionInfo {
  ok: boolean;
  /** Why the selection can't be exported (when ok is false). */
  reason?: string;
  id?: string;
  name?: string;
  type?: string;
  width?: number;
  height?: number;
  layerCount?: number;
}

export interface BundleFile {
  path: string;
  data: Uint8Array;
}

/** Messages from the plugin UI (iframe) to the main thread. */
export type UiToMain =
  | { type: 'ready' }
  | { type: 'export'; options: ExportOptions; purpose: 'download' | 'push' }
  | { type: 'save-prefs'; prefs: UiPrefs }
  | { type: 'notify'; text: string; error?: boolean };

/** Messages from the main thread to the plugin UI. */
export type MainToUi =
  | { type: 'init'; prefs: UiPrefs; selection: SelectionInfo }
  | { type: 'selection'; selection: SelectionInfo }
  | { type: 'progress'; done: number; total: number; label: string }
  | {
      type: 'bundle';
      purpose: 'download' | 'push';
      /** Suggested archive name, e.g. "MainMenu.figmasync.zip". */
      fileName: string;
      frameName: string;
      frameId: string;
      /** figmasync.json contents. Encoded to UTF-8 in the UI (the main thread has no TextEncoder). */
      json: string;
      /** Image files (paths relative to the bundle root). */
      files: BundleFile[];
      warnings: ExportWarning[];
      stats: { nodes: number; images: number };
    }
  | { type: 'export-error'; message: string };
