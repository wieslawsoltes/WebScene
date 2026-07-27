#!/usr/bin/env node
import { spawn } from 'node:child_process';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { basename, join, resolve } from 'node:path';
import {
  configureInteropPolicy,
  createInteropCoverageReport,
  discoverInteropSurface,
  scaffoldInteropPolicy
} from './interop-discovery.mjs';

const args = process.argv.slice(2);

try {
  const declarations = valuesAfter('--declarations');
  if (!declarations.length) {
    throw new Error('At least one --declarations <file.d.ts> input is required.');
  }

  const compileProject = valueAfter('--compile-project');
  const skipCompile = args.includes('--skip-compile');
  if (!compileProject && !skipCompile) {
    throw new Error(
      '--compile-project <project.csproj> is required for end-to-end validation; '
      + 'use --skip-compile only when generating validation artifacts.');
  }

  const roots = commaValues('--roots');
  const outputDirectory = resolve(
    valueAfter('--output-dir') ?? 'obj/webscene-interop-validation');
  const artifactName = safeName(
    valueAfter('--name') ?? declarationName(declarations[0]));
  const namespace = valueAfter('--namespace') ?? 'WebScene.Interop.Generated';
  const manifestPath = join(
    outputDirectory,
    `${artifactName}.webscene-interop-api.json`);
  const policyPath = join(
    outputDirectory,
    `${artifactName}.webscene-interop-policy.json`);
  const reportPath = join(outputDirectory, `${artifactName}.coverage.json`);

  await mkdir(outputDirectory, { recursive: true });
  const manifest = await discoverInteropSurface(declarations, roots);
  const report = createInteropCoverageReport(manifest);
  const policyInput = valueAfter('--policy-input');
  const policy = policyInput
    ? await readReviewedPolicy(policyInput, manifest)
    : configureInteropPolicy(
      scaffoldInteropPolicy(manifest, namespace),
      {
        includeAllModels: true,
        includeAllProxies: true,
        includeAllAdapters: true,
        includeAllFunctions: true,
        includeAllGlobals: true
      });
  policy.api = basename(manifestPath);

  await Promise.all([
    writeJson(manifestPath, manifest),
    writeJson(policyPath, policy),
    writeJson(reportPath, report)
  ]);

  if (report.fallbacks.length && !args.includes('--allow-fallbacks')) {
    const examples = report.fallbacks
      .slice(0, 8)
      .map(fallback => `${fallback.location}: ${fallback.text}`)
      .join('\n');
    throw new Error(
      `Interop validation found ${report.fallbacks.length} unsupported type shape(s). `
      + 'Artifacts were written for inspection. Pass --allow-fallbacks only '
      + `when every fallback is intentional.\n${examples}`);
  }

  if (!skipCompile) {
    const harnessId = safeName(
      valueAfter('--harness-id') ?? `${artifactName}-${manifest.apiFingerprint.slice(0, 12)}`);
    const dotnetArguments = [
      'build',
      resolve(compileProject),
      `-p:WebSceneInteropApiManifest=${manifestPath}`,
      `-p:WebSceneInteropPolicy=${policyPath}`,
      `-p:WebSceneInteropHarnessId=${harnessId}`
    ];
    if (args.includes('--no-restore')) {
      dotnetArguments.push('--no-restore');
    }
    await run('dotnet', dotnetArguments);
  }

  process.stdout.write(JSON.stringify({
    manifest: manifestPath,
    policy: policyPath,
    report: reportPath,
    apiFingerprint: manifest.apiFingerprint,
    declarations: manifest.declarations.length,
    roots: manifest.roots.length,
    namedTypes: report.namedTypes,
    methods: report.methods,
    callbacks: report.callbacks,
    fallbacks: report.fallbacks.length,
    compiled: !skipCompile
  }, null, 2) + '\n');
} catch (error) {
  console.error(error instanceof Error ? error.message : String(error));
  process.exitCode = 1;
}

async function readReviewedPolicy(path, manifest) {
  const policy = JSON.parse(await readFile(resolve(path), 'utf8'));
  if (policy.apiFingerprint
      && policy.apiFingerprint !== manifest.apiFingerprint) {
    throw new Error(
      `Reviewed policy fingerprint '${policy.apiFingerprint}' does not match `
      + `the discovered declarations '${manifest.apiFingerprint}'. Review the `
      + 'API drift and regenerate or update the policy explicitly.');
  }
  return policy;
}

async function writeJson(path, value) {
  await writeFile(path, JSON.stringify(value, null, 2) + '\n');
}

async function run(command, commandArguments) {
  await new Promise((resolvePromise, reject) => {
    const child = spawn(command, commandArguments, {
      stdio: 'inherit'
    });
    child.once('error', reject);
    child.once('exit', (code, signal) => {
      if (code === 0) {
        resolvePromise();
        return;
      }
      reject(new Error(
        signal
          ? `${command} terminated by signal ${signal}.`
          : `${command} exited with code ${code}.`));
    });
  });
}

function declarationName(path) {
  const file = basename(path);
  return file.endsWith('.d.ts')
    ? file.slice(0, -5)
    : file.replace(/\.[^.]+$/, '');
}

function safeName(value) {
  const result = value.replace(/[^A-Za-z0-9_.-]+/g, '-');
  if (!result || result === '.' || result === '..') {
    throw new Error(`'${value}' is not a valid artifact name.`);
  }
  return result;
}

function valueAfter(name) {
  const index = args.indexOf(name);
  return index < 0 ? undefined : args[index + 1];
}

function valuesAfter(name) {
  const values = [];
  for (let index = 0; index < args.length; index++) {
    if (args[index] === name && args[index + 1]) {
      values.push(args[++index]);
    }
  }
  return values;
}

function commaValues(name) {
  return (valueAfter(name) ?? '')
    .split(',')
    .map(value => value.trim())
    .filter(Boolean);
}
