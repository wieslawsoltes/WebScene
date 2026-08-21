# Cross-platform font rendering plan

## Objective

Reduce WebScene text-layout and rendering differences from Chromium on macOS,
Windows, and Linux without regressing existing behaviour, complex-script support, or
rendering performance.

Skia remains the common glyph painter. Platform-specific code supplies full-run glyph
selection and positioning only where it is proven to be more browser-compatible. The
existing HarfBuzz/Skia path remains the fallback until each platform path has adequate
coverage.

The implementation must be generic. Production code and tests must not contain
TradingView-specific selectors, assets, text, or compatibility branches.

## Current findings

The macOS glyph diagnostic in `experiments/WebScene.GlyphDiagnostics` separates font
selection, shaping/positioning, and rasterization.

- WebScene's production Skia masks are pixel-identical to Chromium canvas and DOM for
  all six isolated glyph cases at both 1x and 2x.
- Differences begin when multiple glyphs are positioned into a run.
- Substituting CoreText positions while retaining Skia glyph painting reduces average
  multi-glyph pixel MAE from 0.03391 to 0.01819 at 1x and from 0.03160 to 0.01010 at
  2x.
- Invoking CoreText directly from managed code produces byte-identical Skia images to
  the standalone Swift/CoreText control. A native WebScene runtime helper is therefore
  not required for the macOS path.
- Propagating only `opsz` and `wght`, using public `SKFont.GetGlyphWidths`, or
  reconstructing kerning from isolated advances helps some compact numeric runs but
  regresses ordinary prose. These approaches must not replace production shaping.
- Direct CoreText rasterization differs slightly from Chromium even for isolated
  glyphs. Skia should continue painting the glyph masks.
- The managed CoreText positioner is now promoted into the shared production presenter
  behind a bounded macOS system-font eligibility gate. The service caches verified runs
  and font handles, uses the same positions for measurement and painting, and falls back
  per run to HarfBuzz/Skia. `WEBSCENE_TEXT_POSITIONING=harfbuzz` retains the previous path
  as a before/after and rollback control.

## Architecture

Use one shared managed shaping contract with a platform-specific position provider:

| Platform | Position provider | Painter |
| --- | --- | --- |
| macOS | CoreText | Existing Skia presenter |
| Windows | DirectWrite | Existing Skia presenter |
| Linux | Fontconfig plus correctly configured HarfBuzz/FreeType | Existing Skia presenter |

Introduce a shared abstraction usable by both the Avalonia and Uno presenters:

```csharp
internal interface ITextRunPositioner
{
    bool TryPosition(
        in TextRunRequest request,
        out PositionedGlyphRun run);
}
```

`TextRunRequest` should include:

- text and UTF cluster information;
- resolved family and typeface identity;
- size, weight, stretch, and slant;
- variable-font axes;
- direction, language, and script;
- OpenType features;
- device scale and the active Skia rasterization profile.

`PositionedGlyphRun` should include:

- glyph IDs and source clusters;
- per-glyph X/Y positions, advances, and offsets;
- natural run width;
- ascent, descent, and leading;
- enough face identity to verify that the platform shaper and Skia painter use the
  same font.

The renderer should build an `SKTextBlob` from this result and paint it through the
existing Skia canvas.

```text
Eligible text run
    -> platform positioner succeeds and glyph identity is verified
        -> platform positions + Skia painting
    -> otherwise
        -> existing HarfBuzz/Skia path
```

Do not introduce another Skia build or replace the SkiaSharp supplied by the host
framework.

## macOS implementation

Promote the validated managed CoreText positioner into the shared managed presenter.

Initial scope:

1. Resolve `system-ui`, `-apple-system`, `BlinkMacSystemFont`, and the Apple system
   font to the appropriate `NSFont`/CoreText face.
2. Shape the complete run through CoreText.
3. Compare the returned glyph IDs with the glyph IDs accepted by the active Skia
   typeface.
4. Use CoreText positions only when the face and glyph IDs agree.
5. Paint the positioned run with the existing Skia font and rasterization profile.
6. Cache platform font handles, immutable positioning state, and shaped runs.

Begin with single-face system-font Latin runs. Fall back to the current HarfBuzz path
for downloaded web fonts, font fallback, emoji, complex scripts, unsupported features,
or any glyph-identity mismatch.

## Windows implementation

Build a Windows glyph diagnostic before changing production behaviour. It should
compare Chromium, the current WebScene path, DirectWrite positions with Skia masks,
and the normal framework text control at 100%, 125%, 150%, and 200% display scaling.

The Windows positioner should use DirectWrite APIs such as `IDWriteFactory`,
`IDWriteTextAnalyzer`, or `IDWriteTextLayout` and extract full `DWRITE_GLYPH_RUN`
glyph IDs, advances, and offsets. Those glyphs should still be painted by Skia.

Correct generic-family resolution as part of this work:

- `system-ui` should resolve through the Windows UI font family, normally Segoe UI or
  the appropriate installed system variant;
- `sans-serif` should remain a separate generic mapping rather than being conflated
  with `system-ui`;
- `-apple-system` and `BlinkMacSystemFont` should use normal fallback semantics on
  Windows rather than forcing an Apple-specific identity.

Enable DirectWrite positioning only after the Windows pixel oracle demonstrates an
overall improvement without prose, scaling, or weight regressions.

## Linux implementation

Start with font identity and configuration rather than assuming Linux needs a separate
text engine.

1. Resolve CSS generic and named families with Fontconfig.
2. Record the exact font file, collection index, synthetic style state, and variation
   coordinates.
3. Construct the Skia typeface and HarfBuzz face from the same resolved font data.
4. Compare Chromium, WebScene, and native FreeType/HarfBuzz positions at 1x and 2x.
5. If positions still differ, install FreeType-backed HarfBuzz font functions using
   the same face and size used for Skia painting.

Fontconfig must resolve `system-ui`, `sans-serif`, `serif`, `monospace`, and fallback
families according to the host environment. Do not guess platform filenames or reduce
all generic families to one hard-coded face.

## Measurement and layout

Correct glyph painting alone is insufficient if intrinsic sizing and line breaking use
different advances.

After each platform positioner is validated for painting:

1. use the same positioned run's natural width for text measurement;
2. make line breaking, intrinsic sizing, and flex/grid layout consume that width;
3. preserve clusters and style-run boundaries so spaces, ligatures, and mixed-weight
   text are not lost or merged incorrectly;
4. ensure measurement and painting select the same fallback faces;
5. remove whole-run calibration where the platform positioner is authoritative.

Any managed/native measurement boundary must batch work. Do not introduce a callback
from the native layout engine to managed code for every individual glyph.

## Eligibility and fallback gates

Initially use a platform positioner only when all of the following are true:

- the run uses a supported system font;
- all glyphs come from one verified face;
- the platform and Skia glyph IDs agree;
- direction, script, language, and OpenType features are covered by tests;
- variation coordinates and synthetic style state are known;
- the path is supported at the active device scale.

Fall back to the existing HarfBuzz/Skia path for:

- downloaded web fonts;
- unsupported or missing platform fonts;
- emoji and multi-face fallback runs;
- unverified complex or bidirectional scripts;
- glyph-identity or metric validation failures;
- unsupported platform APIs.

Fallback must be per run and must never fail page rendering.

## Test strategy

Maintain a generic corpus that includes:

- isolated glyphs such as `A`, `a`, `m`, `i`, `1`, and `.`;
- normal, medium, semibold, and bold prose;
- compact numeric and punctuation runs;
- spaces across style boundaries;
- mixed-weight and mixed-style inline text;
- kerning and ligature pairs such as `AV`, `To`, and `ffi`;
- line wrapping and unbreakable tokens;
- combining marks, RTL text, and representative complex scripts;
- emoji, missing glyphs, and multi-face fallback;
- named fonts, generic families, and downloaded web fonts.

For every platform, capture:

- resolved face identity and variation coordinates;
- glyph IDs and clusters;
- per-glyph positions, advances, and offsets;
- total width and font metrics;
- device scale, canvas transform, and rasterization settings;
- pixel comparisons after a bounded whole-device-pixel translation used only to
  separate placement error from mask error.

Required scale coverage:

- macOS: 1x and 2x;
- Windows: 100%, 125%, 150%, and 200%;
- Linux: 1x and 2x, plus fractional scaling where the supported desktop stack exposes
  it deterministically.

Use deterministic local fixtures and platform-specific Chromium pixel oracles. Web
Platform Tests should cover layout invariants, baseline alignment, wrapping, style-run
spacing, and generic-family behaviour. Pixel-oracle tests should cover platform font
appearance and glyph positioning, which cannot be made fully portable across operating
systems and installed font versions.

## Performance requirements

- Cache platform font handles and immutable shaping state by resolved face, size,
  style, axes, features, language, direction, and relevant device-scale properties.
- Cache shaped runs only with bounded memory and explicit invalidation when font or
  device state changes.
- Prefer complete-run or batched shaping APIs; never cross an interop boundary once per
  glyph.
- Avoid creating attributed strings, DirectWrite factories, Fontconfig configurations,
  or FreeType faces in the draw loop.
- Keep Skia text blobs reusable where scene lifetime permits.
- Benchmark cold shaping, warm shaping, allocations, frame time, and cache memory before
  enabling a new path by default.

## Rollout sequence

### Phase 1: shared contract and macOS

- [x] Add the shared request/result contract and platform-positioner selection.
- [x] Move the validated managed CoreText experiment into production behind a macOS
  system-font eligibility gate.
- [x] Add bounded cache and fallback coverage.
- [x] Re-run the 1x and 2x Chromium pixel-oracle profiles through the production
  service path; its images are byte-identical to the validated managed CoreText
  candidate and retain the measured Chromium improvement.

### Phase 2: Windows

- Correct `system-ui` resolution without changing `sans-serif` semantics.
- Add the DirectWrite diagnostic and scaling matrix.
- Implement and enable DirectWrite positioning only for demonstrated wins.

### Phase 3: Linux

- Integrate exact Fontconfig face resolution and identity reporting.
- Ensure Skia and HarfBuzz use the same file, collection index, and axes.
- Add FreeType-backed metrics only if the Linux oracle demonstrates a remaining gap.

### Phase 4: measurement and broader scripts

- Unify intrinsic measurement and line breaking with the validated positioning path.
- Expand eligibility to fallback, emoji, complex scripts, and bidirectional text one
  category at a time.
- Remove superseded calibration logic only after equivalent tests exist.

## Acceptance criteria

A platform path can become the default only when:

- isolated glyph masks do not regress;
- average multi-glyph pixel error improves across the full corpus rather than only one
  compact case;
- prose, numeric, punctuation, weight, and scale cases show no material regression;
- measurement and painting agree on natural width and clusters;
- unsupported runs fall back safely;
- Avalonia and Uno use equivalent behaviour through the shared Skia layer;
- existing unit, integration, Web Platform, and performance suites pass;
- cold-start cost, warm shaping time, draw time, allocation rate, and cache memory stay
  within their established budgets.

## Approaches explicitly rejected

Do not use the following as final compatibility fixes:

- uniformly scaling an entire shaped run;
- snapping every glyph origin to a device pixel;
- drawing one Unicode character at a time;
- reconstructing kerning from isolated character widths;
- replacing matching Skia glyph masks with CoreText or DirectWrite rasterization;
- shipping a custom SkiaSharp or a second Skia build;
- enabling a platform path globally before font fallback and unsupported-run gates
  exist.
