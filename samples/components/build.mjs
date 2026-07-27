import { readFile } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import react from '@vitejs/plugin-react';
import { build } from 'vite';
import { webscene } from '@webscene/sdk/vite';

const componentsRoot = dirname(fileURLToPath(import.meta.url));
const catalog = JSON.parse(await readFile(join(componentsRoot, 'catalog.json'), 'utf8'));

for (const sample of catalog.samples) {
  const sampleRoot = join(componentsRoot, sample.id);
  await build({
    configFile: false,
    root: componentsRoot,
    mode: 'production',
    logLevel: 'warn',
    define: {
      'process.env.NODE_ENV': JSON.stringify('production')
    },
    plugins: [
      webscene({ manifest: join(sampleRoot, 'webscene-component.json') }),
      react()
    ],
    build: {
      outDir: sampleRoot,
      emptyOutDir: false,
      sourcemap: false,
      lib: {
        entry: join(sampleRoot, 'src', 'main.tsx'),
        formats: ['iife'],
        name: `WebScene${sample.id.replaceAll('.', '')}`,
        fileName: () => 'dist/main.js'
      },
      rollupOptions: {
        output: { inlineDynamicImports: true }
      }
    }
  });
  console.log(`BUILT ${sample.id}`);
}
