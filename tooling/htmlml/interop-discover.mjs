#!/usr/bin/env node
import { writeFile } from 'node:fs/promises';
import { basename, resolve } from 'node:path';
import {
  discoverInteropSurface,
  configureInteropPolicy,
  createInteropCoverageReport,
  scaffoldInteropPolicy
} from './interop-discovery.mjs';

const args = process.argv.slice(2);
const declarationValues = valuesAfter('--declarations');
const roots = (valueAfter('--roots') ?? '')
  .split(',')
  .map(value => value.trim())
  .filter(Boolean);
const output = valueAfter('--output');
const policyOutput = valueAfter('--policy-output');
const reportOutput = valueAfter('--report-output');
const namespace = valueAfter('--namespace') ?? 'HtmlML.Interop.Generated';
const proxyRoots = commaValues('--proxy-roots');
const adapterRoots = commaValues('--adapter-roots');
const functionRoots = commaValues('--function-roots');
const globalRoots = commaValues('--global-roots');
const includeAllModels = args.includes('--include-all-models');
const includeAllProxies = args.includes('--include-all-proxies');
const includeAllAdapters = args.includes('--include-all-adapters');
const includeAllFunctions = args.includes('--include-all-functions');
const includeAllGlobals = args.includes('--include-all-globals');
const failOnFallbacks = args.includes('--fail-on-fallbacks');

try {
  if (policyOutput && !output) {
    throw new Error('--policy-output requires --output so the policy can reference its API manifest.');
  }
  const manifest = await discoverInteropSurface(declarationValues, roots);
  const json = JSON.stringify(manifest, null, 2) + '\n';
  if (output) await writeFile(resolve(output), json);
  else process.stdout.write(json);
  if (policyOutput) {
    const policy = configureInteropPolicy(
      scaffoldInteropPolicy(manifest, namespace),
      {
        includeAllModels,
        includeAllProxies,
        includeAllAdapters,
        includeAllFunctions,
        includeAllGlobals,
        proxyRoots,
        adapterRoots,
        functionRoots,
        globalRoots
      });
    policy.api = output ? basename(resolve(output)) : null;
    await writeFile(resolve(policyOutput), JSON.stringify(policy, null, 2) + '\n');
  }
  const report = createInteropCoverageReport(manifest);
  if (reportOutput) {
    await writeFile(resolve(reportOutput), JSON.stringify(report, null, 2) + '\n');
  }
  if (failOnFallbacks && report.fallbacks.length) {
    const examples = report.fallbacks
      .slice(0, 5)
      .map(fallback => `${fallback.location}: ${fallback.text}`)
      .join('\n');
    throw new Error(
      `Interop discovery found ${report.fallbacks.length} unsupported type shape(s).\n${examples}`);
  }
} catch (error) {
  console.error(error instanceof Error ? error.message : String(error));
  process.exitCode = 1;
}

function valueAfter(name) {
  const index = args.indexOf(name);
  return index < 0 ? undefined : args[index + 1];
}

function valuesAfter(name) {
  const values = [];
  for (let index = 0; index < args.length; index++) {
    if (args[index] === name && args[index + 1]) values.push(args[++index]);
  }
  return values;
}

function commaValues(name) {
  return (valueAfter(name) ?? '')
    .split(',')
    .map(value => value.trim())
    .filter(Boolean);
}
