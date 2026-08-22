# Canvas 2D compatibility ledger

This ledger prevents a bounded chart-workload profile from being described as complete
Canvas 2D support. Its source of truth is WPT revision
`2c705104a295c48053eeddf7fe0170d790a4e853`, especially `html/canvas` and the Canvas
IDL in `interfaces/html.idl`.

## Denominator

The pinned WPT tree contains 4,662 files below `html/canvas`, including 3,939 generated
`2d.*` artifacts. The generator YAML contains 895 named cases across path objects,
styles, text, pixels, shadows, images, compositing, transforms, filters, state, and
canvas lifecycle. The repository intentionally pins only promoted tests and their exact
resources; passing the component profile is not evidence that the other cases pass.

## Current operation audit

| IDL surface | Status | Semantic evidence or explicit gap |
| --- | --- | --- |
| state and transforms | partial | save/restore and the six 2D transform operations are retained; `reset()` and `isContextLost()` are absent |
| compositing and alpha | partial | common Skia blend modes and `globalAlpha` replay; complete WPT edge semantics are not certified |
| fill/stroke colors and line styles | partial | solid CSS colors, width/cap/join/miter/dash; gradient and pattern paint replay is absent |
| rectangle drawing | supported slice | clear/fill/stroke rectangles are retained and used by chart regressions |
| current path | partial | begin/close/move/line/quadratic/cubic/arc/arcTo/rect/ellipse/roundRect plus fill/stroke/clip; full Path2D parity is absent |
| `ellipse()` | promoted | unchanged `2d.path.ellipse.basics.html`, packet regression, renderer geometry tests, and selected-trendline visual pass |
| `roundRect()` | supported slice | number, DOMPointInit-shaped, and one-to-four radius sequences, negative-size corner flipping, overlap scaling, RangeError validation, and retained cubic replay; pixel-readback WPT remains blocked |
| `Path2D` | partial | SVG-string construction, `arc()`, and transformed `addPath()`; the remaining CanvasPath methods are absent |
| path hit testing | partial | current flattened paths support nonzero/evenodd fill hits and width-based stroke hits; Path2D, exact dash/cap/join, and unflattened curve geometry remain absent |
| text | partial | fill/stroke/measure with host shaping and common alignment/baselines; complete font/text-style IDL is absent |
| images | partial | retained canvas/image sources used by the component; complete image/video/ImageBitmap overload and tainting semantics are not certified |
| `ImageData` and pixels | partial/absent | object shape and zeroed storage only; synchronous raster readback and `putImageData()` are absent |
| gradients and patterns | absent | gradient objects are placeholders; stops, paint replay, patterns, and pattern transforms are absent |
| shadows | partial | common shadow state replays; full WPT geometry/compositing coverage is not certified |
| filters, focus, context attributes | absent | `filter`, `drawFocusIfNeeded()`, and `getContextAttributes()` are not implemented |
| OffscreenCanvas / workers | out of scope | requires a separate execution, ownership, and presentation surface |

## Promotion rule

An operation moves to “supported” only when it has (1) native binding/packet coverage,
(2) renderer or synchronous semantic coverage as applicable, and (3) at least one
unchanged pinned WPT covering its normative behavior. Pixel-producing operations also
require a non-blank visual/reftest control. Missing harness support is recorded as a
blocker; it is never counted as a pass.
