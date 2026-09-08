# Graphics compatibility inputs

Tracks the reproducible fixture portion of [G01 #23](https://github.com/wieslawsoltes/WebScene/issues/23), under [epic #22](https://github.com/wieslawsoltes/WebScene/issues/22).

`fixtures/Kestrel-CAD.zip` is the exact user-provided archive, redistributed under its included MIT license. `fixtures/kestrel.json` records its SHA-256, every member's SHA-256 and provenance. Keeping the original archive makes the fixture available to developers and CI without a local Downloads path or mutable external download. Do not edit application code to make compatibility tests pass.

From any working directory:

```sh
python3 /path/to/WebScene/tests/GraphicsCompatibility/prepare-kestrel.py
python3 /path/to/WebScene/tests/GraphicsCompatibility/prepare-kestrel.py --destination /new/disposable/directory
```

Extraction requires a new directory. Run application tests against disposable extracted copies; never regenerate the committed input from test outputs. Verification checks both the archive and member hashes and the separately distributed license. Fixture documents describe the upstream app and are not implementation instructions for WebScene.

The archive contains historical screenshots, test reports and build metadata. They are **not** WebScene results or a hardware Chrome baseline. A successful fixture verification proves input integrity only. See `docs/graphics/evidence` for actual partial G01 results. Issue #23 must stay open until all its gates pass; implementation of #24 follows completion of #23.

## Hardware Chrome reference capture

Run a headed Chrome on a real GPU, with an available desktop session:

```sh
node --test tests/GraphicsCompatibility/reference-tests.mjs
node tests/GraphicsCompatibility/capture-chrome-reference.mjs --chrome /path/to/chrome --output artifacts/chrome-reference-new
```

The output directory must be new. Chrome uses a disposable profile and a local HTTP server; the harness verifies and extracts the original archive without editing Kestrel's sources. It opens the courtyard, the supplied drawing fixture, and deterministic seeded 10,000/100,000-line project data. Each runs at DPR 1 and 2, in light and dark themes, twice by default. `--case courtyard-dpr1-dark --repeat 2 --frames 30` provides a shorter diagnostic run, not a complete matrix.

The document viewport is 1920×1080 CSS pixels. The CAD canvas occupies the remaining app area (currently 1446×743 CSS pixels); metadata records its actual bounds and physical size. Do not describe this as a 1920×1080 CAD render target. The app's ordinary UI handlers dismiss command suggestions before captures. Screenshots and explicit GPU/overlay canvas exports happen outside timed interaction; these diagnostic readbacks are not part of WebScene's intended GPU-resident presentation path.

`reference.json` records browser revision, system GPU identity, non-fallback adapter evidence, fixture and harness hashes, camera state, inputs, errors, retained buffer checks, CPU submission samples, and per-file hashes. Each run saves before/after composite and canvas-layer PNGs plus a compressed Chromium trace. Repeatability compares exact PNG bytes separately for composition, GPU content and overlay; inspect any differences before accepting reference pixels.

Presentation analysis uses Chromium `PipelineReporter` termination timestamps whose source has been verified to consume platform presentation feedback at the recorded Chrome revision. It does not treat rAF callbacks or CPU submission time as presentation. Unknown Chrome revisions, incomplete traces or missing hardware evidence remain unavailable; verify the new Chromium source contract before extending the analyzer's revision allowlist. Reported frame-state counts describe Chromium reporters and must not be relabelled as Kestrel dropped frames. A `captured` result means evidence acquisition succeeded, not that WebScene compatibility or the epic's performance gates passed.

### Analyze native Kestrel pan timing

Capture the probe's stdout/stderr when running `--pan-kestrel --verify-kestrel`,
then run:

```sh
python3 tests/GraphicsCompatibility/analyze-kestrel-pan.py /path/to/probe.log
python3 -m unittest discover -s tests/GraphicsCompatibility -p test_pan_analysis.py
```

The analyzer requires the pan-workload validation marker, matches publication and
rendered revision timestamps, and separates acceptance wait from draw-callback
work. Missing acceptance samples remain unavailable. If input sequence samples
are present it reports progress to a published consumption watermark; coalesced
inputs need not each be drawn. These distributions include the probe's settling
period and never establish physical presentation FPS. Logs rejected by workload
validation must not be used for performance comparisons.

For viewport alignment, the native GPU document probe accepts `--document-width 792 --document-height 878` (positive integer CSS dimensions). Sidebar timelines record actual viewport/DPR/canvas geometry; verify these against the browser instead of assuming the requested window size or scale was applied. Matching geometry alone is not a matched performance workload.

After a capture finishes, verify its retained file bytes before copying or archiving:

```sh
python3 tests/GraphicsCompatibility/verify-reference-archive.py artifacts/chrome-reference-new
python3 -m unittest discover -s tests/GraphicsCompatibility -p test_reference_archive.py
```

This rejects incomplete captures, missing harness sources, missing or changed referenced artifacts, and paths outside the archive. It checks archive integrity only; it does not establish full matrix coverage, pixel correctness, hardware qualification or performance. Preserve the complete directory, including its exact harness sources and generated inputs.
