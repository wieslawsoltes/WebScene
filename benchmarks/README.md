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

Run with `probe` and no recognized name to list the focused probes.
