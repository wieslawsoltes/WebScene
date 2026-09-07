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

### Actual Avalonia window using public pinned graphics leases

`WebScene.GpuHost.Probe --ganesh-window` uses an ICustomDrawOperation in a real
Avalonia 11.3.4 macOS window. It acquires ISkiaSharpApiLease.GrContext and
TryLeasePlatformGraphicsApi, checks for IGlContext, and imports/wraps the native
Dawn IOSurface with the production-source helpers. The pinned framework flushes
Skia when entering the platform lease and resets its cached GL state on leaving
it (verified in tag 11.3.4 DrawingContextImpl.ApiLease.PlatformApiLease). No
framework patch is needed for these operations.

The window draws the same imported SKImage 32 times with clipping and alpha,
using one GL import. It disposes the retained SKImage after its last draw, enters
the platform lease to flush Skia, inserts the host GL fence, polls on subsequent
render callbacks, completes the native consumer and deletes the GL texture.
The fixture's synchronous producer setup runs before the window opens. Ordinary
window rendering has no explicit texture copy or pixel readback.

Run with the already-built enabled native runtime and test fixture:

```sh
WEBSCENE_TEST_NATIVE_LIBRARY="$PWD/artifacts/graphics-build/native-v8-enabled/libwebscene_native_engine.dylib" \
WEBSCENE_TEST_GPU_FIXTURE_LIBRARY="$PWD/artifacts/graphics-build/native-v8-enabled/libwebscene_graphics_iosurface_fixture.dylib" \
dotnet run --project experiments/WebScene.GpuHost.Probe -- --ganesh-window
```

Add `--verify-window-pixels` for two explicit diagnostic destination reads inside
the actual host callback: a blended interior pixel and an exterior clip pixel.
Both executions completed 32 frames and fence retirement on the Apple M4 macOS
arm64 host; raw result JSON is in `evidence/ganesh-host`. This proves host render
surface pixels, not physical scanout. Internal driver copies still need tracing.

This is a diagnostic control, not WebScene's retained DOM renderer. Production
scene acquisition, paint ordering, live readiness delivery, resizing, teardown
failure recovery and device recreation remain open. The diagnostic fails/exits on
unsupported or lost host contexts; that is not qualified production loss recovery.

### Retained import owner and host thread migration

`NativeMacOSRetainedGpuImage` now owns the GL import, SKImage and native consumer
in the Avalonia backend. Import admission preserves the scene lease on
backpressure. Draw reuses the imported image under the owning Skia/CGL context;
Retire prevents further draws and flushes through the host platform lease before
creating a fence. TryComplete polls without a CPU GPU wait and releases the GL
texture after native consumer completion. Failed fence creation keeps ownership
and permits retirement retry. This object deliberately has no GC/Dispose-based
GPU completion: the scene cache must retain it until retirement succeeds.

The real-window probe now uses this backend owner rather than local import and
fence fields. A first run exposed an incorrect fixed-thread ownership assumption:
the first callback ran on managed thread 1 and the next on thread 4, with identical
native Skia and CGL handles. Avalonia's active drawing lease serializes the
GRContext while allowing that migration. The owner therefore checks the leased
Skia/CGL context identity. NativeMacOSGpuConsumerFence also offers a host-platform-
lease polling route for migration; its standalone polling still rejects a foreign
thread. No context identity check was removed.

Both real-window modes again completed 32 redraws, one import and retirement;
the diagnostic mode verified both host pixels. General compositor loss recovery
and automatic retirement scheduling are still unfinished, and the retained DOM
renderer still needs to consume this owner.

### Opt-in ordered retained GPU paint replay

NativeCanvasSceneRenderer now accepts `orderedGpuImages: true` when applying a
scene. It compiles contiguous static DOM commands into retained SKPictures and
keeps GPU image command 256 as a dynamic slot between them. Clip, scale, rotation
and opacity scopes replay around both picture segments and image slots. Scope
pairs are validated before applying the diff; replay restores the host canvas
state even if an image draw throws. GPU image changes can therefore replay with
new slot contents without recompiling static DOM pictures.

The actual Avalonia window now feeds a constructed scene command stream through
this shared renderer instead of manually drawing its image. That stream places
DOM behind the image and a yellow DOM rectangle over it, with clip and group
opacity around the GPU slot. The 32-frame run with one import verified three
host-surface pixels (blend, exterior clip and foreground DOM), then retired its
GPU consumer. Result: `evidence/ganesh-host/ordered-scene-pixels.json`.

Validation: 20 focused ordered-renderer/culling/native-interop tests pass on both
net8.0 and net10.0; the complete Avalonia net10.0 suite passes 270 tests with no
skips; the Uno backend builds without warnings/errors. CPU-only callers retain
the existing rendering path.

This opt-in integration is not yet enabled by ordinary native scene acquisition.
The diagnostic constructs its command stream; V8 GPU canvas publication and v3
image-slot binding remain unfinished. Legacy Canvas2D layers have no ordered
placement marker yet, so this mode rejects scenes with those layers instead of
silently painting them in the old global split. Full mixed Canvas2D/SVG/GPU DOM
coverage, transformed bounds/culling qualification, and generation cache updates
remain required before advertising the GPU scene capability in production.

### Explicit retained Canvas2D placements in mixed scenes

The ordered path now recognizes command 257 as a Canvas2D layer placement, using
node_id to resolve the current retained layer and its layout/bitmap dimensions.
Its native contract requires the separate ORDERED_CANVAS capability bit (2);
ordinary callers still advertise neither ordered-canvas nor GPU-image capability.
Static DOM pictures, GPU slots and Canvas2D slots replay in command order under
the same clip/transform/opacity state. Canvas isolation uses the existing layer
semantics, shared with the legacy renderer.

Before applying a diff, the renderer checks that every visible Canvas2D layer has
exactly one placement and that every placement names a visible retained layer.
Offscreen source canvases do not require a paint placement. Layer-only layout
updates preserve the compiled paint list; removing a layer without updating its
placement is rejected before changing live state. This supersedes the earlier
blanket rejection of all Canvas2D layers in the opt-in path.

Tests cover GPU→Canvas2D→GPU→DOM interleaving, layer-only reposition/scale and stale
placement rejection. Four ordered-renderer tests pass on net8.0/net10.0; all 271
Avalonia net10.0 tests pass without skips, and Uno builds without warnings/errors.
The real window now includes a retained Canvas2D layer and verifies four host
pixels across the mixed scene, with one GPU import across 32 draws and fence
retirement (`evidence/ganesh-host/mixed-canvas-pixels.json`).

Native DOM generation does not yet emit these placements or acquire GPU images
through the ordinary scene path. The window continues to construct its diagnostic
scene. Full SVG/text/destructive Canvas2D fixtures, transform bounds, native slot
binding and production lifecycle handling remain required for epic completion.
