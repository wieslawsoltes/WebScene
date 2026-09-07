# Partial G01: hardware Chrome reference matrix

Captured on 2026-09-07 with Chrome 152.0.7977.77, revision `529d9a34b491745086b59458f58a5aae8292adaa`, Apple M4, Metal, hardware WebGPU and GPU composition enabled. All 32 runs confirmed a non-fallback WebGPU adapter. Kestrel source files came from the byte-verified original archive and were not modified.

The courtyard, supplied mechanical fixture, seeded 10,000-line and 100,000-line inputs were each captured twice in dark/light themes at DPR 1/2. All 16 comparisons have byte-identical before and after PNGs, separately for the composited viewport, GPU canvas and overlay canvas. The document viewport was 1920×1080 CSS pixels; the CAD canvas was 1446×743 CSS pixels (2892×1486 physical pixels at DPR 2).

Each timed run performs 180 camera pans. Chromium's platform presentation-feedback-derived reporter cadence ranged from 59.329 to 60.337 reports per second; p95 intervals ranged from 16.667 to 33.333 ms. This counts unique reported feedback timestamps, not independently observed monitor scanouts. Near-coincident feedback timestamps can occur. CPU render/submission samples and rAF intervals are recorded separately and are not GPU execution timings. No WebScene performance claim follows from these Chrome reference results.

`reference.json.gz` contains the complete matrix metadata, per-frame timing samples, hardware identity, camera state, repeat comparisons, input/harness hashes and all artifact hashes. `files.json` hashes the files retained here. Two representative images and one raw compressed trace are committed for inspection and analyzer reproduction. The remaining raw PNGs/traces are retained locally in `artifacts/chrome-reference-matrix-02`; they are **not included in this repository evidence subset**. Full durable archival of those files remains outstanding. The earlier `matrix-01` attempt is also retained locally: its app canvas layers matched, but command suggestions obscured some composite images. The harness now dismisses that UI through normal app input handlers before capture.

Reproduce the full matrix from the repository root:

```sh
node --test tests/GraphicsCompatibility/reference-tests.mjs
node tests/GraphicsCompatibility/capture-chrome-reference.mjs --output artifacts/chrome-reference-new
```

The base commit was `d42a715628738c67c696e9d805d98f9f8697220f`; the then-uncommitted harness is identified by its exact file hashes in the result. Subsequent changes to documentation do not alter those hashes. Unknown Chrome revisions deliberately produce unavailable presentation analysis until the Chromium source contract is verified.

This evidence does not close #23. Other GPU hardware targets, remaining native integration/package validation, and complete durable reference artifact storage are still required. No WebGPU/WebGL browser API or GPU-resident presentation feature is claimed implemented here.

The non-graphics CI at that base commit passed Windows, macOS and Linux. Native package workflow [34110232771](https://github.com/wieslawsoltes/WebScene/actions/runs/34110232771) also passed all three runtime builds and package consumers after rerunning a Linux detached-DOM GC test failure. The initial failure is not suppressed or counted as a successful first attempt.
