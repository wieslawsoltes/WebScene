# WebScene.Backend.Avalonia

The reference Avalonia presenter for WebScene's native ABI 3 engine. It consumes
immutable native scene snapshots and owns Skia rendering, text shaping, input
forwarding, resource loading, frame scheduling, and Avalonia lifecycle integration.

The package contains:

- `NativeWebSceneRuntime`, which validates and prewarms the native runtime;
- `NativeWebSceneView`, which hosts an absolute document URL;
- `NativeSceneSurface`, the focusable Avalonia scene presenter; and
- `NativeWebSceneApi`, the low-level operations used by advanced integrations.

There is no managed DOM, CSS, layout, Canvas, GPU, or JavaScript engine in this
package and no fallback to one. The former `AvaloniaBrowserHost` and
`AvaloniaBackendHost` APIs have been removed.

## Optional GPU provider

WebGPU is negotiated independently from the core runtime. When a native engine
build includes the browser bindings, add the matching
`WebScene.NativeGpu.Runtime.<rid>` package and require the API at load time:

```csharp
await view.LoadAsync(new NativeWebSceneLoadOptions
{
    Source = "app://web/index.html",
    NativeLibraryPath = nativeEnginePath,
    RequiredCapabilities = WebSceneBackendCapabilities.WebGpu
});
```

The view probes for `libwebscene_native_gpu.dylib` beside the native engine; use
`NativeGpuLibraryPath` to override that location. Provider ABI, provider
capabilities, and engine binding build features are validated before V8 navigation
or document-start scripts. If the requested
capability, Metal adapter, or IOSurface zero-copy path is unavailable, loading
fails with `WebSceneBackendCapabilityException`; software rendering and CPU
readback are not substituted. `Capabilities` and `GpuRuntimeInfo` expose the
negotiated result for diagnostics.

On macOS the provider renders with Dawn's Metal backend into an IOSurface-backed
texture ring. Immutable scene leases retain each exported frame until Avalonia's
Skia Metal context has drawn and synchronously submitted it. Resize, view teardown,
engine teardown, and outstanding scene leases preserve provider and texture
ownership; there is no pixel-buffer copy through managed memory.

Native scene publication uses a lock-free mailbox consumed on Avalonia's compositor
clock. Ordinary scene traffic does not enter the UI dispatcher; a coalesced
UI-to-compositor wake remains only for first presentation and cooperative live resize.

Advanced integrations can use this package directly. Packaged Component Profile 1
applications should use `WebScene.Sdk.Avalonia.WebSceneComponentHost`, which composes
this view with package validation, isolated resources, host capabilities, lifecycle,
recovery, and diagnostics.

## Document-start scripts and storage

Use `LoadAsync(NativeWebSceneLoadOptions)` to install ordered compatibility scripts
after the loading document and location exist but before authored JavaScript. Scripts
whose `AllFrames` value is true also run before authored scripts in each subsequently
created frame. A document-start exception fails the initial load and includes the
configured script name in native diagnostics.

The 1.0.19 native runtime supplies synchronous, in-memory `localStorage` and
`sessionStorage` objects for the current engine/page lifetime. It implements
`length`, `key`, `getItem`, `setItem`, `removeItem`, and `clear`, including JavaScript
string coercion, null results, and stable insertion order. This is a compatibility
subset: it does not promise persistence, quotas, storage events, origin/reload
semantics, cross-engine profiles, or IndexedDB.
