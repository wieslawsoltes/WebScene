#!/usr/bin/env node
import { spawn } from "node:child_process";
import { mkdtemp, rm, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";

const values = process.argv.slice(2);
const option = name => {
  const index = values.indexOf(name);
  if (index < 0 || index + 1 >= values.length) throw new Error(`Missing ${name}.`);
  return values[index + 1];
};
const executable = option("--chrome");
const documentUrl = new URL(`file://${path.resolve(option("--document"))}`).href;
const screenshotPath = path.resolve(option("--output"));
const metricsPath = path.resolve(option("--metrics"));
const scale = Number(option("--scale"));
const width = Number(option("--width"));
const height = Number(option("--height"));
const delay = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds));

class Client {
  constructor(socket) {
    this.socket = socket;
    this.nextId = 0;
    this.pending = new Map();
    socket.addEventListener("message", event => {
      const message = JSON.parse(event.data);
      if (!message.id) return;
      const continuation = this.pending.get(message.id);
      if (!continuation) return;
      this.pending.delete(message.id);
      if (message.error) continuation.reject(new Error(JSON.stringify(message.error)));
      else continuation.resolve(message.result);
    });
  }

  static async connect(url) {
    const socket = new WebSocket(url);
    await new Promise((resolve, reject) => {
      socket.addEventListener("open", resolve, { once: true });
      socket.addEventListener("error", reject, { once: true });
    });
    return new Client(socket);
  }

  send(method, params = {}) {
    const id = ++this.nextId;
    this.socket.send(JSON.stringify({ id, method, params }));
    return new Promise((resolve, reject) => this.pending.set(id, { resolve, reject }));
  }

  close() { this.socket.close(); }
}

const profile = await mkdtemp(path.join(os.tmpdir(), "webscene-glyph-chrome-"));
const child = spawn(executable, [
  "--headless=new",
  "--disable-background-networking",
  "--disable-default-apps",
  "--disable-extensions",
  "--disable-sync",
  "--hide-scrollbars",
  "--no-first-run",
  "--no-default-browser-check",
  "--allow-file-access-from-files",
  `--force-device-scale-factor=${scale}`,
  `--window-size=${width},${height}`,
  "--remote-debugging-port=0",
  `--user-data-dir=${profile}`,
  "about:blank"
], { stdio: ["ignore", "ignore", "pipe"] });

let client;
try {
  const endpoint = await new Promise((resolve, reject) => {
    let stderr = "";
    const timeout = setTimeout(() => reject(new Error("Timed out waiting for Chrome DevTools.")), 20_000);
    child.stderr.setEncoding("utf8");
    child.stderr.on("data", chunk => {
      stderr += chunk;
      const match = stderr.match(/DevTools listening on (ws:\/\/[^\s]+)/);
      if (!match) return;
      clearTimeout(timeout);
      resolve(match[1]);
    });
  });
  const endpointUrl = new URL(endpoint);
  const targets = await (await fetch(
    `http://${endpointUrl.hostname}:${endpointUrl.port}/json/list`)).json();
  const target = targets.find(item => item.type === "page" && item.webSocketDebuggerUrl);
  if (!target) throw new Error("Chrome exposed no debuggable page.");
  client = await Client.connect(target.webSocketDebuggerUrl);
  await client.send("Page.enable");
  await client.send("Runtime.enable");
  await client.send("Emulation.setDeviceMetricsOverride", {
    width,
    height,
    deviceScaleFactor: scale,
    mobile: false
  });
  await client.send("Page.navigate", { url: documentUrl });

  let metrics;
  for (let attempt = 0; attempt < 200; attempt++) {
    const response = await client.send("Runtime.evaluate", {
      expression: "globalThis.__glyphDiagnosticMetrics || null",
      returnByValue: true
    });
    metrics = response.result.value;
    if (metrics) break;
    await delay(25);
  }
  if (!metrics) throw new Error("Glyph diagnostic page did not become ready.");
  const capture = await client.send("Page.captureScreenshot", {
    format: "png",
    fromSurface: true,
    captureBeyondViewport: false
  });
  await writeFile(screenshotPath, Buffer.from(capture.data, "base64"));
  await writeFile(metricsPath, JSON.stringify(metrics, null, 2));
} finally {
  if (client) {
    try { await client.send("Browser.close"); }
    catch { child.kill("SIGTERM"); }
    client.close();
  } else {
    child.kill("SIGTERM");
  }
  await Promise.race([
    new Promise(resolve => child.once("exit", resolve)),
    delay(2_000).then(() => child.kill("SIGKILL"))
  ]);
  await rm(profile, { recursive: true, force: true });
}
