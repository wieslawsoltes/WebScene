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

The Uno host also accepts `--document <absolute-uri-or-path>` to load an
arbitrary local application through the same native runtime. This is the
validation lane for source-mapped React applications; combine it with
`--webscene-inspect-brk` to attach before the bundle executes.

Run Flutter on macOS:

```sh
./src/WebScene.Backend.Flutter/example/tool/run_macos.sh
```

`WEBSCENE_NATIVE_ENGINE_LIBRARY` can be used instead of the command-line option.
Add `--editor` after the application arguments to start directly in Monaco.
Add `--webscene-inspect=127.0.0.1:9229` to expose the active Avalonia or Uno WebScene
isolate, or `--webscene-inspect-brk=127.0.0.1:9229` to start the discovery host
before navigation and hold V8 before the first document script. Chrome or the
CDP Inspector app releases that gate with `Runtime.runIfWaitingForDebugger`;
the desktop window and discovery host stay responsive during the wait. Port
`0` selects an ephemeral loopback port and the actual `/json/list` URL is
printed to stdout. For example, `--editor --webscene-inspect-brk=127.0.0.1:0`
provides a deterministic Monaco startup target.

The legacy `--v8-inspector` and `--v8-inspector-port <port>` switches remain
supported. Environment equivalents are `WEBSCENE_INSPECT`,
`WEBSCENE_INSPECT_BRK`, `WEBSCENE_V8_INSPECTOR`, and
`WEBSCENE_V8_INSPECTOR_PORT`. Inspector hosting is off by default and binds to
loopback. A non-loopback endpoint additionally requires
`--webscene-inspect-allow-remote` (or `WEBSCENE_INSPECT_ALLOW_REMOTE=1`) and all
remote discovery and WebSocket clients must present the configured access token.
Set a caller-known token of at least 32 characters in
`WEBSCENE_INSPECT_TOKEN`; unauthenticated remote discovery deliberately does not
disclose it, and the showcase does not print it to the startup log.
For Flutter, use `WEBSCENE_INITIAL_DOCUMENT=monaco`.
The showcase reuses the checked-in Monaco 0.56.0 assets from
`samples/NativeMonacoEditor/Assets`; no browser control or WebView is involved.
All three hosts load `samples/NativeRuntimeShowcase.Web/index.html` and
`showcase.js`, so they start with the same generated-interop C# sample and
Monaco highlighting configuration.
