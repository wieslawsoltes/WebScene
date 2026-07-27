# WebScene performance probes

The benchmark project contains product-neutral V8, DOM, CSS, lifecycle,
compilation-cache, React, iframe, and native-package probes. It requires a reviewed
WebScene ClearScript native build for the current RID, resolved by
`build/WebSceneClearScriptV8Native.targets`.

Run BenchmarkDotNet benchmarks with:

```sh
dotnet run --project benchmarks/JavaScript.Avalonia.Benchmarks -c Release
```

Run a focused probe with:

```sh
dotnet run --project benchmarks/JavaScript.Avalonia.Benchmarks -c Release -- probe v8dom
dotnet run --project benchmarks/JavaScript.Avalonia.Benchmarks -c Release -- probe v8react
dotnet run --project benchmarks/JavaScript.Avalonia.Benchmarks -c Release -- probe v8reactfocus
dotnet run --project benchmarks/JavaScript.Avalonia.Benchmarks -c Release -- probe v8iframepointer
dotnet run --project benchmarks/JavaScript.Avalonia.Benchmarks -c Release -- probe v8domidentity
dotnet run --project benchmarks/JavaScript.Avalonia.Benchmarks -c Release -- probe v8interactioncontracts
dotnet run --project benchmarks/JavaScript.Avalonia.Benchmarks -c Release -- probe v8lifecycle
dotnet run --project benchmarks/JavaScript.Avalonia.Benchmarks -c Release -- probe v8sharedcache
dotnet run --project benchmarks/JavaScript.Avalonia.Benchmarks -c Release -- probe v8nativepackage
dotnet run --project benchmarks/JavaScript.Avalonia.Benchmarks -c Release -- probe css-custom-properties
dotnet run --project benchmarks/JavaScript.Avalonia.Benchmarks -c Release -- probe css-style-storage
```

When overriding `WEBSCENE_CLEARSCRIPT_NATIVE`, point it at the canonical binary under
the executing app's `bin/.../runtimes/<rid>/native` directory after building with
`WebSceneClearScriptNativePath`. Loading two V8 images in one process is unsupported.

The `WEBSCENE_DISABLE_*` environment variables are diagnostic A/B controls for retained
optimization paths; production defaults remain enabled. Probe-specific switches are
documented by their command-line parsers. Use `--help` for BenchmarkDotNet filters. An
unknown focused probe exits with code 2 and prints the valid probe names.
