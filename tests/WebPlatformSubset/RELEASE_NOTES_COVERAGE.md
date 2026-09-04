# Release notes styling — StackWich #1298

## Status

The cache and layout causes below are fixed in WebScene. Variable Roboto weight
selection is implemented using the **existing** HarfBuzz and SkiaSharp 2.88.9
stack, without the experimental dependency upgrade. This remains opt-in until
cross-platform and interactive performance gates pass; the issue is therefore
**not yet fully qualified for default-on shipping**. See
[variable font weights](../../docs/variable-font-weights.md) for behavior, limits,
rollback and qualification status. No page-specific CSS or substitute system
font is used.

YouTube playback/thumbnail behavior belongs to #1296, not this styling gate.

## Root causes and coverage

| Cause | Fix and regression |
|---|---|
| `rem` used a 14px body/legacy baseline | `css-root-rem-body-font.html`: document root starts at 16px; updates track the root. Previously committed, now required. |
| Vertical percentage padding used height | `css-percentage-vertical-padding-aspect-ratio.html`: width-based ratio and absolute child after resize. Previously committed, now required. |
| Native cache hits bypassed font registration in the managed transport loader | `NativeWebFontCacheTests.CachedAndInlineStylesheetsRegisterFontsInEveryDocument`: two real engines share a cache, only one stylesheet HTTP request occurs, and both engines register external and inline font families. |
| Line-height resolved before the final font size | `css-unitless-line-height-inheritance.html`: unitless inheritance, values above four, explicit small lengths, em inheritance as a length, and dynamic font-size changes. |
| An important/inline margin longhand blocked unrelated sides | `css-margin-longhand-priority.html`: auto centering, shorthand/longhand precedence, and inline important priority. |
| Legacy `grid-row-gap` / `grid-column-gap` were ignored on flex | `css-grid-gap-alias-flex.html`: horizontal spacing, wrapping, and CSSOM alias updates. |
| Adjacent rich-text list and preview margins were added | `css-adjacent-block-margin-collapse.html`: positive, negative and mixed margins, with a noncollapsing flex control. |
| Reported SVG marker color mismatch | `SvgPictureRenderingTests.ReleaseNoteSvgBackgroundPreservesExactSrgbFill`: real Skia SVG-background rasterization, exact green/purple interior pixels and unchanged surrounding background. |
| Variable Roboto always used its default weight | `VariableWebFontTests`: independent static references at 400/550/700, TTF/WOFF2 outlines and shaping/raster checks, bounded shared instances and disposal. `css-variable-font-weight.html`: DOM/canvas widths, dynamic weight selection, separate static faces, and late font registration. |

All seven HTML contracts are in the **required** native compatibility profile.
The native package scripts additionally run the real-engine font-cache and SVG
raster tests against the packaged library on each released RID. The Linux build
image installs DejaVu fonts for the platform-font fixture. Font cache metric
tests are serialized because they inspect process-global counters.

The margin implementation covers adjacent nonempty normal-flow block siblings.
It does not claim full parent/child or empty-block-chain collapsing support.
Pseudo-element `getComputedStyle` is not qualified by the line-height contract.

## Evidence / verification

Compare `https://www.sandwichtrading.com/app/release-modal-dark` at an 800 CSS-pixel
viewport in Chrome and the normal sample's `--headless-proof --document-proof`
mode. For equal widths, hide Chrome's reserved scrollbar gutter in the diagnostic
page; WebScene uses overlay scrollbars. Do not compare differently scaled window
screenshots or use the stale registered sample `.app`.

The new layout tests failed on the pre-fix native library and passed in Chrome.
Local macOS verification on 2026-09-04 passed 148 required documents / 508
subtests and the full native test executable. Focused managed tests run on
both .NET 8 and .NET 10. Cross-RID jobs are wired but were not run locally.
The initial live font evidence showed zero registered fonts on a warm load and
four on a cold load; both now register four. The exact current marker colors
match Chrome: green `#9de640` (157,230,64), purple `#d19afc` (209,154,252), on
background `#0b181a`. No speculative opacity/color compensation was added.

Useful commands (substitute the freshly built or packaged library):

```sh
WEBSCENE_VARIABLE_FONT_INSTANCING=1 dotnet run --project tests/WebPlatformSubset/runner -c Release -- \
  --selection required --native-library "$WEBSCENE_TEST_NATIVE_LIBRARY" --output /tmp/release-notes-wpt
WEBSCENE_VARIABLE_FONT_INSTANCING=1 dotnet test tests/WebScene.Backend.Avalonia.Tests -c Release -f net10.0 \
  --filter 'FullyQualifiedName~NativeWebFontCacheTests|FullyQualifiedName~VariableWebFontTests|FullyQualifiedName~SvgPictureRenderingTests'
```

The stylesheet-consumed callback is an append-only, `struct_size`-guarded engine
option. It runs on the worker before style/layout regardless of resource-cache
origin. The managed host registers fonts there rather than treating font loading
as a side effect of HTTP transport. Existing hosts with smaller option structs
retain their previous ABI layout and v3 resource callbacks.

The separate variable-font implementation uses decoded Skia font tables and
HarfBuzz static instantiation; it does not require a new native WebScene ABI.
