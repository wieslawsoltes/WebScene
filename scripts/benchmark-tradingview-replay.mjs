#!/usr/bin/env node

import { spawn } from "node:child_process";
import { createHash } from "node:crypto";
import { existsSync } from "node:fs";
import { mkdir, mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";

const DEFAULT_URL = "https://trading-terminal.tradingview-widget.com/";
const READY_EXPRESSION = `(() => {
  const loading = document.querySelector('.loading-indicator');
  return document.querySelectorAll('canvas').length >= 8
    && loading
    && getComputedStyle(loading).display === 'none';
})()`;

class CdpClient {
  #nextId = 0;
  #pending = new Map();
  #listeners = new Map();

  constructor(address) {
    this.socket = new WebSocket(address);
    this.opened = new Promise((resolve, reject) => {
      this.socket.addEventListener("open", resolve, { once: true });
      this.socket.addEventListener("error", reject, { once: true });
    });
    this.socket.addEventListener("message", event => {
      const message = JSON.parse(event.data);
      if (message.id) {
        const pending = this.#pending.get(message.id);
        if (!pending) return;
        this.#pending.delete(message.id);
        if (message.error) pending.reject(new Error(message.error.message));
        else pending.resolve(message.result ?? {});
        return;
      }
      for (const listener of this.#listeners.get(message.method) ?? []) {
        listener(message.params ?? {});
      }
    });
  }

  async send(method, params = {}) {
    await this.opened;
    const id = ++this.#nextId;
    const result = new Promise((resolve, reject) => {
      this.#pending.set(id, { resolve, reject });
    });
    this.socket.send(JSON.stringify({ id, method, params }));
    return result;
  }

  on(method, listener) {
    const listeners = this.#listeners.get(method) ?? [];
    listeners.push(listener);
    this.#listeners.set(method, listeners);
  }

  close() {
    this.socket.close();
  }
}

function parseArguments(arguments_) {
  const options = { url: DEFAULT_URL, timeout: 30_000 };
  for (let index = 0; index < arguments_.length; index++) {
    switch (arguments_[index]) {
      case "--archive":
        options.archive = path.resolve(arguments_[++index]);
        break;
      case "--chrome":
        options.chrome = path.resolve(arguments_[++index]);
        break;
      case "--url":
        options.url = arguments_[++index];
        break;
      case "--timeout-ms":
        options.timeout = Number(arguments_[++index]);
        break;
      case "--capture-misses":
        options.captureMisses = true;
        break;
      default:
        throw new Error(`Unknown or incomplete argument '${arguments_[index]}'.`);
    }
  }
  if (!options.archive) {
    throw new Error("Pass --archive /path/to/a/WebScene/resource/archive.");
  }
  if (!Number.isFinite(options.timeout) || options.timeout <= 0) {
    throw new Error("--timeout-ms must be a positive number.");
  }
  return options;
}

function defaultChromePath() {
  const candidates = process.platform === "darwin"
    ? ["/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"]
    : process.platform === "win32"
      ? [
          path.join(process.env.PROGRAMFILES ?? "", "Google/Chrome/Application/chrome.exe"),
          path.join(process.env["PROGRAMFILES(X86)"] ?? "", "Google/Chrome/Application/chrome.exe")
        ]
      : ["/usr/bin/google-chrome", "/usr/bin/chromium", "/usr/bin/chromium-browser"];
  const executable = candidates.find(candidate => candidate && existsSync(candidate));
  if (!executable) {
    throw new Error("Chrome was not found; pass --chrome /path/to/chrome.");
  }
  return executable;
}

async function loadArchive(directory) {
  const manifestPath = path.join(directory, "manifest.json");
  const manifest = JSON.parse(await readFile(manifestPath, "utf8"));
  if (manifest.SchemaVersion !== 1 || !Array.isArray(manifest.Entries)) {
    throw new Error(`Unsupported WebScene resource archive '${manifestPath}'.`);
  }

  const entriesByAddress = new Map();
  for (const entry of manifest.Entries) {
    const address = withoutFragment(entry.Address);
    const entries = entriesByAddress.get(address) ?? [];
    entries.push({
      ...entry,
      body: (await readFile(path.join(directory, entry.ContentFile))).toString("base64")
    });
    entriesByAddress.set(address, entries);
  }
  return { directory, manifest, entriesByAddress };
}

function withoutFragment(address) {
  const url = new URL(address);
  url.hash = "";
  return url.href;
}

function chooseEntry(entries, resourceType) {
  if (!entries?.length) return undefined;
  const expectedKind = {
    Script: 0,
    Stylesheet: 1,
    Document: 2,
    Image: 3
  }[resourceType];
  if (resourceType === "Font") {
    return entries.find(entry => entry.ResourceType === "binary");
  }
  return entries.find(entry => entry.Kind === expectedKind)
    ?? entries.find(entry => entry.ResourceType === "text")
    ?? entries[0];
}

function contentType(entry, resourceType) {
  if (entry.MimeType) return entry.MimeType;
  if (resourceType === "Font" || entry.ResourceType === "binary") return "font/woff2";
  if (entry.Kind === 0) return "application/javascript; charset=utf-8";
  if (entry.Kind === 1) return "text/css; charset=utf-8";
  if (entry.Kind === 3 || /\.svg(?:$|\?)/i.test(entry.Address)) return "image/svg+xml";
  if (resourceType === "Document" || /\.(?:html?|xhtml)(?:$|\?)/i.test(entry.Address)) {
    return "text/html; charset=utf-8";
  }
  return "application/json; charset=utf-8";
}

function resourceKind(resourceType) {
  return {
    Script: 0,
    Stylesheet: 1,
    Document: 2,
    Image: 3
  }[resourceType] ?? 2;
}

async function captureResponse(archive, candidate, response, body) {
  const content = Buffer.from(body.body, body.base64Encoded ? "base64" : "utf8");
  const hash = createHash("sha256").update(content).digest("hex");
  const contentFile = `objects/${hash}.bin`;
  await mkdir(path.join(archive.directory, "objects"), { recursive: true });
  await writeFile(path.join(archive.directory, contentFile), content, { flag: "wx" })
    .catch(error => {
      if (error.code !== "EEXIST") throw error;
    });

  const type = candidate.resourceType === "Font" ? "binary" : "text";
  const kind = type === "text" ? resourceKind(candidate.resourceType) : null;
  const key = type === "binary"
    ? `binary:${candidate.url}`
    : `text:${kind}:${candidate.url}`;
  if (archive.manifest.Entries.some(entry => entry.Key === key)) return false;
  const entry = {
    Key: key,
    Address: candidate.url,
    ResourceType: type,
    Kind: kind,
    ContentFile: contentFile,
    ContentLength: content.length,
    CacheKey: candidate.url,
    DisplayName: candidate.url,
    Directory: null,
    EntityTag: null,
    LastModified: null,
    FreshUntil: null,
    IsCacheable: true,
    MimeType: response.mimeType || undefined
  };
  archive.manifest.Entries.push(entry);
  const address = withoutFragment(candidate.url);
  const entries = archive.entriesByAddress.get(address) ?? [];
  entries.push({ ...entry, body: content.toString("base64") });
  archive.entriesByAddress.set(address, entries);
  return true;
}

async function captureFromOrigin(archive, candidate) {
  const response = await fetch(candidate.url);
  if (!response.ok) {
    throw new Error(`origin returned HTTP ${response.status}`);
  }
  const content = Buffer.from(await response.arrayBuffer());
  const captured = await captureResponse(
    archive,
    candidate,
    { mimeType: response.headers.get("content-type") ?? "" },
    { body: content.toString("base64"), base64Encoded: true });
  return {
    captured,
    entry: chooseEntry(
      archive.entriesByAddress.get(withoutFragment(candidate.url)),
      candidate.resourceType)
  };
}

async function writeArchiveManifest(archive) {
  archive.manifest.Entries.sort((left, right) => left.Key.localeCompare(right.Key));
  await writeFile(
    path.join(archive.directory, "manifest.json"),
    JSON.stringify(archive.manifest, null, 2) + "\n");
}

async function waitForDevToolsPort(userDataDirectory, child, timeout) {
  const portFile = path.join(userDataDirectory, "DevToolsActivePort");
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) {
    if (child.exitCode !== null) {
      throw new Error(`Chrome exited before exposing DevTools (code ${child.exitCode}).`);
    }
    if (existsSync(portFile)) {
      const [port] = (await readFile(portFile, "utf8")).trim().split(/\r?\n/);
      if (port) return Number(port);
    }
    await delay(20);
  }
  throw new Error("Timed out waiting for Chrome's DevTools port.");
}

async function waitForPageTarget(port, timeout) {
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) {
    const targets = await fetch(`http://127.0.0.1:${port}/json/list`).then(response => response.json());
    const page = targets.find(target => target.type === "page");
    if (page) return page.webSocketDebuggerUrl;
    await delay(20);
  }
  throw new Error("Timed out waiting for Chrome's page target.");
}

async function waitForChart(client, executionContexts, timeout) {
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) {
    for (const contextId of [...executionContexts]) {
      try {
        const evaluation = await client.send("Runtime.evaluate", {
          expression: READY_EXPRESSION,
          contextId,
          returnByValue: true
        });
        if (evaluation.result?.value === true) return;
      } catch {
        executionContexts.delete(contextId);
      }
    }
    await delay(25);
  }
  throw new Error("Chrome chart did not reach the startup readiness gate.");
}

function delay(milliseconds) {
  return new Promise(resolve => setTimeout(resolve, milliseconds));
}

async function main() {
  const options = parseArguments(process.argv.slice(2));
  const archive = await loadArchive(options.archive);
  const chrome = options.chrome ?? defaultChromePath();
  const userDataDirectory = await mkdtemp(path.join(os.tmpdir(), "webscene-chrome-replay-"));
  const child = spawn(chrome, [
    "--headless=new",
    "--remote-debugging-port=0",
    `--user-data-dir=${userDataDirectory}`,
    "--no-first-run",
    "--no-default-browser-check",
    "--disable-background-networking",
    "--disable-component-update",
    "--disable-sync",
    "about:blank"
  ], { stdio: "ignore" });

  let client;
  try {
    const port = await waitForDevToolsPort(userDataDirectory, child, options.timeout);
    client = new CdpClient(await waitForPageTarget(port, options.timeout));
    const executionContexts = new Set();
    const misses = new Set();
    const captureTasks = new Set();
    let capturedRequests = 0;
    let servedRequests = 0;
    let servedBytes = 0;

    client.on("Runtime.executionContextCreated", event => {
      if (event.context.auxData?.isDefault) executionContexts.add(event.context.id);
    });
    client.on("Runtime.executionContextDestroyed", event => {
      executionContexts.delete(event.executionContextId);
    });
    client.on("Runtime.executionContextsCleared", () => executionContexts.clear());
    const handlePausedRequest = async event => {
      try {
        if (event.request.url === new URL("/favicon.ico", options.url).href) {
          await client.send("Fetch.fulfillRequest", {
            requestId: event.requestId,
            responseCode: 204
          });
          return;
        }
        const entries = archive.entriesByAddress.get(withoutFragment(event.request.url));
        const entry = chooseEntry(entries, event.resourceType);
        if (!entry || event.request.method !== "GET") {
          if (options.captureMisses && event.request.method === "GET") {
            const captured = await captureFromOrigin(archive, {
              url: event.request.url,
              resourceType: event.resourceType
            });
            if (captured.captured) capturedRequests++;
            if (!captured.entry) {
              throw new Error("The captured response was not added to the archive.");
            }
            await client.send("Fetch.fulfillRequest", {
              requestId: event.requestId,
              responseCode: 200,
              responseHeaders: [
                { name: "Content-Type", value: contentType(captured.entry, event.resourceType) },
                { name: "Cache-Control", value: "no-store" },
                { name: "Access-Control-Allow-Origin", value: "*" },
                { name: "Cross-Origin-Resource-Policy", value: "cross-origin" }
              ],
              body: captured.entry.body
            });
            return;
          }
          misses.add(`${event.request.method} ${event.resourceType} ${event.request.url}`);
          await client.send("Fetch.failRequest", {
            requestId: event.requestId,
            errorReason: "BlockedByClient"
          });
          return;
        }
        servedRequests++;
        servedBytes += entry.ContentLength;
        await client.send("Fetch.fulfillRequest", {
          requestId: event.requestId,
          responseCode: 200,
          responseHeaders: [
            { name: "Content-Type", value: contentType(entry, event.resourceType) },
            { name: "Cache-Control", value: "no-store" },
            { name: "Access-Control-Allow-Origin", value: "*" },
            { name: "Cross-Origin-Resource-Policy", value: "cross-origin" }
          ],
          body: entry.body
        });
      } catch (error) {
        misses.add(`interception-error ${event.request.url}: ${error.message}`);
        try {
          await client.send("Fetch.failRequest", {
            requestId: event.requestId,
            errorReason: "Failed"
          });
        } catch {}
      }
    };
    client.on("Fetch.requestPaused", event => {
      const task = handlePausedRequest(event)
        .finally(() => captureTasks.delete(task));
      captureTasks.add(task);
    });

    await Promise.all([
      client.send("Page.enable"),
      client.send("Runtime.enable"),
      client.send("Performance.enable"),
      client.send("Fetch.enable", {
        patterns: [{ urlPattern: "http*", requestStage: "Request" }]
      })
    ]);
    const version = await client.send("Browser.getVersion");
    const startedAt = performance.now();
    await client.send("Page.navigate", { url: options.url });
    await waitForChart(client, executionContexts, options.timeout);
    const readyMilliseconds = performance.now() - startedAt;
    await delay(500);
    await Promise.all([...captureTasks]);
    if (options.captureMisses && capturedRequests) {
      await writeArchiveManifest(archive);
    }
    const performanceMetrics = await client.send("Performance.getMetrics");
    const selectedMetrics = Object.fromEntries(
      performanceMetrics.metrics
        .filter(metric => [
          "TaskDuration",
          "ScriptDuration",
          "LayoutDuration",
          "RecalcStyleDuration",
          "LayoutCount",
          "RecalcStyleCount",
          "JSHeapUsedSize"
        ].includes(metric.name))
        .map(metric => [metric.name, metric.value]));

    console.log(JSON.stringify({
      browser: version.product,
      readyMilliseconds,
      servedRequests,
      servedBytes,
      capturedRequests,
      misses: [...misses].sort(),
      metrics: selectedMetrics
    }, null, 2));
    if (misses.size) process.exitCode = 2;
  } finally {
    client?.close();
    child.kill("SIGTERM");
    if (child.exitCode === null) {
      await Promise.race([
        new Promise(resolve => child.once("exit", resolve)),
        delay(2_000)
      ]);
    }
    for (let attempt = 0; attempt < 5; attempt++) {
      try {
        await rm(userDataDirectory, { recursive: true, force: true });
        break;
      } catch (error) {
        if (attempt === 4) throw error;
        await delay(100);
      }
    }
  }
}

main().catch(error => {
  console.error(error.stack ?? error.message);
  process.exitCode = 1;
});
