// Builds dist/code.js (main thread) and dist/ui.html (UI with the script inlined; Figma loads one HTML file).
import * as esbuild from 'esbuild';
import { mkdir, readFile, writeFile } from 'node:fs/promises';

const watch = process.argv.includes('--watch');
const PLACEHOLDER = '<!-- WIDGETIFY_UI_SCRIPT -->';

await mkdir('dist', { recursive: true });

const common = { bundle: true, logLevel: 'info', target: 'es2017', legalComments: 'none' };

const inlineUi = {
  name: 'inline-ui',
  setup(build) {
    build.onEnd(async (result) => {
      if (result.errors.length > 0) return;
      const js = result.outputFiles?.find((f) => f.path.endsWith('.js'))?.text ?? '';
      const html = await readFile('src/ui/ui.html', 'utf8');
      if (!html.includes(PLACEHOLDER)) throw new Error(`ui.html is missing ${PLACEHOLDER}`);
      // Escape "</script" so the inlined bundle can never terminate its own tag.
      const safeJs = js.replace(/<\/script/gi, '<\\/script');
      await writeFile('dist/ui.html', html.replace(PLACEHOLDER, () => `<script>${safeJs}</script>`));
      console.log('dist/ui.html written');
    });
  },
};

const mainCtx = await esbuild.context({ ...common, entryPoints: ['src/main/code.ts'], outfile: 'dist/code.js', format: 'iife' });
const uiCtx = await esbuild.context({
  ...common,
  entryPoints: ['src/ui/ui.ts'],
  outfile: 'dist/ui.js',
  format: 'iife',
  write: false,
  minify: !watch,
  plugins: [inlineUi],
});

if (watch) {
  await Promise.all([mainCtx.watch(), uiCtx.watch()]);
  console.log('Watching for changes…');
} else {
  await Promise.all([mainCtx.rebuild(), uiCtx.rebuild()]);
  await Promise.all([mainCtx.dispose(), uiCtx.dispose()]);
}
