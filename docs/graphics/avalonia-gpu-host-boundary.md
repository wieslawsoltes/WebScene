# Avalonia 11.3.4 GPU host boundary

Source review, 2026-09-07. No framework GPU execution pass is claimed.
WebScene pins Avalonia 11.3.4 and currently draws through ISkiaSharpApiLeaseFeature.
The standalone Graphite probe does not change that host device.

Avalonia already has a cross-platform GPU import contract, exposed through
ICompositionGpuInterop. Supported image/semaphore handle types and synchronization
capabilities must be queried at runtime; device LUID/UUID are also exposed. A
platform's existence is not evidence that a particular backend imports our handle.
Use the installed 11.3.4 API rather than importing another presentation framework.

[CompositionDrawingSurface](https://github.com/AvaloniaUI/Avalonia/blob/11.3.4/src/Avalonia.Base/Rendering/Composition/CompositionDrawingSurface.cs)
provides UpdateAsync, UpdateWithKeyedMutexAsync and UpdateWithSemaphoresAsync. Its
public contract says completion permits the caller to destroy/dispose the source
image. These are distinct from merely completing import, and from our generic
native queue completion notification. An adapter must follow the chosen backend's
synchronization contract before releasing/reusing a leased source allocation.

The [server implementation](https://github.com/AvaloniaUI/Avalonia/blob/11.3.4/src/Avalonia.Base/Rendering/Composition/Server/ServerCompositionDrawingSurface.cs)
checks import completion/context validity and obtains a snapshot from the imported
image. The [OpenGL/Skia implementation](https://github.com/AvaloniaUI/Avalonia/blob/11.3.4/src/Skia/Avalonia.Skia/Gpu/OpenGl/GlSkiaExternalObjectsFeature.cs)
wraps the native texture in a Skia surface, snapshots it, and flushes Skia/OpenGL.
The keyed-mutex and semaphore paths surround this with acquire/release or wait/signal.
No CPU bitmap loop appears in this reviewed path. Whether snapshot performs a
GPU-local copy must be verified against the concrete Skia backend and traced;
source inspection alone does not prove zero-copy or completion correctness.

## Integration direction

Reuse Avalonia's import/update machinery for a supported host backend. Keep native
platform code limited to producing/importing the external allocation and exchanging
the synchronization primitives actually supported by that contract. Do not assume
our new D3D12 fence helper is required by Avalonia, whose selected backend may
instead expose keyed mutex or semaphore synchronization.

Before implementing the adapter, obtain capabilities from a running 11.3.4 host
on this machine, including actual image handle strings, synchronization flags and
device identity. Then select a supported route and test import, update, source
retention, resize and context loss. An unsupported result must stay explicit.
Graphite shared-device composition reduces internal interop, but a final external
image boundary still exists when Avalonia owns a different GPU device/context.

Preserve HTML stacking, clips, opacity and hit testing when placing the composition
surface. A separate uncomposited native window does not satisfy the scene contract.
Repeat the boundary analysis for Uno; Avalonia's API cannot be assumed to apply to it.
