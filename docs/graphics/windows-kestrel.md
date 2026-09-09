# Windows Avalonia WebGPU implementation and local evidence

This implements the Windows Avalonia path from #45, using PR #43's retained
scene/publication behavior. It is a platform checkpoint for epic #22, not closure
of its conformance, Uno, WebGL, or distribution gates.

## Image path

The producer explicitly requests Dawn D3D12 on the selected hardware DXGI adapter.
For the measured approximately 60 Hz evaluation mode, also set
`WEBSCENE_SINGLE_SCENE_PER_FRAME=1` alongside incremental canvas GPU rendering
and asynchronous preparation. This preserves one ordered scene per compositor
frame and adds approximately one refresh of queue latency. It remains opt-in;
see [the pacing measurements](windows-chromium-pan-comparison.md) for results and
presentation limitations.

Each canvas owns a D3D12 allocator and a bounded four-image pool. BGRA8 sRGB color
allocations have render-target and simultaneous-access flags and are exported as
owned NT handles. Dawn imports these through SharedTextureMemory, brackets each
frame with BeginAccess/EndAccess, and exports all producer fence/value pairs.
Private MSAA/depth attachments stay in the application/Dawn renderer.

`platform_webgpu_canvas.h` selects the platform provider while retaining the
existing macOS rendering-opportunity, exact scene snapshot, generation, resize,
validation and queue-completion logic. Windows uses the DXGI scene snapshot;
macOS continues to use the IOSurface implementation.

The Avalonia consumer obtains the actual ANGLE D3D11 device from its active Skia
platform lease. It verifies the adapter LUID and native D3D11 device/context fence
interfaces, imports the texture, opens every producer fence, and enqueues the
waits before drawing. EGL wraps that D3D11 texture for Skia. Drawing occurs in the
ordinary scene paint order, so DOM content, clips and opacity use the existing
renderer. No presentation pixel readback/upload or CPU GPU-completion wait is
introduced by this path.

Retirement submits Skia/ANGLE's final reads and signals a D3D11 fence on the
composition owner. The bridge and image consumer remain retained until that fence
completes. Detached retirement seals graphics work before transferring polling
to the background worker. Timeout or device loss retains the owner for diagnosis;
it does not certify completion or reuse the allocation.

The common managed presenter/image-group/retirement classes now use platform
neutral names. The macOS image importers remain separate.

## Reference behavior

The implementation follows the existing macOS files and Chromium's separation of
producer access, shared-image dependencies and consumer retirement. Chromium was
inspected at revision `0c3eb92c416a23d3a55a33a22cc2a65073105063`:

- [D3D image backing](https://chromium.googlesource.com/chromium/src/+/0c3eb92c416a23d3a55a33a22cc2a65073105063/gpu/command_buffer/service/shared_image/d3d_image_backing.cc): stage fence imports before enqueueing waits and track completed access separately.
- [GPU canvas context](https://chromium.googlesource.com/chromium/src/+/0c3eb92c416a23d3a55a33a22cc2a65073105063/third_party/blink/renderer/modules/webgpu/gpu_canvas_context.cc): reshape replaces drawing-buffer resources without treating the canvas as unconfigured.
- [WebGPU swap-buffer provider](https://chromium.googlesource.com/chromium/src/+/0c3eb92c416a23d3a55a33a22cc2a65073105063/third_party/blink/renderer/platform/graphics/gpu/webgpu_swap_buffer_provider.cc).

Avalonia's actual 11.3.4 EGL/D3D11 sources were used to identify the public platform
lease and texture-wrapping APIs. Its advertised keyed-mutex import route is not
assumed to support Dawn's D3D12 fence handoff.

## Local verification (2026-09-08)

Host: Windows 11 build 26200, x64, NVIDIA GeForce GTX 1660 Ti,
driver `32.0.15.9186`, adapter LUID low `65215`, high `0`.
Managed SDK: .NET 10.0.103. V8: verified inspector-enabled `15.3.10`, pointer
compression/shared cage enabled, PartitionAlloc and ThinLTO disabled.
Dawn and ANGLE retain the revisions in `eng/graphics/dependencies.lock.json`.

- Five hardware/handle probes pass: Dawn D3D12, ANGLE D3D11 ES2/ES3, NT handle
  ownership, and the new Dawn-to-D3D11 shared-pixel probe. The latter checks all
  68 pixels, channel order, mismatched adapter rejection, unsealed completion
  rejection and consumer retention. Its CPU readback is diagnostic only.
- All 18 graphics-enabled native tests pass, including asynchronous mapping,
  device loss isolation, retained/resized image pixels and V8 runtime coverage.
- A clean graphics-disabled native build passes all 12 tests.
- The Avalonia managed suite passes 277 tests with 15 platform-dependent skips.
- All 15 graphics build/packaging Python tests pass.
- The original Kestrel archive verifies all 54 immutable files. The document hash
  remains `0549ac0817db91f4df5ff8e6274843a72cec3b91a5aa6e32101e3f2a888c0563`.
- Unchanged Kestrel reports WebGPU with 4x MSAA and zero application errors. Its
  courtyard drawing was visually inspected, and native mouse-wheel zoom changed
  the rendered drawing. Native-input panning, sidebar changes, discrete and
  continuous resize, and command editing/undo/redo pass the existing workloads.
  Verification runs await view disposal before shutting down.

Logs and manifests are under `artifacts/windows-kestrel/`, including
`native-probes.json`, `test-all-probes.log`, `test-native-integrated.log`,
`test-native-disabled.log`, `test-results/windows-integration.trx`, and
`kestrel-{integrated-startup,pan,sidebar-resize,continuous-resize,edit}.log`.
`pan-analysis.json` measures callback timing, not physical presentation latency.
These local artifacts are deliberately not committed as platform certification.

## Reproduce

Build the pinned graphics SDKs as described in `eng/graphics/README.md`. Windows
needs the exact shader compiler and license from `windows-runtime.json`; the
builder verifies and stages them beside Dawn. A short source/build root avoids
upstream path-length restrictions. The tested compiler build used Visual Studio
18.4 and SDK 10.0.28000.2526; the local ANGLE SDK override used installed
10.0.26100.7705 debugger helpers for symbol tooling. SDK manifests record source
graphs, compiler versions, flags, licenses and binary hashes.

```powershell
./scripts/build-native-engine-runtime.ps1 -Rid win-x64 `
  -V8Root <verified-v8-root> -BuildDirectory C:/graphics/native `
  -GraphicsSdk artifacts/graphics-sdk/win-x64
dotnet build experiments/WebScene.GpuHost.Probe -c Release
$env:WEBSCENE_TEST_NATIVE_LIBRARY='C:/graphics/native/Release/webscene_native_engine.dll'
dotnet experiments/WebScene.GpuHost.Probe/bin/Release/net10.0/WebScene.GpuHost.Probe.dll `
  --webgpu-document --kestrel tests/GraphicsCompatibility/fixtures/Kestrel-CAD.zip `
  --verify-kestrel
```

Add `--pan-kestrel`, `--sidebar-kestrel --resize-kestrel`,
`--continuous-resize-kestrel`, or `--edit-kestrel` in separate runs. Omit
`--verify-kestrel` to leave the application open for inspection.

## Remaining qualification

### Font preparation and frame-clock investigation (2026-09-08)

PR #43 already contains `5547397` (system-font-name probe caching), following
`859afac` (text preparation attribution). The macOS history and measurements are
in `coherent-gpu-scene-publication.md`, under “Cache repeated system font-name
probes”: CPU application fell from approximately 15.6 ms to 2.93 ms in the
recorded single run. The existing 256-entry lookup cache checks document/global
web fonts first, so a cached system-family miss cannot hide a subsequently loaded
web font. That implementation is present on this branch.

Windows sampled-thread profiling also identified repeated `SKShaper.Shape`
calls during DOM and Canvas2D preparation. `NativeTextShaping` now caches
origin-relative glyph runs by actual typeface object identity, text, size,
horizontal scale and encoding. Weak face references avoid owning document font
resources; identity is checked after hash lookup. Admission is bounded by 2048
entries and 4 MiB of estimated run storage. Draw position, alignment, spacing and
rasterization remain outside the cache. Replaced fonts cannot reuse an old face's
glyphs. The native-enabled net10 text/font subset passes 124 tests with two
platform-specific skips, including uncached-versus-cached pixel equality at two
baselines. See `test-pacing-fonts-net10.log` and its TRX under local artifacts.

The initial cache trial's median CPU application was 5.92 ms, versus approximately
16.82 ms in the earlier Windows pan log. These are exploratory, unmatched runs,
not a controlled performance comparison or proof of 60 fps.

Chromium reference revision `0c3eb92c416a23d3a55a33a22cc2a65073105063`:

- [`shape_cache.h`](https://chromium.googlesource.com/chromium/src/+/0c3eb92c416a23d3a55a33a22cc2a65073105063/third_party/blink/renderer/platform/fonts/shaping/shape_cache.h)
  bounds repeated shaping and invalidates when its font version changes.
- [`vsync_thread_win_dcomp.cc`](https://chromium.googlesource.com/chromium/src/+/0c3eb92c416a23d3a55a33a22cc2a65073105063/ui/gl/vsync_thread_win_dcomp.cc)
  waits on the Windows compositor clock.
- [`vsync_thread_win.cc`](https://chromium.googlesource.com/chromium/src/+/0c3eb92c416a23d3a55a33a22cc2a65073105063/ui/gl/vsync_thread_win.cc)
  guards waits that return early during display sleep or occlusion.

Avalonia 11.3.4's default WinUI timer follows commit completion. Its alternative
LowLatencyDxgiSwapChain mode spun rapidly while the monitor was off; that trial
was rejected. Windows returned `0xc01e0006`
(`STATUS_GRAPHICS_PRESENT_OCCLUDED`) from `DCompositionWaitForCompositorClock`.
The user confirmed the monitor was switched off. Cadence measured in that state
must not be interpreted as active-display performance.

The **diagnostic-only** `--webgpu-vsync` probe installs Avalonia's existing render
loop with a Windows 11 compositor-clock timer. Avalonia 11.3.4 has no public loop
injection API, so this probe opts into private APIs and reflects the pinned loop
registration before platform initialization. This is not enabled in the backend
or the normal `--webgpu-document` host. Occluded/early waits back off and emit
separately counted fallback ticks to keep startup and disposal live. The probe
prints real/fallback tick counts on exit. The clock has its own thread, separate
from rendering, with one pending notification carrying the latest timestamp.
Active-display tests now receive approximately 60 compositor callbacks per second.
Completed D3D11 retirement fences are polled before admitting the next RAF, so
already-free producer storage does not unnecessarily defer it by one display
interval. This is a nonblocking completion poll, not a GPU wait.

The input worker now keeps pointer moves out of frame/wheel coalescing prefixes
when a compositor clock is active. An already-observed frame event also cannot
prematurely release a newer retained pointer sample. Button, wheel and keyboard
events remain ordering barriers. The native `raf-aligned-mouse-moves` regression
includes the delayed-frame case and verifies that the next boundary delivers the
retained position exactly once.

Additional preparation caches retain bounded text blobs, reuse immutable paints
within a replay, pool up to 64 shapers per renderer, and reuse unchanged DOM
pictures after exact command/string/viewport/font-version comparisons. The text
blob key includes autohinting and the resolved font rasterization profile. Font
registration invalidates renderer caches. The optional environment setting
`WEBSCENE_ASYNC_CANVAS_PREPARATION=1` prepares immutable Canvas2D pictures ahead
of drawing; it does not acknowledge publication or change the visible scene.
It remains experimental and off by default: lower application time did not
establish 60 fps overall.

### Active-display measurements and remaining Canvas2D bottleneck

Measurements below use the same immutable fixture on the GTX 1660 Ti at 175%
display scale, six 80-move pan cycles, and 120 Hz synthetic pointer input. The
first second and the final settling interval are excluded. Rates count completed
draw callbacks, not physical presentation. These short runs are diagnostic,
not a statistical benchmark.

| Run | Window width | Draw callbacks/s | Host frame callbacks/s |
| --- | ---: | ---: | ---: |
| Corrected input ordering, three-image pool | 1282 | 58.38 | 60.00 |
| Same build, default window size | 1280 | 42.94 | 43.90 |
| Default size, optional asynchronous preparation | 1280 | 47.97 | 50.68 |

Local logs are `pacing-pointer-clean.log`, `pacing-pointer-default.log` and
`pacing-pointer-async.log` under `artifacts/windows-kestrel`. The minimal WebGPU
triangle reached approximately 60 RAF/draw callbacks per second after pre-frame
retirement polling. A separate headed Chromium/Edge control using real pointer
input reached approximately 60 application RAF callbacks per second on the same
GPU; neither result certifies physical scanout.

Kestrel rounds its CSS canvas size, multiplies by DPR, then rounds the bitmap
size again. At the default width, its 806 CSS-pixel clear spans 1410.5 pixels in
a 1411-pixel bitmap. That clear does not geometrically cover the backing store,
so the native display list retains previous drawing commands. It grew from
roughly 300 to over 40,000 commands during a pan. At window width 1282, the
808-pixel clear exactly covers the 1414-pixel backing width and native full-clear
compaction keeps roughly 320 commands. Width 1282 is a diagnostic control,
not a general fix or the default evaluation size.

Chromium GPU canvas readback confirms that a half-pixel clear edge retains
partial alpha. The CPU `willReadFrequently` path can behave differently, so
reusing one context across repeated readbacks is not a reliable GPU control.
Rounding the clear up to discard the retained border would be incorrect.
Extra image-pool slots and early read-fence signaling did not establish 60 fps
and were removed. The optional `WEBSCENE_INCREMENTAL_CANVAS_GPU=1` implementation
now retains a GPU Canvas2D backing and compiles only appended commands when the
previous replay state can safely resume. Fractional clear edges are preserved.
Clips, unbalanced saves, canvas dependencies and external SVG images use full
replay. Generation, dimensions, scale and font-registration changes invalidate
the continuation. CPU rendering/export retains a separate, balanced picture
history and does not depend on reading the GPU backing.

Native publications still contain complete replacement payloads. The optional
`WEBSCENE_CANVAS_LAYER_UNCHANGED_PREFIX` flag certifies an unchanged prefix against
the acknowledged base revision; older consumers can ignore it. Native command
hashing resumes from that base, and a three-entry scene-storage pool retains at
most 64 MiB of reusable vector capacity. Explicit low-memory notifications clear
the pool and publication scratch buffers. Eligible canvases now retire old
commands using [raster checkpoints](windows-canvas-checkpoints.md); payloads
contain a bounded current window rather than the entire drawing history.

With both GPU backing and asynchronous preparation enabled, 24-cycle, 16-second
runs at the default width now measured 58.46 drawn callbacks/s for out-and-back
panning and 58.33 for circular panning. Host frame callbacks remained 60.00/s
with zero fallback clock ticks. Logs are `pacing-version-prefix-sustained.log`
and `pacing-circular-sustained.log`. The circular control avoids repeated points
at direction reversals, and still misses frames. Smooth 60 fps at arbitrary
sizes is **not complete**. Both features remain opt-in.

A subsequent 90-cycle, one-minute circular run exposed history-dependent
degradation: 44.92 drawn callbacks/s while the host clock remained 60.00/s
(`pacing-backing-minute.log`). Transferring the already-built command allocation
into the publication, instead of copying it again, improved this to 49.55/s
(`pacing-transfer-minute.log`). Dependency discovery now also caches the
`drawImage(canvas)` graph within each canvas generation and scans only appended
commands. Reset invalidation and transitive detached dependencies have a native
regression check. These longer runs take precedence over the short-run figures
when assessing sustained performance.

Removing the managed full-history dependency scans measured 52.13/s. Raising
the bounded scene-storage cache from 64 to 256 MiB then measured 56.00/s over
one minute, with ten-second segments between 54.99 and 57.80/s. Final native
publication time fell from 18.97 to 8.32 ms. See
`pacing-managed-prefix-minute.log` and `pacing-pool-budget-minute.log`.
This prevents cache rejection once a reusable buffer exceeds 64 MiB, but does
not bound the underlying command history. A larger cache is not a replacement
for incremental publication; indefinite panning and physical 60 Hz presentation
remain unqualified.

Use `scripts/test-windows-kestrel.ps1 -VSync` for the compositor-clock workload
suite. For a longer pacing trace, add `--pan-long --pan-input-120hz
--pan-high-resolution-input` to the probe. The latter is a Windows high-resolution
waitable timer for **synthetic input only**; display scheduling remains driven by
the compositor clock. Logs record both requested input frequency and whether
high-resolution input pacing was enabled.

The checkpoint build passes all 18 native CTest targets and 306 managed
Avalonia tests on each of .NET 8 and .NET 10 (eight platform-specific skips). All seven compositor-clock
Kestrel workloads pass with both optional features enabled, including awaited
shutdown. The GPU pixel probe compares two sets of 48 frames, including repeated
checkpoints and recovery after discarding retained GPU pictures. Periodic
checkpoint capture also reads back pixels for CPU recovery storage; it is not
part of the WebGPU presentation bridge. See `test-checkpoint-native-all.log`,
`test-checkpoint-managed-all.log`, `test-checkpoint-managed-net8.log`,
`test-checkpoint-gpu-final.log`, and `checkpoint-qualification/results.json`.
The shared Uno renderer also compiles successfully; this is not a hardware
qualification of its presenter.
The separate evaluation copy runs from
`artifacts/windows-kestrel/evaluation-bounded-history` with its own native runtime
directory and the title “Kestrel in WebScene — bounded canvas history”. Earlier
evaluation processes and their document state are preserved.

Only win-x64 on this adapter/driver is covered. The current producer selects DXGI
adapter zero and rejects a different host LUID; hybrid GPU selection, monitor/DPR
transitions and win-arm64 require further work. Hardware removal, prolonged
stress/leak counters, physical presentation timing and native live-resize frame
capture are not certified by these workload logs. A static visual inspection does
not establish absence of resize flicker.

Uno composition, WebGL browser API/fallback behavior, full CTS/WPT coverage and
macOS regression hardware runs remain separate epic gates. Native ANGLE probes
alone do not qualify browser WebGL. Unsupported color formats/tone mapping fail
explicitly; there is no CPU fallback presenter.
