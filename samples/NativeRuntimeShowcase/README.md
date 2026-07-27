# Native runtime showcase

This showcase runs the hosted
[`https://trading-terminal.tradingview-widget.com/`](https://trading-terminal.tradingview-widget.com/)
application and a local, unchanged Monaco Editor bundle through WebScene's native
V8/DOM/canvas runtime. The same experience is available through Avalonia and
Uno Skia hosts.

The editor's .NET API is generated at build time from
`NativeRuntimeShowcase.Interop/MonacoApi.d.ts`. Both hosts use the emitted
`MonacoEditor`, `MonacoTextModel`, and `MonacoApi` types to load text into
Monaco, select a language, read edited text, and save it. The Open button uses
each UI framework's native file picker and accepts every file type.

Build the native runtime first if a compatible library is not already
available:

```sh
./scripts/build-native-engine-runtime.sh \
  --rid osx-arm64 \
  --output artifacts/native-runtime-showcase \
  --package-version 11.3.4-showcase.1
```

Run Avalonia:

```sh
dotnet run --project samples/NativeRuntimeShowcase.Avalonia -c Release -- \
  --native-library \
  "$PWD/artifacts/native-engine-runtime-build/osx-arm64/libwebscene_native_engine.dylib"
```

Run Uno desktop:

```sh
dotnet run --project samples/NativeRuntimeShowcase.Uno -f net10.0-desktop \
  -c Release -- \
  --native-library \
  "$PWD/artifacts/native-engine-runtime-build/osx-arm64/libwebscene_native_engine.dylib"
```

`WEBSCENE_NATIVE_ENGINE_LIBRARY` can be used instead of the command-line option.
Add `--editor` after the application arguments to start directly in Monaco.
The showcase reuses the checked-in Monaco 0.56.0 assets from
`samples/NativeMonacoEditor/Assets`; no browser control or WebView is involved.
