/**
 * Regenerates samples/MainMenu.figmasync.zip (and an extracted copy) from the mock document, so the Unreal
 * importer can be tried without Figma. The zip is also copied into the Unreal plugin, where the panel's
 * "Try sample" button finds it. Runs as part of the normal test suite; output is deterministic.
 */
import { mkdirSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { expect, it } from 'vitest';
import { BUNDLE_JSON_NAME } from '../src/shared/bundle';
import { DEFAULT_OPTIONS } from '../src/shared/messages';
import { FrameExporter } from '../src/main/walker';
import { buildZip, readStoredZip } from '../src/ui/zip';
import { asScene, mockHost } from './mockFigma';
import { buildSampleDoc, sampleArt } from './sampleDoc';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
const samplesDir = join(repoRoot, 'samples');
const pluginSamplesDir = join(repoRoot, 'unreal', 'Plugins', 'Widgetify', 'Resources', 'Samples');

it('writes the MainMenu sample bundle', async () => {
  const doc = buildSampleDoc();
  const result = await new FrameExporter(DEFAULT_OPTIONS, mockHost({ art: sampleArt })).run(asScene(doc.root), {
    fileName: 'Sovereign Call UI',
    pageName: 'Menus',
    exportedAt: '2026-10-13T12:00:00.000Z',
  });
  const entries = [{ path: BUNDLE_JSON_NAME, data: new TextEncoder().encode(result.json) }, ...result.files];
  const zip = buildZip(entries, new Date(2026, 9, 13, 12, 0, 0));
  expect(readStoredZip(zip).size).toBe(entries.length);

  for (const dir of [samplesDir, pluginSamplesDir]) {
    mkdirSync(dir, { recursive: true });
    writeFileSync(join(dir, 'MainMenu.figmasync.zip'), zip);
  }
  const folder = join(samplesDir, 'MainMenu');
  rmSync(folder, { recursive: true, force: true });
  for (const entry of entries) {
    const target = join(folder, entry.path);
    mkdirSync(dirname(target), { recursive: true });
    writeFileSync(target, entry.data);
  }
});
