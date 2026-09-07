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

## Runtime capability evidence

The new `experiments/WebScene.GpuHost.Probe` ran successfully on the current M4
macOS desktop with Avalonia 11.3.4. Default platform selection returned a non-lost
interop object but empty SupportedImageHandleTypes and SupportedSemaphoreTypes.
The probe exits cleanly after posting shutdown to the dispatcher (synchronous
shutdown during startup initially triggered a lifetime initialization error).

This rules out selecting an external-handle import route from this host's reported
capabilities. It does not prove that all Avalonia macOS configurations lack interop.
Next inspect the shared-context import contract and available host backend options;
do not implement an IOSurface handle adapter assuming this host will accept it.

The probe now queries IOpenGlTextureSharingRenderInterfaceContextFeature through
Compositor.TryGetRenderInterfaceFeature. It reports CanCreateSharedContext=true on
the same default macOS host, with external handle lists still empty. This is a
capability result, not a successful import. The public feature creates a compatible
GL context and composition texture, avoiding arbitrary user-supplied texture wrappers.
Avalonia's Skia importer specifically verifies the context share group.

Next exercise context creation, drawing into its composition texture, import and
surface update. Then determine the supported Metal/Dawn-to-GL allocation bridge;
shared GL context availability alone does not make a Dawn Metal texture importable.

The shared-context import/update sequence now executes successfully in the probe:
a 32x32 shared GL texture is cleared through a complete framebuffer, flushed,
imported, and copied/snapshotted through an awaited drawing-surface update. Async
import disposal precedes texture/context teardown. No CPU pixel upload/readback
is used by the probe. This establishes successful API execution, not displayed
pixel correctness: the surface is not yet attached to a visual, and the source
is GL-produced rather than Dawn/Metal-produced. These remain separate gates.

The host probe also attaches the updated surface to a composition visual and awaits
its render-thread commit, then detaches and commits before releasing resources.
Both commits complete on M4. Per Avalonia's API contract, this is render-thread
state application, not a display/GPU-completion fence or a pixel correctness test.
The next boundary test still needs independent displayed-pixel verification and
connection of the Dawn-produced allocation to the host-compatible source.

## Visible output evidence

A targeted capture of the exact probe window during `--inspect` shows the expected
blue composition surface and white remaining background. See
[evidence](evidence/avalonia-host/shared-gl-window.png). This independently confirms
visible output for the shared-OpenGL host route. It is not exact color validation;
window capture/display color management differs from raw texture verification.
The Dawn/Metal-to-host allocation bridge remains unimplemented and unverified.

### Production-source IOSurface import operation

NativeMacOSGpuImageImport now imports a checked native consumer's BGRA8 IOSurface
into a rectangle texture already bound in the host's current CGL context. GL
texture/state creation remains with the host; this operation uses only Apple's
CGL/IOSurface entrypoints, avoiding ambiguous GL symbol lookup alongside ANGLE.
It borrows through NativeGpuImageConsumerV3 and does not complete the consumer,
wait on producer work or enable GPU scene acquisition.

The separate native fixture creates an accelerated CGL 3.2 context and rectangle
texture. The managed test imports a native IOSurface lease and checks its GL
level dimensions (17x4), then deletes GL references before completing the lease.
All six NativeGpuSceneInteropTests passed without skips on net8.0 and net10.0.

This verifies the managed-to-native storage import operation. It submits no
draws and proves no rendered pixels, adapter pairing, producer synchronization
or final Avalonia presentation. The retained renderer still needs the complete
import/cache/completion path before it can advertise GPU scene capability.

### Dawn-produced pixels through the managed import

The CGL import fixture now optionally initializes its versioned IOSurface using
the pinned Dawn Metal backend. It requires an integrated/discrete adapter and
IOSurface/shared-event features, imports via dawn_shared_image, clears on the
GPU, ends shared access and waits for diagnostic queue completion before
publishing the ready lease. Callback state remains owned with the device.

The managed test obtains that lease through the real native ABI, imports it
using NativeMacOSGpuImageImport, and reads all 68 pixels from the CGL framebuffer.
RGBA [51,102,153,255] matched within one channel unit, including BGRA storage
conversion. The six-test interop suite passed on net8.0 and net10.0 without skips.

The fixture's waits and GL readback are explicit diagnostics, not an ordinary
presentation implementation. Constant-color output does not verify orientation,
transparent edges or compositing. Asynchronous production readiness, GL fences,
host texture conversion and retained scene rendering still need integration.

### GL fence retirement component

NativeMacOSGpuConsumerFence obtains GL procedures from a caller-supplied host
resolver (intended to be GlInterface.GetProcAddress), inserts a GPU completion
fence and flushes. TryComplete uses glClientWaitSync with zero timeout. It retains
the consumer on pending/failed results, requires the original thread/current CGL
context, deletes a signaled fence and completes the native consumer once.
Retired polling is idempotent. The host must retain and poll this component;
there is no GC-based completion or automatic render scheduling.

The real CGL import test now exercises this path, including wrong-thread polling
rejection and duplicate consumer completion rejection. All six interop tests
passed on net8.0 and net10.0. The fixture now queues a GPU framebuffer blit from
its imported rectangle into independent 2D texture storage before fence insertion.
The native provider remains alive before polling and expires after successful
retirement. Only then does diagnostic readback verify all 68 destination pixels.
This measures one GPU-local copy, with no CPU pixel transport between APIs; it
is not a forced-delayed-GPU stress test. Wrong-thread rejection uses a dedicated
thread because a queued Task can execute inline on a waiting worker thread.
Fixture-only failure cleanup drains GL before releasing native image ownership.
Production context-loss cleanup and renderer scheduling remain unfinished.

### Direct pinned Ganesh sampling of the IOSurface rectangle

The .NET interop fixture now exercises `NativeMacOSGpuImageImport.TryWrapRectangleTexture`
with the repository's SkiaSharp 2.88.9 Ganesh GL backend. It imports the Dawn-written
17×4 IOSurface into a CGL rectangle texture, wraps that borrowed texture as a
texture-backed SKImage, and draws it into a GPU SKSurface. The image is clipped
and blended at alpha 128 over opaque blue. All 192 destination pixels are checked
against the expected clipped extent and channel values after Skia submission and
native consumer fence retirement. Both net8.0 and net10.0 pass all seven interop
tests without skips on the macOS arm64 fixture host.

The route uses the existing pinned Ganesh API and no explicit intermediate texture
blit or CPU upload. The only explicit readback is diagnostic destination validation
after the source fence. Internal driver/Skia copy counts still require a trace;
this is not a claim that every underlying operation is copy-free. The native source
owner is observed released after the fence, independently of the destination.
The wrapper borrows the caller's GL texture and consumer; SKImage disposal is not
GPU completion. The caller supplies negotiated origin and alpha interpretation.

This is an actual GPU Skia composition test, not yet an Avalonia-window retained
scene test. Uniform source color cannot qualify texture orientation or transparent
source edges. Host context acquisition, import caching, retained replay lifetime,
ordered DOM/canvas composition, and device-loss handling remain integration work.
Graphite migration is not required by this demonstrated direct Ganesh route.
