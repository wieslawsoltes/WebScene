import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { webscene as viteWebScene } from '../vite.mjs';

const manifest = {
  schemaVersion: '1.0', id: 'dev.webscene.integration', displayName: 'Integration', version: '1.0.0',
  profileVersion: '1.0', entryPoint: 'src/main.ts', assets: ['src/main.ts'], capabilities: ['dom']
};

test('Vite plugin checks sources and emits packaged assets', async () => {
  const root = await mkdtemp(join(tmpdir(), 'webscene-vite-'));
  const manifestPath = join(root, 'webscene-component.json');
  await writeFile(manifestPath, JSON.stringify(manifest));
  const plugin = viteWebScene({ manifest: manifestPath });
  const emitted = [];
  const context = {
    error(value) { throw new Error(typeof value === 'string' ? value : value.message); },
    warn() {},
    emitFile(value) { emitted.push(value); }
  };
  await plugin.buildStart.call(context);
  assert.equal(plugin.transform.call(context, 'document.body.textContent = "ready";', join(root, 'src/main.ts')), null);
  plugin.generateBundle.call(context, {}, { 'dist/main.js': { type: 'chunk', isEntry: true, fileName: 'dist/main.js' } });
  const packaged = JSON.parse(emitted[0].source);
  assert.equal(packaged.entryPoint, 'dist/main.js');
  assert.deepEqual(packaged.assets, ['dist/main.js']);
});

test('runtime forwards abort to the native bridge request', async () => {
  let cancelled;
  globalThis.__webSceneHostBridge = {
    invoke(request, resolve) { resolve(JSON.stringify({ requestId: JSON.parse(request).requestId, ok: true, result: 7 })); },
    cancel(requestId) { cancelled = requestId; }
  };
  const { webscene } = await import(`../runtime.mjs?test=${Date.now()}`);
  assert.equal(await webscene.host.commands.invoke('value'), 7);

  globalThis.__webSceneHostBridge.invoke = () => {};
  const controller = new AbortController();
  void webscene.host.commands.invoke('wait', {}, { signal: controller.signal });
  controller.abort();
  assert.equal(typeof cancelled, 'string');
});
