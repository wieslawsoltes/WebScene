# V8 Inspector production performance gate

The WebScene native production package includes the patched V8 Inspector
capability. Pull requests changing it must pass the required **Build and test
Inspector-capable V8 production flavor (osx-arm64)** job before merge.

## Matched full-stack comparison

The job creates two native variants from the same source revision:

- an Inspector-free control built without the compile-time feature;
- the patched Inspector-capable production library.

“Managed” here describes the C# wrapper and view layer; it does not mean a
second managed JavaScript runtime or a replacement for the native V8 engine.

Both variants use the same patched pinned V8 15.3.10 SDK, source revision,
Release configuration, managed assemblies, workload, and package settings. The
only intended difference is the native Inspector compile-time feature. This
isolates the idle production cost from unrelated changes between commits.

The gate runs 20 fresh processes per variant in repeated control, candidate,
candidate, control order. It records source revisions and SHA-256 values for
the managed backend assembly and both native libraries. Raw process JSON and
the comparison report are uploaded as the
`inspector-capable-production-comparison` workflow artifact.

## Connection-cost boundary

The native `V8Inspector`, context registrations, managed Inspector registry,
per-engine lifetime, session table, callback delegate, native callback thunk,
channels, and message buffers are created only when a native Inspector session
is opened. Starting the normal `--inspect`
discovery listener does not open that session; the WebSocket upgrade from a
DevTools client does. Ordinary showcase launches do not install the
pre-navigation diagnostic hook.

`--inspect-brk` is the intentional exception. It must create a waiting session
before document JavaScript is queued, otherwise it cannot guarantee a pause at
startup. This cost is therefore paid when the user explicitly requests the
break-on-start behavior rather than when the application merely uses WebScene.

## Acceptance policy

Inspector-capable production builds must satisfy all of the following:

- the control reports build features `0`, the candidate reports only the V8
  Inspector feature bit, and package metadata reports `v8Inspector: true`;
- completed timers, animation frames, console signals, and representative DOM
  workloads are identical;
- ordinary workloads leave the managed Inspector registry uninitialized;
- managed allocations for ordinary view construction, prewarm, blank engine
  lifecycle, and multi-view creation do not increase;
- blank-lifecycle, blank multi-view, and post-workload native memory totals are
  identical for V8 heap, code/metadata, external script source, DOM nodes and
  storage, wrappers, and the latest scene;
- median RSS does not increase by more than one 64 KiB measurement page;
- idle CPU fails when its median increase exceeds 0.01 percentage points and
  the paired bootstrap 95% interval is also wholly above that limit;
- timing fails when its median ratio exceeds 1.01 and the paired bootstrap 95%
  interval is also wholly above 1.01.

Samples with matching indices are paired because the workflow deliberately
alternates process order to cancel thermal and scheduler drift. The timing,
CPU, and RSS limits are measurement-noise guards, not feature budgets. A
repeatable non-zero regression must be investigated and removed even if it is
inside a guard.

The production binary audit requires the live-edit implementation marker and
Inspector-enabled package metadata. Stable unavailable-returning ABI exports
remain in feature-off control builds.

## Local comparison

Build the same revision once with Inspector disabled and once with Inspector
enabled, publish one Release benchmark executable, then collect at least 20 JSON
files per variant:

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
