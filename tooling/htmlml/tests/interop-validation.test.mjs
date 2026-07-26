import test from 'node:test';
import assert from 'node:assert/strict';
import { execFile } from 'node:child_process';
import { mkdtemp, readFile, writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { promisify } from 'node:util';

const execFileAsync = promisify(execFile);
const validator = new URL('../interop-validate.mjs', import.meta.url).pathname;

test('validation command creates a strict all-surface artifact set', async () => {
  const root = await mkdtemp(join(tmpdir(), 'htmlml-interop-validate-'));
  const declarations = join(root, 'library.d.ts');
  const output = join(root, 'generated');
  await writeFile(declarations, `
    export interface Options {
      symbol: string;
    }
    export interface Widget {
      configure(options: Options): Promise<boolean>;
    }
    export function createWidget(options: Options): Widget;
  `);

  const result = await execFileAsync(process.execPath, [
    validator,
    '--declarations', declarations,
    '--output-dir', output,
    '--name', 'Library',
    '--namespace', 'Tests.Generated',
    '--skip-compile'
  ]);
  const summary = JSON.parse(result.stdout);
  const manifest = JSON.parse(await readFile(summary.manifest, 'utf8'));
  const policy = JSON.parse(await readFile(summary.policy, 'utf8'));
  const report = JSON.parse(await readFile(summary.report, 'utf8'));

  assert.equal(summary.compiled, false);
  assert.equal(summary.fallbacks, 0);
  assert.equal(policy.api, 'Library.htmlml-interop-api.json');
  assert.equal(policy.apiFingerprint, manifest.apiFingerprint);
  assert.ok(policy.models.every(model => model.include));
  assert.ok(policy.bindings.every(binding => binding.include));
  assert.ok(policy.adapters.every(adapter => adapter.include));
  assert.ok(policy.functions.every(method => method.include));
  assert.deepEqual(report.fallbacks, []);
});

test('validation command rejects a reviewed policy after declaration drift', async () => {
  const root = await mkdtemp(join(tmpdir(), 'htmlml-interop-drift-'));
  const declarations = join(root, 'library.d.ts');
  const policy = join(root, 'reviewed.htmlml-interop-policy.json');
  await writeFile(declarations, `
    export interface Widget {
      value(): string;
    }
  `);
  await writeFile(policy, JSON.stringify({
    schemaVersion: '1.0',
    api: 'old.htmlml-interop-api.json',
    apiFingerprint: 'old-fingerprint',
    namespace: 'Tests.Generated',
    typeMappings: {},
    functions: [],
    globalProperties: [],
    models: [],
    bindings: [],
    adapters: []
  }));

  await assert.rejects(
    execFileAsync(process.execPath, [
      validator,
      '--declarations', declarations,
      '--output-dir', join(root, 'generated'),
      '--policy-input', policy,
      '--skip-compile'
    ]),
    error => {
      assert.match(error.stderr, /does not match the discovered declarations/);
      assert.match(error.stderr, /Review the API drift/);
      return true;
    });
});
