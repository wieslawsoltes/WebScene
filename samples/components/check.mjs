import { readFile } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { checkSource, readManifest, validateManifest } from '@webscene/sdk/compatibility';

const componentsRoot = dirname(fileURLToPath(import.meta.url));
const catalog = JSON.parse(await readFile(join(componentsRoot, 'catalog.json'), 'utf8'));
let failures = 0;

for (const sample of catalog.samples) {
  const root = join(componentsRoot, sample.id);
  const manifest = await readManifest(join(root, 'webscene-component.json'));
  const manifestErrors = validateManifest(manifest);
  const sourcePath = join(root, 'src', 'main.tsx');
  const diagnostics = manifestErrors.map(message => ({ severity: 'error', message }))
    .concat(checkSource(await readFile(sourcePath, 'utf8'), manifest, sourcePath));
  for (const diagnostic of diagnostics) {
    console.error(`${sample.id}: ${diagnostic.severity}: ${diagnostic.message}`);
  }
  if (diagnostics.some(item => item.severity === 'error')) failures += 1;
  else console.log(`PASS ${sample.id}`);
}

if (failures) process.exitCode = 1;
