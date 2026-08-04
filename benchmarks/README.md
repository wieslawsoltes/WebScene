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
