import { readFile } from 'node:fs/promises';
import { basename, resolve } from 'node:path';
import { checkSource, readManifest, validateManifest } from './compatibility.mjs';

export function webscene(options = {}) {
  const manifestPath = resolve(options.manifest ?? 'webscene-component.json');
  return {
    name: 'webscene-component-profile',
    setup(build) {
      let manifest;
      build.onStart(async () => {
        manifest = await readManifest(manifestPath);
        return { errors: validateManifest(manifest).map(text => ({ text })) };
      });
      build.onLoad({ filter: /\.[cm]?[jt]sx?$/ }, async args => {
        if (args.path.includes('/node_modules/')) return undefined;
        const contents = await readFile(args.path, 'utf8');
        const diagnostics = checkSource(contents, manifest, args.path);
        return {
          contents,
          loader: loaderFor(args.path),
          errors: diagnostics.filter(item => item.severity === 'error').map(toMessage),
          warnings: diagnostics.filter(item => item.severity === 'warning').map(toMessage)
        };
      });
      build.onEnd(async result => {
        if (result.errors.length || !build.initialOptions.write) return;
        const outputs = Object.keys(result.metafile?.outputs ?? {}).map(path => basename(path)).sort();
        const entryPoint = Object.entries(result.metafile?.outputs ?? {}).find(([, value]) => value.entryPoint)?.[0];
        if (!entryPoint || !build.initialOptions.outdir) return;
        const { writeFile } = await import('node:fs/promises');
        await writeFile(resolve(build.initialOptions.outdir, 'webscene-component.json'), JSON.stringify({ ...manifest, entryPoint: basename(entryPoint), assets: outputs }, null, 2) + '\n');
      });
    }
  };
}

function loaderFor(path) {
  if (path.endsWith('.tsx')) return 'tsx';
  if (path.endsWith('.ts')) return 'ts';
  if (path.endsWith('.jsx')) return 'jsx';
  return 'js';
}

function toMessage(item) {
  return { text: `${item.code}: ${item.message}`, location: { file: item.source, line: item.line, column: item.column - 1 } };
}

export default webscene;
