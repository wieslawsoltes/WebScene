# Monaco compatibility report

Status: Monaco Editor 0.56.0 now boots without application shims in the native
WebScene runtime and is integrated into an actual Avalonia `NativeWebSceneView`
sample. The source-built runtime passes the new Monaco prerequisite contract.

This work targets the native V8/DOM/canvas path. It does not use the managed
ClearScript backend and does not modify Monaco.

## What was incompatible

The released `11.3.4-alpha.6` native runtime failed before Monaco could render:

1. `queueMicrotask` was absent.
2. `customElements` was absent.
3. `TextDecoder` was absent.
4. The outer document did not deliver browser-ordered `DOMContentLoaded` and
   `load` events.
5. Monaco's multi-line view renderer needed `Element.insertAdjacentHTML()` and
   `Element.replaceWith()`. Without their sibling/identity semantics,
   `_ViewLayerRenderer` reached a null `previousSibling`.
6. `offsetWidth` incorrectly returned the containing block width for an
   auto-sized inline element. Monaco measures 256 repeated digits and divides
   that inline width to size its gutter; the incorrect result expanded the
   line-number column to 977 px.
7. An unhandled Enter key on a native `<textarea>` did not perform the browser
   default `insertLineBreak` mutation, so Monaco received the key event but its
   hidden textarea never published the corresponding input change.
8. Monaco's folding controls use its bundled Codicon web font. The selective
   sample bundle initially omitted Monaco's Codicon registration, and the
   native Avalonia text shaper only resolved installed system fonts, producing
   a missing-glyph box for the collapsed chevron.
9. Monaco identifies editor hit targets by walking live-node constants and
   resolving a character caret from viewport coordinates. Native nodes exposed
   `Node.ELEMENT_NODE` statically but not through live node instances, and
   `Document.caretRangeFromPoint()` was absent, so text clicks were reported as
   unknown targets.
10. `window.scrollX`, `scrollY`, `pageXOffset`, and `pageYOffset` were
    undefined. Monaco subtracts the current window offsets from mouse page
    coordinates, turning otherwise valid native pointer coordinates into
    `NaN`. Monaco consequently did not prevent the pointer default, its hidden
    textarea blurred, and click, drag-selection, and text entry appeared dead.

The earlier single-line screenshot was therefore not evidence of Monaco
compatibility. Its visible source was a temporary text mirror and input used a
temporary keydown bridge. Neither exists in the new sample.

## Native runtime changes

The native runtime now supplies:

- Browser-ordered `loading` → `interactive` → `DOMContentLoaded` → `complete`
  → `load` document lifecycle.
- `queueMicrotask`.
- UTF-8 `TextEncoder`, including `encodeInto`.
- UTF-8 and UTF-16LE/BE `TextDecoder` over `ArrayBuffer` and typed-array views.
- A `customElements` registry with `define`, `get`, `getName`, `whenDefined`,
  and `upgrade`.
- Outer-realm `MutationObserver`, `ResizeObserver`, and Monaco-used event
  constructors.
- `Element.insertAdjacentHTML()` for all four positions, including multi-root
  fragments.
- `Element.replaceWith()` with node identity, detachment, sibling ordering,
  recascade, resource activation, and scene invalidation.
- Intrinsic native text shaping for auto-sized inline `offsetWidth`, while
  preserving layout-box semantics for SVG and non-inline elements.
- Browser-compatible textarea Enter defaults with `beforeinput` and `input`
  events whose `inputType` is `insertLineBreak`.
- Downloadable `@font-face` registration in the native Avalonia resource
  bridge, including relative and data-URI sources, with the resulting
  typefaces shared by native measurement and retained-scene painting.
- Standard Node constants inherited by live element and text wrappers.
- Finite window scroll offsets and their legacy aliases in outer and frame
  realms.
- `Document.caretRangeFromPoint()` with text-node ancestry and a font-measured
  UTF-16 caret offset.

These capabilities are installed by the native runtime in outer and frame
realms. Applications do not patch globals and the Monaco bundle remains
unmodified.

## Certification

`contracts/monaco-editor-runtime-primitives.html` is a candidate
WebPlatformSubset contract. It certifies:

- encoding and custom-element primitives;
- Monaco-shaped multi-root line insertion and replacement;
- complete `previousSibling` chains and replacement identity;
- live-node constants, finite window scroll coordinates, and character-level
  caret hit testing;
- monospace style, fixed line-height geometry, and observable token color;
- intrinsic repeated-digit font measurement used by Monaco's gutter sizing;
- microtask ordering.

Baseline result with `11.3.4-alpha.6`:

```text
HARNESS-ERROR: ReferenceError: queueMicrotask is not defined
0/1 documents; 0 subtests reached
```

Source-built result:

```text
PASS
1/1 documents; 4/4 subtests
```

The native C++ suite also directly covers outer-document lifecycle ordering,
the editor browser primitives, Monaco's view-line DOM mutation sequence,
intrinsic inline font measurement, and textarea Enter/input behavior.
The runtime build script now copies ICU data beside the build artifact and runs
that suite automatically, so a package cannot be produced after a native test
failure.

## Sample

`samples/NativeMonacoEditor` is a real Avalonia desktop application. Its only
web dependency is Monaco 0.56.0 from npm, bundled unchanged with esbuild. It
demonstrates:

- a multi-line editable JavaScript model;
- JavaScript token highlighting;
- line numbers and an explicit monospace font;
- fixed 20 px line height;
- code folding and folding highlight;
- bracket matching;
- focus and native Avalonia input routing;
- resize-driven editor layout.

The page has no compatibility shim, visual text mirror, or document-level
keyboard bridge.

The same editor is also available from the `Monaco (native)` tab in
`samples/JavaScriptPlayground`. The tab lazy-loads `NativeWebSceneView` and links
the standalone sample's generated web assets, so it neither starts the managed
ClearScript DOM path when launched with `--monaco` nor carries a second Monaco
bundle.

`samples/NativeMonacoEditor.Headless` loads the same unchanged page and Monaco
bundle in Avalonia Headless with Skia and renders the retained native scene to
PNG. Its integration gate sends a real top-level Avalonia click, types at the
resolved Monaco caret, drags the mouse to create a non-empty selection, types
to replace that selection, and captures the visible selected range. It then
inserts a comment plus Enter through `NativeSceneSurface` and captures the
retokenized nine-line result. A final frame invokes Monaco's own fold command
and captures the collapsed function body with Monaco's Codicon chevron. The
gate fails unless the textarea retains focus, Monaco reports a content-text
mouse target, the model changes, the drag selection is non-empty, replacement
typing succeeds, and both the registered native typeface and fold control's
computed font family resolve as `codicon`. The runtime probe reports a 42 px
line-number column and an 88 px content origin, replacing the incorrect
977/1023 px geometry.

Build and run:

```sh
./scripts/build-native-engine-runtime.sh \
  --rid osx-arm64 \
  --output artifacts/native-monaco-runtime \
  --package-version 11.3.4-monaco.1

dotnet run --project samples/NativeMonacoEditor -c Release -- \
  --native-library \
  "$PWD/artifacts/native-engine-runtime-build/osx-arm64/libwebscene_native_engine.dylib"

dotnet run --project samples/JavaScriptPlayground -c Release -- \
  --monaco \
  --native-library \
  "$PWD/artifacts/native-engine-runtime-build/osx-arm64/libwebscene_native_engine.dylib"

dotnet run --project samples/NativeMonacoEditor.Headless -c Release -- \
  --native-library \
  "$PWD/artifacts/native-engine-runtime-build/osx-arm64/libwebscene_native_engine.dylib" \
  --output "$PWD/artifacts/monaco-headless"
```

## Remaining compatibility boundary

The sample intentionally certifies Monaco's editor core and local JavaScript
tokenization. The following are not yet blanket-certified:

- Web Worker-based TypeScript/JavaScript language services, diagnostics,
  completion, and hover.
- IME/composition, dead-key, CJK, and accessibility/automation behavior.
- Native clipboard copy/cut/paste across all platforms.
- Very large models, minimap, diff editor, multi-cursor, and complex widgets.
- Extensions that require unsupported browser APIs beyond the certified
  component profile.

These are follow-on certification areas, not reasons to patch Monaco. Each
should be closed by adding a small native-runtime contract first, implementing
the missing browser behavior, and then adding an end-to-end Monaco scenario.

## Relevant files

- `experiments/WebScene.NativeEngine.Probe/native/webscene_v8_runtime.cpp`
- `experiments/WebScene.NativeEngine.Probe/tests/native_v8_runtime_tests.cpp`
- `tests/WebPlatformSubset/contracts/monaco-editor-runtime-primitives.html`
- `tests/WebPlatformSubset/webscene-component-profile.json`
- `samples/NativeMonacoEditor`
- `samples/NativeMonacoEditor.Headless`
- `samples/JavaScriptPlayground`
- `scripts/build-native-engine-runtime.sh`
