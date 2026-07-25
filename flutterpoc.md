# Flutter native-runtime proof of concept

> Status: deferred design note. Revisit after the native Avalonia runtime, public
> scene ABI, lifecycle behavior, and certification process have matured.

## Summary

The native HtmlML runtime can be hosted inside a Flutter application through
`dart:ffi`. The runtime already exposes a renderer-neutral C ABI with opaque
engine handles, primitive input records, callbacks, and immutable scene tables.
It does not require .NET or Avalonia.

The existing SkiaSharp scene renderer is a strong reference implementation for
Flutter. Its drawing operations map closely to `dart:ui.Canvas`,
`PictureRecorder`, `Picture`, `Paint`, and `Path`. This is an API-level port of
the scene projector rather than a new rendering architecture.

The initial PoC should target Flutter desktop. The currently released native
runtime matrix covers:

- macOS ARM64
- Linux x64
- Windows x64

Android requires an additional NDK/V8 build and packaging lane. iOS requires a
separate static, JIT-less V8 configuration. Flutter Web cannot load the native
runtime through `dart:ffi`.

## Proposed architecture

```text
Flutter input, lifecycle and frame clock
                  |
                  | Dart FFI
                  v
       HtmlML native engine worker
       V8 + DOM + CSS + layout
                  |
                  | immutable scene diffs
                  v
         Dart scene-diff projector
                  |
                  | retained dart:ui Pictures
                  v
        CustomPainter / dart:ui.Canvas
                  |
                  v
       Flutter rasterizer (Impeller/Skia)
```

Flutter does not expose an internal native `SkCanvas` to application code.
Rendering should therefore target the public `dart:ui` API. This also keeps the
HtmlML integration independent of whether a particular Flutter platform uses
Impeller or Skia internally.

## Runtime lifecycle

The Flutter host should:

1. Load the RID-appropriate HtmlML library and colocated `icudtl.dat`.
2. Verify `htmlml_engine_get_abi_version()` before creating an engine.
3. Call `htmlml_engine_prewarm()` during application startup.
4. Create an engine with `htmlml_engine_create_with_options()`.
5. Configure the resource root and load the initial document.
6. Forward viewport, scale, pointer, keyboard, text, visibility, and frame
   events.
7. Schedule a repaint when the native scene-publication callback fires.
8. Acquire the next immutable scene during scene processing.
9. Validate and apply its diff to retained Flutter pictures and resources.
10. Acknowledge the revision only after successfully applying the diff.
11. Release every acquired scene lease, including error paths.
12. Destroy the engine and close callbacks when the Flutter widget is disposed.

The host should request a complete scene checkpoint after renderer or graphics
context recreation.

## FFI package

Create a Flutter FFI package, for example `htmlml_flutter`, containing:

```text
htmlml_flutter/
  lib/
    htmlml_flutter.dart
    src/
      bindings.dart
      engine.dart
      input.dart
      scene.dart
      renderer.dart
      widget.dart
  hook/
    build.dart
  ffigen.yaml
  example/
```

Generate bindings from:

`experiments/HtmlML.NativeEngine.Probe/native/htmlml_native_engine.h`

The package must bundle the correct library, ICU data, notices, and runtime
manifest for the target architecture. It should validate the ABI and manifest
at startup and report an actionable error for an incompatible binary.

## Renderer mapping

The Avalonia renderer in
`src/HtmlML.Backend.Avalonia/NativeSceneRuntime.cs` provides the behavioral
reference.

| SkiaSharp reference | Flutter equivalent |
| --- | --- |
| `SKCanvas` | `dart:ui.Canvas` |
| `SKPictureRecorder` | `dart:ui.PictureRecorder` |
| `SKPicture` | `dart:ui.Picture` |
| `SKPaint` | `dart:ui.Paint` |
| `SKPath` | `dart:ui.Path` |
| `SKRect` / `SKRRect` | `Rect` / `RRect` |
| `Save`, `Restore`, `SaveLayer` | `save`, `restore`, `saveLayer` |
| Matrix transforms | `transform`, `translate`, `scale`, `rotate` |
| Clip operations | `clipRect`, `clipRRect`, `clipPath` |
| Blend modes | `BlendMode` |
| Shaped text | `ParagraphBuilder` or `TextPainter` |

DOM foreground and background commands can be recorded into retained
`Picture`s. Canvas layers should be cached by stable node ID and generation.
Only changed layers need to be re-recorded when a compatible scene diff is
applied.

Damage rectangles may be used for invalidation and clipping, but every retained
layer intersecting a damaged region must be redrawn. Rendering only newly
appended commands is incorrect for moves, removal, clearing, clipping, opacity,
and overlapping content.

Canvas layers that use destructive composition must remain isolated with
`saveLayer`. A `CustomPainter` canvas may be shared with other Flutter content,
so an HtmlML clear or destination blend operation must never affect neighboring
widgets.

## Scene ABI improvements

Before treating the Flutter backend as maintainable, promote all renderer
semantics into the public native ABI:

- Add enums for every DOM scene-command kind.
- Add enums for every Canvas command kind.
- Document command flags and packed values.
- Document color byte order and coordinate units.
- Document string/resource indexing rules.
- Define checkpoint, replacement, removal, and layer-order semantics.
- Add compile-time and runtime struct-size assertions.
- Add ABI fixtures that can be consumed independently of Avalonia.

The current reference renderer uses numeric command values in its switches.
Duplicating those magic numbers in Dart would make the Flutter backend fragile.

## Input and frame scheduling

Flutter events map to `htmlml_input_event`:

- Pointer hover/move → `HTMLML_INPUT_POINTER_MOVE`
- Pointer down → `HTMLML_INPUT_POINTER_DOWN`
- Pointer up/cancel → `HTMLML_INPUT_POINTER_UP`
- Scroll → `HTMLML_INPUT_WHEEL`
- Viewport change → `HTMLML_INPUT_RESIZE`
- Keyboard down/up → `HTMLML_INPUT_KEY_DOWN` / `HTMLML_INPUT_KEY_UP`
- Committed Unicode scalars → `HTMLML_INPUT_TEXT`
- Flutter frame timestamp → `HTMLML_INPUT_FRAME`

Coordinates should use HtmlML CSS pixels. The resize event must include
Flutter's device-pixel ratio in `delta_x`.

Use `htmlml_engine_enqueue_resize_frame()` when a resize and rendering
opportunity belong to the same Flutter frame. Preserve ordering for down, up,
cancel, capture, and key transitions. Pointer moves and wheel input may be
coalesced according to the native contract.

The native scene-publication callback runs on the engine worker and must not
touch Flutter UI state directly. It should send a non-blocking notification to
the owning Dart isolate, which then invalidates the painter.

## Text shaping constraint

Text is the main architectural issue for a production backend.

Native HtmlML layout can call `htmlml_text_measure_callback` synchronously on
the engine worker. Layout and painting must use compatible glyph advances to
avoid wrapping drift, clipping, and fallback-font differences.

An ordinary Dart FFI callback cannot safely call Flutter text APIs from that
native worker. Dart's thread-safe asynchronous callback mechanism supports
`void` notifications, whereas text measurement must synchronously return
metrics.

PoC options:

1. Omit the callback and use HtmlML's fallback width approximation. This is
   suitable only for proving engine, input, scene, and drawing integration.
2. Add a small native text-shaping shim that is safe on the engine worker and
   use the same font selection and advances when painting.
3. Redesign the measurement boundary around an asynchronous or precomputed
   shaping cache.

Option 2 is the most direct production path. The PoC must explicitly record
text-layout mismatches instead of treating fallback measurement as parity.

## Resource loading

For the first PoC, extract Flutter assets to an application-support directory
and pass that directory to `htmlml_engine_set_resource_root()`. This avoids a
synchronous Dart resource callback on the native worker.

A later host-resource adapter may support packaged assets, HTTP caching, and
application-provided resources. Because the native callback is synchronous, it
should be implemented in a native shim or backed by data already resident in
native memory.

## SVG and images

Flutter can draw paths but does not provide all conveniences used by
`Svg.Skia`. The backend needs one of:

- a Dart SVG/path parser producing `dart:ui.Path`;
- a small native parser that publishes normalized path commands; or
- an ABI enhancement that represents parsed SVG geometry directly.

The PoC should begin with the scene command subset used by a small deterministic
fixture, then add SVG, images, gradients, shadows, and complex blend modes with
pixel tests.

## Widget surface

The package should expose a widget similar to:

```dart
HtmlMlView(
  document: HtmlMlDocument.asset('assets/example/index.html'),
  onReady: () {},
  onConsoleMessage: (message) {},
  onError: (error) {},
)
```

Internally it can use a focused `Listener`/keyboard surface around
`CustomPaint`. It must:

- update the native viewport from layout constraints;
- forward device scale and lifecycle visibility;
- participate in Flutter's frame clock;
- manage focus and pointer capture;
- expose cursor changes on desktop;
- dispose all native and retained-picture resources deterministically.

Accessibility and full IME behavior may be deferred from the first visual PoC,
but they are required for a production backend.

## PoC phases

### Phase 1: ABI and headless smoke

- Generate Dart bindings.
- Load and validate the native library.
- Prewarm, create, load a local document, execute JavaScript, and evaluate JSON.
- Enqueue resize and frame events.
- Acquire, validate, acknowledge, and release scenes.
- Verify clean shutdown and repeated creation.

### Phase 2: Basic Flutter renderer

- Add `HtmlMlView` and `CustomPainter`.
- Render rectangles, borders, rounded rectangles, transforms, clipping, and
  opacity.
- Translate pointer and resize input.
- Drive `requestAnimationFrame` from Flutter timestamps.
- Render a deterministic interactive fixture.

### Phase 3: Retained Canvas

- Port Canvas state and command switches.
- Cache `Picture`s by node ID and generation.
- Implement isolation, clipping, paths, text, shadows, and blend modes.
- Apply ordered diffs and recover from stale bases with checkpoints.

### Phase 4: Compatibility

- Add accurate text shaping.
- Add SVG and image resources.
- Add keyboard, committed text, IME, focus, capture, cursor, and lifecycle
  behavior.
- Reuse the managed/native conformance fixtures and deterministic screenshots.

### Phase 5: Packaging

- Produce a Flutter package with supported desktop artifacts.
- Validate architecture, ABI, manifest, ICU data, signing, and licenses.
- Add clean-consumer CI builds for macOS ARM64, Linux x64, and Windows x64.

## Initial acceptance criteria

The PoC is successful when:

- A Flutter desktop app loads the packaged native runtime without .NET.
- A local HtmlML document executes JavaScript and signals readiness.
- Resize, pointer, keyboard, text, and animation-frame inputs reach the engine
  in order.
- Scene publication schedules Flutter painting without blocking the engine.
- The painter safely applies, acknowledges, and releases ordered scene diffs.
- Basic DOM and Canvas output matches a deterministic reference fixture.
- Retained pictures are reused when their layer generation is unchanged.
- Renderer recreation recovers through a requested checkpoint.
- Repeated mount/unmount cycles do not leak engines, scene leases, callbacks,
  or retained pictures.
- Known text, SVG, accessibility, or platform gaps are reported explicitly.

## Platform outlook

| Flutter target | Assessment |
| --- | --- |
| macOS ARM64 | Best first target; a native runtime package already exists. |
| Windows x64 | Good desktop target after the macOS PoC. |
| Linux x64 | Good desktop target after the macOS PoC. |
| Android | Feasible; requires NDK, Android V8, ABI packaging, and device CI. |
| iOS | Requires static JIT-less V8, iOS-specific validation, and distribution review. |
| Web | Not supported by this native FFI architecture. |

## References

- `experiments/HtmlML.NativeEngine.Probe/native/htmlml_native_engine.h`
- `experiments/HtmlML.NativeEngine.Probe/README.md`
- `docs/architecture/native-v8-scene-engine.md`
- `docs/backends.md`
- `src/HtmlML.Backend.Avalonia/NativeSceneRuntime.cs`
- `packaging/HtmlML.NativeEngine.Runtime/README.md`
- [Flutter: Bind to native code using FFI](https://docs.flutter.dev/platform-integration/bind-native-code)
- [Flutter architectural overview](https://docs.flutter.dev/resources/architectural-overview)
- [Flutter `dart:ui.Canvas`](https://api.flutter.dev/flutter/dart-ui/Canvas-class.html)
- [Dart `NativeCallable`](https://api.dart.dev/dart-ffi/NativeCallable-class.html)
- [V8: Cross-compiling for iOS](https://v8.dev/docs/cross-compile-ios)
