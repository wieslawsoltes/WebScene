# Table View header follow-up

The September 4 live TradingView probe found `Date · 1h` in the DOM with
nonzero geometry, but no painted label or borders. Chrome reported sticky
positioning for `thead` (z-index 8) and its rowspan date cell (z-index 9).
WebScene reduced both to static. Its positive-z traversal then failed to open
a paint scope when entering the elevated static ancestor, dropping the nested
cell. Separately, logical padding shorthands split `calc()` at internal spaces,
and unsupported container blocks leaked all breakpoint declarations into the
unconditional cascade.

## Red-to-green reductions

| Contract | Baseline failure | Coverage |
| --- | --- | --- |
| `stacking-static-nested-z-index.html` | Zero blue border pixels | Nested elevated traversal must not drop descendant paint |
| `css-sticky-table-header.html` | Static computed position; header scrolled to -120 | Sticky header/date column geometry, rowspan alignment, hit testing, scroll restoration |
| `css-logical-padding-calc.html` | Padding computed as 0 | Stylesheet and generated-box calculation tokens; inherited variable updates |
| `css-container-rule-not-unconditional.html` | 100px instead of 20px | A conditional breakpoint must not override unconditional spacing |
| `element-scroll-to-options.html` | `scrollTo is not a function` | Numeric/options overloads, preserved axis, aliases, bounds, nonfinite values, scroll events |

The testharness reductions pass in Chromium. These are local WPT-style required
contracts, not upstream WPT contributions. The static-z case also checks actual
painted pixels rather than relying only on DOM geometry.

The live back-to-top callback animates the virtualizer with `scrollToOffset`,
which uses `Element.scrollTo`. An instance-only diagnostic implementation of
that missing method restored scrolling to zero. The native API replaces that
probe. Native smooth interpolation is not implemented; TradingView supplies
its own animation steps.

`--headless-proof --table-view-proof` now records the date header geometry and
hit result, checks that large wheel movements advance the virtualized table
without blanking its rows, submits a native pointer click to the arrow, and
requires scrollTop to return to zero. It saves before/after images as well.

The final local sample proof passed with wheel offsets 10000, 17937, and 24169,
17 visible rows at each step, a hit-testable date header, and scrollTop 0 after
the native back-to-top click. The full native test executable also passed.

## Remaining limits

Container queries are still unsupported. Their bodies are now ignored rather
than applied unconditionally, allowing the application's media-query fallback.
That fallback uses viewport width, not container width, so header insets can
still differ from modern Chrome when a sidebar reduces the chart area across a
breakpoint. Do not claim complete container-query or pixel parity from these
tests. Sticky coverage is currently bounded to the horizontal-writing,
untransformed table/block cases; more general margin, writing-mode, and nested
transformed-container cases require additional coverage.
