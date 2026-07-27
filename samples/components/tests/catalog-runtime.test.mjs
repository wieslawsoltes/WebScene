import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';
import { JSDOM } from 'jsdom';

const componentsRoot = join(dirname(fileURLToPath(import.meta.url)), '..');
const catalog = JSON.parse(await readFile(join(componentsRoot, 'catalog.json'), 'utf8'));

const expectations = {
  'ComponentHost.Basic': ['Basic component host', interact('Clicked 0 times', 'Clicked 1 time')],
  'Hybrid.ReactIslands': ['Independent component root', interact('Island count: 0', 'Island count: 1', 'counterChanged')],
  TypeScriptDesktop: ['Desktop workspace', interact('Form', 'Profile form')],
  ReactDashboard: ['Operations overview', interact('Inspect chart', 'Chart details')],
  PluginWorkbench: ['Plugin workbench', interact('Load plugin', 'Unload plugin', 'pluginLoaded')],
  MultiInstanceWorkstation: ['Isolated chart instance', interact('Local count: 0', 'Local count: 1')],
  WebToNativeMigration: ['Existing React order panel', interact('#1048 · Contoso', 'Legacy component note', 'selectionChanged')],
  OfflineKiosk: ['Station kiosk', interact('Print ticket', 'K-5499')],
  HeadlessComponentTests: ['Headless component tests', interact('Pointer events: 0', 'Pointer events: 1')],
  'HostBridge.Services': ['Host bridge services', interact('Invoke service', 'Completed', 'execute')],
  AccessibilityGallery: ['Accessibility gallery', interact('Save profile', 'Profile saved successfully')],
  'CanvasWorkbench.Advanced': ['Interactive canvas workbench', interact('Add indicator', 'Moving Average')]
};

for (const sample of catalog.samples) {
  test(`${sample.id} executes, renders, interacts and unmounts`, async () => {
    const runtime = await createRuntime(sample.id);
    try {
      runtime.window.mount({ instanceId: `test-${sample.id}` });
      await settle(runtime.window);

      const [visibleText, interaction] = expectations[sample.id];
      assert.match(runtime.document.body.textContent, new RegExp(escapeRegex(visibleText)));
      assert.ok(runtime.document.querySelector('main'), 'sample must render a visible application root');
      assert.ok(runtime.document.body.textContent.trim().length > 80, 'sample must contain meaningful visible content');
      assert.ok(runtime.document.querySelector('button, input'), 'sample must expose an interactive control');

      await interaction(runtime);
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

function interact(buttonText, expectedText, expectedInvocation) {
  return async runtime => {
    findButton(runtime.document, buttonText).click();
    await settle(runtime.window);
    assert.match(runtime.document.body.textContent, new RegExp(escapeRegex(expectedText)));
    if (expectedInvocation) {
      assert.ok(
        runtime.invocations.some(invocation => invocation.method === expectedInvocation),
        `expected host invocation '${expectedInvocation}'`
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
