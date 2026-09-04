# Variable-font weights on the existing rendering stack

## Qualification status

This implementation retains Avalonia 11.3.4, SkiaSharp 2.88.9 and its existing
HarfBuzz dependency. Uno continues using its existing dependency versions and
shares the instancer and registry source. No public host API or native WebScene
ABI is added by font instantiation.

**Instancing is enabled by default.** No environment variable is required.
Set `WEBSCENE_VARIABLE_FONT_INSTANCING=0` to opt out and retain the previous
variable font default-face behavior. Only an explicit `0` disables instancing;
`1` or an unset/empty value enables it. Read once at startup; restart the process
after changing this diagnostic switch. The pending qualification checks below
remain outstanding; default-on is not a claim that they have passed.

## Supported behavior

WebScene retains separate `@font-face` registrations, including CSS weight
descriptors and ranges. Multiple static faces use CSS weight matching, including
the special 400–500 search order. For a variable face, the requested CSS weight
is clamped to the selected declaration and then its `wght` axis bounds. Other
axes are pinned to their defaults. Static and system fonts do not undergo
conversion.

The first uncached weight is synchronously instantiated by the HarfBuzz subset
functions already in the shipped native library. This is **not text subsetting**:
all glyph IDs, shaping features, metrics, hinting and necessary tables are kept.
WebScene decodes WOFF/WOFF2 containers to SFNT before registering them with
Skia: the existing Windows/Linux Skia builds cannot consistently decode WOFF2
themselves. The shared managed decoder uses .NET's existing Brotli/zlib APIs,
preserves all tables and glyphs, and reconstructs glyf/loca and hmtx transforms.
SFNT/TTF input and system fonts are unchanged. The resulting static font is
loaded by the existing Skia renderer.

Container decoding happens once per live source-byte cache entry, not per
weight or frame. Input, expanded table payloads and reconstructed font output
are each limited to 64 MiB; table count is limited to 256. Malformed containers,
unknown transform versions and WOFF2 font collections fail registration and
use the normal font fallback. This adds no package dependency or native ABI.

DOM layout/drawing and canvas measurement/drawing use the same registry and
instance. Font registration invalidates native text measurements; render-pass
shapers are scoped to the current compilation. Previously painted canvas pixels
are not retroactively redrawn when a font arrives.

General `font-variation-settings`, optical sizing, selection of variable width or
italic axes, and replacement of platform text rendering are not implemented.

## Cost, ownership and fallback

Instances are keyed by source-byte SHA-256 and normalized/clamped weight
coordinate (all other coordinates are fixed at defaults). Concurrent requests
are coalesced. Warm lookups reuse the face without conversion or global cache
locking. Cold conversions are serialized to enforce process-wide limits.

Document registries and retained renderers hold leases. Generated faces are
released after the last owning registry/renderer releases them; a face in use
is never evicted. Limits are 64 generated variants per source, 256 process-wide,
and 64 MiB of generated font data. The byte counter measures generated font
payloads, not Skia's total native heap usage. No persistent cache is used.

Unsupported conversion, invalid output, missing native functions or exhausted
limits return the previous default face. Failed coordinates are remembered;
unavailable functions and capacity exhaustion disable new conversions for that
source's remaining lifetime. Diagnostics are deduplicated for those attempts
and written to the existing process error stream with the
`[WebScene font instancing]` prefix. This is not a new JavaScript exception or
public host diagnostics API. Internal counters expose attempts, hits, failures,
conversion duration, live instance count and retained payload bytes.

## Tests and shipping gates

- `VariableWebFontTests`: independent FontTools reference outlines and advances
  for weights 400/550/700; TTF and WOFF2; raster ink; clamping, static selection,
  concurrency, lease/disposal, disabled mode, malformed input, injected failures,
  missing exports and all three retention limits. Canvas command replay is
  compared with static-reference rasters and verified to retain its font lease.
  Applicable instancer/registry/raster tests are also compiled into Uno's tests;
  this does not add downloaded `@font-face` transport to the Uno resource loader.
- `css-variable-font-weight.html`: required WPT-style DOM and canvas widths,
  dynamic weights, separate static faces, and font arrival after measurement.
- `NativeWebFontCacheTests`: real-engine cold/warm stylesheet loading in separate
  documents, instance reuse and registry cleanup.
- Native packaging scripts enable the feature for the required contracts and
  focused managed regressions against the extracted package on all three RIDs.

Local macOS arm64 qualification on 2026-09-04 measured the actual release-page
Roboto: cold conversion p95 **4.6 ms** over 30 fresh registries; warmed text
measurement ratio **0.974** (enabled/disabled, alternating batches). No further
conversions occurred in warmed loops, no failures occurred, and live instances
and bytes returned to baseline after disposal. This is **not** an interactive
chart frame-time benchmark or a claim of a speedup.

Local suites passed: 148 required WPT-style documents / 508 assertions, the full
native test executable, 239 Avalonia backend tests on each of .NET 8 and .NET 10,
and 38 Uno backend tests. Packaging scripts are wired for cross-RID qualification;
these local results are not Windows/Linux package execution evidence.

The refreshed Chrome comparison at 800 × 1100 CSS pixels matched title-row and
list-row geometry (first heading width differed by 0.12 px). WebScene's blank
YouTube preview regions were outside that font qualification; they now have a
separate [thumbnail/external-browser fallback](embedded-media-fallback.md). The
actual release-page Roboto TTF, Roboto Mono TTF and Manrope WOFF2 all instantiated
at 400/550/700 with the existing native library.

Reproduce the focused performance probe:

```sh
dotnet run --project benchmarks/WebScene.NativeEngine.Benchmarks -c Release -- \
  probe variable-font /absolute/path/to/variable-font.ttf
```

Before a package release, require packaged macOS arm64 / Windows x64 / Linux
x64 execution, the applicable Uno tests, a same-viewport Chrome release-notes
comparison, and warmed TradingView crosshair and colour-picker drag comparisons
against the restored baseline. Require zero new warm conversions, no reproducible
steady-state CPU/frame-time regression above 5%, cold p95 below 50 ms, and live
counts/bytes returning to baseline after repeated document disposal. Cross-RID
runtime and interactive performance gates have **not yet been completed**.
