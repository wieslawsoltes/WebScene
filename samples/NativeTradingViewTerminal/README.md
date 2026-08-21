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

Platform positioning for eligible system-font runs is enabled by default while Skia
continues to paint the glyphs: CoreText on macOS and DirectWrite on Windows. Launch
separate processes with the following modes for a direct before/after comparison:

```bash
WEBSCENE_TEXT_POSITIONING=harfbuzz dotnet run \
  --project samples/NativeTradingViewTerminal -- \
  --native-library /absolute/path/to/libwebscene_native_engine.dylib

WEBSCENE_TEXT_POSITIONING=coretext dotnet run \
  --project samples/NativeTradingViewTerminal -- \
  --native-library /absolute/path/to/libwebscene_native_engine.dylib
```

On Windows, use `WEBSCENE_TEXT_POSITIONING=directwrite` for an explicit candidate run.

`harfbuzz`, `legacy`, `off`, or `0` selects the previous renderer. An unset value or
`auto` enables the platform service; `coretext` and `directwrite` select their matching
platform candidate explicitly. Unsupported fonts, scripts, styles, features, or glyph
identities always fall back to HarfBuzz/Skia per run.

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

Profile desktop startup until the chart iframe has rendered at least eight canvases
and hidden its loading indicator:

```bash
WEBSCENE_PROBE_PROFILE_STARTUP=1 dotnet run \
  --project samples/NativeTradingViewTerminal -c Release -- \
  --startup-profile \
  --native-library /absolute/path/to/libwebscene_native_engine.dylib \
  --cache /tmp/webscene-tradingview-profile-cache
```

Capture the resolved HTTP(S) text resources and web-font bytes after starting with
an empty WebScene cache, then replay them without any network fallback:

```bash
WEBSCENE_PROBE_PROFILE_STARTUP=1 dotnet run \
  --project samples/NativeTradingViewTerminal -c Release -- \
  --startup-profile \
  --native-library /absolute/path/to/libwebscene_native_engine.dylib \
  --cache /tmp/webscene-tradingview-capture-cache \
  --capture-resources /tmp/webscene-tradingview-resources

WEBSCENE_PROBE_PROFILE_STARTUP=1 dotnet run \
  --project samples/NativeTradingViewTerminal -c Release -- \
  --startup-profile \
  --native-library /absolute/path/to/libwebscene_native_engine.dylib \
  --cache /tmp/webscene-tradingview-replay-cache \
  --replay-resources /tmp/webscene-tradingview-resources
```

Resource capture and replay are mutually exclusive. Replay fails immediately when
the application requests an uncaptured HTTP(S) resource; it never silently reaches
the origin. Use a separate compilation/resource cache directory for each cold run,
or intentionally reuse one when measuring the warm-cache case.

Compare Chrome against the same response bodies without installing an extension.
The runner starts a temporary headless Chrome profile and fulfills requests directly
through the Chrome DevTools Protocol:

```bash
node scripts/benchmark-tradingview-replay.mjs \
  --archive /tmp/webscene-tradingview-resources \
  --capture-misses

node scripts/benchmark-tradingview-replay.mjs \
  --archive /tmp/webscene-tradingview-resources
```

`--capture-misses` is a one-time preparation pass that adds Chrome-only responses
to the shared archive. Omit it for every measured run. The JSON result reports the
chart-ready wall time, Chrome task/script/style/layout
durations, served bytes, and every blocked archive miss. Any miss makes the command
fail so a changed resource graph cannot silently contaminate the comparison.

Certification builds can reproduce the former broad custom-property recascade as an
A/B control by additionally setting
`WEBSCENE_PROBE_DISABLE_CSS_VARIABLE_DEPENDENCY_FILTER=1`. Run the optimized and
control processes against the same warm cache; compare `stylesheet-recascade`,
`stylesheet-nodes`, and `stylesheet-variable-nodes` in the compact profile output.

Use `WEBSCENE_PROBE_DISABLE_STYLE_RECASCADE_BATCHING=1` as the control for immediate
DOM-mutation recascades. Certification profiles report `script-phase-top` and
`task-phase-top`, including nested CSS work, forced layout passes, and dirty-state
transitions for the hottest scripts and timer/animation-frame callbacks.

Use `WEBSCENE_PROBE_DISABLE_STYLESHEET_CANDIDATE_FILTER=1` to restore the legacy
connected-stylesheet path that finalizes and dirties every existing element, even
when no appended selector can match it. The optimized profile additionally reports
`stylesheet-candidate-nodes` beside the total number of visited stylesheet nodes.

### August 2026 startup results

Five cold runs against one captured HTTP response archive initially measured a
1,505.7 ms WebScene median and a 1,253.2 ms Chrome median. The archive fixes HTTP
response bodies, but TradingView's WebSocket data remains live, so use medians and
nested CPU counters instead of treating individual wall-time runs as paired samples.

Per-task mutation batching reduced the WebScene median from 1,034.2 ms with immediate
recascade to 1,004.2 ms, while median CSS-application CPU fell from 145.1 ms to
86.8 ms. In a later five-run candidate-filter A/B, median incremental stylesheet CPU
fell from 27.9 ms to 8.2 ms and total stylesheet recascade fell from 53.3 ms to
48.1 ms. That pass did not yet produce a statistically useful wall-time improvement
(1,509.2 ms control versus 1,529.9 ms optimized) because live-data scheduling noise
was larger than the saved CPU interval. A representative optimized run finalized
280 of 56,521 visited nodes (0.50%).

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
