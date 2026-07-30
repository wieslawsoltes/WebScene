import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';

test('native invoker is a forward-only ABI 3 binary adapter', async () => {
  const source = await readFile(new URL(
    '../../../src/WebScene.JavaScript.Interop/NativeJavaScriptInvoker.cs',
    import.meta.url), 'utf8');

  assert.match(source, /IJavaScriptBinaryInvoker/);
  assert.match(source, /NativeJavaScriptInvoker\(IJavaScriptBinaryTransport transport\)/);
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
  assert.doesNotMatch(header, /\bwebscene_(?:engine|interop)_[a-z0-9_]+_v[12]\b/);
  assert.match(
    header,
    /webscene_interop_result_release_v3\s*\([^)]*uint64_t lease_id\s*\)/s);
});
