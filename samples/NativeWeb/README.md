# Foco Native Web

A first working Native Web vertical slice: HTML/CSS compiled to C++, C++ application
logic, WebScene native DOM/layout/Canvas, and Foco Cocoa/Skia Graphite/Metal output.
The application contains no V8 or HTML/CSS/selector parser. Those parsers are linked
only into the build-time `webscene-uic` executable.

## Build and run

Prerequisites: macOS, Xcode command-line SDKs, CMake, Ninja, Cargo, Python 3, and a
FocoUI checkout with its existing `.deps/skia` native Graphite build. The helper uses
Homebrew LLVM when available and explicitly supplies the Xcode SDK. It does not
modify the Foco checkout or download a replacement graphics stack.

From the WebScene repository root:

```sh
python3 samples/NativeWeb/build.py --foco /path/to/FocoUI --run
```

This builds the compiler, compiles `Main.html` and `Main.css`, runs the native and
compiler contracts, verifies system-only application dependencies, checks for
V8/parser symbols/assets, captures the rendered application, and tests a relocated
bundle. Outputs live in `artifacts/native-web-foco/`:

- `FocoNativeWeb.app`: launchable application; its only custom resource is `about.txt`.
- `native-web.png`: actual Foco compositor capture.
- `verification.json`: measurements and verification results.
- `build.log`: configure, compilation and test output.

Foco needs the accompanying embedded-focus navigation hook and renderer change
that reads WebScene's current
seven-field DOM text record (including font smoothing). A checkout with the older
six-field reader renders the smoothing field instead of the text.

For just the compiler/runtime contracts, without Foco or Skia:

```sh
python3 samples/NativeWeb/build.py
```

For optional comparison against a V8-enabled WebScene reference engine **built from
the same WebScene revision**, include `--reference-engine /path/to/libwebscene_native_engine.dylib`.
Its ICU/bootstrap assets must be beside that library. V8 is used only by the separate
reference test process and is never linked into or packaged with the application.

## Authoring

Edit `Main.html` and `Main.css`, then rerun the build. CMake depfiles track external
stylesheets. `app.hpp` contains native state, handlers and drawing; `main.mm` supplies
the macOS application bootstrap and resource loading. These files form the initial
copyable application template; a unified `foco new` command remains subsequent work.

Generated code constructs live WebScene DOM nodes and installs selectors plus typed
style-setting functions. Lengths and colors are converted at build time. Selectors
are parsed by WebScene's existing Servo frontend, HTML by html5ever and stylesheet
syntax by cssparser. Dynamic selector matching, cascade, layout and rendering remain
native runtime work. Unsupported authoring fails compilation; nothing falls back to
JavaScript. Native events bubble; subscriptions own callback registrations.

The C++ `document` API supports creation, text/attribute/class mutation, removal,
named lookup, subscription/disposal, focus/Tab/button activation, hit testing and
Canvas rectangle drawing. Documents enforce creating-thread access. Applications
must dispatch external work to that thread and call the host's `refresh()` after
mutations outside host input handling. The host does not request continuous idle
frames. Node IDs are document-local, are not recycled, and become invalid on removal.

The Foco adapter uses one control as a scene carrier for the entire native document.
HTML elements are not Foco controls. A wider extraction of Foco's host from its control
assembly is deliberately deferred; this slice reuses its existing application shell.

## Initial supported compiler profile

- Structural HTML: body, main, section, div, span, p, h1–h3, button, canvas, ul/li,
  header/footer/nav. Head metadata and local stylesheet links are build-time inputs.
- Selectors: tag/universal, ID, classes, compound selectors, descendant/child
  combinators, selector lists, `:focus`, `:hover`.
- Cascade: specificity, source order, inline styles and `!important`.
- Responsive conditions: nested `@media (min-width: Npx)` / `(max-width: Npx)`.
- Styles: block/inline/inline-block/flex/none, basic flex direction/alignment/growth,
  size and positional lengths, margin/padding/gap/radii, box sizing, color,
  background color, pixel font sizes, numeric font weight and opacity.
- Canvas: clear and fill rectangles using the existing native display-list protocol.
- Resources: native app bundle resources, demonstrated by C++ loading `about.txt`.
  Image/font URL compilation and general web resource loading are not implemented.

This is not full HTML/CSS parity or a completed SDK. Grid, CSS variables, animation,
advanced selectors, arbitrary CSS functions, editable text/IME, DOM accessibility
projection and general Canvas/GPU/media native APIs are not exposed by this initial
profile. Existing engines retain their broader capabilities. Optional JS, language
projections, hot design and the proposed typed HTML data contexts/bindings are future
work, recorded in `docs/design/native-web.md`.

## Lifetime and future hot design

Each application owns its `document`; generated view metadata contains only node IDs.
Keep view-model state outside generated UI and dispose the document/subscriptions
before releasing captured application state. Destruction unregisters callbacks;
explicit disposal rejects subsequent operations. Generated source mappings and named
references preserve a boundary for a future replaceable-view implementation. No
reload ABI, subtree hot replacement or state-restoration protocol is promised yet.

## Measurements

After building, run:

```sh
python3 samples/NativeWeb/measure.py artifacts/native-web-foco
```

This records 20 native construction/interaction runs, build-time UI parsing and C++
compilation costs, process RSS and bundle size in `measurements.json`. The report
separates native document construction from process/GPU startup and explicitly labels
warm filesystem caches and the Cocoa smoke test's deliberate delay.

### Compiled templates

Reusable dynamic UI can be declared as inert HTML templates:

```html
<template id="item"><button data-ref="button">Item</button></template>
```

The compiler emits native construction functions. Application code calls
`compiled_ui::instantiate(document, parent, "item")`, obtains a `template_view`
with `roots` and `named("button")`, and updates text/classes through the native
API. No HTML string is parsed at runtime. Each instance has independent node
handles. Remove its roots explicitly when disposing the instance; dropping the
reference struct alone does not remove the nodes.

Template children use `data-ref` rather than document-global IDs, so repeated
instances cannot overwrite each other's ID lookup. Styles live in the document's
compiled stylesheet and continue to match dynamic classes. Nested templates,
unwrapped root text, scripts and unsupported elements are rejected at compile
time. The Native Web sample's Add Item command now uses this path.

### Native WebGPU validation

Configure `src/WebScene.NativeWeb` with `WEBSCENE_NATIVE_WEB_GPU=ON` and
`WEBSCENE_GRAPHICS_SDK_ROOT` pointing at a verified Dawn SDK RID directory.
Build `native_web_gpu`, then run `ctest -R '^native_web_gpu$' --output-on-failure`
from that build directory. This target links Dawn and system libraries without V8.

The native headers provide explicit device discovery and Canvas
configure/current-texture/end-frame/resize/unconfigure APIs. `native_webgpu_surface`
uses WebScene's existing IOSurface/DXGI providers and retained image snapshots.
The hardware tests verify diagnostic pixels and IOSurface lifecycle separately.

The Foco baseline is `codex/webscene-kestrel` (hosting commit `a0e7c0fd`), integrated
on `codex/native-web-kestrel`. It already supplies Metal import, GPU synchronization,
retained resource transport and ordered WebScene image composition. The native
adapter fills that existing `webscene_gpu_frame` interface directly from native
leases; it does not load a JS runtime or introduce another Metal renderer.
Earlier experimental duplicate Foco image transport/resolver changes were reverted.

The optional GPU sample submits a first native WebGPU frame and publishes it only
after producer completion. This initial sample waits during startup; general
asynchronous redraw and resize scheduling, multi-canvas support and full Kestrel
porting remain outstanding. The compiled HTML/CSS path still uses the initial
supported subset plus predefined templates, not general Kestrel CSS coverage.

Reconciled-path validation: the optional GPU app was built and run on macOS Metal.
`--capture` produced `artifacts/native-web-foco/native-gpu.png`, showing the blue
WebGPU clear in the compiled Canvas bounds. Cocoa input/template smoke completed
with `count=1 items=1`. This proves a first native GPU frame through the existing
Foco importer, not full redraw/resize stress coverage or Kestrel parity.

The compiler can also emit a C++20 module:
`webscene-uic input.html output.cppm --module myapp.ui`.
CMake consumers can use `webscene_compile_html_module(target input module_name)`.
Module output exports the same construction, named-reference and template APIs.
The `native_web_module_templates` test verifies compiled template use via import.
Existing header output remains for current hosts while their migration continues.
