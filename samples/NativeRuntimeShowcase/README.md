# Native runtime showcase

This showcase runs the hosted
[`https://trading-terminal.tradingview-widget.com/`](https://trading-terminal.tradingview-widget.com/)
application and a local, unchanged Monaco Editor bundle through WebScene's native
V8/DOM/canvas runtime. The same experience is available through Avalonia, Uno
Skia, and Flutter hosts.

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

Run Flutter on macOS:

```sh
./src/WebScene.Backend.Flutter/example/tool/run_macos.sh
```

`WEBSCENE_NATIVE_ENGINE_LIBRARY` can be used instead of the command-line option.
Add `--editor` after the application arguments to start directly in Monaco.
Add `--v8-inspector` to expose the active Avalonia WebScene isolate at
`http://127.0.0.1:9229/json/list`; use `--v8-inspector-port <port>` to choose a
different port. For example, `--editor --v8-inspector` provides a deterministic
local target for Chrome `chrome://inspect` and the CDP Inspector app. The
equivalent environment switches are `WEBSCENE_V8_INSPECTOR=1` and
`WEBSCENE_V8_INSPECTOR_PORT`.
For Flutter, use `WEBSCENE_INITIAL_DOCUMENT=monaco`.
The showcase reuses the checked-in Monaco 0.56.0 assets from
`samples/NativeMonacoEditor/Assets`; no browser control or WebView is involved.
All three hosts load `samples/NativeRuntimeShowcase.Web/index.html` and
`showcase.js`, so they start with the same generated-interop C# sample and
Monaco highlighting configuration.
