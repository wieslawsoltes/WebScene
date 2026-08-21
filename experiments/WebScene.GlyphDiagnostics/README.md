# Glyph-level text diagnostics

This macOS-only experiment compares the same system-UI glyph corpus through several
rendering paths:

- WebScene's production HarfBuzz/Skia text path;
- the same shaped glyphs with Skia's default font raster settings;
- the HarfBuzz-selected glyphs positioned with CoreText-backed Skia advances;
- those platform advances plus HarfBuzz's pair-kerning offsets for one-to-one runs;
- HarfBuzz OpenType shaping after injecting the missing `opsz` and `wght` axes;
- a fully managed HarfBuzz sub-font with advances supplied by the painting `SKFont`;
- direct CoreText full-run positioning invoked from managed code, with Skia painting;
- Skia glyph masks placed at direct CoreText run positions (the decisive control);
- Skia glyph masks placed at Chromium prefix positions (the oracle control);
- direct CoreText/CoreGraphics drawing;
- Chromium canvas and DOM text at the same CSS sizes and device scale, including a
  separate inherited `-webkit-font-smoothing: antialiased` DOM oracle.

It is product-neutral and intentionally contains no TradingView assets or selectors.
The corpus includes isolated glyphs, normal and bold body text, punctuation, numeric
text, and the compact timeline label that exposed the original compatibility gap.

Run it from the repository root on macOS:

```sh
dotnet run --project experiments/WebScene.GlyphDiagnostics -c Release
```

Set `WEBSCENE_CHROMIUM_EXECUTABLE` if Google Chrome is not installed in its standard
application path. Generated PNGs, native/Chromium glyph metrics, `report.json`, and a
compact `report.md` are written to `TestResults/GlyphDiagnostics` by default. Pass
`--output <directory>` to use another artifact directory. The Skia and CoreText
metric files contain glyph IDs, positions, and advances; the Chromium screenshot
provides the pixel oracle for both canvas and DOM rendering.

The comparison finds the best whole-device-pixel translation before computing pixel
error. The translation reports placement differences; the residual error compares the
glyph masks and compositing. Coverage and edge-pixel counts help distinguish a heavier
glyph mask from different antialiasing.

## Current finding

On the reference macOS run with Chrome 151, WebScene's production Skia path was
pixel-identical to Chromium canvas and DOM for all six isolated glyph cases at both 1x
and 2x. The visible difference begins only when several glyphs are positioned in a run.
The uniformly scaled HarfBuzz origins drifted by as much as 4.286 CSS pixels in the
short numeric/punctuation case, while CoreText origins stayed within 0.205 CSS pixels of
Chromium's prefix-origin proxy for that case.

Keeping WebScene's Skia glyph masks but substituting CoreText run positions reduced the
mean multi-glyph pixel MAE from 0.03391 to 0.01819 at 1x and from 0.03160 to 0.01010 at
2x. Supplying the Chromium prefix-origin proxy reduced it further to 0.01226 and
0.00136. Direct CoreText rasterization was slightly different even for isolated glyphs,
so moving glyph painting out of Skia would discard an already matching raster path.

Raw platform advances and platform advances plus reconstructed HarfBuzz kerning were
not general solutions: they helped compact numeric runs but regressed ordinary prose.
Injecting the missing `opsz` and `wght` variation axes alone produced the same pattern:
it helped the timeline and numeric cases, but increased average multi-glyph MAE from
0.03391 to 0.03901 at 1x and from 0.03160 to 0.03959 at 2x. Variation propagation is
therefore necessary state to preserve, but it is not a substitute for shaping against
the same platform-backed metrics that will be painted.

The managed HarfBuzz sub-font confirmed that public `SKFont.GetGlyphWidths` is not
equivalent to Chromium's internal strike-backed HarfBuzz metric callback. It nearly
eliminated the timeline error and improved digits, but regressed prose and increased
average MAE to 0.03710 at 1x and 0.03502 at 2x. This path should not replace production
shaping.

Directly invoking CoreText positioning from managed code produced byte-identical 1x
and 2x Skia images to the standalone Swift/CoreText control. It retains the improvement
to 0.01819 and 0.01010 without changing the native WebScene runtime or using CoreText
for rasterization. The next implementation experiment should therefore promote this
managed macOS system-font positioner behind a narrow eligibility gate and cache, while
keeping the existing HarfBuzz path for web fonts, complex or unsupported runs. It must
continue to pass both 1x and Retina pixel-oracle profiles before becoming the default.

The application that exposed the residual weight difference inherits
`-webkit-font-smoothing: antialiased` from `body`. WebScene previously discarded that
vendor-prefixed declaration and therefore painted those runs with its default smoothed
profile. Carrying the inherited token through the native scene and selecting Blink's
macOS grayscale/unhinted Skia flags reduces the Retina multi-glyph MAE against the
matching Chrome DOM oracle from 0.01408 to 0.00625. The remaining non-zero error shows
that identical public flags in SkiaSharp 2.88.9 do not completely reproduce current
Chromium's macOS A8 glyph masks; it is now isolated from font selection, shaping,
positioning, and CSS cascade.
