# Private TradingView example

This macOS application demonstrates the HtmlML Flutter backend against the
private hosted TradingView document. The example owns only TradingView-specific
initialization and deterministic bar-data responses; rendering and engine
lifecycle behavior live in `htmlml_flutter`.

Run from the package directory:

```shell
./example/tool/run_macos.sh
```

The script builds the native bridge and passes absolute library paths with Dart
defines. You can override its defaults:

```shell
HTMLML_NATIVE_ENGINE_LIBRARY=/path/libhtmlml_native_engine.dylib \
HTMLML_DOCUMENT_URL=https://host/document.html \
./example/tool/run_macos.sh
```

The macOS target disables the app sandbox because the example loads development
libraries by absolute path. Production applications should bundle and sign the
native libraries as part of their own distribution workflow.
