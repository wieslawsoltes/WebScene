# HtmlML Flutter backend

`htmlml_flutter` renders HtmlML's immutable native scene stream with Flutter.
It owns the native engine lifecycle, translates Flutter input and lifecycle
events, services host requests, and paints scene revisions without embedding a
browser view.

The current backend targets macOS on Apple silicon. The native engine remains
on its worker thread; Flutter only consumes immutable scene snapshots.

## Use the backend

Add this repository package to a Flutter application's `pubspec.yaml`:

```yaml
dependencies:
  htmlml_flutter:
    path: ../HtmlML/src/HtmlML.Backend.Flutter
```

Build the worker-safe macOS bridge:

```shell
./tool/build_bridge_macos.sh
```

Then provide the native runtime and bridge to `HtmlMlView`:

```dart
HtmlMlView(
  documentUrl: 'https://example.test/application.html',
  runtime: const HtmlMlRuntimeConfiguration(
    runtimeLibraryPath: '/absolute/path/libhtmlml_native_engine.dylib',
    bridgeLibraryPath: '/absolute/path/libhtmlml_flutter_bridge.dylib',
  ),
  onHostRequest: (controller, request) {
    // Implement application-specific host services here.
  },
  onError: (error) => debugPrint('$error'),
)
```

Use `HtmlMlController.executeScript` for imperative calls into the document.
`HtmlMlView` handles resize, frame, pointer, keyboard, cursor, visibility,
memory-pressure, checkpoint, console, and scene acknowledgement behavior.

The host app must allow JIT execution and access to the runtime and bridge
paths. The included macOS example carries the required entitlements.

## TradingView example

The `example` directory contains the private TradingView integration and its
deterministic market-data host service. TradingView-specific behavior is kept
out of the backend package.

```shell
./example/tool/run_macos.sh
```

The script stores Pub, temporary, Clang, and Swift caches under
`/Volumes/SSD/danw/caches/htmlml-flutter` by default. Override that location
with `HTMLML_FLUTTER_CACHE_ROOT`, or select another runtime with
`HTMLML_NATIVE_ENGINE_LIBRARY`.

For a UI-free native acceptance check:

```shell
dart tool/runtime_smoke.dart \
  /absolute/path/libhtmlml_native_engine.dylib \
  build/native/libhtmlml_flutter_bridge.dylib \
  https://example.test/application.html
```

## Current scope

- macOS arm64 is the supported host.
- The native bridge is built explicitly rather than packaged as a Flutter
  plugin binary.
- Standard scene text, paths, clipping, transforms, images, and input are
  implemented. Complex inline SVG and full IME composition remain follow-up
  backend work.
