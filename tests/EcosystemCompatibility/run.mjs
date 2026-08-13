#!/usr/bin/env node
import { spawn, spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import { existsSync } from "node:fs";
import { access, mkdir, mkdtemp, readFile, readdir, rm, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import process from "node:process";
import { fileURLToPath, pathToFileURL } from "node:url";
import { CdpClient } from "../WebPlatformSubset/chrome/cdp-client.mjs";

const delay = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds));
const root = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(root, "../..");
const manifestPath = path.join(root, "ecosystem-profile.json");
const runnerProject = path.join(repositoryRoot, "tests/WebPlatformSubset/runner/WebScene.WebPlatformSubset.Runner.csproj");

function parseArguments(values) {
  const options = new Map();
  for (let index = 0; index < values.length; index++) {
    const name = values[index];
    if (!name.startsWith("--")) throw new Error(`Unexpected argument '${name}'.`);
    const value = values[index + 1];
    if (!value || value.startsWith("--")) throw new Error(`Missing value after '${name}'.`);
    options.set(name, value);
    index++;
  }
  return options;
}

function selectedEngines(value) {
  if (value === "all") return ["chrome", "native"];
  const engines = value.split(",").map(item => item.trim()).filter(Boolean);
  if (!engines.length || engines.some(item => !["chrome", "native"].includes(item))) {
    throw new Error("--engine must be all or a comma-separated subset of chrome,native.");
  }
  return [...new Set(engines)];
}

async function ensureEmptyDirectory(directory) {
  await mkdir(directory, { recursive: true });
  if ((await readdir(directory)).length) {
    throw new Error(`Output directory '${directory}' must be empty; evidence is never overwritten.`);
  }
}

function runBuild() {
  const result = spawnSync(process.execPath, [path.join(root, "build.mjs")], {
    cwd: root,
    encoding: "utf8"
  });
  if (result.status !== 0) throw new Error(`Fixture build failed:\n${result.stderr || result.stdout}`);
}

async function verifyVersions(manifest) {
  for (const consumer of manifest.consumers) {
    const packageFile = path.join(root, "node_modules", consumer.package, "package.json");
    const installed = JSON.parse(await readFile(packageFile, "utf8"));
    if (installed.version !== consumer.version) {
      throw new Error(`${consumer.package} ${installed.version} does not match manifest ${consumer.version}.`);
    }
  }
}

function runNativeEngine(outputDirectory, nativeLibrary, timeoutSeconds) {
  if (!nativeLibrary || !existsSync(nativeLibrary)) {
    throw new Error("Native ecosystem evidence requires --native-library <existing path> or WEBSCENE_NATIVE_ENGINE_LIBRARY.");
  }
  const args = [
    "run", "--project", runnerProject, "-c", "Release", "--no-restore", "--",
    "--manifest", manifestPath,
    "--output", outputDirectory,
    "--selection", "candidate",
    "--timeout-seconds", String(timeoutSeconds),
    "--native-library", nativeLibrary
  ];
  const result = spawnSync("dotnet", args, { cwd: repositoryRoot, encoding: "utf8" });
  process.stdout.write(result.stdout || "");
  process.stderr.write(result.stderr || "");
  // Candidate failures are evidence, not an infrastructure failure. The runner
  // returns nonzero only for required failures, so any nonzero here is fatal.
  if (result.status !== 0) throw new Error(`Native consumer runner failed with exit code ${result.status}.`);
}

function chromeCandidates() {
  return [
    process.env.CHROME_BIN,
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    "/Applications/Google Chrome Canary.app/Contents/MacOS/Google Chrome Canary",
    "/usr/bin/google-chrome",
    "/usr/bin/google-chrome-stable",
    "/usr/bin/chromium",
    "/usr/bin/chromium-browser"
  ].filter(Boolean);
}

function findChrome() {
  const executable = chromeCandidates().find(candidate => existsSync(candidate));
  if (!executable) throw new Error("Chrome was not found. Set CHROME_BIN to its executable path.");
  const version = spawnSync(executable, ["--version"], { encoding: "utf8" });
  if (version.status !== 0) throw new Error(`Could not read Chrome version: ${version.stderr}`);
  return { executable, version: version.stdout.trim() };
}

async function launchChrome(executable) {
  const userDataDirectory = await mkdtemp(path.join(os.tmpdir(), "webscene-ecosystem-chrome-"));
  const child = spawn(executable, [
    "--headless=new",
    "--disable-background-networking",
    "--disable-component-update",
    "--disable-default-apps",
    "--disable-extensions",
    "--disable-features=Translate",
    "--disable-sync",
    "--metrics-recording-only",
    "--no-first-run",
    "--no-default-browser-check",
    "--remote-debugging-port=0",
    `--user-data-dir=${userDataDirectory}`,
    "about:blank"
  ], { stdio: ["ignore", "ignore", "pipe"] });

  const endpoint = await new Promise((resolve, reject) => {
    let buffer = "";
    const timeout = setTimeout(() => reject(new Error("Timed out waiting for Chrome DevTools endpoint.")), 20_000);
    child.stderr.setEncoding("utf8");
    child.stderr.on("data", chunk => {
      buffer += chunk;
      const match = buffer.match(/DevTools listening on (ws:\/\/[^\s]+)/);
      if (!match) return;
      clearTimeout(timeout);
      resolve(match[1]);
    });
    child.once("exit", code => {
      clearTimeout(timeout);
      reject(new Error(`Chrome exited before DevTools was ready (code ${code}).\n${buffer}`));
    });
  });
  const url = new URL(endpoint);
  const origin = `http://${url.hostname}:${url.port}`;
  let targets;
  for (let attempt = 0; attempt < 100; attempt++) {
    try {
      targets = await (await fetch(`${origin}/json/list`)).json();
      if (targets.some(target => target.type === "page")) break;
    } catch { /* Chrome endpoint can race initial target publication. */ }
    await delay(50);
  }
  const target = targets?.find(item => item.type === "page");
  if (!target?.webSocketDebuggerUrl) throw new Error("Chrome exposed no debuggable page target.");
  const client = await CdpClient.connect(target.webSocketDebuggerUrl);
  await client.send("Page.enable");
  await client.send("Runtime.enable");
  await client.send("Page.bringToFront");
  await client.send("Emulation.setFocusEmulationEnabled", { enabled: true });
  return { child, client, userDataDirectory };
}

async function closeChrome(chrome) {
  try { await chrome.client.send("Browser.close", {}, 2_000); } catch { chrome.child.kill("SIGTERM"); }
  chrome.client.close();
  await Promise.race([
    new Promise(resolve => chrome.child.once("exit", resolve)),
    delay(2_000).then(() => chrome.child.kill("SIGKILL"))
  ]);
  await rm(chrome.userDataDirectory, { recursive: true, force: true });
}

async function evaluate(client, expression) {
  const response = await client.send("Runtime.evaluate", { expression, returnByValue: true, awaitPromise: true });
  if (response.exceptionDetails) throw new Error(JSON.stringify(response.exceptionDetails));
  return response.result.value;
}

async function runChromeDocument(client, documentPath, timeoutSeconds) {
  const started = Date.now();
  const exceptions = [];
  const listener = event => exceptions.push(
    event.exceptionDetails?.exception?.description
      || event.exceptionDetails?.text
      || "Chrome runtime exception");
  client.on("Runtime.exceptionThrown", listener);
  try {
    await client.send("Page.navigate", { url: pathToFileURL(documentPath).href });
    const deadline = Date.now() + timeoutSeconds * 1000;
    let state = null;
    while (Date.now() < deadline) {
      try {
        const json = await evaluate(client, "JSON.stringify(globalThis.__webSceneWptState || null)");
        state = JSON.parse(json);
        if (state?.complete) break;
      } catch { /* Execution context is replaced while navigating. */ }
      await delay(25);
    }
    if (!state?.complete) {
      return { path: documentPath, type: "contract", status: "TIMEOUT", duration: Date.now() - started,
        message: "Chrome contract did not complete.", subtests: [], artifacts: null };
    }
    const subtests = state.results || [];
    const expectedRuntimeExceptions = Array.isArray(state.expectedRuntimeExceptions)
      ? state.expectedRuntimeExceptions.map(String)
      : [];
    const unexpectedRuntimeExceptions = exceptions.filter(message =>
      !expectedRuntimeExceptions.some(expected => String(message).includes(expected)));
    const passed = state.harness?.status === 0
      && subtests.every(result => result.status === "PASS")
      && unexpectedRuntimeExceptions.length === 0;
    return {
      path: documentPath,
      type: "contract",
      status: passed ? "PASS" : "FAIL",
      duration: Date.now() - started,
      message: passed ? null : state.harness?.message || unexpectedRuntimeExceptions.join("\n") || "Chrome subtest failed.",
      subtests,
      artifacts: null
    };
  } catch (error) {
    return { path: documentPath, type: "contract", status: "HARNESS-ERROR", duration: Date.now() - started,
      message: String(error?.stack || error), subtests: [], artifacts: null };
  }
}

function summarize(results) {
  const subtests = results.flatMap(result => result.subtests || []);
  return {
    tests: results.length,
    passed: results.filter(result => result.status === "PASS").length,
    failed: results.filter(result => result.status === "FAIL").length,
    timedOut: results.filter(result => result.status === "TIMEOUT").length,
    harnessErrors: results.filter(result => result.status === "HARNESS-ERROR").length,
    subtests: subtests.length,
    subtestsPassed: subtests.filter(result => result.status === "PASS").length,
    subtestsFailed: subtests.filter(result => result.status !== "PASS").length
  };
}

async function runChrome(manifest, outputDirectory, timeoutSeconds) {
  const chromeIdentity = findChrome();
  const chrome = await launchChrome(chromeIdentity.executable);
  const results = [];
  try {
    for (const test of manifest.candidate) {
      const documentPath = path.join(root, test.path);
      process.stdout.write(`RUN  chrome ${test.path} ... `);
      const result = await runChromeDocument(chrome.client, documentPath, timeoutSeconds);
      result.path = test.path;
      results.push(result);
      process.stdout.write(`${result.status} (${result.duration} ms)\n`);
      for (const subtest of result.subtests.filter(item => item.status !== "PASS")) {
        process.stdout.write(`     ${subtest.status}: ${subtest.name}: ${subtest.message}\n`);
      }
    }
  } finally {
    await closeChrome(chrome);
  }
  const artifact = {
    schema: "webscene-ecosystem-chrome-result-v1",
    profile: manifest.profile,
    engine: "chrome",
    identity: chromeIdentity.version,
    startedAt: new Date().toISOString(),
    summary: summarize(results),
    results
  };
  await mkdir(outputDirectory, { recursive: true });
  await writeFile(path.join(outputDirectory, "results.json"), JSON.stringify(artifact, null, 2) + "\n");
  return artifact;
}

function engineSummary(engine, artifact, outputRoot) {
  return {
    identity: engine === "chrome" ? artifact.identity : artifact.nativeEngineIdentity,
    documents: artifact.summary.tests,
    passed: artifact.summary.passed,
    failed: artifact.summary.failed,
    timedOut: artifact.summary.timedOut,
    harnessErrors: artifact.summary.harnessErrors,
    subtests: artifact.summary.subtests,
    subtestsPassed: artifact.summary.subtestsPassed,
    subtestsFailed: artifact.summary.subtestsFailed,
    resultPath: path.relative(outputRoot, path.join(outputRoot, engine, "results.json")).replaceAll(path.sep, "/")
  };
}

function consumerEngineSummary(consumer, artifact) {
  const documents = new Set(consumer.documents);
  const results = artifact.results.filter(result => documents.has(result.path));
  const summary = summarize(results);
  return {
    documents: summary.tests,
    passed: summary.passed,
    failed: summary.failed + summary.timedOut + summary.harnessErrors,
    subtests: summary.subtests,
    subtestsPassed: summary.subtestsPassed,
    subtestsFailed: summary.subtestsFailed
  };
}

const options = parseArguments(process.argv.slice(2));
const engines = selectedEngines(options.get("--engine") || "all");
const outputRoot = path.resolve(options.get("--output") || path.join(
  repositoryRoot,
  "TestResults/EcosystemCompatibility",
  new Date().toISOString().replaceAll(":", "-").replaceAll(".", "-")));
const nativeLibrary = options.get("--native-library") || process.env.WEBSCENE_NATIVE_ENGINE_LIBRARY;
const timeoutSeconds = Number(options.get("--timeout-seconds") || 20);
if (!Number.isFinite(timeoutSeconds) || timeoutSeconds <= 0) throw new Error("--timeout-seconds must be positive.");

await ensureEmptyDirectory(outputRoot);
const manifest = JSON.parse(await readFile(manifestPath, "utf8"));
await verifyVersions(manifest);
runBuild();
const provenance = JSON.parse(await readFile(path.join(root, "contracts/provenance.json"), "utf8"));
const artifacts = {};

for (const engine of engines) {
  const engineOutput = path.join(outputRoot, engine);
  if (engine === "chrome") {
    artifacts.chrome = await runChrome(manifest, engineOutput, timeoutSeconds);
    continue;
  }
  runNativeEngine(engineOutput, nativeLibrary, timeoutSeconds);
  artifacts[engine] = JSON.parse(await readFile(path.join(engineOutput, "results.json"), "utf8"));
}

const report = {
  schema: "webscene-ecosystem-consumer-result-v1",
  profile: manifest.profile,
  recordedAt: new Date().toISOString(),
  packageLockSha256: provenance.packageLockSha256,
  upstreamSourceManifestSha256: provenance.upstreamSourceManifestSha256,
  upstreamFiles: provenance.upstreamFiles,
  contractFiles: provenance.files,
  engines: Object.fromEntries(engines.map(engine => [engine, engineSummary(engine, artifacts[engine], outputRoot)])),
  consumers: manifest.consumers.map(consumer => ({
    id: consumer.id,
    package: consumer.package,
    version: consumer.version,
    stage: consumer.stage,
    selected: consumer.selected,
    runnable: consumer.runnable,
    excluded: consumer.excluded,
    harnessBlocked: consumer.harnessBlocked,
    upstreamSuite: consumer.upstreamSuite,
    engineResults: Object.fromEntries(engines.map(engine => [engine, consumerEngineSummary(consumer, artifacts[engine])]))
  }))
};

await writeFile(path.join(outputRoot, "ecosystem-results.json"), JSON.stringify(report, null, 2) + "\n");
process.stdout.write(`Ecosystem report: ${path.join(outputRoot, "ecosystem-results.json")}\n`);
for (const [engine, summary] of Object.entries(report.engines)) {
  process.stdout.write(`${engine}: ${summary.passed}/${summary.documents} documents, ${summary.subtestsPassed}/${summary.subtests} subtests\n`);
}
