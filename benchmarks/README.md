# Native engine benchmarks

This project contains native ABI, binary interop, DOM lookup, context-memory, runtime
work, and lifecycle measurements. It has no managed-engine dependency.

Set `WEBSCENE_NATIVE_ENGINE_PATH` to a built native library, then run BenchmarkDotNet
or a focused probe:

```bash
dotnet run --project benchmarks/WebScene.NativeEngine.Benchmarks -c Release

WEBSCENE_NATIVE_ENGINE_PATH=/absolute/path/to/libwebscene_native_engine.dylib \
  dotnet run --project benchmarks/WebScene.NativeEngine.Benchmarks -c Release -- \
  probe native-context-memory
```

The Inspector idle-cost gate builds the same source twice: once with Inspector
compiled out and once as the patched Inspector-capable production runtime. Both
variants use the same published managed benchmark executable. It reports V8 build
features, managed allocations, prewarm and warm
context startup, first scene, idle CPU, timer/animation-frame throughput, console-heavy
execution, a representative DOM workload, per-engine memory, and multi-view RSS as
machine-readable JSON:

```bash
WEBSCENE_NATIVE_ENGINE_PATH=/absolute/path/to/libwebscene_native_engine.dylib \
  dotnet run --project benchmarks/WebScene.NativeEngine.Benchmarks -c Release --no-build -- \
  probe native-inspector-disabled-performance \
  --contexts 4 --samples 10 --duration-ms 1500
```

Run at least 20 fresh processes per variant in control/candidate/candidate/control order,
then use `scripts/compare-inspector-disabled-performance.py` to enforce the production
thresholds. Timing and idle-CPU decisions use paired bootstrap 95% intervals so process
noise is not mistaken for a regression. The comparison requires an Inspector-free
control and an Inspector-capable candidate, and fails if ordinary work initializes the
managed Inspector registry.

Run with `probe` and no recognized name to list the focused probes.

## Resize cadence

`native-resize-cadence` drives a real Avalonia window at a configurable cadence and
emits machine-readable engine, publication, render, CPU, and latency metrics. Its
default local fixture exercises generic grid, flex, text, synchronous geometry reads,
resize listeners, and animation-frame work; `--url` can qualify any external page
without adding page-specific behavior to the engine.

```bash
WEBSCENE_NATIVE_ENGINE_PATH=/absolute/path/to/libwebscene_native_engine.dylib \
  dotnet run --project benchmarks/WebScene.NativeEngine.Benchmarks -c Release -- \
  probe native-resize-cadence --warmup-seconds 2 --seconds 10 --hz 60
```

Add `--composition` to exercise Avalonia's composition projection and `--enforce` to
return a failure unless p95 resize-to-render latency is at most 16.7 ms, throughput is
at least 58 rendered frames/second, no render interval exceeds 33.4 ms, and no input is
dropped. `--output <file>` writes the same JSON emitted on stdout. Use repeated fresh
control/candidate processes for performance decisions, then compare paired directories
with `scripts/compare-native-resize-cadence.py`. The comparator uses paired bootstrap
95% intervals, rejects supported regressions above 3%, and can require both a material
improvement and the practical-vsync gate. It also rejects pairs that mix certification
and production runtimes.

```bash
python3 scripts/compare-native-resize-cadence.py \
  --control-dir artifacts/resize-control \
  --candidate-dir artifacts/resize-candidate \
  --minimum-samples 10 \
  --output artifacts/resize-comparison.json
```

For a browser reference, the CDP profiler launches an isolated headed Chrome process
and applies the same 60 Hz triangular window resize. It records rAF cadence,
resize-to-rAF latency, long tasks, Chrome performance counters, and trace-stage totals:

```bash
node scripts/profile-chrome-resize-cadence.mjs \
  --url https://trading-terminal.tradingview-widget.com/ \
  --warmup-seconds 5 --seconds 10 --hz 60 \
  --output artifacts/chrome-resize.json
```

Use `--headless` only for automation smoke tests; headed Chrome is the visual smoothness
reference.

Pass that immutable Chrome result back to the native probe to report an exact matched
cadence comparison. Add `--enforce-chrome-reference` when the native run should fail
unless its measured presentation throughput and p95 interval equal or beat Chrome:

```bash
WEBSCENE_NATIVE_ENGINE_PATH=/absolute/path/to/libwebscene_native_engine.dylib \
  dotnet run --project benchmarks/WebScene.NativeEngine.Benchmarks -c Release -- \
  probe native-resize-cadence --composition \
  --url https://trading-terminal.tradingview-widget.com/ \
  --warmup-seconds 5 --seconds 10 --hz 60 \
  --chrome-reference artifacts/chrome-resize.json
```

## Retained scene rendering

The retained-render probe exercises the Avalonia Skia renderer without requiring the
native V8 library. It clears the surface before every render, checks the sparse result
against a visible-only pixel reference, and reports sparse and fully-visible timing so
viewport-culling gains can be weighed against their dense-scene overhead:

```bash
dotnet run --project benchmarks/WebScene.NativeEngine.Benchmarks -c Release -- \
  probe native-retained-render --layers 2048 --visible 32 --iterations 40 --samples 11
```

The companion apply probe measures one-layer replacement, batched replacement, and
z-order changes independently. It is intended to catch linear identity lookup and
unnecessary total-order rebuilds in the retained consumer:

```bash
dotnet run --project benchmarks/WebScene.NativeEngine.Benchmarks -c Release -- \
  probe native-retained-apply --layers 4096 --batch 256 --iterations 100 --samples 11
```

The complete NativePF candidate inventory, measurements, and accepted/rejected
decisions are recorded in
[`docs/nativepf-render-optimization-audit.md`](../docs/nativepf-render-optimization-audit.md).

## Native resource bridge

The resource-bridge probe measures the synchronous managed response-envelope path
independently from HTTP, archive lookup, V8 compilation, and page execution. It reports
both the ABI's required-size probe followed by an exact copy and a speculative
single-copy call with sufficient destination capacity:

```bash
dotnet run --project benchmarks/WebScene.NativeEngine.Benchmarks -c Release -- \
  probe native-resource-bridge --payload-bytes 32768 --iterations 200 --samples 11
```

Pass `--archive` and `--url` to compare decoded-text replay with direct UTF-8 replay
for a captured script. Use `--archive-iterations` to bound work for multi-megabyte
resources. The accepted implementation and baseline/candidate measurements are
recorded in
[`docs/native-resource-bridge-optimization-audit.md`](../docs/native-resource-bridge-optimization-audit.md).
