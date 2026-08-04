# V8 Inspector production performance gate

The ordinary WebScene native package compiles V8 Inspector out. The opt-in
Inspector package is a separate diagnostic flavor. Pull requests changing the
Inspector must pass the required **Build and test V8 production flavor
(osx-arm64)** job before merge.

## Matched full-stack comparison

The job creates two complete stacks:

- the control C# interop/view assemblies and production native library from the
  current `origin/main` commit;
- the candidate C# interop/view assemblies and production native library from
  the pull request head.

“Managed” here describes the C# wrapper and view layer; it does not mean a
second managed JavaScript runtime or a replacement for the native V8 engine.

Both stacks use the same unmodified pinned V8 15.3.10 SDK and identical
Inspector-disabled build flags. The candidate benchmark harness source is
copied into the temporary control checkout so the measured workload is
identical while each executable still references its own revision's managed
WebScene projects. Pointing one candidate executable at two native libraries is
not a valid full-stack comparison.

The gate runs 20 fresh processes per variant in repeated control, candidate,
candidate, control order. It records source revisions and SHA-256 values for
both managed backend assemblies and both native libraries. Raw process JSON and
the comparison report are uploaded as the
`inspector-disabled-production-comparison` workflow artifact.

## Connection-cost boundary

The managed Inspector registry, per-engine lifetime, session table, callback
delegate, native callback thunk, channels, and message buffers are created only
when a native Inspector session is opened. Starting the normal `--inspect`
discovery listener does not open that session; the WebSocket upgrade from a
DevTools client does. Ordinary showcase launches do not install the
pre-navigation diagnostic hook.

`--inspect-brk` is the intentional exception. It must create a waiting session
before document JavaScript is queued, otherwise it cannot guarantee a pause at
startup. This cost is therefore paid when the user explicitly requests the
break-on-start behavior rather than when the application merely uses WebScene.

## Acceptance policy

Inspector-disabled builds must satisfy all of the following:

- both libraries report build features `0` and package metadata reports
  `v8Inspector: false`;
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

The production binary audit also rejects Inspector implementation markers such
as the live-edit flag. Stable unavailable-returning ABI exports are retained so
managed packages can load either native flavor safely.

## Local comparison

Build and publish the same benchmark source separately against `main` and the
candidate, then collect at least 20 JSON files per variant:

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
