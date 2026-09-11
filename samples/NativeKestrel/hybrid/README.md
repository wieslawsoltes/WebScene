# Compiled hybrid Kestrel on Foco

The original Kestrel HTML and CSS compile into a C++ module. The complete pinned
JavaScript application retains ownership of CAD state, history and WebGPU rendering.
Dynamic UI expressions call 238 compiled native templates in the same WebScene DOM.
No second native CAD controller runs beside JavaScript.

`upstream/` preserves the 37 application scripts and IO worker at commit
`7a1e84c67fd24410c22f0d1a45b2e54120b32d0e`, including the MIT license. Browser builds
continue to use these unchanged inputs and `../reference/index.html` / `src/style.css`.
`compile-js-templates.mjs` is a **Kestrel-specific build-time migration frontend**;
it is not a general JavaScript-to-C++ compiler. Its generated source and template
catalog live in the build output. General CAD feature ports to C++ remain paused.

## Build and run on macOS

Use configured WebScene NativeWeb, graphics/V8 engine and Foco build trees with
CMake 3.28+, Ninja, LLVM with C++20 module support, an Xcode SDK, Node and Python 3.
The engine requires the matching V8/ICU/snapshot and Dawn graphics SDK described by
its CMake options. The packager deliberately reuses those configured dependencies.
Do not point it at the native-only engine build.

```sh
python3 samples/NativeKestrel/hybrid/build-macos.py \
  --compiler-build artifacts/native-web-modules \
  --engine-build artifacts/hybrid-kestrel/build \
  --foco-build /path/to/FocoUI/build-production-check --run
```

The engine configuration needs `WEBSCENE_NATIVE_ENGINE_ENABLE_V8=ON`,
`WEBSCENE_NATIVE_ENGINE_ENABLE_GRAPHICS=ON`, `WEBSCENE_GRAPHICS_SDK_ROOT`,
`WEBSCENE_V8_ROOT` and `WEBSCENE_V8_OUTPUT_ROOT`, plus the pointer-compression and
partition-allocation flags matching that V8 build. Configure the compiler and
engine with the same LLVM toolchain and Xcode SDK from the start. Foco uses its
existing Skia/Graphite WebScene integration. The packager sets the generated module
and package name, builds `foco_kestrel_hybrid`, copies required runtime assets,
rewrites local library paths, signs locally and audits the bundle.

Output: `artifacts/hybrid-kestrel/package/Kestrel FocoUI Hybrid.app`.
`build-report.json` records build duration, package bytes and template count.
Edits use rebuild/relaunch. No hot reload machinery is installed.

The base UI uses system fonts and inline SVG. Scripts, root markup/styles and the
worker source are embedded in the compiled module; V8 support files and Dawn are
bundled locally. User-selected drawings/fonts use Foco's native file picker.
Additional application images/fonts must be packaged locally too; a general
arbitrary-resource embedding compiler is not claimed by this sample.

## Compiler and runtime boundary

The reusable compiler remains `tooling/webscene-uic`. Its engine backend accepts:

```sh
webscene-uic --engine-module index.html view.cppm --module webscene.application \
  --script-root generated --templates generated/src/templates.html \
  --bootstrap generated/bootstrap.js
```

It emits native construction, namespaces, attributes, embedded scripts and prepared
root stylesheet payloads using WebScene's existing CSS frontend. CSS cascade,
layout, property values and inline styles retain the shared runtime semantics.
Template catalogs preserve inert styles until insertion and include source
file/line/column records in `templates.json`. Script names appear in runtime errors.
SVG UI producers also use native templates; SVG file export remains a serializer.

The optional host ABI is `webscene_engine_load_compiled_document_v1`; Foco exposes
`web_scene_view::load_compiled_document`. Construction occurs on the engine worker
before application scripts. The host passes copied strings and a viewport, never
C++ objects across the library boundary.

Kestrel rejects nonempty runtime HTML parsing. Other hybrid applications default
to allowing runtime HTML insertion and can migrate incrementally. Pure C++ builds
retain their separate native document host and need no V8, initialization or JS
assets. Neither generated native headers nor native DOM libraries depend on V8.

## Verification

```sh
python3 tests/NativeWeb/audit_hybrid_macos_bundle.py "$BUNDLE"
python3 tests/NativeWeb/hybrid_kestrel_smoke.py "$RUNTIME"
python3 tests/NativeWeb/hybrid_kestrel_parity.py "$RUNTIME"
ctest --test-dir artifacts/hybrid-kestrel/build -R '^webscene_hybrid_v8_runtime_tests$' --output-on-failure
ctest --test-dir artifacts/native-web-modules --output-on-failure
```

Set `BUNDLE` to the app above and `RUNTIME` to its
`Contents/Resources/Runtime/libwebscene_native_engine.dylib`. The ABI smoke checks
all ribbon tabs, dialogs including rich MTEXT SVG, strict HTML rejection, original
history and worker DXF export. The parity test compares original parsed content
with compiled content at three sizes/themes. Those headless tests use Canvas 2D;
the actual Foco app and pan benchmark exercise WebGPU presentation.

## Existing host limits

`window.open('', '_blank')` and browser printing are not implemented by the current
WebScene host. Print UI and SVG producers compile, but opening the original print
popup still fails; this is not full print/PDF acceptance. Native multiwindow/print
services need separate implementation. Optional DWG/OpenCascade services retain
upstream availability checks and are not bundled codecs. General browser API
conformance and exhaustive testing of every CAD command are not claimed.

See `docs/design/hybrid-kestrel-epic.md` for delivery evidence and remaining acceptance.
