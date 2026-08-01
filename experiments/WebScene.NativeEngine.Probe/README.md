# Native scene engine

This experiment is the product-neutral native DOM, CSS, V8, and immutable-scene
implementation used to validate WebScene's native execution architecture.

The engine owns a V8 isolate and mutable DOM/CSS state on its worker thread. It
publishes immutable scene checkpoints and diffs through the C ABI in
`native/webscene_native_engine.h`. A managed renderer acquires a scene pointer, traverses
the fixed-layout arrays in place, renders the changed layers, acknowledges the revision,
and releases the lease. No product assets, bootstrap code, or application APIs belong in
this directory.

## Build

The portable engine can be built without V8:

```sh
cmake -S experiments/WebScene.NativeEngine.Probe \
  -B artifacts/native-engine-probe \
  -DCMAKE_BUILD_TYPE=Release
cmake --build artifacts/native-engine-probe --config Release
```

For the V8 build, provide the reviewed V8 headers and libraries used by the packaging
pipeline:

```sh
cmake -S experiments/WebScene.NativeEngine.Probe \
  -B artifacts/native-engine-probe-v8 \
  -DCMAKE_BUILD_TYPE=Release \
  -DWEBSCENE_NATIVE_ENGINE_WITH_V8=ON \
  -DWEBSCENE_V8_INCLUDE_DIR=/absolute/path/to/v8/include \
  -DWEBSCENE_V8_LIBRARY=/absolute/path/to/v8/library
cmake --build artifacts/native-engine-probe-v8 --config Release
```

## Standards-based default stack

New build directories default to the accepted `html5ever` HTML parser, `cssparser` CSS
syntax parser, Servo selector parser, generated WebIDL catalog, and V8 bootstrap snapshot.
The Rust library is linked statically into the existing WebScene native library; it does
not add a deployed dynamic library. For a V8-free build and parser tests:

```sh
cmake -S experiments/WebScene.NativeEngine.Probe \
  -B artifacts/native-engine-probe-standards \
  -DCMAKE_BUILD_TYPE=Release \
  -DWEBSCENE_NATIVE_ENGINE_BUILD_HTML_PARSER_BENCHMARK=ON
cmake --build artifacts/native-engine-probe-standards --config Release
ctest --test-dir artifacts/native-engine-probe-standards --output-on-failure
artifacts/native-engine-probe-standards/webscene_html_parser_benchmark
```

The release scripts accept explicit `--html-parser`, `--css-parser`, `--selector-parser`,
`--dom-bindings`, and `--v8-snapshot` overrides; the PowerShell script exposes the
equivalent named parameters. The legacy implementations remain selectable for bounded
comparisons, but are no longer the default. Comment preservation is the production
policy; the discard mode exists only in the parser benchmark.

## Generated DOM bindings

The native runtime defaults to the WebRef-validated generated `EventTarget` through
`HTMLElement` catalog. An explicit comparison build can still select
`-DWEBSCENE_NATIVE_ENGINE_DOM_BINDINGS=legacy`.

```sh
cmake -S experiments/WebScene.NativeEngine.Probe \
  -B artifacts/native-engine-probe-generated-bindings \
  -DCMAKE_BUILD_TYPE=Release \
  -DWEBSCENE_NATIVE_ENGINE_ENABLE_V8=ON \
  -DWEBSCENE_NATIVE_ENGINE_DOM_BINDINGS=generated
```

The generated source is committed. Regenerate or verify it with the commands in
`tools/webidl-v8-bindings/README.md`; CMake intentionally has no Node or network
dependency.

V8 release builds default to a bootstrap snapshot. The generated
`webscene_bootstrap_snapshot.bin` and `webscene_bootstrap_snapshot.meta` files must remain
beside the native library; the package targets copy all three files together and reject a
missing sidecar.

Certification telemetry and profiling hooks are excluded by default from both
Release and Debug builds. Enable them only for compatibility evidence or native
profiling:

```sh
cmake -S experiments/WebScene.NativeEngine.Probe \
  -B artifacts/native-engine-certification \
  -DCMAKE_BUILD_TYPE=Release \
  -DWEBSCENE_NATIVE_ENGINE_ENABLE_V8=ON \
  -DWEBSCENE_NATIVE_ENGINE_CERTIFICATION=ON
```

The packaged native runtime always sets
`WEBSCENE_NATIVE_ENGINE_CERTIFICATION=OFF`. The C ABI remains link-compatible;
certification report functions return an explicit `telemetry-disabled` report
when their implementation was compiled out. `webscene_engine_get_build_features`
returns `WEBSCENE_ENGINE_BUILD_FEATURE_CERTIFICATION` only when the loaded binary
contains those hooks, so a certification runner can fail fast if it was given
a normal runtime accidentally.

This is a compile-time choice, independent of `CMAKE_BUILD_TYPE`: ordinary
Debug and Release configurations both exclude the code unless
`WEBSCENE_NATIVE_ENGINE_CERTIFICATION=ON` is supplied explicitly. The excluded
surface includes feature-use inventories, scene/canvas diagnostic snapshots,
CSS and binding profiling maps, V8 CPU profiling, and their hot-path counters
and timers.

## Canvas hot-path allocation policy

Canvas commands read only the paint-state groups required by that operation.
For example, `fillRect` does not query stroke, line, dash, text, or image state;
stroke and text operations materialize those groups when first needed. Full
state reconstruction remains mandatory at save/restore and retained-command
compaction boundaries.

Unchanged JavaScript string properties are compared against the retained native
state through a bounded stack buffer. A native `std::string` conversion and
resource-table lookup occur only when the property actually changes. This is
important for components that issue thousands of draw calls per frame with a
stable `fillStyle`, composite mode, or shadow state.

## Host contract

- `webscene_engine_prewarm` pays the process-wide V8 initialization cost early.
- `webscene_engine_get_build_features` identifies whether the loaded binary
  includes certification-only evidence and profiling hooks.
- `webscene_engine_create_with_options` creates an engine and configures its persistent
  compilation-unit cache.
- `webscene_engine_set_resource_root` provides the filesystem root used to resolve
  component-owned iframe, script, and stylesheet resources.
- `webscene_engine_set_preferred_color_scheme` provides the host's effective
  light/dark preference for CSS media queries and `Window.matchMedia`.
- `webscene_engine_execute_script` is the fire-and-forget execution boundary.
- `webscene_engine_begin_evaluate_v3` and
  `webscene_engine_begin_invoke_v3` are the leased tagged-result
  interoperation boundaries.
- Component code sets `globalThis.__webSceneComponentReady = true` when its application
  lifecycle is ready. The generic scene flag and metric expose that state to a host.
- `webscene_engine_acquire_latest_scene` returns an immutable pointer view. The host must
  acknowledge and release it according to the ABI comments before acquiring the next
  dependent diff.

Product hosts own their assets, bootstraps, readiness policy, API facade, screenshots,
and compatibility/performance suites. WebScene keeps only reusable engine behavior and
the shared managed/native conformance contracts.
