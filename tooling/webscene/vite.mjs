import { readFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { checkSource, readManifest, validateManifest } from './compatibility.mjs';

export function webscene(options = {}) {
  const manifestPath = resolve(options.manifest ?? 'webscene-component.json');
  let manifest;
  return {
    name: 'webscene-component-profile',
    enforce: 'pre',
    async buildStart() {
      manifest = await readManifest(manifestPath);
      const errors = validateManifest(manifest);
      if (errors.length) this.error(`Invalid WebScene component manifest: ${errors.join('; ')}`);
    },
    transform(source, id) {
      if (!/\.[cm]?[jt]sx?$/.test(id) || id.includes('/node_modules/')) return null;
      for (const diagnostic of checkSource(source, manifest, id)) {
        const text = `${diagnostic.code}: ${diagnostic.message}`;
        if (diagnostic.severity === 'error') this.error({ message: text, id, line: diagnostic.line, column: diagnostic.column });
        else this.warn({ message: text, id, line: diagnostic.line, column: diagnostic.column });
      }
      return null;
    },
    generateBundle(_, bundle) {
      const entry = Object.values(bundle).find(item => item.type === 'chunk' && item.isEntry);
      if (!entry) this.error('WebScene requires one Vite entry chunk.');
      const assets = Object.keys(bundle).sort();
      const packaged = { ...manifest, entryPoint: entry.fileName, assets };
      this.emitFile({ type: 'asset', fileName: 'webscene-component.json', source: JSON.stringify(packaged, null, 2) + '\n' });
    }
  };
}

export default webscene;
