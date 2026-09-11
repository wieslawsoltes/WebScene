#!/usr/bin/env node
import { createHash } from "node:crypto";
import { spawnSync } from "node:child_process";
import { createServer } from "node:http";
import { existsSync } from "node:fs";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { gzipSync } from "node:zlib";
import { startChrome, stopChrome, evaluate, waitFor, delay } from "./chrome-session.mjs";
import { lineProject, referenceCases } from "./reference-workloads.mjs";
import { analyzePresentation, distribution } from "./presentation-trace.mjs";

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, "../..");
const sha = bytes => createHash("sha256").update(bytes).digest("hex");

export function hardwareAssessment(system, app) {
  const descriptions = [system.gpu?.auxAttributes?.glRenderer, app.adapter?.description,
    ...(system.gpu?.devices ?? []).map(device => device.deviceString)].join(" ");
  const confirmed = app.secureContext === true && app.backend === "WebGPU"
    && app.adapter?.isFallbackAdapter === false && system.gpu?.featureStatus?.webgpu === "enabled"
    && system.gpu?.featureStatus?.gpu_compositing === "enabled"
    && system.gpu?.devices?.length > 0 && !/swiftshader|llvmpipe|softpipe|software|warp/i.test(descriptions);
  return { status: confirmed ? "confirmed" : "unavailable", hardwareAccelerated: confirmed,
    reason: confirmed ? "Non-fallback WebGPU adapter and hardware browser GPU features confirmed"
      : "Required hardware/non-fallback evidence is missing or reports software" };
}

function options(argv) {
  const result = { chrome: process.env.CHROME_BIN, repeat: 2, frames: 180 };
  for (let index = 0; index < argv.length; index += 2) {
    const key = argv[index], value = argv[index + 1];
    if (!value) throw new Error(`Missing value for ${key}`);
    if (key === "--chrome") result.chrome = value;
    else if (key === "--output") result.output = path.resolve(value);
    else if (key === "--case") result.case = value;
    else if (key === "--repeat") result.repeat = Number(value);
    else if (key === "--frames") result.frames = Number(value);
    else throw new Error(`Unknown option ${key}`);
  }
  if (!result.output) throw new Error("--output must name a new evidence directory");
  if (!Number.isInteger(result.repeat) || result.repeat < 1 || result.repeat > 10) throw new Error("Invalid repeat count");
  if (!Number.isInteger(result.frames) || result.frames < 10 || result.frames > 1800) throw new Error("Invalid frame count");
  if (result.case && !referenceCases.some(test => test.id === result.case)) throw new Error("Unknown reference case");
  result.chrome ??= ["/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    "/Volumes/SSD/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    "/usr/bin/google-chrome", "/usr/bin/chromium",
    path.join(process.env.PROGRAMFILES ?? "", "Google/Chrome/Application/chrome.exe")].find(existsSync);
  if (!result.chrome || !existsSync(result.chrome)) throw new Error("Set --chrome or CHROME_BIN to a hardware-capable Chrome installation");
  return result;
}

async function fixtureServer(directory, generated) {
  const types = { ".html": "text/html", ".js": "text/javascript", ".css": "text/css",
    ".json": "application/json", ".kcad": "application/json", ".png": "image/png" };
  const server = createServer(async (request, response) => {
    try {
      const pathname = decodeURIComponent(new URL(request.url, "http://localhost").pathname);
      if (request.method !== "GET") { response.writeHead(405).end(); return; }
      let content = generated.get(pathname);
      if (!content) {
        const filename = path.resolve(directory, "." + (pathname === "/" ? "/index.html" : pathname));
        if (!filename.startsWith(directory + path.sep)) { response.writeHead(403).end(); return; }
        content = await readFile(filename);
      }
      response.writeHead(200, { "Content-Type": types[path.extname(pathname)] ?? "application/octet-stream",
        "Cache-Control": "no-store" });
      response.end(content);
    } catch { response.writeHead(404).end(); }
  });
  await new Promise((resolve, reject) => { server.once("error", reject); server.listen(0, "127.0.0.1", resolve); });
  return { server, url: `http://127.0.0.1:${server.address().port}` };
}

async function settled(page) {
  await evaluate(page, `(async()=>{
    await new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
    if(kestrel.renderer.device) await kestrel.renderer.device.queue.onSubmittedWorkDone();
    return true;
  })()`);
}

async function settleUi(page) {
  // A neutral title click blurs the command input through the app's normal handlers.
  // Merely moving the pointer leaves its startup suggestion menu open on some navigations.
  const point = await evaluate(page, "(()=>{const r=document.getElementById('title-name').getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2};})()");
  for (const event of [{ type: "mouseMoved", ...point },
    { type: "mousePressed", ...point, button: "left", buttons: 1, clickCount: 1 },
    { type: "mouseReleased", ...point, button: "left", buttons: 0, clickCount: 1 }])
    await page.send("Input.dispatchMouseEvent", event);
  await waitFor(page, "document.getElementById('command-suggestions').hidden && document.getElementById('file-menu').hidden");
  await delay(200); // Allow the app's 150 ms blur handler and hover transitions to finish.
  await settled(page);
}

export function validateReferenceUi(state) {
  if (!state || state.tool !== null || state.bannerHidden !== true
      || state.suggestionsHidden !== true || state.fileMenuHidden !== true) {
    throw new Error(`Reference UI is not neutral: ${JSON.stringify(state)}`);
  }
}

async function referenceUiState(page) {
  const state = await evaluate(page, `({tool:kestrel.tool?.id ?? null,
    bannerHidden:document.getElementById('tool-banner').hidden,
    suggestionsHidden:document.getElementById('command-suggestions').hidden,
    fileMenuHidden:document.getElementById('file-menu').hidden})`);
  validateReferenceUi(state);
  return state;
}

async function snapshot(page, output, name, clip) {
  const uiBefore = await referenceUiState(page);
  const capture = await page.send("Page.captureScreenshot", { format: "png", fromSurface: true,
    captureBeyondViewport: false, clip: { ...clip, scale: 1 } });
  const bytes = Buffer.from(capture.data, "base64");
  await writeFile(path.join(output, name), bytes);
  const canvasData = await evaluate(page, "({gpu:kestrel.renderer.canvas.toDataURL('image/png'),overlay:kestrel.renderer.overlay.toDataURL('image/png')})");
  const layers = {};
  for (const [layer, dataUrl] of Object.entries(canvasData)) {
    if (!dataUrl.startsWith("data:image/png;base64,")) throw new Error("Canvas PNG serialization failed");
    const layerBytes = Buffer.from(dataUrl.slice("data:image/png;base64,".length), "base64");
    const layerName = name.replace(/\.png$/, `-${layer}.png`);
    await writeFile(path.join(output, layerName), layerBytes);
    layers[layer] = { file: layerName, sha256: sha(layerBytes), width: layerBytes.readUInt32BE(16), height: layerBytes.readUInt32BE(20) };
  }
  const uiAfter = await referenceUiState(page);
  return { uiBefore, uiAfter, file: name, sha256: sha(bytes), width: bytes.readUInt32BE(16), height: bytes.readUInt32BE(20),
    layers, purpose: "Diagnostic reference capture outside the timed interaction; not a presentation path" };
}

async function beginTrace(browser) {
  let timer;
  const completion = new Promise((resolve, reject) => {
    timer = setTimeout(() => reject(new Error("Trace completion timed out")), 120_000);
    browser.on("Tracing.tracingComplete", event => { clearTimeout(timer); resolve(event); });
  });
  // Avoid an unhandled rejection if an earlier page command fails; callers still observe rejection.
  completion.catch(() => {});
  await browser.send("Tracing.start", { transferMode: "ReturnAsStream", streamFormat: "json",
    traceConfig: { recordMode: "recordAsMuchAsPossible", includedCategories: ["benchmark", "cc", "gpu", "viz",
      "blink.user_timing", "devtools.timeline", "disabled-by-default-devtools.timeline.frame"] } });
  return { completion, cancel: () => clearTimeout(timer) };
}

async function endTrace(browser, tracing, output, filename, revision) {
  await browser.send("Tracing.end");
  const result = await tracing.completion;
  if (result.dataLossOccurred) throw new Error("Chrome lost trace events");
  if (!result.stream) throw new Error("Chrome returned no trace stream");
  const chunks = [];
  try {
    while (true) {
      const chunk = await browser.send("IO.read", { handle: result.stream, size: 1024 * 1024 });
      chunks.push(Buffer.from(chunk.data, chunk.base64Encoded ? "base64" : "utf8"));
      if (chunk.eof) break;
    }
  } finally { await browser.send("IO.close", { handle: result.stream }); }
  const raw = Buffer.concat(chunks);
  const presentationTiming = analyzePresentation(JSON.parse(raw.toString("utf8")), revision);
  const bytes = gzipSync(raw);
  await writeFile(path.join(output, filename), bytes);
  return { file: filename, sha256: sha(bytes), dataLossOccurred: false,
    presentationTiming };
}

async function captureCase(chrome, serverUrl, output, test, repetition) {
  const page = chrome.page, errors = [];
  page.on("Runtime.exceptionThrown", error => errors.push(error));
  await page.send("Emulation.setDeviceMetricsOverride", { ...test.documentViewport, deviceScaleFactor: test.dpr, mobile: false });
  await page.send("Page.navigate", { url: `${serverUrl}/index.html?reference=${test.id}&repeat=${repetition}` });
  await page.send("Page.bringToFront");
  await waitFor(page, "document.documentElement?.dataset.ready==='true' && !!window.kestrel?.backendReady");
  const projectPath = test.scene.startsWith("lines-") ? `/reference/${test.scene}.kcad` : `/examples/${test.scene}.kcad`;
  await evaluate(page, `(async()=>{
    const response=await fetch(${JSON.stringify(projectPath)});
    if(!response.ok) throw new Error('Project fetch failed');
    await kestrel.openFile(new File([await response.text()],${JSON.stringify(test.scene + ".kcad")},{type:'application/json'}));
    kestrel.theme=${JSON.stringify(test.theme)}; kestrel.applyTheme();
    kestrel.setView(${JSON.stringify(test.view)}); kestrel.setStyle(${JSON.stringify(test.style)});
    kestrel.settings.grid=true; kestrel.settings.lineweights=false; kestrel.fit(false);
    return true;
  })()`);
  // Let the app's own transient UI expire; do not patch/remove its DOM for stable screenshots.
  await waitFor(page, "document.querySelectorAll('#toast-stack .toast').length===0");
  await settleUi(page);
  const app = await evaluate(page, `(()=>{
    const r=kestrel.renderer, info=r.adapter?.info, rect=document.getElementById('viewport').getBoundingClientRect();
    return {backend:r.backend,secureContext:isSecureContext,fallbackReason:r.fallbackReason,
      adapter:info?{vendor:info.vendor,architecture:info.architecture,device:info.device,description:info.description,isFallbackAdapter:info.isFallbackAdapter}:null,
      clip:{x:rect.x,y:rect.y,width:rect.width,height:rect.height},
      documentViewport:{width:innerWidth,height:innerHeight,dpr:devicePixelRatio},
      canvas:{width:r.canvas.width,height:r.canvas.height},stats:{...r.stats},
      entities:kestrel.doc.entities.length,camera:kestrel.camera.serialize(),visibility:document.visibilityState,focused:document.hasFocus()};
  })()`);
  const system = await chrome.browser.send("SystemInfo.getInfo");
  const hardware = hardwareAssessment(system, app);
  if (!hardware.hardwareAccelerated) return { test, repetition, status: "unavailable", hardware, app, system };
  if (app.documentViewport.width !== test.documentViewport.width || app.documentViewport.height !== test.documentViewport.height
      || app.documentViewport.dpr !== test.dpr || app.visibility !== "visible") throw new Error("Viewport/DPR/visibility mismatch");
  const prefix = `${test.id}-run${repetition}`;
  const before = await snapshot(page, output, `${prefix}-before.png`, app.clip);
  const tracing = await beginTrace(chrome.browser);
  let trace, samples, input;
  try {
    // Exercise real browser mouse input once, then restore the camera for deterministic timed pans.
    const x = app.clip.x + app.clip.width / 2, y = app.clip.y + app.clip.height / 2;
    const events = [{ type: "mouseMoved", x, y }, { type: "mousePressed", x, y, button: "middle", buttons: 4, clickCount: 1 },
      { type: "mouseMoved", x: x + 40, y: y + 20, button: "middle", buttons: 4 },
      { type: "mouseReleased", x: x + 40, y: y + 20, button: "middle", buttons: 0, clickCount: 1 }];
    for (const event of events) await page.send("Input.dispatchMouseEvent", event);
    await settled(page);
    input = await evaluate(page, "({target:kestrel.camera.target.slice(),camera:kestrel.camera.serialize()})");
    input.events = events;
    input.changedCamera = JSON.stringify(input.target) !== JSON.stringify(app.camera.target);
    if (!input.changedCamera) throw new Error("Middle-button input did not pan the Kestrel camera");
    await settleUi(page);
    await evaluate(page, `kestrel.camera.restore(${JSON.stringify(app.camera)});kestrel.invalidate();true`);
    await settled(page);
    samples = await evaluate(page, `new Promise(resolve=>{
      const app=kestrel, config=${JSON.stringify(test.interaction)}, samples=[];
      const buffers=Object.fromEntries(Object.entries(app.renderer.buffers).map(([key,value])=>[key,value.buffer]));
      let previous=null, moved=0;
      performance.mark('webscene-reference-pan-start');
      const step=timestamp=>{
        if(previous!==null) samples.push({rafIntervalMs:timestamp-previous,cpuRenderSubmissionMs:app.renderer.stats.cpuMs});
        if(moved===config.frames) {
          performance.mark('webscene-reference-pan-end');
          resolve({samples,finalCamera:app.camera.serialize(),stats:{...app.renderer.stats},
            retainedBuffers:Object.fromEntries(Object.entries(buffers).map(([key,value])=>[key,app.renderer.buffers[key]?.buffer===value])),gpuErrors:app.renderer.gpuErrors.slice()});
          return;
        }
        app.camera.pan(config.dxCssPixels,config.dyCssPixels);app.invalidate();moved++;previous=timestamp;
        requestAnimationFrame(step);
      }; requestAnimationFrame(step);
    })`);
    await settled(page);
    await delay(100); // Allow platform presentation feedback for the last submitted update to arrive.
    trace = await endTrace(chrome.browser, tracing, output, `${prefix}-trace.json.gz`, chrome.revision);
  } finally { tracing.cancel(); }
  await settled(page);
  const after = await snapshot(page, output, `${prefix}-after.png`, app.clip);
  if (samples.gpuErrors.length || errors.length) throw new Error("Kestrel reported browser/GPU errors");
  samples.cpuRenderSubmissionMilliseconds = distribution(samples.samples.map(sample => sample.cpuRenderSubmissionMs));
  samples.rafIntervalMilliseconds = distribution(samples.samples.map(sample => sample.rafIntervalMs));
  return { test, repetition, status: "captured", hardware, app, system, input, before, after, trace, samples,
    timingScope: "Application CPU render/submission plus rAF intervals; GPU execution and presentation require separate trace analysis" };
}

export async function main(argv) {
  const args = options(argv);
  await mkdir(path.dirname(args.output), { recursive: true });
  await mkdir(args.output); // Never overwrite evidence, including failed attempts.
  const evidence = { schemaVersion: 1, status: "running", capturedAt: new Date().toISOString(),
    scope: "Hardware Chrome reference for G01; not WebScene support or full epic qualification",
    host: { platform: os.platform(), release: os.release(), architecture: os.arch() }, results: [] };
  let chrome, service;
  try {
    const fixtureRoot = path.join(args.output, "fixture-input");
    const validation = spawnSync(process.env.PYTHON ?? (process.platform === "win32" ? "python" : "python3"),
      [path.join(here, "prepare-kestrel.py"), "--destination", fixtureRoot], { encoding: "utf8" });
    if (validation.status !== 0) throw new Error(validation.stderr || validation.stdout);
    evidence.fixture = JSON.parse(await readFile(path.join(here, "fixtures/kestrel.json"), "utf8"));
    evidence.repositoryCommit = spawnSync("git", ["rev-parse", "HEAD"], { cwd: root, encoding: "utf8" }).stdout.trim();
    const harnessArchive = await archiveReferenceHarness(args.output);
    evidence.harness = harnessArchive.hashes;
    evidence.harnessFiles = harnessArchive.files;
    await mkdir(path.join(args.output, "generated-inputs"), { recursive: true });
    const generated = new Map();
    evidence.generatedProjects = {};
    for (const count of [10_000, 100_000]) {
      const bytes = Buffer.from(JSON.stringify(lineProject(count)));
      generated.set(`/reference/lines-${count}.kcad`, bytes);
      const file = `generated-inputs/lines-${count}.kcad`;
      await writeFile(path.join(args.output, file), bytes);
      evidence.generatedProjects[`lines-${count}`] = { file, sha256: sha(bytes), bytes: bytes.length, count };
    }
    service = await fixtureServer(path.join(fixtureRoot, "Kestrel-CAD"), generated);
    chrome = await startChrome(args.chrome);
    evidence.browser = await chrome.browser.send("Browser.getVersion");
    chrome.revision = evidence.browser.revision;
    evidence.launch = { executable: args.chrome, args: chrome.args, headless: false };
    await chrome.page.send("Page.addScriptToEvaluateOnNewDocument", { source: "try { localStorage.clear(); } catch {}" });
    for (const test of referenceCases.filter(test => !args.case || test.id === args.case)) {
      for (let repetition = 1; repetition <= args.repeat; ++repetition) {
        const configured = { ...test, interaction: { ...test.interaction, frames: args.frames } };
        const result = await captureCase(chrome, service.url, args.output, configured, repetition);
        evidence.results.push(result);
        await writeFile(path.join(args.output, "reference.json"), JSON.stringify(evidence, null, 2) + "\n");
        console.log(`${test.id} run ${repetition}: ${result.status}`);
        if (result.status === "unavailable") break;
      }
    }
    evidence.status = evidence.results.every(result => result.status === "captured") ? "captured" : "unavailable";
    evidence.repeatability = referenceCases.filter(test => !args.case || test.id === args.case).map(test => {
      const runs = evidence.results.filter(result => result.test.id === test.id && result.status === "captured");
      return { case: test.id, runs: runs.length, beforeExactPngMatch: runs.length >= 2 && new Set(runs.map(run => run.before.sha256)).size === 1,
        afterExactPngMatch: runs.length >= 2 && new Set(runs.map(run => run.after.sha256)).size === 1,
        layerMatches: Object.fromEntries(["before", "after"].flatMap(phase => ["gpu", "overlay"].map(layer =>
          [`${phase}-${layer}`, runs.length >= 2 && new Set(runs.map(run => run[phase].layers[layer].sha256)).size === 1]))) };
    });
    evidence.remainingVerification = ["Presentation timing is unavailable for unverified Chrome revisions or incomplete traces",
      "Repeat pixel differences require inspection if hashes differ",
      "Other target hardware and WebScene comparison remain separate gates"];
  } catch (error) {
    evidence.status = "failed"; evidence.error = error.stack ?? String(error);
    console.error(evidence.error);
  } finally {
    if (chrome) { evidence.chromeStderr = chrome.stderr; await stopChrome(chrome); }
    if (service) await new Promise(resolve => service.server.close(resolve));
    await writeFile(path.join(args.output, "reference.json"), JSON.stringify(evidence, null, 2) + "\n");
  }
  return evidence.status === "captured" ? 0 : evidence.status === "unavailable" ? 77 : 1;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main(process.argv.slice(2)).then(code => { process.exitCode = code; }).catch(error => { console.error(error); process.exitCode = 1; });
}

// Persist the exact source bytes used to identify a reference capture, including
// uncommitted harness edits. A repository SHA alone cannot recover those bytes.
export async function archiveReferenceHarness(output, sourceDirectory = here) {
  const hashes = {}, files = {};
  await mkdir(path.join(output, "harness"), { recursive: true });
  for (const name of ["capture-chrome-reference.mjs", "chrome-session.mjs", "reference-workloads.mjs", "presentation-trace.mjs",
    "prepare-kestrel.py", "../WebPlatformSubset/chrome/cdp-client.mjs"]) {
    const bytes = await readFile(path.join(sourceDirectory, name));
    const file = path.posix.join("harness/tests/GraphicsCompatibility", name);
    await mkdir(path.dirname(path.join(output, file)), { recursive: true });
    await writeFile(path.join(output, file), bytes);
    hashes[name] = sha(bytes);
    files[name] = { file, sha256: hashes[name], bytes: bytes.length };
  }
  return { hashes, files };
}
