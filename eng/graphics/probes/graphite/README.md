# Shared-device Graphite spike

Experimental source pin: Skia `0f366c36621fc156664662b8ea5426d2f41cefe1`.
WebScene Dawn SDK pin: `2ca8cbfe0f8275aa0f739e7b6b4345a16e2f0378`.
Skia's own DEPS instead pins Dawn `f45d1eb98a88b29d2ec38171613525cd5e54c0c0`.
Do not silently substitute that runtime or pass objects between the two builds.

The first compatibility check is compile-only:

```sh
clang++ -std=c++20 -fsyntax-only \
  -I artifacts/graphics-src/skia \
  -I artifacts/graphics-sdk/osx-arm64/dawn/include \
  eng/graphics/probes/graphite/dawn_header_check.cpp
```

Passed on macOS arm64 with the revisions above. This verifies declarations for
supplying the existing Dawn device/queue and wrapping a WGPUTexture. It does not
compile Graphite's implementation, verify ABI compatibility, link Skia, compose
pixels, or prove host presentation.

Skia's `third_party/dawn/BUILD.gn` normally builds a static `dawn_combined` library.
The next build step must isolate Graphite's dependency on the existing Dawn shared
runtime, including any internal Dawn/Tint dependencies. Determine whether the
selected Skia revision supports that arrangement before changing production SDK
pins. Then run the composition and lifetime tests in
`docs/graphics/cross-platform-reuse-review.md`.

Implementation compatibility check:

```sh
python3 eng/graphics/probes/graphite/check-backend.py \
  --skia artifacts/graphics-src/skia \
  --dawn-include artifacts/graphics-sdk/osx-arm64/dawn/include
```

All 15 Dawn backend `.cpp` files passed syntax compilation on macOS arm64 at the
pins above with SK_GRAPHITE and SK_DAWN enabled. This strengthens the header-only
result but still does not link Skia, verify its full build configuration, or execute
GPU composition. The source scan found no direct `dawn::native` or Tint includes
in that backend directory; Skia's separate developer shader tools do depend on Tint.
Full dependency synchronization was started for the isolated build.

## Isolated macOS build

```sh
python3 eng/graphics/probes/graphite/use-external-dawn.py \
  --skia artifacts/graphics-src/skia \
  --dawn-sdk artifacts/graphics-sdk/osx-arm64/dawn
artifacts/graphics-src/skia/bin/gn gen out/webscene-graphite \
  --root=artifacts/graphics-src/skia \
  --args="$(cat eng/graphics/probes/graphite/spike-args.gn)"
ninja -C out/webscene-graphite skia -j 8
```

The patch verifies the source pin and refuses unrelated edits. It redirects
Dawn headers/link metadata to the existing shared SDK and removes the dependency
on Skia's `dawn_cmake` action. The initial configuration deliberately excludes
optional codecs, text libraries, PDF and tools; those exclusions are for the
composition experiment and are not production capability decisions.

Result: all 797 build steps completed, producing `out/webscene-graphite/libskia.a`.
A static archive does not resolve its external symbols. Executable linkage to the
single Dawn dylib and an actual GPU composition test are the next required checks.
No runtime ABI, pixel correctness or host-presentation pass is claimed yet.

## Shared-device rendering probe

```sh
clang++ -std=c++20 -O2 -DSK_GRAPHITE -DSK_DAWN \
  -I artifacts/graphics-src/skia -I artifacts/graphics-sdk/osx-arm64/dawn/include \
  eng/graphics/probes/graphite/graphite_probe.cpp out/webscene-graphite/libskia.a \
  -L artifacts/graphics-sdk/osx-arm64/dawn/lib -lwebgpu_dawn \
  -framework CoreFoundation -framework CoreGraphics -framework CoreText \
  -framework Foundation -framework ImageIO -framework Metal -framework QuartzCore \
  -o out/webscene-graphite/graphite_probe
DYLD_LIBRARY_PATH="$PWD/artifacts/graphics-sdk/osx-arm64/dawn/lib" \
  out/webscene-graphite/graphite_probe metal
otool -L out/webscene-graphite/graphite_probe
```

Passed on Apple M4 / Metal, macOS 26.6.2 (25G83): Graphite wraps a Dawn-created
17x4 texture using the same device/queue, records a SkCanvas clear and submits it.
A subsequent Dawn diagnostic copy/map verifies all 68 pixels as RGBA 51,102,153,255
(tolerance 1). The executable links the existing `libwebgpu_dawn.dylib`; no second
Dawn library is linked. Readback is confined to this diagnostic executable.

This proves context creation, texture wrapping, submission and pixel correctness
for this clear operation. It does not yet prove sampling a retained canvas image,
clip/opacity/transform composition, resize lifetime, JavaScript WebGPU, or the
Avalonia/Uno host presentation boundary. Those remain the next integration tests.

## GPU image composition

The probe now produces a separate red image with a Dawn render pass, then samples
that texture through Graphite on the same device/queue. SkCanvas applies a clip
(2,1)-(8,3), a translation and 50% paint opacity over the existing background.
All 68 pixels pass on Apple M4: 12 clipped pixels match RGBA 153,51,77,255 and the
remaining pixels match 51,102,153,255 (tolerance 1). Producer-to-compositor ordering
uses queue submission order, with no intervening CPU wait or pixel upload. Only
the final diagnostic verification copies pixels to a mapped buffer.

This supersedes the clear-only scope above. The translation is exercised but a
uniform source cannot independently establish transform positioning correctness.
The probe still retains objects directly, not through WebScene's versioned image
lease pool. Patterned transforms, retained-image resize/disposal and actual host
presentation remain outstanding before claiming integrated canvas composition.

## WebScene lease lifetime integration

The source image now comes from `dawn_canvas_images::acquire/submit`, and Graphite
resolves it through an `owned_image_pool::consumer`. Before resolution, the probe
allocates a differently sized generation, verifies that it cannot alias the retained
image, cancels that unsubmitted replacement, then disposes the canvas owner and
retained scene reference. The consumer still renders the old pixels correctly.
It completes only after the diagnostic map (ordered after Graphite on the same
queue) establishes completion of GPU sampling. The full 68-pixel test passes on M4.

This replaces the direct-owner lifetime in the earlier probe. It tests preservation
across an allocated/cancelled resize, not presentation of a submitted replacement.
Actual scene-v3 acquisition, framework presentation and JS canvas bindings remain
outside this standalone native test.

## Submitted resize generations

The resize replacement is now submitted and cleared green through Dawn, rather
than cancelled. Both old and new images are retained by consumers after the canvas
and scene references are disposed, then sampled in the same Graphite recording.
The old generation remains red with 50% opacity in its 12-pixel clip; the resized
9x4 generation appears green in a separate 10-pixel clip. All 68 pixels pass on M4,
including the remaining background. Both consumer leases retire only after the
ordered diagnostic readback completes. This supersedes the cancelled-resize scope
above; it still does not exercise the application host or JavaScript bindings.

## IOSurface output boundary

On macOS, add `-framework IOSurface -framework CoreVideo` to the link command and
run `graphite_probe metal iosurface`. This enables Dawn IOSurface/shared-event
features, creates an RGBA IOSurface, imports it through SharedTextureMemory and
uses its texture as Graphite's output target. BeginAccess precedes rendering;
EndAccess follows the queued diagnostic copy. All 68 composition pixels pass on
M4, including old and resized source generations. The default private-texture
mode remains available.

This is the first actual shareable output allocation for the host bridge. It does
not import into CGL/OpenGL or implement host waits on the exported Metal fences.
No IOSurface CPU mapping or pixel upload is used; final readback remains diagnostic.
The native implementation follows the pinned Dawn IOSurface white-box test's
allocation/import pattern. This experimental mode is not a production allocator.

## CGL consumer verification

The IOSurface mode now uses BGRA storage and also imports the completed surface
into an accelerated CGL 3.2 context using a rectangle texture. Link additionally
with `-framework OpenGL` and define `GL_SILENCE_DEPRECATION`. The initial RGBA/byte
CGL tuple failed with error 10008; BGRA with UNSIGNED_INT_8_8_8_8_REV imports. Both
Dawn and GL diagnostic reads verify all 68 pixels after explicit channel-order
normalization. This supersedes the earlier RGBA output choice.

Passed on M4. CGL receives the IOSurface itself, not a CPU-uploaded bitmap. However,
the test deliberately starts CGL after the Dawn diagnostic map establishes producer
completion, and GL also reads back for verification. Thus it proves cross-API pixel
compatibility only, not asynchronous steady-state synchronization or Avalonia host
integration. Rectangle-to-host-2D texture handling and completion handoff remain.

## Rectangle-to-2D compatibility copy

The CGL consumer now blits its IOSurface-backed rectangle texture into a separate
GL_TEXTURE_2D framebuffer before diagnostic verification. All 68 pixels still pass
on M4. The destination allocation uses null initial data; glBlitFramebuffer performs
the transfer on the GPU. This is one explicit GPU-local copy, not zero-copy, and
must be counted if the actual Avalonia host adapter uses this route.

The destination is currently created by the standalone probe, not supplied by
Avalonia's shared context. Producer readiness still uses the diagnostic completion
wait. Host context integration, asynchronous fences and sustained performance
qualification remain outstanding.

## Connected Avalonia diagnostic host

Build the same source as a dylib by adding `-dynamiclib
-DWEBSCENE_GRAPHITE_HOST_PROBE -Dmain=graphite_probe_main` to the executable build
command and outputting `out/webscene-graphite/libwebscene_graphite_host_probe.dylib`.
Include all frameworks above. Run:

```sh
DYLD_LIBRARY_PATH="$PWD/out/webscene-graphite:$PWD/artifacts/graphics-sdk/osx-arm64/dawn/lib" \
  dotnet run --project experiments/WebScene.GpuHost.Probe -- --graphite --inspect
```

The managed host creates a 17x4 composition texture and calls the diagnostic native
entry point with that texture current in its shared CGL context. Native Dawn/Graphite
renders the retained generations into IOSurface; CGL blits into the host-owned 2D
texture and checks its pixels. Avalonia imports and displays it. The window capture
`docs/graphics/evidence/avalonia-host/dawn-graphite-window.png` shows the blended red
and green regions on blue background. Filtering of the enlarged tiny texture is
expected. This connects the previously separate probes on M4.

This remains diagnostic: native work blocks for map/readback verification, GPU
copies occur, resources are recreated per call, and there are no JS WebGPU bindings.
Do not use the exported probe entry point as a production API. Async readiness,
lease retirement through host completion, pooling and actual WebScene scene updates
remain required. No Kestrel or end-to-end WebScene WebGPU pass is claimed.

## Host transfer without diagnostic pixel traffic

The in-process host entry point now skips readback-buffer allocation, texture-to-
buffer copy, map and glReadPixels. It awaits Dawn queue completion without a pixel
transfer, performs the IOSurface-to-host GPU blit, and uses glFinish for GL lifetime
safety. The host import/update/visual commit succeeds on M4 with diagnosticReadback
false and verifiedPixels zero. The standalone mode still performs all 68-pixel
checks and passes, so execution success is not mislabeled as pixel verification.

This supersedes the host's readback-dependent readiness above. It remains a
synchronous prototype: queue waiting and glFinish must be replaced by asynchronous
readiness and consumer retirement before performance acceptance. There is still
one explicit GPU-local blit, plus any host snapshot work; this is not zero-copy.

## Nonblocking GL consumer completion

Normal host blits now insert GL_SYNC_GPU_COMMANDS_COMPLETE and flush, retaining
IOSurface and temporary GL objects in one thread-local pending transfer. The host
polls with glClientWaitSync(timeout=0) under the owning context, yielding between
checks. Successful signal retires retained objects before the Avalonia update.
A second pending transfer is rejected. The connected M4 host test passes without
normal-path glFinish and without readback. glFinish remains only for error/timeout
cleanup. Failure/device-loss cleanup still needs dedicated qualification.

Dawn producer readiness still blocks, and this is a single-transfer diagnostic
bridge, not the persistent production scheduler. The later engine implementation
must use its wake/completion mechanisms instead of a polling UI prototype.

Pending host transfers now retain their CGL context as well as IOSurface/GL
resources. The native entry point rejects overlapping work before constructing a
new Dawn/Graphite frame and rejects a missing current context. The managed probe
attempts a second submission while the first fence is pending, verifies rejection,
then checks that polling after retirement rejects duplicate completion. The full
host update/commit test passes on M4. These checks do not qualify context-loss
recovery or replace the remaining synchronous Dawn producer wait.

## Deferred Dawn producer delivery

The host submission now returns after registering AllowSpontaneous queue completion.
A pending owner retains Graphite context/recorder/recording, output IOSurface and
both source consumers. Host polling checks the completion flag without waiting;
once ready, it performs the CGL blit on the owning context, retires source consumers
and then polls GL completion. Overlap rejection covers both pending stages. An
explicit timeout drain can still wait, but normal producer and consumer completion
do not block. The M4 host import/update/commit test passes, and standalone diagnostic
verification still passes all 68 pixels.

This supersedes the normal producer wait noted above. Adapter/device initialization
still uses synchronous request waits and is recreated per invocation. Pending state
is single-transfer and thread-local; cancellation, allocation-failure and device-loss
paths need further qualification. Production requires persistent engine-owned state,
bounded pooling and completion wakes rather than this diagnostic polling interface.

### Persistent Dawn runtime across host submissions (2026-09-07)

The host diagnostic now retains its Dawn instance, hardware adapter and device
on its calling thread. Initialization occurs once; an uncaptured device error
makes later submissions fail instead of silently rebuilding the device.
Callback state outlives device destruction. Standalone invocations retain their
own runtime and pixel-verification path.

The Avalonia probe submits eight sequential native compositions before importing
the final texture into its compositor. Every submission rejects overlap, polls
producer and GL completion, and rejects duplicate completion. It checks exactly
one successful Dawn device initialization. This verifies repeated native
submission and resource retirement, not eight displayed frames.

Local Apple M4 / Metal validation:
- Host run exited 0: graphiteSubmissionsCompleted=8,
  dawnDeviceInitializations=1, sharedTextureUpdateCompleted=true,
  visualCommitCompleted=true, presentationVerified=false.
- Standalone IOSurface/CGL diagnostic exited 0 with all 68 pixels verified.

Graphite contexts, recordings, canvas pools and output allocations are still
created per submission. The probe has no device-loss recovery, throughput
qualification, browser bindings or production WebScene presenter integration.
This evidence closes no epic sub-issue.

### Persistent Graphite context (2026-09-07)

The retained Dawn runtime now also owns one Graphite context. Frame closures
retain that context while producer work is pending. Completion polling services
Graphite's completion queue on its owner thread and waits until
hasUnfinishedGpuWork() is false before successful delivery. This avoids relying
on context destruction to retire each submission.

The host diagnostic now runs 64 sequential submissions and asserts one Dawn
device and one Graphite context initialization. Local Apple M4 / Metal run exited
0 with graphiteSubmissionsCompleted=64, graphiteContextInitializations=1,
dawnDeviceInitializations=1, sharedTextureUpdateCompleted=true and
visualCommitCompleted=true. The standalone IOSurface/CGL test still passed all
68 pixels. Only the final texture is imported into Avalonia; this does not
verify 64 displayed frames or establish a throughput/memory benchmark.

Recorder, recording, canvas-pool and output-texture allocation reuse remain
unfinished. No browser API or production presenter is added by this change.

### Repeated Avalonia handoffs (2026-09-07)

The host diagnostic now imports the shared GL texture once and attaches its
drawing surface before the submission loop. Each of the 64 native submissions
awaits producer/GL completion, awaits CompositionDrawingSurface.UpdateAsync,
and awaits a compositor commit before the next write. The imported image and
surface remain alive throughout; detach/commit occurs before disposal.

On Apple M4 the run exited 0 with graphiteSubmissionsCompleted=64 and
hostUpdatesCompleted=64, one Dawn device and one Graphite context. The plain GL
baseline also exited 0 with one host update and zero native initializations.
This supersedes the earlier final-texture-only handoff test. The image content
is unchanged across submissions, so this tests repeated consumption/lifetime
and API completion, not detection of stale frames or a count of physical
display presentations. No pixel readback was added to the host path.

### Reuse the fixed-size output allocation (2026-09-07)

The diagnostic runtime retains its 17x4 IOSurface, Dawn shared-memory import and
output texture. Each submission begins a new access interval and clears the
whole output; EndAccess and existing producer/CGL completion checks still gate
the next submission. The managed test also waits for Avalonia consumption before
overwriting its separate borrowed GL destination.

Apple M4 validation exited 0 with 64 native submissions, 64 host updates and
outputTextureAllocations=1 (also one device and one Graphite context).
Standalone diagnostic verification passed all 68 pixels. This is fixed-size,
serialized reuse; it does not qualify resizing, device loss or overlapping
presentation. Canvas source pools, recorders and CGL import objects are still
allocated per submission. No CPU pixel transfer was introduced.

The prerequisite review still leaves #23 open: the latest hosted CI was pending
at this check, and required cross-platform hardware evidence remains incomplete.

### Detect stale output during reuse (2026-09-07)

Host submissions now paint their caller-supplied frame serial into the otherwise
unused pixel at (16,0). The optional --verify-markers host argument calls a
diagnostic CGL readback after native completion and before Avalonia consumption.
It checks that pixel against the independently supplied expected serial and also
requires an intentionally wrong expected serial to fail. The verifier preserves
read-framebuffer and pixel-pack state. Normal --graphite runs never call it.

Apple M4 validation passed both modes:
- --graphite --verify-markers: 64 matching markers, 64 wrong-marker rejections,
  64 host updates, one output allocation, one device and one Graphite context.
- --graphite: 64 host updates with diagnosticMarkersVerified=0.

The marker tests changing content at the native-to-host texture boundary. It
does not read Avalonia's final compositor output or prove physical presentation.
The previous claim that all host submissions use identical content is superseded.
The diagnostic export/signature is private to this probe, not the native engine ABI.

### Reuse the source canvas pool (2026-09-07)

The host runtime now retains its bounded dawn_canvas_images pool. Submissions
advance allocation-generation/content metadata, retain the old 17x4 image while
producing its 9x4 replacement, and complete both consumers before reuse.
Delivery additionally waits for both producer callback statuses, avoiding a
race between aggregate queue completion and individual producer retirement.
Standalone mode still destroys the canvas owner before resolving the consumers,
preserving that separate ownership test.

Apple M4 host runs with and without --verify-markers both passed 64 native and
64 Avalonia updates with exactly two source allocations and one output allocation.
The final pool busy count was asserted zero. Marker mode verified all 64 changing
markers and rejected all deliberately wrong expectations; normal mode performed
no marker readbacks. Standalone IOSurface/CGL verification passed all 68 pixels.

This removes source-pool recreation from this serialized diagnostic workload.
Recorders, command/recording objects and CGL imports remain per-submission.
It does not qualify production scheduling, resize of the host target, memory
pressure, device loss or actual browser APIs.
