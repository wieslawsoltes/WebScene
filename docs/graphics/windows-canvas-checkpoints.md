# Bounded Canvas2D history on the Windows GPU host

The optional incremental Canvas2D backing now checkpoints eligible canvases after
32,768 retained commands. This fixes the growing annotation history in the
immutable Kestrel fixture at fractional DPR without rounding its clear rectangle
or modifying the application.

## Ownership and publication

The renderer captures the current bitmap and replay state under its owning GPU
lease. On Windows ANGLE it transfers the immutable snapshot to a pending readback
and inserts a GL fence after flushing Skia work. Later render callbacks poll that
fence with zero timeout. Once ready, readback runs before submitting new frame
draws. Reset, generation change or context replacement discards the pending
snapshot. Hosts without this fence path retain synchronous capture.
A periodic readback produces an independent CPU image; PNG encoding and
JSON serialization run on a background task. Only one encoding task is retained
per renderer. That task never accesses the native engine or GPU context.

On a subsequent render, the composition owner submits the result through
`webscene_engine_submit_canvas_checkpoint_v3`. The native engine copies the
payload into a one-entry queue (maximum 32 MiB). Submission success means queued,
not committed. The worker validates the canvas identity, generation and prefix
length, then replaces that prefix with a raster/state checkpoint command. It
preserves later commands, rebuilds their string indices, and advances the canvas
generation. A reset or an already-applied checkpoint causes stale work to be
discarded. All existing scene leases continue to own their immutable old data.

The generation change causes the managed renderer to release its old CPU picture
forest. CPU rendering/export and reconstruction of GPU backing use the raster
checkpoint plus the remaining commands. Superseded generation string-cache
entries are removed as well.

Scene payloads still replace the current command window; this change does not
introduce append-only transport. Because the old prefix is retired, that window
no longer grows with elapsed drawing time on the eligible path. The 32,768-command
threshold is a trigger, not an exact hard maximum: commands can arrive while the
checkpoint is being encoded and applied. Native backpressure and the one-entry
checkpoint queue bound in-flight work; the long-run probe checks the observed
retained window stays below twice the trigger.

The scene-storage cache is back to its original 64 MiB budget. Enlarging that
cache is no longer needed to accommodate Kestrel's ever-growing history.

## Protocol

V3 capability bit 3, `WEBSCENE_SCENE_CAPABILITY_CANVAS_CHECKPOINTS`, is required
when a scene contains command kind 58. Its resource is UTF-8 JSON, version 1:

* `Png`: base64 PNG of the complete bitmap, preserving alpha.
* `Matrix`: nine row-major Skia matrix components.
* `State`: paint, stroke, dash, text, compositing and smoothing state.
* `Path`: exact float path verbs/points and conic weights, not SVG text conversion.
* `FillType`: current path fill rule.

Path segments contain ten floats: verb, four point pairs, and conic weight. Verb
values are Move=0, Line=1, Quad=2, Conic=3, Cubic=4, Close=5. Each verb uses its
corresponding points from the raw path iterator; unused point slots are ignored.
Bitmap dimensions and resource version are validated on replay. Rendering the
checkpoint replaces bitmap pixels before restoring matrix/path/paint state.

Legacy V2 acquisition rejects scenes requiring this capability. V3 consumers
must explicitly advertise support. The submit API is a trusted-host recovery
interface; it does not accept application JavaScript checkpoint payloads.

## Scope and costs

This path remains enabled by `WEBSCENE_INCREMENTAL_CANVAS_GPU=1`. Unsupported
continuations (active save stacks, clips, external image dependencies and
canvas-to-canvas dependencies) retain their existing behavior. Native code also
refuses to rebase a canvas referenced by a recorded `drawImage(canvas)` operation,
whose generation/index semantics need separate work. Intrinsically growing
application path state is not eliminated by rasterizing old pixels.

Normal WebGPU presentation and ordinary Canvas2D GPU backing updates remain GPU
operations. Checkpoint creation does perform a periodic GPU readback for durable
CPU recovery/export storage. Its largest observed capture time was 15.72 ms in
the monitor-off test; active-display frame pacing still needs measurement.
PNG encoding is off the render thread, but the readback itself is synchronous.
The Windows fence path avoids initiating that copy while the snapshot's recorded
GPU work is unfinished. It does not make the GPU-to-CPU pixel copy asynchronous.

## Deferred readback validation

The fence update passes the two 48-frame GPU pixel comparisons and an additional
test that defers snapshot readback across render callbacks after later drawing
and surface disposal. The original snapshot pixels survive. Managed .NET 10
regressions pass (306 tests, eight platform skips); Uno compilation is checked.
The full pipeline run pacing-fenced-checkpoint-final.log exercised 17 deferred
readbacks with peak history 33,985 and maximum checkpoint operation 7.58 ms.
That run used display-clock fallback and failed strict input validation, so it
does not qualify active-display performance or prove improvement over prior runs.
The --verify-checkpoint-fence probe option requires repeated deferred readbacks,
preventing a synchronous fallback from silently passing that path's smoke check.
The final smoke run test-fenced-checkpoint-pan-final.log passed input and bounded
history validation, with five deferred readbacks, peak history 33,973 and maximum
checkpoint operation 2.87 ms. It also used display fallback, so this is a
correctness result, not a frame-rate comparison. The final .NET 8 and .NET 10
suites each passed 306 tests with eight platform skips; Uno built without warnings.

The subsequent active-display run pacing-fenced-display-active.log passed input,
bounded-history and deferred-readback checks: 3,890 real vsync ticks, zero
fallbacks, 17 deferred readbacks, peak history 34,810 and maximum checkpoint
operation 9.93 ms. The instrumented draw-callback rate was 50.85 Hz over the
60-second pan (excluding its first second). A separate 20-second run with
performance telemetry disabled also passed input validation and had zero
fallbacks; its trimmed unique RAF rate was 54.15 Hz, median interval 16.67 ms,
p95 33.35 ms and maximum 34.79 ms. Neither measurement proves physical display
cadence or a causal improvement over the earlier build. Consistent 60 Hz remains
unqualified even though checkpoint ownership and history retirement pass.

## Validation

`pacing-checkpoint-bounded-long.log` runs 120 circular pan cycles over 160.5 seconds
using the immutable fixture and `--verify-canvas-history`:

| Metric | Result |
| --- | ---: |
| Checkpoint submissions | 48 |
| Peak retained commands | 34,341 |
| Final retained commands | 6,844 |
| Final native canvas storage | 1,705,228 bytes |
| Final scene storage (including capacity) | 4,194,997 bytes |
| Last publication time | 3.27 ms |

The earlier one-minute run without checkpoint retirement retained 997,662
commands and about 84 MB of native canvas storage. Different clocks/input rates
mean these runs must not be used as a frame-rate comparison.

The display was unavailable for the long test: zero real vsync ticks, 5,481
fallback ticks, status `0xC01E0006`. This validates repeated compaction and workload
correctness, not 60 Hz presentation.

Validation also covers 306 managed tests on each of .NET 8 and .NET 10 (eight
platform skips each), 18 native CTest targets, and two 48-frame GPU pixel
comparisons. The checkpoint comparison includes repeated rebasing and recreation
after discarding retained GPU pictures. Pixel tolerance is one channel value,
matching the existing GPU comparison. CPU export and continued path/transform
state have a managed regression test. Native tests cover stale generations,
resource preservation and legacy-consumer rejection. Uno compilation is checked;
no Uno or macOS hardware qualification is implied.
