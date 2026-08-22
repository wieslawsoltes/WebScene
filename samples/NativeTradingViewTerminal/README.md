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
the origin. Reusing a capture directory merges newly observed responses into its
existing valid manifest, which is useful for stabilizing conditional resource paths.
Capture runs observe the page for two seconds after visual readiness so nearby lazy
chunks are included before the manifest is flushed.
Use a separate compilation/resource cache directory for each cold run, or
intentionally reuse one when measuring the warm-cache case.
The startup sample reports archive preparation, cold host-to-ready, and
navigation-to-ready separately. Compare Chrome's `readyMilliseconds` with the
navigation interval; the cold interval intentionally includes fixture preparation
and native-engine initialization.

Compare Chrome against the same response bodies without installing an extension.
The runner starts a temporary headless Chrome profile and fulfills requests directly
through the Chrome DevTools Protocol:

```bash
node scripts/benchmark-tradingview-replay.mjs \
  --archive /tmp/webscene-tradingview-resources \
  --capture-misses

# Add a WebScene-only conditional URL reported by a strict replay miss.
node scripts/benchmark-tradingview-replay.mjs \
  --archive /tmp/webscene-tradingview-resources \
  --capture-url https://example.test/conditional-chunk.js

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
`frame-phase-top` applies the same attribution to each hydrated iframe.
Set `WEBSCENE_PROBE_PROFILE_BINDINGS=1` to add per-category binding totals and the
eight hottest named DOM mutation/geometry APIs. Use
`WEBSCENE_PROBE_DISABLE_CONNECTED_RESOURCE_STYLE_BATCHING=1` to isolate the legacy
dynamic-resource boundary, which flushed after script evaluation and then ran its
`load` event and microtasks with immediate recascades.
Use `WEBSCENE_PROBE_DISABLE_IFRAME_DOCUMENT_PREFETCH=1` to restore synchronous
remote iframe-document acquisition at `appendChild`; CSS/script execution order is
unchanged by this control.
Use `WEBSCENE_PROBE_DISABLE_IFRAME_PREPARATION=1` to retain concurrent iframe
document fetches but restore owner-thread HTML scanning and CSS/script discovery.
The optimized path scans a completed remote frame document off the isolate and starts
its subresource prefetches while the outer document is still executing; DOM creation,
V8 context installation, script execution, and lifecycle dispatch remain ordered on
the owner thread. Startup telemetry reports `frame-prepare`, `frame-prepare-wait`,
and `frame-prepare-lead`.

Use `WEBSCENE_PROBE_DISABLE_COOPERATIVE_IFRAME_HYDRATION=1` to restore the former
single-task iframe hydration and strict frame-before-timer/resource task ordering.
The optimized path retains an independent cascade/index per browsing context,
executes blocking and deferred script groups as resumable owner-thread work, and
alternates yielded frame work with already-ready timer, resource, and resize-observer
tasks. Startup telemetry reports `frame-slices`, `frame-yields`, and
`frame-max-slice`. Individual JavaScript calls remain atomic inside V8.

Use `WEBSCENE_PROBE_DISABLE_DEFERRED_COMPILATION_CACHE_TOUCH=1` to restore an
immediate filesystem timestamp update on every persistent V8 code-cache hit. The
optimized path records cache use in memory and flushes deduplicated timestamps during
runtime teardown, while cache pruning treats pending touches as recent.

Use `WEBSCENE_PROBE_DISABLE_STYLESHEET_CANDIDATE_FILTER=1` to restore the legacy
connected-stylesheet path that finalizes and dirties every existing element, even
when no appended selector can match it. The optimized profile additionally reports
`stylesheet-candidate-nodes` beside the total number of visited stylesheet nodes.

### August 2026 startup results

An initial five-run comparison measured a 1,505.7 ms WebScene median and a
1,253.2 ms Chrome median, but that result included archive reads and native-engine
creation in WebScene while Chrome's archive and browser were already initialized.
It is retained here only as the benchmark asymmetry that motivated the separate
preparation, cold-host, and navigation clocks.

With both sides timed from navigation to the same eight-canvas/loading-hidden gate,
five strict-replay runs measured a 983.3 ms WebScene navigation median and a
1,140.6 ms Chrome median. WebScene was 157.3 ms (13.8%) faster at that shared gate.
Its full cold host-to-ready median, including archive preparation and engine
creation, was 1,068.4 ms; archive preparation itself was 28.1 ms. Chrome task/script
metrics overlap and use different semantics from WebScene's nested buckets, so they
are reported for within-engine attribution rather than subtracted across engines.
The archive fixes HTTP response bodies, but TradingView's WebSocket data remains
live, so use medians and CPU counters instead of treating individual wall-time runs
as paired samples.

Per-task mutation batching reduced the WebScene median from 1,034.2 ms with immediate
recascade to 1,004.2 ms, while median CSS-application CPU fell from 145.1 ms to
86.8 ms. In a later five-run candidate-filter A/B, median incremental stylesheet CPU
fell from 27.9 ms to 8.2 ms and total stylesheet recascade fell from 53.3 ms to
48.1 ms. That pass did not yet produce a statistically useful wall-time improvement
(1,509.2 ms control versus 1,529.9 ms optimized) because live-data scheduling noise
was larger than the saved CPU interval. A representative optimized run finalized
280 of 56,521 visited nodes (0.50%).

Named binding attribution then found that dynamically connected resource tasks
were flushing style work after script evaluation, before dispatching `load` and its
microtasks. Extending the batch across that complete browser-task boundary reduced
immediate subtree recascades from 1,743 to 101 in representative diagnostic runs;
DOM-mutation binding CPU fell from 90.5 ms to 22.2 ms and `Node.appendChild` from
42.1 ms to 5.7 ms. In a five-pair cold A/B, median CSS-application CPU fell from
101.7 ms to 70.8 ms (-30.4%) and subtree-recascade CPU from 65.9 ms to 44.8 ms
(-32.1%). Navigation medians were 1,016.6 ms control and 1,044.8 ms optimized, so
no wall-time improvement is claimed for that pass.

Forced-layout attribution subsequently showed that geometry reads were the largest
remaining native binding category. `getClientRects()` and
`getBoundingClientRect()` now pass their subject into the scoped client-geometry
reuse check instead of conservatively appearing as unscoped reads. More
importantly, stylesheet recascade now compares the completed box-affecting style
against its previous value and publishes paint-only changes without dirtying
layout. `WEBSCENE_FORCE_RECASCADE_LAYOUT_INVALIDATION=1` restores the former
always-dirty behavior for certification A/B runs. Across five interleaved
strict-replay pairs, median forced-layout work fell from 72 passes / 40.5 ms to
68 passes / 30.6 ms (-24.3% CPU). Navigation medians were 900.3 ms control and
911.4 ms optimized, so live-data variance again prevents a chart-ready wall-time
claim.

The iframe attribution shows why parallel script execution is not the first lever: a
representative main frame spent 80.7 ms of its 105.5 ms hydration interval executing
application scripts, 11.8 ms reading resources, and only about 3 ms parsing/applying
CSS and laying out. The secondary frame similarly spent 17.5 ms of 22.4 ms in script.
Remote iframe documents were nevertheless still fetched synchronously before they
entered that hydration path. Starting sibling document requests concurrently while
preserving ordered single-isolate hydration improved all five interleaved strict-replay
pairs: median navigation fell from 953.4 ms to 934.4 ms (-19.0 ms, -2.0%), and cold
host-to-ready from 1,038.5 ms to 1,016.2 ms (-22.3 ms, -2.1%). A delayed native host
loader regression also requires two remote iframe document requests to overlap.

The follow-up frame-preparation path moves subresource discovery ahead of owner-thread
hydration. A delayed-loader A/B regression observes discovery at least 70 ms earlier
while an outer script is busy and verifies that the prepared external frame script still
executes. Across five interleaved strict-replay pairs with warm code cache and cold
resource cache, median iframe hydration fell from 104.6 ms to 97.5 ms (-7.1 ms,
-6.8%). Median preparation CPU was 0.5 ms, owner-thread preparation wait was 0.002 ms,
and preparation-to-consumption lead was 7.4 ms. Navigation medians were effectively
flat at 929.3 ms control and 931.7 ms optimized, so this result supports earlier I/O
overlap rather than a chart-ready wall-time claim.

Separating script compilation from execution showed that persistent-cache bookkeeping,
not V8 parsing, was the next cache-path cost. With all 129 persistent entries accepted,
V8 compilation itself took about 4.5 ms, while `compile_script` included dozens of
milliseconds of cache reads, validation, and per-hit timestamp writes. Deferring and
deduplicating those timestamp writes reduced the five-run median compile phase from
59.7 ms to 53.5 ms (-6.2 ms, -10.4%). Navigation medians favored the optimized path
in four of five interleaved pairs, but live market-data variance remains too large for a
wall-time claim.

Cooperative iframe hydration was then measured in five interleaved warm-code-cache
strict-replay pairs. It split the two frame hydrations from two monolithic tasks into
six slices with four scheduler yields. Median maximum frame-task duration fell from
65.6 ms to 59.7 ms (-5.9 ms, -9.0%), while navigation-to-ready remained flat at
872.9 ms control versus 874.3 ms optimized. A deterministic A/B regression also
verifies that a due outer timer runs after a long blocking frame script but before the
frame's deferred script and lifecycle; the control runs that timer only after `load`.
The same regression proves outer and frame stylesheets retain independent cascade
results. The residual roughly 55 ms TradingView library script is one atomic V8 call,
so further scheduler slicing cannot remove that long task without application-level
code splitting or a separate isolate/worker architecture.

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
