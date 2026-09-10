# WebScene Flutter backend

Use `onJavaScriptException` for production page error logging and `onRuntimeFailed`
for terminal failures. `onConsoleMessage` enables opt-in console capture without
frame polling. See [Runtime diagnostics](../../docs/runtime-diagnostics.md) for
controller state, custom failure UI, native integration tests and legacy migration.

`webscene_flutter` renders WebScene's immutable native scene stream with Flutter.
It owns the native engine lifecycle, translates Flutter input and lifecycle
events, services host requests, and paints scene revisions without embedding a
browser view.

The current backend targets macOS on Apple silicon. The native engine remains
on its worker thread; Flutter only consumes immutable scene snapshots.

## Use the backend

Add this repository package to a Flutter application's `pubspec.yaml`:

```yaml
dependencies:
  webscene_flutter:
    path: ../WebScene/src/WebScene.Backend.Flutter
```

Build the worker-safe macOS bridge:

```shell
./tool/build_bridge_macos.sh
```

Then provide the native runtime and bridge to `WebSceneView`:

```dart
WebSceneView(
  documentUrl: 'https://example.test/application.html',
  runtime: const WebSceneRuntimeConfiguration(
    runtimeLibraryPath: '/absolute/path/libwebscene_native_engine.dylib',
    bridgeLibraryPath: '/absolute/path/libwebscene_flutter_bridge.dylib',
  ),
  onHostRequest: (controller, request) {
    // Implement application-specific host services here.
  },
  onError: (error) => debugPrint('$error'),
)
```

Use `WebSceneController.executeScript` for imperative calls into the document.
`WebSceneView` handles resize, frame, pointer, keyboard, cursor, visibility,
memory-pressure, checkpoint, console, and scene acknowledgement behavior.

The host app must allow JIT execution and access to the runtime and bridge
paths. The included macOS example carries the required entitlements.

## Monaco and TradingView example

The `example` directory hosts both the checked-in Monaco Editor bundle and the
private TradingView integration with its deterministic market-data host
service. Library-specific behavior stays outside the backend package.

```shell
./example/tool/run_macos.sh
```

The script stores Pub, temporary, Clang, and Swift caches under
`/Volumes/SSD/danw/caches/webscene-flutter` by default. Override that location
with `WEBSCENE_FLUTTER_CACHE_ROOT`, or select another runtime with
`WEBSCENE_NATIVE_ENGINE_LIBRARY`.

For a UI-free native acceptance check:

```shell
dart tool/runtime_smoke.dart \
  /absolute/path/libwebscene_native_engine.dylib \
  build/native/libwebscene_flutter_bridge.dylib \
  https://example.test/application.html
```

## Current scope

- macOS arm64 is the supported host.
- The native bridge is built explicitly rather than packaged as a Flutter
  plugin binary.
- Standard scene text, paths, clipping, transforms, images, and input are
  implemented. Complex inline SVG and full IME composition remain follow-up
  backend work.
