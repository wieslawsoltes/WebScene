# Native TradingView Terminal

This sample loads the hosted
`https://trading-terminal.tradingview-widget.com/` JavaScript application
directly in `NativeWebSceneView`. Its browser-facing `WebSocket` API is backed by
the portable C++ socket transport inside the WebScene native runtime; no .NET
WebSocket callback is involved.

Run the desktop sample:

```bash
dotnet run --project samples/NativeTradingViewTerminal \
  -- --native-library /absolute/path/to/libwebscene_native_engine.dylib
```

On macOS, platform CoreText positioning for eligible system-font runs is enabled by
default while Skia continues to paint the glyphs. Launch separate processes with the
following modes for a direct before/after comparison:

```bash
WEBSCENE_TEXT_POSITIONING=harfbuzz dotnet run \
  --project samples/NativeTradingViewTerminal -- \
  --native-library /absolute/path/to/libwebscene_native_engine.dylib

WEBSCENE_TEXT_POSITIONING=coretext dotnet run \
  --project samples/NativeTradingViewTerminal -- \
  --native-library /absolute/path/to/libwebscene_native_engine.dylib
```

`harfbuzz`, `legacy`, `off`, or `0` selects the previous renderer. An unset value,
`auto`, or `coretext` enables the platform service. Unsupported fonts, scripts,
styles, features, or glyph identities always fall back to HarfBuzz/Skia per run.

The macOS default also applies Chromium-compatible Skia font flags. Use
`WEBSCENE_TEXT_RASTERIZATION=current` to retain the former rasterization profile or
`WEBSCENE_TEXT_RASTERIZATION=chromium` to select the new profile explicitly. The
positioning and rasterization controls are independent, which keeps both stages easy
to compare and roll back.

To capture the real Avalonia presenter surface and its scale, matrix, GPU, pixel
geometry, and color-space metadata after startup, set an output directory:

```bash
WEBSCENE_TEXT_PRESENTER_DIAGNOSTICS=/tmp/webscene-presenter \
  dotnet run --project samples/NativeTradingViewTerminal -- \
  --native-library /absolute/path/to/libwebscene_native_engine.dylib
```

Generate repeatable headless evidence (JSON plus a PNG):

```bash
dotnet run --project samples/NativeTradingViewTerminal -c Release -- \
  --headless-proof \
  --native-library /absolute/path/to/libwebscene_native_engine.dylib \
  --output artifacts/native-tradingview-terminal
```

Run the Sandwich Trading Platform multi-chart geometry proof with a
deterministic in-process market-data bridge:

```bash
dotnet run --project samples/NativeTradingViewTerminal -c Release -- \
  --sandwich-layout-proof \
  --url https://tv.sandwichtrading.com/tp-v1/index.html \
  --native-library /absolute/path/to/libwebscene_native_engine.dylib \
  --output artifacts/sandwich-layout-proof
```

Pass `--composition` to run the same round trip through the compositor-backed
presenter used by the interactive sample. Run both modes when certifying a layout
transition fix.

The hosted terminal delegates its data connection to a separately navigated
TradingView iframe. The proof runs that iframe in its own native V8 realm and
observes the `WebSocket` created organically by TradingView's datafeed code; it
does not inject or open a synthetic test socket. It fails unless the widget
renders a substantial native scene, the socket opens and receives live data,
and the captured PNG contains real visual variation. The runtime does not call
a .NET WebSocket implementation.
