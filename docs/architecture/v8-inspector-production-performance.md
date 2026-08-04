# V8 Inspector production performance validation

The WebScene native production package includes the patched V8 Inspector
capability on macOS, Linux, and Windows. Pull requests changing the native
runtime or its packaging run the existing production package matrix for all
three supported RIDs. Performance comparisons are deliberate local or release
investigations rather than a GitHub Actions prerequisite.

## Matched full-stack comparison

The comparison procedure compares two complete revisions:

- the current `main` runtime and managed backend as the historical control;
- the pull request's Inspector-capable production runtime and managed backend.

“Managed” here describes the C# wrapper and view layer; it does not mean a
second managed JavaScript runtime or a replacement for the native V8 engine.

Both use the same patched pinned V8 15.3.10 SDK, Release configuration,
workload, and package settings. The control is built only as benchmark evidence
from the exact `main` revision; it is not a second supported or published
runtime flavor. Production packaging always includes CDP support. Comparing the
full revisions protects the user-visible requirement that ordinary workloads
do not regress relative to the runtime being replaced.

The procedure runs 20 fresh processes per variant in repeated control, candidate,
candidate, control order. It records source revisions and SHA-256 values for
the managed backend assembly and both native libraries. Keep the raw process
JSON and comparison report with the investigation evidence.

## Connection-cost boundary

Each engine retains only atomic publication fields before use. The native
Inspector state (mutex, condition variable, action queue, session and async-task
maps, promise-rejection tracking), `V8Inspector`, context registrations,
managed Inspector registry, callback delegate, native callback thunk, channels,
and message buffers are created only when a native Inspector session is opened.
The performance probe reads a native diagnostic export and fails if ordinary
blank, idle, timer, console, or representative DOM workloads create that lazy
state. Starting the normal `--inspect`
discovery listener does not open that session; the WebSocket upgrade from a
DevTools client does. Ordinary showcase launches do not install the
pre-navigation diagnostic hook.

`--inspect-brk` is the intentional exception. It must create a waiting session
before document JavaScript is queued, otherwise it cannot guarantee a pause at
startup. This cost is therefore paid when the user explicitly requests the
break-on-start behavior rather than when the application merely uses WebScene.

## Comparison policy

Inspector-capable production builds must satisfy all of the following:

- the current-main control reports its actual build features, the candidate
  reports only the V8 Inspector feature bit, and package metadata reports
  `v8Inspector: true`;
- completed timers, animation frames, console signals, and representative DOM
  workloads are identical;
- ordinary workloads leave the managed Inspector registry uninitialized;
- ordinary workloads leave native Inspector state unallocated;
- managed allocations for ordinary view construction, prewarm, blank engine
  lifecycle, and multi-view creation do not increase;
- blank-lifecycle, blank multi-view, and post-workload native memory totals are
  identical for V8 heap, code/metadata, external script source, DOM nodes and
  storage, wrappers, and the latest scene;
- the native library grows by no more than 1 MiB;
- median incremental multi-view RSS does not increase by more than one 64 KiB
  measurement page;
- total workload RSS may additionally reflect the measured native-library size
  delta because macOS includes resident file-backed executable pages;
- idle CPU fails when its median increase exceeds 0.01 percentage points and
  the paired bootstrap 95% interval is also wholly above that limit;
- timing fails when its median ratio exceeds 1.01 and the paired bootstrap 95%
  interval is also wholly above 1.01.

Samples with matching indices are paired because the procedure deliberately
alternates process order to cancel thermal and scheduler drift. The timing,
CPU, allocation, incremental RSS, and native-memory limits guard runtime cost.
The separate 1 MiB binary-size budget bounds the shipped Inspector code rather
than disguising it as heap usage.

The production package matrix validates the native tests, package metadata,
complete package set, and clean consumers on macOS, Linux, and Windows. Stable
unavailable-returning ABI exports remain for compatibility with historical
runtimes.

## Local comparison

Build the current `main` revision and the candidate revision against the same V8
SDK, publish the Release benchmark executable for each revision, then collect at
least 20 JSON files per variant:

```bash
WEBSCENE_NATIVE_ENGINE_PATH=/absolute/path/to/libwebscene_native_engine.dylib \
  dotnet /absolute/path/to/WebScene.NativeEngine.Benchmarks.dll \
    probe native-inspector-disabled-performance > sample.json
```

Compare the two sample directories with:

```bash
python3 scripts/compare-inspector-disabled-performance.py \
  --control-dir /absolute/path/to/control-samples \
  --candidate-dir /absolute/path/to/candidate-samples \
  --control-sha CONTROL_COMMIT \
  --candidate-sha CANDIDATE_COMMIT \
  --control-managed /absolute/path/to/control/WebScene.Backend.Avalonia.dll \
  --candidate-managed /absolute/path/to/candidate/WebScene.Backend.Avalonia.dll \
  --control-native /absolute/path/to/control/libwebscene_native_engine.dylib \
  --candidate-native /absolute/path/to/candidate/libwebscene_native_engine.dylib \
  --output /absolute/path/to/comparison.json
```
