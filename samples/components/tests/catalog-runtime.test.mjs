import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFile } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';
import { JSDOM } from 'jsdom';

const componentsRoot = join(dirname(fileURLToPath(import.meta.url)), '..');
const catalog = JSON.parse(await readFile(join(componentsRoot, 'catalog.json'), 'utf8'));
const matrix = JSON.parse(await readFile(join(componentsRoot, 'compatibility-matrix.json'), 'utf8'));
const packageLock = JSON.parse(await readFile(join(componentsRoot, 'package-lock.json'), 'utf8'));
const expectations = new Map(matrix.components.map(component => [component.id, component]));

test('compatibility matrix pins every catalog bundle and evidence lane', async () => {
  assert.equal(matrix.schemaVersion, 1);
  assert.equal(matrix.productNeutral, true);
  for (const [name, version] of Object.entries(matrix.pinnedToolchain)) {
    assert.equal(packageLock.packages[`node_modules/${name}`]?.version, version,
      `${name} toolchain pin`);
  }
  assert.deepEqual(
    matrix.components.map(component => component.id).sort(),
    catalog.samples.map(sample => sample.id).sort());
  for (const component of matrix.components) {
    const bundle = await readFile(join(componentsRoot, component.id, 'dist', 'main.js'));
    const manifest = JSON.parse(await readFile(
      join(componentsRoot, component.id, 'webscene-component.json'), 'utf8'));
    assert.equal(createHash('sha256').update(bundle).digest('hex'), component.bundleSha256,
      `${component.id} bundle digest`);
    assert.equal(manifest.entryPoint, 'dist/main.js', `${component.id} pinned local entry`);
    assert.deepEqual(manifest.lifecycle, { mountExport: 'mount', unmountExport: 'unmount' },
      `${component.id} deterministic lifecycle exports`);
    assert.ok(manifest.capabilities.includes('dom'), `${component.id} DOM capability inventory`);
    assert.equal(component.jsdom, 'pass');
    assert.equal(component.native, 'pass');
  }
});

for (const sample of catalog.samples) {
  test(`${sample.id} executes, renders, interacts and unmounts`, async () => {
    const runtime = await createRuntime(sample.id);
    try {
      runtime.window.mount({ instanceId: `test-${sample.id}` });
      await settle(runtime.window);

      const expectation = expectations.get(sample.id);
      assert.ok(expectation, `${sample.id} must have a compatibility-matrix row`);
      assert.match(runtime.document.body.textContent, new RegExp(escapeRegex(expectation.visibleText)));
      assert.ok(runtime.document.querySelector('main'), 'sample must render a visible application root');
      assert.ok(runtime.document.body.textContent.trim().length > 80, 'sample must contain meaningful visible content');
      assert.ok(runtime.document.querySelector('button, input'), 'sample must expose an interactive control');

      await interact(expectation.interaction)(runtime);
      runtime.window.unmount();
      await settle(runtime.window);
      assert.equal(runtime.document.body.children.length, 0, 'unmount must remove the component root');
    } finally {
      runtime.dom.window.close();
    }
  });
}

test('Hybrid.ReactIslands keeps state isolated across two JavaScript realms', async () => {
  const first = await createRuntime('Hybrid.ReactIslands');
  const second = await createRuntime('Hybrid.ReactIslands');
  try {
    first.window.mount({ instanceId: 'island-a' });
    second.window.mount({ instanceId: 'island-b' });
    await Promise.all([settle(first.window), settle(second.window)]);
    findButton(first.document, 'Island count: 0').click();
    await settle(first.window);
    assert.match(first.document.body.textContent, /Island count: 1/);
    assert.match(second.document.body.textContent, /Island count: 0/);
  } finally {
    first.window.unmount();
    second.window.unmount();
    first.dom.window.close();
    second.dom.window.close();
  }
});

function interact({ buttonText, expectedText, hostMethod }) {
  return async runtime => {
    const button = findButton(runtime.document, buttonText);
    let keyObserved = false;
    button.addEventListener('keydown', event => { keyObserved = event.key === 'Enter'; }, { once: true });
    button.focus();
    button.dispatchEvent(new runtime.window.KeyboardEvent('keydown', { key: 'Enter', bubbles: true }));
    assert.equal(runtime.document.activeElement, button, 'keyboard focus must reach the interaction control');
    assert.equal(keyObserved, true, 'keyboard events must reach the interaction control');
    button.click();
    await settle(runtime.window);
    assert.match(runtime.document.body.textContent, new RegExp(escapeRegex(expectedText)));
    if (hostMethod) {
      assert.ok(
        runtime.invocations.some(invocation => invocation.method === hostMethod),
        `expected host invocation '${hostMethod}'`
      );
    }
  };
}

async function createRuntime(sampleId) {
  const dom = new JSDOM('<!doctype html><html><body></body></html>', {
    pretendToBeVisual: true,
    runScripts: 'outside-only',
    url: 'https://webscene.local/'
  });
  const { window } = dom;
  const invocations = [];
  const client = capability => ({
    invoke(method, argumentsValue = {}) {
      invocations.push({ capability, method, arguments: argumentsValue });
      return Promise.resolve({ accepted: true });
    }
  });
  window.webscene = {
    profileVersion: '1.0',
    host: {
      commands: client('host.commands'),
      settings: client('host.settings'),
      notifications: client('host.notifications'),
      network: client('host.network'),
      clipboard: client('host.clipboard'),
      files: client('host.files')
    }
  };
  window.HTMLCanvasElement.prototype.getContext = () => ({
    beginPath() {},
    clearRect() {},
    fillRect() {},
    fillText() {},
    lineTo() {},
    moveTo() {},
    stroke() {},
    fillStyle: '',
    lineWidth: 1,
    strokeStyle: ''
  });
  const source = await readFile(join(componentsRoot, sampleId, 'dist', 'main.js'), 'utf8');
  window.eval(source);
  assert.equal(typeof window.mount, 'function', `${sampleId} must export mount`);
  assert.equal(typeof window.unmount, 'function', `${sampleId} must export unmount`);
  return { dom, window, document: window.document, invocations };
}

function findButton(document, text) {
  const button = [...document.querySelectorAll('button')]
    .find(candidate => candidate.textContent.includes(text));
  assert.ok(button, `expected button containing '${text}'`);
  return button;
}

function settle(window) {
  return new Promise(resolve => window.setTimeout(resolve, 30));
}

function escapeRegex(value) {
  return value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}
