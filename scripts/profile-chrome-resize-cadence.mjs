#!/usr/bin/env node

import { spawn, spawnSync } from "node:child_process";
import { existsSync } from "node:fs";
import { mkdir, mkdtemp, rm, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import process from "node:process";
import { CdpClient } from "../tests/WebPlatformSubset/chrome/cdp-client.mjs";

const delay = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds));

function parseArguments(values) {
  const options = {
    url: "https://trading-terminal.tradingview-widget.com/",
    output: null,
    seconds: 10,
    warmupSeconds: 5,
    hz: 60,
    width: 1180,
    height: 720,
    widthSpan: 24,
    heightSpan: 30,
    headless: false
  };
  for (let index = 0; index < values.length; index++) {
    const name = values[index];
    if (name === "--headless") {
      options.headless = true;
      continue;
    }
    const value = values[++index];
    if (!value) throw new Error(`Missing value after '${name}'.`);
    if (name === "--url") options.url = value;
    else if (name === "--output") options.output = path.resolve(value);
    else if (name === "--seconds") options.seconds = Number(value);
    else if (name === "--warmup-seconds") options.warmupSeconds = Number(value);
    else if (name === "--hz") options.hz = Number(value);
    else if (name === "--width") options.width = Number(value);
    else if (name === "--height") options.height = Number(value);
    else if (name === "--width-span") options.widthSpan = Number(value);
    else if (name === "--height-span") options.heightSpan = Number(value);
    else throw new Error(`Unknown argument '${name}'.`);
  }
  if (!options.output) throw new Error("Pass --output <new JSON file>.");
  for (const name of ["seconds", "hz", "width", "height", "widthSpan", "heightSpan"]) {
    if (!Number.isFinite(options[name]) || options[name] <= 0) {
      throw new Error(`--${name.replace(/[A-Z]/g, match => `-${match.toLowerCase()}`)} must be positive.`);
    }
  }
  if (!Number.isFinite(options.warmupSeconds) || options.warmupSeconds < 0) {
    throw new Error("--warmup-seconds must be non-negative.");
  }
  if (existsSync(options.output)) {
    throw new Error(`Output '${options.output}' already exists; evidence is never overwritten.`);
  }
  return options;
}

function chromeIdentity() {
  const executable = [
    process.env.CHROME_BIN,
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    "/Applications/Google Chrome Canary.app/Contents/MacOS/Google Chrome Canary",
    "/usr/bin/google-chrome",
    "/usr/bin/chromium"
  ].filter(Boolean).find(existsSync);
  if (!executable) throw new Error("Chrome was not found; set CHROME_BIN.");
  const version = spawnSync(executable, ["--version"], { encoding: "utf8" });
  if (version.status !== 0) throw new Error(version.stderr || "Could not read Chrome version.");
  return { executable, version: version.stdout.trim() };
}

async function launchChrome(identity, headless) {
  const userDataDirectory = await mkdtemp(path.join(os.tmpdir(), "webscene-resize-chrome-"));
  const arguments_ = [
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
  ];
  if (headless) arguments_.unshift("--headless=new");
  const child = spawn(identity.executable, arguments_, {
    stdio: ["ignore", "ignore", "pipe"]
  });
  const endpoint = await new Promise((resolve, reject) => {
    let stderr = "";
    const timeout = setTimeout(
      () => reject(new Error("Timed out waiting for Chrome DevTools.")),
      20_000);
    child.stderr.setEncoding("utf8");
    child.stderr.on("data", chunk => {
      stderr += chunk;
      const match = stderr.match(/DevTools listening on (ws:\/\/[^\s]+)/);
      if (!match) return;
      clearTimeout(timeout);
      resolve(match[1]);
    });
    child.once("exit", code => {
      clearTimeout(timeout);
      reject(new Error(`Chrome exited before DevTools was ready (${code}).\n${stderr}`));
    });
  });
  const endpointUrl = new URL(endpoint);
  const origin = `http://${endpointUrl.hostname}:${endpointUrl.port}`;
  let target = null;
  for (let attempt = 0; attempt < 100 && !target; attempt++) {
    try {
      const targets = await (await fetch(`${origin}/json/list`)).json();
      target = targets.find(item => item.type === "page" && item.webSocketDebuggerUrl);
    } catch { /* Target publication can race the endpoint. */ }
    if (!target) await delay(50);
  }
  if (!target) throw new Error("Chrome exposed no debuggable page target.");
  const client = await CdpClient.connect(target.webSocketDebuggerUrl);
  await client.send("Page.enable");
  await client.send("Runtime.enable");
  await client.send("Performance.enable");
  return { child, client, targetId: target.id, userDataDirectory };
}

async function closeChrome(chrome) {
  try { await chrome.client.send("Browser.close", {}, 2_000); }
  catch { chrome.child.kill("SIGTERM"); }
  chrome.client.close();
  await Promise.race([
    new Promise(resolve => chrome.child.once("exit", resolve)),
    delay(2_000).then(() => chrome.child.kill("SIGKILL"))
  ]);
  await rm(chrome.userDataDirectory, { recursive: true, force: true });
}

async function evaluate(client, expression) {
  const response = await client.send("Runtime.evaluate", {
    expression,
    returnByValue: true,
    awaitPromise: true
  });
  if (response.exceptionDetails) {
    throw new Error(response.exceptionDetails.exception?.description
      || response.exceptionDetails.text
      || "Chrome evaluation failed.");
  }
  return response.result.value;
}

async function waitForDocument(client, timeoutMilliseconds) {
  const deadline = Date.now() + timeoutMilliseconds;
  while (Date.now() < deadline) {
    try {
      const state = await evaluate(client, `({
        ready: document.readyState,
        canvases: document.querySelectorAll('canvas').length,
        frames: document.querySelectorAll('iframe').length
      })`);
      if (state?.ready === "complete") return state;
    } catch { /* Navigation replaces the execution context. */ }
    await delay(100);
  }
  throw new Error("Chrome document did not finish loading.");
}

function metricsMap(response) {
  return Object.fromEntries(response.metrics.map(metric => [metric.name, metric.value]));
}

function metricDelta(before, after, name) {
  return (after[name] ?? 0) - (before[name] ?? 0);
}

function summary(values) {
  if (!values.length) return { count: 0, average: 0, p50: 0, p95: 0, maximum: 0 };
  const ordered = [...values].sort((left, right) => left - right);
  const percentile = proportion => ordered[Math.min(
    ordered.length - 1,
    Math.max(0, Math.ceil(ordered.length * proportion) - 1))];
  return {
    count: ordered.length,
    average: ordered.reduce((total, value) => total + value, 0) / ordered.length,
    p50: percentile(0.5),
    p95: percentile(0.95),
    maximum: ordered.at(-1)
  };
}

function traceSummary(events) {
  const complete = events.filter(event => event.ph === "X" && Number.isFinite(event.dur));
  const totals = new Map();
  for (const event of complete) {
    const value = totals.get(event.name) ?? { count: 0, microseconds: 0 };
    value.count++;
    value.microseconds += event.dur;
    totals.set(event.name, value);
  }
  const selectedNames = [
    "RunTask", "FunctionCall", "EventDispatch", "UpdateLayoutTree", "Layout",
    "PrePaint", "Paint", "Commit", "CompositeLayers", "DrawFrame", "GPUTask"
  ];
  const selected = Object.fromEntries(selectedNames.map(name => {
    const value = totals.get(name) ?? { count: 0, microseconds: 0 };
    return [name, { count: value.count, totalMilliseconds: value.microseconds / 1000 }];
  }));
  const top = [...totals.entries()]
    .sort((left, right) => right[1].microseconds - left[1].microseconds)
    .slice(0, 20)
    .map(([name, value]) => ({
      name,
      count: value.count,
      totalMilliseconds: value.microseconds / 1000
    }));
  const counts = new Map();
  for (const event of events) counts.set(event.name, (counts.get(event.name) ?? 0) + 1);
  const presentation = [...counts.entries()]
    .filter(([name]) => /(draw|swap|present|display.*frame|submit.*frame)/i.test(name))
    .sort((left, right) => right[1] - left[1])
    .slice(0, 40)
    .map(([name, count]) => ({
      name,
      count,
      totalMilliseconds: (totals.get(name)?.microseconds ?? 0) / 1000
    }));
  return { selected, top, presentation };
}

async function driveResizeCadence(client, windowId, options) {
  const frames = Math.round(options.seconds * options.hz);
  const started = performance.now();
  for (let index = 0; index < frames; index++) {
    const widthOffset = index % (options.widthSpan * 2);
    const heightOffset = index % (options.heightSpan * 2);
    const width = Math.round(options.width + (widthOffset < options.widthSpan
      ? widthOffset : options.widthSpan * 2 - widthOffset));
    const height = Math.round(options.height + (heightOffset < options.heightSpan
      ? heightOffset : options.heightSpan * 2 - heightOffset));
    await client.send("Browser.setWindowBounds", {
      windowId,
      bounds: { width, height }
    });
    const deadline = started + (index + 1) * 1000 / options.hz;
    const remaining = deadline - performance.now();
    if (remaining > 1) await delay(remaining);
  }
  return { submitted: frames, elapsedMilliseconds: performance.now() - started };
}

const options = parseArguments(process.argv.slice(2));
const identity = chromeIdentity();
const chrome = await launchChrome(identity, options.headless);
const traceEvents = [];
let traceComplete;
const traceCompleted = new Promise(resolve => { traceComplete = resolve; });
chrome.client.on("Tracing.dataCollected", event => traceEvents.push(...event.value));
chrome.client.on("Tracing.tracingComplete", () => traceComplete());

try {
  const { windowId } = await chrome.client.send("Browser.getWindowForTarget", {
    targetId: chrome.targetId
  });
  await chrome.client.send("Browser.setWindowBounds", {
    windowId,
    bounds: { windowState: "normal" }
  });
  await chrome.client.send("Browser.setWindowBounds", {
    windowId,
    bounds: { width: Math.round(options.width), height: Math.round(options.height) }
  });
  await chrome.client.send("Page.navigate", { url: options.url }, 60_000);
  const loadedState = await waitForDocument(chrome.client, 30_000);
  await delay(options.warmupSeconds * 1000);

  await evaluate(chrome.client, `(() => {
    const state = {
      running: true,
      started: performance.now(),
      animationFrames: [],
      resizeEvents: [],
      resizeToAnimationFrame: [],
      longTasks: []
    };
    const onResize = () => {
      const observed = performance.now();
      state.resizeEvents.push({ timestamp: observed, width: innerWidth, height: innerHeight });
      requestAnimationFrame(() => state.resizeToAnimationFrame.push(performance.now() - observed));
    };
    addEventListener('resize', onResize);
    const observer = typeof PerformanceObserver === 'function'
      ? new PerformanceObserver(list => {
          for (const entry of list.getEntries()) {
            state.longTasks.push({ startTime: entry.startTime, duration: entry.duration });
          }
        })
      : null;
    try { observer?.observe({ type: 'longtask', buffered: false }); } catch {}
    const frame = timestamp => {
      if (!state.running) return;
      state.animationFrames.push(timestamp);
      requestAnimationFrame(frame);
    };
    requestAnimationFrame(frame);
    globalThis.__webSceneChromeResize = state;
    globalThis.__webSceneStopChromeResize = () => {
      state.running = false;
      state.finished = performance.now();
      removeEventListener('resize', onResize);
      observer?.disconnect();
      return state;
    };
  })()`);
  const beforeMetrics = metricsMap(await chrome.client.send("Performance.getMetrics"));
  await chrome.client.send("Tracing.start", {
    categories: [
      "devtools.timeline",
      "disabled-by-default-devtools.timeline",
      "disabled-by-default-devtools.timeline.frame",
      "benchmark",
      "blink",
      "cc",
      "gpu",
      "viz",
      "disabled-by-default-viz",
      "disabled-by-default-gpu.service"
    ].join(","),
    options: "sampling-frequency=10000",
    transferMode: "ReportEvents"
  });
  const cadence = await driveResizeCadence(chrome.client, windowId, options);
  await delay(500);
  const pageState = await evaluate(
    chrome.client,
    "globalThis.__webSceneStopChromeResize()" );
  const afterMetrics = metricsMap(await chrome.client.send("Performance.getMetrics"));
  await chrome.client.send("Tracing.end");
  await Promise.race([
    traceCompleted,
    delay(20_000).then(() => { throw new Error("Chrome trace did not complete."); })
  ]);

  const frameIntervals = pageState.animationFrames
    .slice(1)
    .map((value, index) => value - pageState.animationFrames[index]);
  const longTaskDurations = pageState.longTasks.map(task => task.duration);
  const renderedFramesPerSecond = frameIntervals.length > 0
    ? frameIntervals.length * 1000
      / (pageState.animationFrames.at(-1) - pageState.animationFrames[0])
    : 0;
  const trace = traceSummary(traceEvents);
  const result = {
    schema: "webscene-chrome-resize-cadence-v1",
    engine: "chrome",
    identity: identity.version,
    url: options.url,
    headless: options.headless,
    requestedHz: options.hz,
    warmupSeconds: options.warmupSeconds,
    requestedSeconds: options.seconds,
    submitted: cadence.submitted,
    elapsedMilliseconds: cadence.elapsedMilliseconds,
    loadedState,
    resizeEvents: pageState.resizeEvents.length,
    animationFrames: pageState.animationFrames.length,
    renderedFramesPerSecond,
    animationFrameIntervalMilliseconds: summary(frameIntervals),
    resizeToAnimationFrameMilliseconds: summary(
      pageState.resizeToAnimationFrame.filter(value => value >= 0)),
    longTaskMilliseconds: summary(longTaskDurations),
    performanceMetrics: {
      taskMilliseconds: metricDelta(beforeMetrics, afterMetrics, "TaskDuration") * 1000,
      scriptMilliseconds: metricDelta(beforeMetrics, afterMetrics, "ScriptDuration") * 1000,
      layoutMilliseconds: metricDelta(beforeMetrics, afterMetrics, "LayoutDuration") * 1000,
      styleMilliseconds: metricDelta(beforeMetrics, afterMetrics, "RecalcStyleDuration") * 1000,
      layoutCount: metricDelta(beforeMetrics, afterMetrics, "LayoutCount"),
      styleCount: metricDelta(beforeMetrics, afterMetrics, "RecalcStyleCount")
    },
    trace
  };
  await mkdir(path.dirname(options.output), { recursive: true });
  await writeFile(options.output, JSON.stringify(result, null, 2) + "\n");
  process.stdout.write(JSON.stringify(result, null, 2) + "\n");
} finally {
  await closeChrome(chrome);
}
