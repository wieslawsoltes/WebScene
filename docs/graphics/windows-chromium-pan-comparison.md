# Chromium comparison and remaining WebScene work

The same unmodified Kestrel fixture runs at approximately 60 application RAF
callbacks/sec in Chrome on this machine. Kestrel's spatial-index work alone does
not establish a sub-60 Hz limit. The next optimization work should stay in
WebScene's frame pipeline, consistent with the user's requested scope.

## Workload and evidence

Chrome for Testing 153.0.8010.36, revision
507c6ee3e2f3b2ca0e660547e5b9ea4820c67f4c, V8 15.3.76.10, was downloaded from the
official Chrome for Testing distribution. No existing browser installation or
profile was modified. Extracted fixture files were byte-compared with the ZIP:
zero mismatches. ZIP SHA256 is
1e9a272449923ea1d2a24b4ce7f1a1f424d0a6c9a979ef155e2a67c1424d9f7d.

The workload uses the default 265-entity drawing, a 1280x800 CSS document at DPR
1.75, 806x463 canvas layout and 1411x810 bitmap, and 15 circular pan cycles of
80 steps with radius 40 CSS pixels and 60 Hz input deadlines. Chrome uses CDP
mouse injection; WebScene uses its native input queue. These paths have different
delivery/coalescing costs, so this is not a perfectly isolated renderer A/B test.

Chrome reports WebGPU active, GPU compositing and WebGPU enabled, and an ANGLE
NVIDIA GTX 1660 Ti D3D11 renderer. The app does not expose adapter information in
the queried renderer field; per-device WebGPU fallback status was not captured.

| Run | Application rate | Validation |
| --- | ---: | --- |
| Chrome, method timing wrappers | 59.94 RAF/sec | ordered drag input passed |
| Chrome, RAF/event tracing only, fresh visible window | 59.94 RAF/sec | input passed; camera movement and panning checked at three points |
| WebScene, previous active-display light run | 54.15 RAF/sec | input passed, zero vsync fallbacks |

Chrome input delivery measured 59.93 and 59.90 Hz respectively. RAF rates exclude
the first second and final half-second of recorded timestamps. All figures are
application callbacks, not physical scanout. The initial second-navigation run
was abandoned; a later hidden-page run failed validation and was discarded.
The final visible run explicitly checks visibility and changing camera targets.

Artifacts under artifacts/windows-kestrel:

* compare-chrome-pan.mjs: repeatable benchmark, --light disables method wrappers.
* chrome-pan-methods.json: first valid method-level trace.
* chrome-pan-light-visible.json: valid fresh-window run with camera checks.
* bottleneck-methods.log: earlier WebScene method-level trace.
* pacing-fenced-display-no-telemetry.log: latest WebScene light trace.

## Measured costs

| Inclusive application method | Chrome median / p95 ms | WebScene median / p95 ms |
| --- | ---: | ---: |
| ensureIndex | 7.0 / 8.8 | 5.65 / 12.06 |
| pointerMove | 7.2 / 9.1 | 6.00 / 12.38 |
| RAF callback | 0.80 median | 1.49 median |

These runs were not simultaneous and use different V8 builds and clock
resolution. Method wrappers have overhead and nested times must not be added.
Chrome's median index cost is not lower; WebScene has a larger measured tail.
The most recent preceding pointer event's document-listener timestamp to RAF
start has median 0.10 ms / p95 0.20 ms in Chrome, versus 1.58 / 2.58 ms in
WebScene. That is an interval between observable callbacks, not an isolated
scheduler measurement: native work, microtasks and style/layout can contribute.

## Chromium source findings

1. **Clock phase and interval.** Chromium's compositor-clock path obtains the
   completed frame ID and statistics after waiting, deriving phase and period
   from those statistics. WebScene's diagnostic timer currently uses wake time
   and does not fetch those statistics. Chromium also conditionally disables
   DXGI vblank virtualization when this path is enabled. The
   [January 2026 change](https://chromium.googlesource.com/chromium/src/+/6f50085ee81fb259aca44cd2c4704bf28955da17)
   specifically addresses pacing regressions. This is a candidate, not proof of
   the cause here: WebScene already measures a stable 60 Hz wake rate. Test phase
   alignment and input-to-RAF delay before assuming this fixes throughput.
2. **Readback completion is asynchronous to the caller.** Chromium's
   [raster implementation](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/gpu/command_buffer/client/raster_implementation.cc)
   queues ARGB readback through shared memory and a completion query, then drains
   callbacks in request order. Its source explicitly says the GPU-service
   readback implementation used by that path is synchronous. This is not a
   universal nonblocking GPU-copy primitive to transplant. WebScene's fence now
   defers waiting for prior GPU work, but its pixel copy still runs on the
   composition owner. Further work requires staging/readback ownership or a
   separate suitable execution path, not merely another completion fence.
3. **History retirement does not inherently require PNG.** The
   [canvas resource provider](https://chromium.googlesource.com/chromium/src/+/fba1706eb1819925eaf9791a13b5cd04fa6a2134/third_party/blink/renderer/platform/graphics/canvas_resource_provider.cc)
   releases recorded commands into raster backing and normally drops the retained
   recording afterward. WebScene additionally maintains portable CPU recovery and
   export checkpoints. A GPU-resident checkpoint could avoid periodic readback on
   the render path, but needs an explicit recovery/export design first.

Source review used Chromium main and the linked pinned canvas/clock revisions;
it does not prove which feature flags the measured Chrome binary enabled.

Recommended sequence: instrument WebScene input completion, style/layout, RAF
start and publication within one frame; experimentally compare compositor-clock
statistics-based phase against the current wake-time approach; then move remaining
checkpoint pixel transfer off the composition critical path. Keep the fixture and
Kestrel snapping behavior unchanged, and retain existing pixel/recovery tests.

## Experimental clock phase and worker readback (September 9)

The probe now supports `--compositor-clock-statistics`, sampling completed-frame
start time and period after the compositor wait and publishing that phase to
WebScene's frame callback. It also disables DXGI vblank virtualization when this
option is selected. Live-resize expiry still uses current wall-clock QPC.

`WEBSCENE_ASYNC_CANVAS_READBACK=1` enables an experimental GLES pixel-pack buffer:
queue a staging copy and readback, poll its fence without waiting, then map and
copy on a worker before encoding. The worker must acquire Avalonia's graphics
context lock; this does not eliminate contention or the pixel copy. The default
remains the previous deferred-fence path. Neither experiment changes Kestrel.

Same-build, 20-second camera-validated circular pans, active display, 60 Hz input:

| Mode | Draw callbacks/second |
| --- | ---: |
| Existing copy path | 54.78 |
| Compositor statistics only | 54.83 |
| Worker readback only | 53.72 |
| Both experiments | 55.25 |

All used the compositor clock without fallback. These single runs are diagnostic,
not a statistically established ranking or physical presentation measurement.
Logs are `artifacts/windows-kestrel/pacing-copy-baseline.log`,
`pacing-clock-only.log`, `pacing-pbo-only.log`, and `pacing-pbo-clock-both.log`.

With `WEBSCENE_TRACE_CANVAS_CHECKPOINTS=1`, nine checkpoints occurred per run.
Baseline completion cost 3.90–7.47 ms on the compositor thread. Worker-mode
completion cost 0.018–0.362 ms, but queueing cost 1.93–9.48 ms and worker transfer
including context-lock acquisition cost 6.49–22.51 ms. Thus copy work explains
periodic pressure, but moving it did not establish a throughput improvement.
The remaining approximately five missed draw callbacks per second cannot be
attributed solely to nine checkpoint completions in twenty seconds.

The GPU probe verifies transferred RGBA/alpha, row orientation and immutable
snapshot contents after the source surface changes. Existing GPU backing and
checkpoint recovery comparisons also pass. Both experiments remain opt-in pending
evidence of a benefit; removing portable checkpoints entirely would require a
separate recovery/export design. Next isolate input completion through RAF and
publication, where the earlier Chrome comparison showed additional WebScene delay.

## Input admission and pipeline trace

`WEBSCENE_TRACE_FRAME_PIPELINE=1` now records worker input/frame dispatch,
animation batches, scene publication outcomes, runtime events and layout checks.
Each engine/runtime owns a bounded 65,536-entry ring; it is allocated only when
enabled and printed at shutdown, avoiding per-frame console I/O. The records use
steady-clock nanoseconds. `eng/graphics/analyze-frame-pipeline.py` merges records
by timestamp and restricts analysis to the probe's panning interval after warmup.
Layout can be nested inside event work; do not add overlapping durations.

The trace exposed a concrete admission error: a queued pointer sample was assigned
the compositor boundary observed when the worker dequeued it. If a previous input
handler crossed a boundary, an already eligible sample could be delayed another
refresh. The private queue now carries the boundary observed at enqueue, without
changing the public input ABI. A last-dispatch boundary prevents continuous input
from running twice in one interval; button/key/wheel and script ordering barriers
continue to flush pending input as before.

In `pacing-input-admission.log`, approximately 60 pointer updates and animation
batches per second ran during the measured interval. Input-end to animation-start
was 0.021 ms median and 0.032 ms p95, compared with 0.023/6.247 ms in the preceding
detailed trace. The latter includes animation batches without a fresh pointer
dispatch, so it is not purely scheduler execution overhead. Median input dispatch
was 7.73 ms, including about 1.24 ms of layout checks and 6.23 ms of event dispatch;
these categories can overlap. Animation batches cost 1.46 ms and publication
2.09 ms median. Publication did not defer during this measured interval.

With pipeline tracing and probe performance telemetry disabled,
`pacing-input-admission-light.log` validated the circular pan and measured
59.994 actual animation callback starts/second after trimming one second from
each end. Median callback gap was 16.674 ms and maximum 28.503 ms. The compositor
reported 1,467 vsync ticks and zero fallback ticks. This confirms approximately
60 Hz animation cadence in this run, not uniform frame times or physical scanout.
The instrumented draw callback rate remained 56.02 Hz; publication-to-draw latency
was 8.86 ms median and 12.32 ms p95. Renderer/presentation timing remains unresolved.

Validation: all 18 native CTest suites passed, including a regression that queues
a second pointer during a slow handler, advances the clock before completion,
and requires dispatch without an additional clock edge or script barrier.
Managed net10 tests passed (306 passed, 8 platform skips). The original Kestrel
ZIP remains unchanged (SHA256
`1e9a272449923ea1d2a24b4ce7f1a1f424d0a6c9a979ef155e2a67c1424d9f7d`).

## Publication deadlines and ordered pacing

The missing draw slots commonly coincided with a new publication arriving
0.5–2 ms after the compositor's acquisition attempt. The previous scene had
already been applied, so acquisition correctly returned empty. This was not a
slow acquisition call. Publication tracing now separates layout, DOM construction,
metadata and canvas-list construction. In `pacing-publication-detail.log`, layout
cost 1.20 ms median, DOM construction 0.64 ms and canvas-list construction 0.13 ms.
Most frames also forced 1.26 ms of layout during input solely to resolve the cursor.

Frame-paced Windows pointer moves now defer that final cursor lookup until
publication layout and ResizeObserver delivery finish. JavaScript geometry reads
remain synchronous, as do unpaced pointer updates. A native regression verifies
that a cursor changed again by RAF is resolved from the final publication state.
The measured input layout cost fell to approximately zero and median input
dispatch fell from 7.65 to 6.43 ms. A 58.20 Hz instrumented draw run followed,
but a draw-only repeat was 56.57 Hz; do not treat the first run as a stable rate.

`WEBSCENE_TRACE_DRAW_CALLBACKS=1` records only bounded draw-completion timestamps
without enabling the full performance census. It still measures OnRender
completion, not DXGI presentation or scanout.

`WEBSCENE_SINGLE_SCENE_PER_FRAME=1` is an opt-in pacing mode. It applies one ordered
scene per compositor frame instead of consuming a second queued scene and drawing
only the newest. This preserves cadence through small producer timing variations
at the cost of approximately one refresh of additional publication-to-draw latency.
An initial three-image test reached 59.95 draw callbacks/sec but only 52.76 RAF/sec;
that was producer backpressure, not a successful 60 Hz canvas result.

Windows D3D12 canvas storage now has four bounded slots, still constrained by the
existing byte budget. The extra slot lets queued drawing coexist with producer
and GPU retirement work. The generic pool defaults to three slots, preserving
other providers. The allocator, lease pool, pending submissions and idle-storage
eviction all account for the fourth slot. A regression fills four slots, rejects
a fifth, and ensures GPU consumer completion is required before reuse.

With four slots and single-scene pacing:

| Run | Draw callbacks/sec | Actual RAF starts/sec | Median publication-to-draw |
| --- | ---: | ---: | ---: |
| 20 seconds | 59.99 | 59.99 | 26.50 ms |
| 60 seconds | 59.96 | 59.98 | 26.42 ms |

Both original-fixture circular pans validated. The longer run submitted 32
checkpoints and peaked at 35,375 retained commands. Median draw gap was 16.71 ms,
maximum 32.46 ms: this is approximately 60 Hz, not perfectly uniform frame timing.
Physical presentation remains unqualified. Logs:
`pacing-single-four-images.log` and `pacing-single-four-long.log` under
`artifacts/windows-kestrel`. The compositor-clock statistics and worker pixel-copy
experiments were disabled. Single-scene pacing stays opt-in because of its latency
tradeoff; the evaluation window is titled **Kestrel in WebScene — steady frame pacing**.

Final validation: all 18 native CTest suites passed on the final full run;
managed net8 and net10 each passed 306 tests with eight platform skips. A
checkpoint resource test initially raced an intermediate checkpoint-only scene;
it now waits for the appended command before checking resource preservation.
Repeated native testing also encountered one idle detached-DOM collection deadline
failure; that test was not relaxed and passed on the final full rerun. This
intermittent result remains a validation limitation, not a demonstrated rendering
failure. Logs: `test-checkpoint-test-barrier.log` and
`test-steady-final-native-repeat.log`. The evaluation build has tracing disabled.
