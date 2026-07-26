import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import vm from 'node:vm';

test('native interop bootstrap separates JSON values from retained objects', async () => {
  const source = await readFile(new URL(
    '../../../src/HtmlML.JavaScript.Interop/NativeJavaScriptInvoker.cs',
    import.meta.url), 'utf8');
  const marker = 'private const string Bootstrap = """';
  const start = source.indexOf(marker);
  assert.notEqual(start, -1);
  const bodyStart = start + marker.length;
  const bodyEnd = source.indexOf('""";', bodyStart);
  assert.notEqual(bodyEnd, -1);

  const context = vm.createContext({});
  vm.runInContext(source.slice(bodyStart, bodyEnd), context);
  vm.runInContext(`
    globalThis.TestLibrary = {
      plainAsync: () => Promise.resolve([{ value: 7 }]),
      liveAsync: () => Promise.resolve(new Map([["value", 7]])),
      plainValue: () => ({ value: 9 }),
      liveArray: () => [new Map([["id", 1]]), new Map([["id", 2]])],
      version: "1.2.3",
      current: new Map([["active", true]])
    };
  `, context);
  const bridge = context.__htmlMlDotNetInterop;

  const plainOperation = bridge.invokeGlobalPromise(
    'TestLibrary.plainAsync',
    '[]');
  const liveOperation = bridge.invokeGlobalPromise(
    'TestLibrary.liveAsync',
    '[]');
  await new Promise(resolve => setImmediate(resolve));

  const plain = bridge.takePromise(plainOperation);
  const live = bridge.takePromise(liveOperation);
  assert.deepEqual(
    JSON.parse(JSON.stringify(plain)),
    {
      status: 'fulfilled',
      value: [{ value: 7 }]
    });
  assert.equal(live.status, 'fulfilled');
  assert.ok(Number.isInteger(live.objectHandle));
  assert.deepEqual(
    JSON.parse(JSON.stringify(
      bridge.invokeGlobalValue('TestLibrary.plainValue', '[]'))),
    { value: 9 });
  assert.equal(bridge.getGlobalValue('TestLibrary.version'), '1.2.3');
  assert.ok(Number.isInteger(
    bridge.getGlobalObject('TestLibrary.current')));
  const liveArray = bridge.invokeGlobalValue(
    'TestLibrary.liveArray',
    '[]');
  assert.equal(liveArray.length, 2);
  assert.ok(liveArray.every(item =>
    Number.isInteger(item.__htmlMlHandle)));
});

test('native interop bootstrap preserves undefined callback arguments', async () => {
  const source = await readFile(new URL(
    '../../../src/HtmlML.JavaScript.Interop/NativeJavaScriptInvoker.cs',
    import.meta.url), 'utf8');
  const marker = 'private const string Bootstrap = """';
  const bodyStart = source.indexOf(marker) + marker.length;
  const bodyEnd = source.indexOf('""";', bodyStart);
  const context = vm.createContext({});
  vm.runInContext(source.slice(bodyStart, bodyEnd), context);
  vm.runInContext(
    'globalThis.TestLibrary = { live: () => new Map([["value", 1]]) };',
    context);
  const bridge = context.__htmlMlDotNetInterop;
  const target = bridge.createCallbackTarget(
    4,
    JSON.stringify([{ name: 'accept', returnKind: 'Void' }]));

  bridge.invokeVoid(
    target,
    'accept',
    '[{"__htmlMlUndefined":true}]');

  const request = bridge.takeCallback();
  assert.deepEqual(
    JSON.parse(JSON.stringify(request.arguments)),
    [{ __htmlMlUndefined: true }]);

  const live = bridge.invokeGlobalObject('TestLibrary.live', '[]');
  bridge.invokeVoid(
    target,
    'accept',
    JSON.stringify([{ __htmlMlHandle: live }]));
  const liveRequest = bridge.takeCallback();
  assert.ok(Number.isInteger(liveRequest.arguments[0].__htmlMlHandle));
});
