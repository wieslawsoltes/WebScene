# Native runtime showcase

This macOS application demonstrates the WebScene Flutter backend against both
the checked-in Monaco Editor bundle and the private hosted TradingView
document. Use the native toolbar to switch between them. The example owns only
library-specific initialization and deterministic TradingView bar-data
responses; rendering and engine lifecycle behavior live in `webscene_flutter`.

Run from the package directory:

```shell
./example/tool/run_macos.sh
```

The script builds the native bridge and passes absolute library paths with Dart
defines. You can override its defaults:

```shell
WEBSCENE_NATIVE_ENGINE_LIBRARY=/path/libwebscene_native_engine.dylib \
WEBSCENE_DOCUMENT_URL=https://host/document.html \
./example/tool/run_macos.sh
```

Set `WEBSCENE_INITIAL_DOCUMENT=monaco` to launch directly into the editor.
`WEBSCENE_MONACO_DOCUMENT_PATH` can select another local Monaco entry document.
By default, the launcher stages the shared
`samples/NativeRuntimeShowcase.Web` Monaco document and the checked-in Monaco
assets under `WEBSCENE_FLUTTER_CACHE_ROOT`. Flutter, Uno, and Avalonia therefore
open the same C# sample with the same `csharp` language, `vs-dark` theme, and
editor options.

The macOS target disables the app sandbox because the example loads development
libraries by absolute path. Production applications should bundle and sign the
native libraries as part of their own distribution workflow.
