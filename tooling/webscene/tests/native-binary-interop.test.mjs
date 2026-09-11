import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';

test('native invoker is a bidirectional ABI 3 binary adapter', async () => {
  const source = await readFile(new URL(
    '../../../src/WebScene.JavaScript.Interop/NativeJavaScriptInvoker.cs',
    import.meta.url), 'utf8');

  assert.match(source, /IJavaScriptBinaryBidirectionalInvoker/);
  assert.match(
    source,
    /NativeJavaScriptInvoker\(\s*IJavaScriptBinaryTransport transport,\s*Func<CancellationToken, ValueTask>\? waitForCallbackAsync = null\)/);
  assert.match(source, /PumpCallbackAsync/);
  assert.match(source, /generated ABI 3 binary codec/);
  assert.doesNotMatch(source, /private const string Bootstrap/);
  assert.doesNotMatch(source, /System\.Text\.Json/);
  assert.doesNotMatch(source, /IJavaScriptBidirectionalInvoker/);
});

test('native engine publishes only the versioned leased interop surface', async () => {
  const [header, exports] = await Promise.all([
    readFile(new URL(
      '../../../experiments/WebScene.NativeEngine.Probe/native/webscene_native_engine.h',
      import.meta.url), 'utf8'),
    readFile(new URL(
      '../../../experiments/WebScene.NativeEngine.Probe/native/webscene_native_engine.exports',
      import.meta.url), 'utf8')
  ]);

  for (const symbol of [
    'webscene_engine_begin_evaluate_v3',
    'webscene_engine_begin_invoke_v3',
    'webscene_engine_take_invoke_result_v3',
    'webscene_engine_cancel_invoke_v3',
    'webscene_interop_result_release_v3'
  ]) {
    assert.match(header, new RegExp(`\\b${symbol}\\b`));
    assert.match(exports, new RegExp(`_${symbol}\\b`));
  }

  assert.doesNotMatch(header, /\bwebscene_engine_evaluate_json\b/);
  assert.doesNotMatch(exports, /_webscene_engine_evaluate_json\b/);
  // File services have their own versioned ABI; they are not legacy interop.
  const fileServiceSymbols = new Set([
    'webscene_engine_enable_file_service_v1',
    'webscene_engine_take_file_request_v1',
    'webscene_engine_complete_file_request_v1',
    'webscene_file_request_release_v1'
  ]);
  for (const symbol of fileServiceSymbols) {
    assert.match(header, new RegExp(`\\b${symbol}\\b`));
    assert.match(exports, new RegExp(`_${symbol}\\b`));
  }
  for (const [name, source] of [['header', header], ['exports', exports]]) {
    const legacySymbols = [...source.matchAll(
      /\b_?(webscene_(?:engine|interop)_[a-z0-9_]+_v[12])\b/g
    )].map(match => match[1]).filter(symbol => !fileServiceSymbols.has(symbol));
    assert.deepEqual(legacySymbols, [], `${name} must not expose legacy interop`);
  }
  assert.match(
    header,
    /webscene_interop_result_release_v3\s*\([^)]*uint64_t lease_id\s*\)/s);
});
