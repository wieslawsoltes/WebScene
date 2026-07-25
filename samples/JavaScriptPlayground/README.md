# HtmlML Playground runtimes

The Playground has two surfaces:

- **DOM Playground** runs editable XAML and JavaScript through the managed
  ClearScript-backed Avalonia DOM.
- **Monaco (native)** runs the unmodified Monaco editor bundle through
  `NativeHtmlMlView`, rendered on the native HtmlML canvas.

Once the reviewed native binary has been built/packed for the current RID, the ordinary
command needs no engine or native-path flags:

```sh
dotnet run --project samples/JavaScriptPlayground/JavaScriptPlayground.csproj \
  -c Release
```

No engine-selection property or environment variable is needed. The build resolves the
current RID automatically and checks the stable local
cache at `artifacts/v8-native/runtimes/<rid>/native`, followed by an existing Playground
output. The native pack script populates that cache. `HTMLML_CLEARSCRIPT_NATIVE` remains
an explicit override for testing another reviewed build, not a normal launch
requirement. The build still stops if no reviewed or correctly named RID asset exists.

At runtime, HtmlML resolves and loads the bundled RID asset, logs the result to stderr,
and checks that owner and iframe V8 contexts can exchange objects. A stale stock
ClearScript binary therefore fails immediately instead of leaving the chart waiting for
`onChartReady`. Fresh checkouts must build/package a reviewed native once using the
commands in `third-party/clearscript-patches/README.md`; subsequent Playground builds
need no native configuration.

The optional managed runtime no longer restores `Microsoft.ClearScript.Complete` or
any stock `Microsoft.ClearScript.V8.Native.*` package. Local builds copy only the
explicit reviewed binary above. Production packages are created per RID with
`scripts/build-clearscript-v8-native.sh` and `scripts/pack-clearscript-v8-native.sh`;
see `third-party/clearscript-patches/README.md` for supported RIDs and verification.
After execution, the status line reports `Script executed (V8)`.

To launch directly into the native Monaco tab, pass the ABI 2 native HtmlML engine:

```sh
dotnet run --project samples/JavaScriptPlayground/JavaScriptPlayground.csproj \
  -c Release -- \
  --monaco \
  --native-library \
  "$PWD/artifacts/native-engine-runtime-build/osx-arm64/libhtmlml_native_engine.dylib"
```

`HTMLML_NATIVE_ENGINE_LIBRARY` can be used instead of `--native-library`.
The Monaco tab is lazy-loaded, so the native engine is not required when only the
DOM Playground is used. Monaco's generated web assets remain owned by
`samples/NativeMonacoEditor`; the Playground links those files rather than carrying a
modified or duplicate Monaco bundle.
