# Native Monaco editor sample

This sample hosts the unmodified Monaco Editor 0.56.0 core in
`NativeHtmlMlView`. JavaScript tokenization supplies syntax highlighting, the
editor exposes line numbers and folding controls, and keyboard/text input is
routed through the native HtmlML scene surface in a real Avalonia window.

The web build imports Monaco from npm and bundles it as a browser IIFE. It does
not patch Monaco and contains no compatibility shims, visual text mirror, or
synthetic keyboard bridge.

Build the native runtime from this checkout:

```sh
./scripts/build-native-engine-runtime.sh \
  --rid osx-arm64 \
  --output artifacts/native-monaco-runtime \
  --package-version 11.3.4-monaco.1
```

Run the sample against that exact native library:

```sh
dotnet run --project samples/NativeMonacoEditor -c Release -- \
  --native-library \
  "$PWD/artifacts/native-engine-runtime-build/osx-arm64/libhtmlml_native_engine.dylib"
```

The first .NET build runs `npm install` and bundles the local Monaco dependency.
Later builds reuse `Assets/monaco.js` and `Assets/monaco.css`.

Generate deterministic native headless screenshots of both the initial editor
and a post-edit state:

```sh
dotnet run --project samples/NativeMonacoEditor.Headless -c Release -- \
  --native-library \
  "$PWD/artifacts/native-engine-runtime-build/osx-arm64/libhtmlml_native_engine.dylib" \
  --output "$PWD/artifacts/monaco-headless"
```

The capture verifies that Monaco booted, rendered eight tokenized view lines,
accepted text and Enter through the Avalonia input surface, and updated its
model to nine lines. It then invokes Monaco's fold command and captures the
collapsed function body. The run fails if Monaco's downloadable Codicon font
does not register with the native text shaper or the fold control does not
select it. It writes:

- `artifacts/monaco-headless/monaco-native-headless-initial.png`
- `artifacts/monaco-headless/monaco-native-headless-edited.png`
- `artifacts/monaco-headless/monaco-native-headless-folded.png`
