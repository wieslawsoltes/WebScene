import test from "node:test";
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { lineProject, referenceCases } from "./reference-workloads.mjs";
import { hardwareAssessment, archiveReferenceHarness, validateReferenceUi } from "./capture-chrome-reference.mjs";
import { analyzePresentation, supportedChromeRevision } from "./presentation-trace.mjs";

test("seeded line input has stable bytes and complete unique entities", () => {
  const project = lineProject(10_000);
  assert.equal(createHash("sha256").update(JSON.stringify(project)).digest("hex"),
    "07ebda76dc757bb365a092f9ed7fb7c7d5c8a6498ceb11760f23f953b05c9099");
  assert.equal(new Set(project.entities.map(entity => entity.id)).size, 10_000);
  assert.notDeepEqual(lineProject(2, 1).entities, lineProject(2, 2).entities);
  assert.equal(lineProject(100_000).entities.length, 100_000);
  assert.throws(() => lineProject(200_001));
  assert.throws(() => lineProject(2, -1));
});

test("matrix includes both DPRs and themes for all four required scenes", () => {
  assert.equal(referenceCases.length, 16);
  assert.equal(new Set(referenceCases.map(value => value.id)).size, 16);
  for (const scene of ["courtyard", "fixture", "lines-10000", "lines-100000"])
    assert.equal(referenceCases.filter(value => value.scene === scene).length, 4);
});

test("Chrome hardware evidence rejects missing, fallback and software records", () => {
  const system = { gpu: { devices: [{ deviceString: "Synthetic discrete GPU" }],
    featureStatus: { webgpu: "enabled", gpu_compositing: "enabled" } } };
  const app = { secureContext: true, backend: "WebGPU", adapter: { isFallbackAdapter: false } };
  assert.equal(hardwareAssessment(system, app).status, "confirmed");
  for (const fallback of [undefined, true, "false", 0])
    assert.equal(hardwareAssessment(system, { ...app, adapter: { isFallbackAdapter: fallback } }).status, "unavailable");
  assert.equal(hardwareAssessment(system, { ...app, backend: "Canvas 2D" }).status, "unavailable");
  assert.equal(hardwareAssessment({}, app).status, "unavailable");
  system.gpu.devices[0].deviceString = "SwiftShader";
  assert.equal(hardwareAssessment(system, app).status, "unavailable");
});

function fixture() {
  // Synthetic trace data tests the parser contract, never hardware qualification.
  const traceEvents = [
    { name: "webscene-reference-pan-start", ph: "I", ts: 1000, pid: 10 },
    { name: "webscene-reference-pan-end", ph: "I", ts: 60000, pid: 10 }
  ];
  const frame = (id, begin, end, state = "STATE_PRESENTED_ALL", pid = 10) => {
    traceEvents.push({ name: "PipelineReporter", ph: "b", ts: begin, pid, id2: { local: id },
      args: { frame_reporter: { state, frame_sequence: begin, has_missing_content: false } } });
    traceEvents.push({ name: "PipelineReporter", ph: "e", ts: end, pid, id2: { local: id } });
  };
  frame("0x1", 2000, 20000);
  frame("0x2", 18000, 36667);
  frame("0x3", 34000, 53334);
  frame("0x4", 35000, 53334); // Two reporters may share one physical presentation.
  frame("0x5", 50000, 55000, "STATE_DROPPED");
  frame("0x6", 50000, 59000, "STATE_PRESENTED_ALL", 20); // Another renderer must not contaminate cadence.
  frame("0x7", 100, 59000); // A pre-interaction reporter is excluded.
  return { traceEvents };
}

test("presentation cadence uses feedback timestamps, not rAF or submission events", () => {
  const result = analyzePresentation(fixture(), supportedChromeRevision);
  assert.equal(result.status, "measured");
  assert.equal(result.uniquePresentedFrames, 3);
  assert.equal(result.intervalMilliseconds.p95, 16.667);
  assert.equal(result.reporterStates.STATE_DROPPED, 1);
  assert.ok(Math.abs(result.framesPerSecond - 60) < 0.01);
});

test("unknown Chrome, missing markers and malformed pairs remain unavailable", () => {
  assert.equal(analyzePresentation(fixture(), "unknown").status, "unavailable");
  const missing = fixture();
  missing.traceEvents.shift();
  assert.equal(analyzePresentation(missing, supportedChromeRevision).status, "unavailable");
  const incomplete = fixture();
  incomplete.traceEvents = incomplete.traceEvents.filter(event => !(event.ph === "e" && event.id2?.local === "0x3"));
  assert.equal(analyzePresentation(incomplete, supportedChromeRevision).status, "unavailable");
});

test("reference archive retains exact harness bytes after the source changes", async t => {
  const { mkdtemp, mkdir, writeFile, readFile, rm } = await import("node:fs/promises");
  const { tmpdir } = await import("node:os");
  const path = await import("node:path");
  const root = await mkdtemp(path.join(tmpdir(), "webscene-harness-archive-"));
  t.after(() => rm(root, { recursive: true, force: true }));
  const source = path.join(root, "source"), output = path.join(root, "capture");
  await mkdir(source);
  const names = ["capture-chrome-reference.mjs", "chrome-session.mjs", "reference-workloads.mjs", "presentation-trace.mjs",
    "prepare-kestrel.py", "../WebPlatformSubset/chrome/cdp-client.mjs"];
  for (const name of names) {
    await mkdir(path.dirname(path.join(source, name)), { recursive: true });
    await writeFile(path.join(source, name), `// original ${name}\r\n`);
  }
  const archive = await archiveReferenceHarness(output, source);
  for (const name of names) await writeFile(path.join(source, name), "// changed after capture");
  for (const name of names) {
    const stored = await readFile(path.join(output, archive.files[name].file));
    assert.equal(stored.toString(), `// original ${name}\r\n`);
    assert.equal(createHash("sha256").update(stored).digest("hex"), archive.hashes[name]);
    assert.equal(archive.files[name].sha256, archive.hashes[name]);
    assert.equal(archive.files[name].bytes, stored.length);
  }
});


test("reference capture rejects active commands and visible transient UI", () => {
  const neutral = { tool: null, bannerHidden: true, suggestionsHidden: true, fileMenuHidden: true };
  assert.doesNotThrow(() => validateReferenceUi(neutral));
  assert.throws(() => validateReferenceUi({ ...neutral, tool: "erase" }), /not neutral/);
  for (const key of ["bannerHidden", "suggestionsHidden", "fileMenuHidden"])
    assert.throws(() => validateReferenceUi({ ...neutral, [key]: false }), /not neutral/);
  assert.throws(() => validateReferenceUi({}), /not neutral/);
});
