#!/usr/bin/env node
import { readdir, readFile } from 'node:fs/promises';
import { extname, join, resolve } from 'node:path';
import { checkSource, readManifest, validateManifest } from './compatibility.mjs';

const args = process.argv.slice(2);
const manifestPath = resolve(valueAfter('--manifest') ?? 'webscene-component.json');
const sourceRoot = resolve(valueAfter('--source') ?? 'src');
const format = valueAfter('--format') ?? 'text';
const manifest = await readManifest(manifestPath);
const manifestErrors = validateManifest(manifest);
const diagnostics = [];

if (manifestErrors.length) {
  diagnostics.push(...manifestErrors.map(message => ({ code: 'WEBSCENE0001', severity: 'error', message, source: manifestPath, line: 1, column: 1 })));
} else {
  for (const path of await sourceFiles(sourceRoot)) {
    diagnostics.push(...checkSource(await readFile(path, 'utf8'), manifest, path));
  }
}

if (format === 'json') console.log(JSON.stringify({ compatible: !diagnostics.some(item => item.severity === 'error'), diagnostics }, null, 2));
else for (const item of diagnostics) console.error(`${item.source}:${item.line}:${item.column} ${item.severity} ${item.code}: ${item.message}`);
process.exitCode = diagnostics.some(item => item.severity === 'error') ? 1 : 0;

function valueAfter(name) {
  const index = args.indexOf(name);
  return index < 0 ? undefined : args[index + 1];
}

async function sourceFiles(root) {
  const files = [];
  for (const entry of await readdir(root, { withFileTypes: true })) {
    const path = join(root, entry.name);
    if (entry.isDirectory()) files.push(...await sourceFiles(path));
    else if (['.js', '.jsx', '.mjs', '.ts', '.tsx'].includes(extname(entry.name))) files.push(path);
  }
  return files.sort();
}
