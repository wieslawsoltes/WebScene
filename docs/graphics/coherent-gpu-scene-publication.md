# Coherent GPU scene publication

Status: initial bounded scene capture/commit integration implemented; failure
recovery and end-to-end coherence/performance remain unqualified. This preserves
epic #22 scope and its Dawn/Skia GPU-resident route.

## Required invariant

A scene captured after an application rendering opportunity must retain both its
CPU/2D display lists and the exact GPU image outputs requested during that
opportunity. Publish that immutable scene only after all of those outputs are
ready. Do not combine the captured lists with whichever GPU image is newest at
publication time. Keep replaying the previously accepted complete scene meanwhile.

Evidence: `evidence/kestrel/split-gpu-overlay-publications.json`. In particular,
revisions 16–18 advance the overlay and GPU image separately. The two counters
are independent; equal numeric values are not a general correctness condition.

## Changes to implement

1. Add an internal completion ticket to `dawn_iosurface_submission.h`. A ticket
   retains one exact image allocation/generation/content serial and shares the
   submission's queue-completion and validation state. Capturing a ticket must
   not expose a consumer handle before both completion signals succeed. Tickets
   must remain resolvable if the provider has already drained its ordinary ready
   queue. Destructive `take_ready()` alone cannot provide this ownership model.
   Retention is metadata/lease retention, with no pixel readback, blit or copy.
2. In `dawn_iosurface_canvas_host.h`, associate the latest submitted output with
   its ticket. Capture tickets at the rendering-opportunity boundary. Do not
   substitute a subsequently submitted image. A configured canvas without new
   work reuses its previously complete image. Reset/unconfigure/detach must
   invalidate obsolete captured generations without prematurely releasing
   allocations still in producer or consumer use.
3. Extend the runtime/document snapshot boundary to provide image placeholders
   and tickets for outputs that are not ready yet. Today `build_scene()` emits a
   GPU command only for a completed image, which cannot represent the first
   pending frame. Each placeholder must carry stable canvas identity and exact
   target version, later mapped to a scene-local image index.
4. Split `webscene_native_engine_scene.inc::publish_scene()` into capture and
   commit phases. Capture freezes command/string/layer arrays, dimensions,
   generation identifiers and GPU dependencies together. Commit resolves only
   those dependencies, finalizes image tables and their hash, and exposes the
   immutable scene to C ABI v3. Allocate public revision/base relationships at
   commit against the consumer's actual predecessor; a superseded capture must
   not leave a diff targeting a revision the consumer never received.
5. Bound staged captures and image tickets by the existing three-image budget.
   Select/coalesce complete opportunities as whole scenes, never per-canvas
   fragments. On pressure retain the last displayed scene and defer producer
   RAF admission using existing host wakes. Continue servicing input, promise
   completion and device-loss callbacks. Do not add an unbounded snapshot queue,
   a GPU wait on the UI/engine thread, or a one-frame-only GPU allocation policy.
6. Preserve dirty work that arrives after capture. Committing a frozen scene
   must not clear the worker's `scene_pending` flag for later DOM/2D mutations.
   Audit the `starting_scene_generation`/`scene_pending` loop in
   `webscene_native_engine_worker.inc`. Completion wakeups should retry commit;
   no polling timer should become necessary for ordinary producer completion.
7. On failure, cancel the entire staged opportunity and retain the prior complete
   scene. Request a coherent replacement/checkpoint where necessary. Never
   declare a failed producer ready or let a failed canvas strand all future
   scenes indefinitely. Device loss, navigation, resize and hidden-window
   retirement need explicit state transitions and tests.

## Verification before accepting the fix

- Deterministic delayed producer: draw geometry and an overlay marker at A,
  capture B with its producer unresolved, then mutate live state to C. While B
  is pending, presentation stays at complete A. Completing B presents B/B,
  never C/B or B/A. Then complete C and verify C/C.
- Two canvases completing out of order: no partial scene is visible. Include a
  canvas with unchanged content and a first-ever pending GPU image.
- Fill all bounded slots, supersede/cancel a capture and verify exact lease
  release only after relevant GPU reads/writes complete. Check fixed memory
  limits and continued input service, without synchronous GPU waits.
- Cover generation changes, reset at unchanged bitmap size, detach/navigation,
  unconfigure, validation failure, queue/device loss and shutdown during pending
  capture. No stale output can enter a replacement generation.
- Verify diff predecessor integrity when several captures are superseded before
  a consumer acknowledgement. Replay the resulting stream into the real retained
  renderer and check pixels/order, not only revision arithmetic.
- Run unchanged Kestrel native wheel and right-button pan plus live resize.
  Capture presented frames with aligned geometry/overlay landmarks; producer
  counters alone do not certify display coherence or smoothness. Compare timing
  distributions against the browser and report instrumentation overhead.
- Re-run existing GPU lease/retirement, ordered canvas paint, native runtime and
  ordinary-view GPU tests. Confirm zero explicit presentation pixel copies.

Do not count these requirements as passing until implemented and evidenced. The
current CSS fixes and startup success do not qualify this presentation change.


## Completion-ticket foundation (2026-09-08)

`dawn_iosurface_submission::capture_snapshot()` retains the exact output while
sharing the submission completion state. The snapshot exposes only metadata
until both queue completion and handoff validation succeed. Its one-shot
`take_ready()` also accepts the provider-consumed state, so draining the original
ready queue does not invalidate a captured scene's ownership. Retention pressure
returns no ticket; failed or discarded submissions cannot yield an image.

The Dawn fixture captures before its completion waits, drains the ordinary
reference, and verifies the snapshot still resolves the identical allocation and
content serial exactly once. The Ganesh window fixture passes 32 frames, two
imports, eight diagnostic pixel readbacks and GPU retirement, with zero explicit
transport copies. Native GPU runtime regressions also pass. Evidence:
`evidence/kestrel/gpu-completion-ticket-fixture.json`.

This fixture does not deterministically delay completion, and does not prove
pending-scene coherence or physical display timing. The provider/runtime/scene
capture integration and delayed-producer tests above remain mandatory.


## Provider capture boundary (2026-09-08)

`dawn_iosurface_canvas_host::capture_latest_submission()` exposes the latest
submitted ticket before ordinary ready-queue draining. It rejects capture while
an acquired texture still belongs to an open rendering opportunity, and discard
retirement clears the lookup. A weak submission lookup adds no hidden allocation
retention; only the captured ticket retains the output. Capture must therefore
happen before the ordinary ready queue consumes the provider reference.

The fixture now captures through the provider, drains its ready queue and returns
the captured image for the real Ganesh pixel/retirement test, verifying allocation
and content serial identity and one-shot transfer. Discarded work yields no
capture. The 32-frame fixture and native GPU runtime test pass; evidence is in
`evidence/kestrel/gpu-provider-ticket-fixture.json`. Runtime snapshot capture,
first-pending-image commands, atomic scene commit and delayed-completion tests
remain unimplemented; this provider API alone does not fix presentation.


## Deterministic completion-phase coverage (2026-09-08)

The production Dawn submission now uses `producer_completion_gate` under its
existing mutex. The gate requires queue completion and validation, even when
one reports failure; a validation error alone cannot certify finished GPU writes.
Duplicate phase delivery is ignored, preventing a terminal consumed state from
being overwritten by a repeated completion signal.

The platform-independent completion tests withhold either phase, check that
polling cannot make it ready, and cover all success/failure combinations and
duplicate delivery before/after completion. Completion and V8 GPU runtime tests
pass (0.92s together). The real Ganesh fixture also passes 32 frames and retirement
with zero explicit transport copies. Evidence:
`evidence/kestrel/producer-completion-gate.json`.

This deterministic test covers the shared production gate, not an artificially
stalled hardware queue or whole captured scene. Runtime snapshot integration and
the A/B/C delayed-scene regression remain outstanding.


## Runtime output capture (2026-09-08)

The runtime now captures a backend-neutral `webscene_gpu_image_snapshot` at the
end of each submitted rendering opportunity, before `publish_ready_gpu_canvases`
drains provider outputs. Canvas state holds the latest dependency; a scene can
retain that shared dependency independently of future canvas changes. The Dawn
adapter caches successful resolution, preserving stable image identity without
pixel copying. Bitmap reset and configure/unconfigure clear the canvas's current
reference; existing captures keep ownership of their exact image.

The native runtime regression resolves the captured output after normal image
publication, checks allocation/serial identity and repeated resolution, then
verifies unconfigure clears current canvas state without destroying an existing
capture. Both native suites pass (12.44s). Unchanged Kestrel handles all forty
wheel events with no bitmap mutations or app errors; results are in
`evidence/kestrel/runtime-output-snapshot.json`.

The scene builder still consumes completed `gpu_image` state. Pending-image
command representation, frozen CPU scene capture and atomic commit remain
mandatory. Failed/pressure-rejected capture also needs explicit staged-scene
failure handling at that integration point; no coherence claim is made yet.


## Pending image scene representation (2026-09-08)

`native_document::build_gpu_canvas_bindings()` captures node identity, immutable
image metadata and either its completion snapshot or an existing completed lease.
The binding resolves its captured dependency without consulting live canvas state.
`build_scene(..., ordered_canvas=true, capture_gpu_outputs=true)` emits GPU paint
placeholders for pending first images, propagating the capture mode through normal,
fixed, modal and elevated paint traversal. The default published-scene path is
unchanged until engine capture/commit integration is complete.

A controlled delayed-image regression verifies that a first pending image has a
paint placeholder but no resolvable image, then clears the live canvas reference
and completes the captured dependency: it must resolve the original allocation.
The pre-existing pool-release assertion remains intact. Native engine tests pass
(11.77s); GPU runtime tests pass after scoping the test capture to release its
owned reference (0.75s). This tests dependency capture, not atomic publication of
the full A/B/C scene sequence. The engine still needs bounded staging, failure and
generation invalidation, dirty-work preservation and atomic commit.


## Initial engine capture/commit integration (2026-09-08)

The engine now freezes scene commands, layer arrays, input sequence, viewport and
exact GPU bindings together. One staged capture waits for all captured images;
commit validates canvas identity, bitmap generation/content floor, image version,
viewport and the consumer predecessor. It rechecks mailbox capacity before
publication. Later live document changes remain pending after a frozen capture
commits. Open GPU rendering opportunities defer capture. Resolution retains GPU
leases without pixel copies or a synchronous GPU wait.

Revision numbers are reserved at capture, rather than commit as originally
proposed. Discarded captures can leave gaps; predecessor validation prevents a
diff from targeting an unpublished capture. Both native suites pass after the
identity/capacity hardening (14.57 seconds). A forty-wheel Kestrel run had no app
errors, but manual input contaminated its timing counters, so it is not
performance evidence.

Remaining acceptance work includes controlled full capture/presentation tests,
multi-canvas completion order, explicit missing-ticket handling under retention
pressure, failure recovery, navigation/detach retirement, controlled right-button
pan and resize, and browser-comparable displayed-frame measurements. A failed
snapshot currently discards its captured scene; recovery from that state is not
yet qualified. No claim that physical flicker or panning latency is fixed.

The production commit helper now has deterministic white-box coverage for a
pending dependency retaining scene A, frozen CPU commands B surviving a newer
capture C, dirty-generation preservation, mailbox capacity, failed output, stale
predecessor, viewport change and same-size bitmap reset. Both native suites pass
with these regressions (14.16 seconds). These tests reuse a controlled image lease
and vary CPU commands; they do not establish full multi-version rendered A/B/C
coherence or exercise the runtime capture checkpoint. Consumer predecessor
validation and queue insertion now share one lock to prevent a checkpoint reset
from intervening between them.


## Missing output and native right-button pan (2026-09-08)

A submitted output that cannot produce a retention ticket now installs an
explicit failed snapshot carrying the current canvas version. Scene capture
cannot fall back to the older completed image. Submitted output capture also
marks the document generation changed, including first pending images and failed
outputs that will never reach the ordinary ready queue. The commit regression
checks failed dependency selection in the presence of an old image, whole-scene
rejection and successful replacement. Its canvas now allocates its own backing
before metadata capture; this corrects an earlier fixture identity mistake.
Native engine tests pass (12.55s); GPU runtime tests pass after that fixture
correction (0.89s). Device-loss recovery remains a broader outstanding gate.

The unchanged Kestrel probe adds `--pan-kestrel`, driving eighty right-button
moves through the native input queue, out and back, with requested 16ms spacing.
The recorded run delivered 74 moves and coalesced six, entered Kestrel's own
panning state for every delivered move and exited on release, with no dropped
inputs or application errors. It invoked 43 app animation callbacks but rendered
66 scenes and performed 119 layout passes, with nine blocked publications.
Median observed move spacing was 20.53ms; maximum was 37.04ms. These include host
scheduling/coalescing and are not GPU execution or physical presentation timings.

Evidence: `evidence/kestrel/native-right-button-pan.json`. No pointer capture
events were observed, so capture-event conformance remains unqualified. The next
performance investigation should separate input/RAF/layout scheduling, producer
completion and consumer retirement. Browser timings, presented-frame coherence
and resize qualification remain required.


## Completion invalidation and full A/B/C capture (2026-09-08)

Ordinary completion of the exact already-captured GPU output no longer marks the
document changed a second time. Submission remains the content invalidation;
completion wakes and resolves the existing dependency. A completed image without
a matching valid snapshot still invalidates normally. Native suites pass with
that regression (12.18 seconds together).

A further white-box regression uses the production document and scene builder,
three distinct content serials and CPU background colors, and real ABI v3 scene
acknowledgements. It verifies A/A remains published while B is pending, changing
live state to C does not alter frozen B, completing B publishes B/B, and completing
C publishes C/C. This addresses the earlier commit-only test limitation; it does
not yet cover multiple canvases or physical presentation. The expanded GPU
runtime suite passes (0.65 seconds).

The same eighty-move native pan probe completed without errors or dropped inputs.
This run produced one no-damage build (previously ten), 62 rendered scenes
(previously 66), and zero blocked publication attempts (previously nine). It still
performed 117 layouts for 42 application RAF callbacks. Counts vary with scheduling
and coalescing, so these single runs do not qualify an FPS or latency improvement.
Evidence: `evidence/kestrel/completion-invalidation-and-full-capture.json`.


## Multiple canvas completion order and failure preflight (2026-09-08)

The production scene-builder regression now introduces a second canvas whose
first image is pending. It checks both producer completion orders: the previous
complete scene remains published until both exact outputs are ready. Published
GPU command indices must map to each canvas identity and expected content serial;
the CPU marker must belong to the same capture.

A failing second output initially exposed a bug: the commit loop returned at the
first pending dependency before inspecting later failures. Commit now validates
every captured dependency before trying to resolve any pending output. The test
verifies failed and bitmap-reset second-canvas captures are discarded promptly
while the first producer remains pending. Previous published content stays intact.
The native engine suite passes (12.55s), and the expanded GPU runtime suite passes
(0.67s). Evidence: `evidence/kestrel/two-canvas-publication.json`.

These tests use controlled native completion snapshots; they do not stall a
hardware queue or prove physical presentation timing. Browser-comparable pan
performance, live resize timing, device-loss recovery, cross-platform interop and
all other epic gates remain required.


## Retry allocation and callback timing (2026-09-08)

Scene allocation and initial command reservation now happen only after GPU
opportunity/staged-capture checks and consumer-mailbox admission. A deferred
commit no longer creates and discards a second scene on each retry. Native engine
and GPU runtime suites pass (14.76 seconds together).

The native pan probe temporarily wraps requestAnimationFrame to record callback
wall time while preserving callback receiver, return value and cancellation ID.
Cleanup restores the original function. The observed run had 0.83ms median and
5.30ms p95 callback duration, but only 30 callbacks inside the native counter
interval. Instrumentation recorded 31 samples including setup/settle. This directs
subsequent investigation toward frame admission, producer completion and consumer
retirement; it does not identify GPU execution as the bottleneck. Evidence:
`evidence/kestrel/native-pan-callback-timing.json`. The existing interactive demo
remained open, so this is investigative evidence rather than an isolated benchmark.


## Frame admission ownership trace (2026-09-08)

A thread-safe native image-pool occupancy snapshot now distinguishes occupied
images, unfinished producers, retained-reference images and outstanding-consumer
images. Roles can overlap; counts are images, not reference counts. The owned
pool and IOSurface provider expose the same read-only snapshot. Tests cover an
image with simultaneous producer/retained/consumer ownership, completed producer
with retained ownership only, and full release to idle. Image lease and GPU
runtime suites pass (0.95 seconds together).

Temporary admission tracing recorded 37 admitted and 17 blocked host frame
signals during a controlled eighty-move pan (66 delivered, 14 coalesced). Every
blocked signal had three busy images, zero unfinished producers, two retained
images and two images with outstanding consumer leases. This does not prove
consumer GPU execution was still running: the lease may instead await polling
of an already-signaled GL fence. The next investigation must measure that
distinction in the Skia/GL retirement path.

Trace logging was removed and the native library rebuilt. Evidence:
`evidence/kestrel/frame-admission-ownership.json`. An earlier trace contained
extra manual input and is excluded from controlled counts. No frame-rate or
physical presentation qualification is inferred from this diagnostic run.


## Consumer retirement after drawing (2026-09-08)

Fence tracing showed most initial zero-timeout checks were unsignaled and the
next check collected completion around 20ms later. A trial polling through a
separately acquired host context before producer-frame submission reduced that
age but processed fewer frames/moves; it was removed.

The retained implementation instead checks retiring groups again after recording
the current frame, under the existing Skia graphics lease. It reuses the same
retirement/fence logic, never waits for GPU completion, and leaves the current
image group intact. The traced trial's median collection age was 3.57ms with 41
application RAF callbacks, compared with 20.40ms and 30 callbacks in the initial
trace. A final run after removing trace code invoked 52 callbacks, delivered 71
moves and coalesced nine, with zero app errors. These are individual investigative
runs, not a statistically qualified speedup or browser comparison.

Eleven native GPU interop tests pass on each of .NET 8 and .NET 10, with no skips.
The Ganesh fixture completes 32 frames and retirement with zero explicit transport
copies (eight diagnostic readbacks; physical presentation not certified).
Evidence: `evidence/kestrel/consumer-retirement-after-draw.json`. No temporary
trace code or separate-context polling experiment remains in the implementation.

### Vsync/mailbox integration audit (2026-09-08)

The macOS Avalonia path already sends its compositor frame timestamp through
`NativeSceneComposition.OnAnimationFrameUpdate` to native frame input.
`v8_dom_runtime::signal_animation_frame` admits the RAF batch only when configured
GPU canvases have available storage. Finishing that rendering opportunity submits
current textures and captures versioned image dependencies. Scene publication
uses the existing bounded acknowledgement mailbox, including the staged scene's
CPU/GPU coherence checks. GPU completion wakes the native worker; it does not
constitute a new display vsync or permission to invoke another RAF batch.

There is still a separate producer timing gate: the worker permits ordinary scene
publication only after `next_scene_publication`, advanced on a fixed 16ms cadence.
Only paired resize/host-frame boundaries currently bypass that gate. This is an
observed architectural mismatch with display-driven scheduling, not yet proof of
the remaining pan bottleneck. A ready GPU dependency may encounter this gate
before entering the compositor mailbox. Completion-to-publication delay must be
measured separately from actual GPU execution and consumer fence collection.

Next scheduling verification must cover completed ordinary host-frame batches,
asynchronous GPU completion after that batch, full-mailbox acknowledgement,
multiple canvases, resize, and idle behavior. Publication eligibility should
follow completed rendering opportunities and mailbox capacity without adding an
independent display clock. Completion must never run future RAF callbacks, expose
partial CPU/GPU scenes, introduce unbounded queued frames, or block the compositor.
Retain a bounded fallback for non-frame-driven document updates. Qualify against
actual presented-frame intervals on the user's monitors; application RAF counts
and the nominal 16ms interval do not establish sustained 60fps.

Follow-up control-flow inspection narrows the suspected gate: a deferred capture
or commit does not advance `next_scene_publication`. An ordinary staged scene was
therefore captured after the gate had already opened, and its later GPU completion
normally retries against that same expired deadline. Simply exempting staged
scenes from the gate would not remove the ordinary pan delay; that trial was
removed before retention. The remaining question is delay *before initial scene
capture*, and whether ordinary host-frame completion should bypass the producer
phase as paired resize already does. This correction supersedes any inference
above that every GPU-completion retry incurs another 16ms wait.

The next unchanged-Kestrel run completed with zero application errors, but
recorded 185 pointer moves for the scheduled 80-move workload. It is not a valid
controlled baseline. Both attached LG displays report 3840x2160, logical
1920x1080 at 60Hz. The run observed 89 blocked publication attempts and 56
published scenes over 1.859s including settling; neither counter measures physical
presentation. Evidence is retained in
`evidence/kestrel/vsync-current-pan-investigation.json`. Before comparing scheduling
changes, the probe must distinguish its injected workload from other routed input
and invalidate contaminated runs automatically. Existing interactive windows were
preserved. Native engine and graphics runtime tests passed (11.58s and 1.82s).

The pan probe now clears warmup observations immediately before measurement and
validates the gesture boundaries and delivered moves as an ordered subsequence of
the 80 injected right-button moves. Coalesced moves are allowed; extra, reordered,
or wrong-button events fail the probe before startup verification can report an
overall successful run. Matching coordinates cannot establish provenance for an
identical external event. A rebuilt probe completed with 51 delivered moves,
37 application callbacks, 43 rendered scenes, one blocked publication and zero
script errors. See `evidence/kestrel/validated-pan-workload.json`. This establishes
a usable workload trace only, not presentation timing or sustained performance.

An ordinary-host-frame publication bypass was built and run against the validated
pan workload. The run passed workload/startup checks with zero script errors,
28 application callbacks and 32 rendered scenes, versus 37/43 in the preceding
validated run. This is not a statistical regression finding, but provides no
support for retaining the change. The experiment was removed and the original
scheduler restored. Evidence: `evidence/kestrel/host-frame-gate-experiment.json`.
A same-iteration host-frame flag also cannot represent an RAF batch that finishes
in a later worker iteration; any eventual scheduling change must track the actual
completed rendering opportunity. Next profiling should timestamp host-frame
admission, RAF completion, scene capture/commit and compositor drawing separately.

The pan probe now emits the existing per-view publication, rendered revision and
end-of-draw timestamps, filtered to the measurement window. No new rendering-path
instrumentation was added. The property's legacy `PresentationTimestamps` name is
reported as `drawCallbackCompletions`: its implementation records OnRender, not
physical presentation. The rebuilt probe passed workload/startup validation and
recorded 64 publications, 64 rendered scenes and 65 draw callbacks. Matching
revision timestamps yields median publication-to-draw latency 33.37ms. This directs
further investigation toward compositor consumption/retirement; it does not prove
a GPU execution bottleneck or a physical frame rate. Evidence:
`evidence/kestrel/pan-composition-timeline.json`.

A bounded consumer-drain prototype now applies at most a second queued GPU diff
before invalidating/drawing, preserving diff order and combining both damage
regions. Manual frame certification remains one diff per frame. The existing
presenter capacity/retirement checks can reject the second acquisition without
advancing its acknowledgement. A validated trial recorded 50 publications, 30
rendered scenes, 48 RAF callbacks and median publication-to-draw latency 28.42ms.
Intermediate revisions need not be drawn, but every accepted diff is applied.
Existing interop/frame-policy/damage tests pass: 18 each on net8/net10, no skips.
The prototype remains under evaluation and needs dedicated combined-damage and
multi-scene lifetime coverage before acceptance. Evidence:
`evidence/kestrel/bounded-mailbox-drain-trial.json`. No physical 60fps claim follows.

Dedicated damage regressions now cover separated changes, unchanged-following
scenes, and full invalidation in either order. A native IOSurface ownership test
retains four undrawn scene groups, accepts only the bounded current-plus-two
retiring groups, and verifies that the rejected group remains caller-owned until
explicit discard. This checks undrawn ownership, not imported GPU fence execution.
The focused suite passes 23 tests each on net8/net10 with no skips. The bounded
consumer change is retained for further performance and physical-render testing;
its single-run latency result remains unqualified.

Post-change GPU fixture verification completes 32 frames, two imports and GPU
retirement with zero explicit transport copies (eight diagnostic readbacks).
A repeated validated pan measured 34.06ms median publication-to-draw latency,
so the earlier 28.42ms sample must not be treated as an established speedup.
Kestrel resize verification now fails on application errors, nonpositive/nonfinite
CSS size or DPR, bitmap/DPR mismatch exceeding one pixel, or viewport/workbench
height disagreement. All four settled checkpoints pass in the rebuilt probe.
This adds an unchanged-application regression gate alongside the existing grid
WPT contracts; it does not qualify physical resize smoothness. Evidence:
`evidence/kestrel/mailbox-resize-verification.json`.

Render samples now include an optional acceptance timestamp in the same Stopwatch
clock as publication and draw timestamps. It is sampled after applying and
acknowledging the revision, only while performance instrumentation is enabled;
zero means unavailable. Existing positional construction/deconstruction remains
unchanged. A rebuilt, validated pan run matched 16 rendered revisions and verified
publication <= acceptance <= draw for each. Median publication-to-acceptance was
59.77ms; acceptance-to-end-of-draw was 2.99ms. Single-run variability remains high,
but this trace locates most observed latency before acceptance, not inside drawing.
Next investigation should distinguish presenter retirement backpressure from
missed compositor acquisition opportunities. Evidence:
`evidence/kestrel/acceptance-to-draw-timeline.json`. No physical presentation claim.

Undrawn intermediate scene groups no longer occupy GPU retirement slots when
ImportedCount is zero. Replacement discards only their CPU source leases; any
partially or fully imported group still requires the bounded fence-retirement
path. Admission uses the same condition before mutating the renderer. The native
IOSurface regression now verifies four consecutive unimported replacements leave
no retirement queue and release the final source on discard. All 23 focused tests
pass on net8/net10, with no skips; Ganesh completes 32 frames and retirement with
zero explicit transport copies. A validated Kestrel run measured 63.41ms median
publication-to-draw latency, so this resource-lifetime simplification is not an
established performance fix. Evidence: `evidence/kestrel/unimported-scene-release.json`.

Temporary acquisition-reason tracing in a validated pan observed 35 native
acquisitions and 35 successful presenter applications during the measured window.
There were no recorded empty/backpressure outcomes or outstanding-draw gate
rejections. Draw callback intervals had median 78.76ms. Synchronous trace output
can affect timing, so the value is not a performance qualification; the reason
counts nevertheless do not support presenter admission failure as this run's
explanation. Investigate compositor callback cadence and host scheduling next.
All temporary tracing was removed and the probe rebuilt successfully. Evidence:
`evidence/kestrel/acquisition-reasons-trace.json`.

Callback-cadence audit corrects the inference from draw intervals: the traced run
had 74 compositor animation callbacks over 1.853s (~39.94 callbacks/s), but only
20 rendered scenes. Slow draw intervals therefore do not establish an equally
slow native vsync timer. Across four captured runs compositor rates vary roughly
33–52 callbacks/s; application and draw rates differ further. These intervals
include settling and are not FPS qualifications. The probe uses the ordinary
Avalonia UsePlatformDetect configuration without a custom render timer override.
Before altering that configuration, correlate individual compositor ticks with
native publication availability and frame demand. Evidence:
`evidence/kestrel/callback-cadence-audit.json`.

A temporary per-compositor-callback trace recorded 70 measured callbacks in a
validated pan. At callback entry none had accepted work awaiting drawing or
pending GPU retirements. Thirty-three callbacks had one pending mailbox signal;
37 had none. Of 33 matched rendered revisions, 18 had one callback between
publication and acceptance and 15 had none; none spanned multiple observed
callbacks before acceptance. Demand was caret-only (bit 4) on 45 callbacks, zero
on 24, and RAF/current-texture demand (bit 1) on one. Thus this run does not show
ready scenes being repeatedly ignored at compositor boundaries. Investigate
input-to-publication timing and callback delivery without assuming every tick
has application GPU work. Trace logging was removed and the probe rebuilt.
Evidence: `evidence/kestrel/compositor-demand-trace.json`; timing remains affected
by tracing and does not qualify physical presentation.

The pan probe now records each injected move's native sequence, monotonic
submission timestamp and coordinates in its composition timeline. Both initial
and retry runs captured exactly 80 strictly increasing input sequences with
ordered timestamps, but failed workload validation because of additional input.
Neither run is used for a latency comparison. This also exercises the probe's
nonzero failure exit instead of allowing startup success to mask contamination.
Future sequence-to-scene correlation must be described as consumed-input progress,
not proof that each coalesced move was individually drawn. Evidence:
`evidence/kestrel/input-sequence-trace-validation.json`.

User recording (8.27s, 60Hz capture) shows the drawing/grid disappearing during
window resize while surrounding HTML remains. Kestrel's ResizeObserver resets the
bitmap and queues invalidate() through RAF. The native engine could publish the
cleared canvas between that observer and its next rendering opportunity. Bitmap
reset now records a one-opportunity hold, effective only while configured and a
RAF callback is waiting. Host-frame admission clears the hold; normal GPU output
capture/completion takes over. A callback that draws nothing cannot indefinitely
retain the preceding scene, nor can unconfigure or absence of RAF.
Runtime regressions for these boundaries pass, and all four unchanged Kestrel
resize geometry checkpoints pass. This is a candidate fix for the recorded
flicker; a new physical capture still needs to verify it and HTML/canvas 60fps.
Evidence: `evidence/kestrel/resize-redraw-boundary.json`.

A new window-scoped 8.99s capture of the rebuilt original Kestrel shows the drawing
remaining visible in inspected 2Hz overview and 15Hz transition samples across
stepped resize checkpoints. `resize-redraw-check.mp4` is a resized viewing copy;
the source capture reports 60Hz, which is not application FPS. This supports the
blank-frame fix but does not certify every captured frame or continuous native
window dragging. The capture also shows transient uncovered host area when the
window grows before layout catches up. Continuous-drag visual checks and separate
HTML/sidebar cadence measurements remain required. The probe's optional
`--capture-resize-kestrel` flag adds a five-second attachment delay before its
existing resize sequence; it does not alter application source.

Continuous-resize verification attempt: a window-scoped capture around synthetic
native edge-drag events did not show changing window dimensions in the inspected
frames. The action therefore did not exercise the intended live-resize path and
is not a pass. Do not substitute its absence of blank frames for continuous-drag
qualification. No FPS result is derived. Existing stepped-resize evidence remains
limited to that workload; real continuous dragging still requires verification.

The unchanged-application sidebar probe uses native left-button input on the real
`.left-resizer`, moving 120px across 60 steps, and asserts final width. It passes
222→342px with zero script errors. The first run records 92 compositor callbacks,
47 RAF callbacks, 154 layouts and only 11 rendered scenes; median matched
publication-to-draw latency is 32.62ms. Input contamination is not yet checked in
this probe and counters include settling, so this is investigative evidence, not
FPS qualification. It directs the next check toward scene publication/captured
output invalidation during repeated bitmap resizes, rather than assuming low
compositor callback frequency. Evidence: `evidence/kestrel/sidebar-drag-baseline.json`.

Sidebar publication tracing identifies a scheduling cost of the resize hold.
The detailed run records 137 resize-awaiting-RAF deferrals, 30 pending-output
retries and 19 publications including startup, with no stale-binding rejection.
Thus the current evidence does not support repeated invalidation of frozen GPU
dependencies as the primary cause; repeated new layout/ResizeObserver changes
before a queued redraw can publish are the next ordering concern. Preserve input
sequence barriers and bounded coherent capture when correcting this; simply
bypassing the hold would restore intermediate blank canvases. Temporary logging
was removed. Evidence: `evidence/kestrel/sidebar-publication-deferrals.json`.

A reset performed after host-frame admission could leave its hold active even
when the same RAF batch submitted the replacement output. Finishing a submitted
canvas now clears that hold; the captured GPU dependency governs publication.
The regression resets inside RAF, obtains a replacement texture, queues another
RAF, and verifies publication is not held for that unrelated next callback.
Runtime tests pass. Original sidebar geometry/startup checks pass with 13 rendered
scenes, 51 RAF callbacks and 94 compositor callbacks, so the broader continuous
resize starvation remains unresolved. Evidence:
`evidence/kestrel/within-frame-resize-hold.json`.
The WebGPU canvas reference confirms configure clears the drawing buffer; this
change preserves reset semantics and changes only readiness tracking:
https://gpuweb.github.io/types/interfaces/GPUCanvasContext

Live Chrome reference audit confirms WebGPU is active. The existing reference tab
was 792x878 CSS pixels at DPR2 with a 175px explorer, unlike the native 1280x800,
DPR2, 222px explorer. A temporary fresh comparison tab with a 1280x800 viewport
override reported DPR1, so it still did not match native pixel load; backend was
WebGPU and application errors were zero. The override was reset and temporary tab
closed, preserving the user's original reference tab. Do not derive a native /
Chrome performance ratio from these mismatched conditions. A comparison harness
must verify settled canvas dimensions as well as viewport, DPR and application
state before measuring identical interaction workloads.

### Rejected pre-RAF resize layout experiment

Moving layout and ResizeObserver delivery before the admitted GPU RAF batch produced 26 scene draws over a 1.577-second sidebar workload (including settling), compared with 13 in the preceding run. This is neither an FPS measurement nor a controlled speedup claim. Both native test suites passed, but the experiment changes observable rendering phase ordering: ResizeObserver delivery belongs after animation callbacks. The production experiment was removed. Evidence: `evidence/kestrel/rejected-pre-raf-resize-layout.json`.

The retained resize redraw hold passed the stepped resize checks, but continuous window-edge resizing and physical 60fps presentation remain unqualified. The hold can still defer too many scenes under continuous sidebar resizing; resolving that requires preserving browser scheduling and coherent CPU/GPU scene boundaries.

### Validated sidebar input baseline

The sidebar probe now records all submitted move sequences and timestamps, observes delivered pointer events, and rejects unexpected boundaries, buttons, or moves outside the ordered submitted path. Coalesced omissions are allowed. Temporary observers are removed in `finally`; the Kestrel fixture remains unchanged. Eight managed regression cases cover both pan and sidebar validation on net8.0 and net10.0.

The validated baseline in `evidence/kestrel/validated-sidebar-publication-baseline.json` reproduced the publication deficit: 94 compositor callbacks, 49 application RAF callbacks, and 14 scene draws in 1.580 seconds including 500ms settling. Geometry reached 342px from 222px with no application errors. These counts are not physical FPS or a sustained-rate measurement. Next work remains the resize reset/publication boundary, without promoting ResizeObserver ahead of RAF or allowing mismatched CPU/GPU generations.

### Commit completed captures independently of newer GPU opportunities

`publish_scene` now commits an existing staged CPU/GPU capture before checking whether the current runtime opportunity is open. The layout/ResizeObserver checkpoint remains in its existing position. The capture still passes viewport, canvas generation/content, producer completion, predecessor, and mailbox capacity validation. New capture creation remains gated until the newer rendering opportunity ends.

The production scene lease regression opens a newer runtime GPU opportunity while frozen B completes: it failed at B publication before the change, then passed after the change. It also verifies that C cannot be newly captured while the opportunity remains open. Both native engine and graphics runtime suites passed. A validated unchanged-Kestrel sidebar smoke run reached the expected width without errors, with 15 scene draws and 49 RAF callbacks; it overlapped a test-target build and is not a controlled performance comparison. The principal continuous-resize publication deficit remains unresolved, and physical 60fps remains unqualified.

### Headless non-GPU resize measurement scope

The existing native resize cadence probe uses Avalonia.Headless. Its timestamps previously named presentation timestamps are CPU draw callback completions, not scanout. Report schema v2 now names these fields `drawCallbackCompletions`, `drawCallbackCompletionsPerSecond`, and `drawCallbackIntervalMilliseconds`, labels the measurement headless, and reports `physicalPresentationVerified: false`. The former practical-vsync threshold is now explicitly `cpuCadenceGate`. The comparison script accepts v2, exposes `--require-cpu-cadence`, and always rejects `--require-vsync` for this measurement source. Legacy v1 artifacts are not silently promoted. Two regression tests protect legacy rejection and failure of physical qualification even when CPU cadence passes.

The initial current-library non-GPU run reported about 30 draw callbacks per second, then aborted during cleanup with `mutex lock failed: Invalid argument` (exit 134). `evidence/kestrel/non-gpu-headless-resize-failed.json` records the command, native binary hash, partial metrics and failure. This is not a passed baseline or a physical presentation result. Headless scheduling and the cleanup failure require investigation before qualifying this benchmark.

### Revoke compositor engine access before native destruction

The shutdown crash report identified `std::mutex::lock` inside `webscene_engine_acquire_next_scene`. Surface detachment previously queued an asynchronous Stop and allowed engine destruction before the handler consumed it; queued wake/live-resize/manual/render callbacks could still acquire scenes. The surface now retains its handler and revokes engine access synchronously before queuing Stop on engine replacement or visual detachment. A shared callback gate joins an in-flight message, animation, or render callback. Later callbacks cannot touch the engine; Stop can still retire resources. Revoked capture requests fail explicitly rather than hanging. Stop is terminal for an individual handler; reattachment creates a new one. No GPU completion wait or pixel transport is introduced.

An attempted compositor-commit wait was removed because a detached headless visual could stop servicing commits. The retained implementation does not depend on another compositor tick. Seven capture/lifecycle regression cases pass on net8.0 and net10.0, including late messages and rendering after both Stop and synchronous revocation. The short and repeated five-second headless resize runs exited zero after revocation, including the final render-callback guard. See `evidence/kestrel/non-gpu-resize-cleanup-fixed.json`; the CPU cadence gate remains false and this is not physical presentation evidence.

### Observe the headless clock separately from scene draws

Avalonia 11.3.4 configures its headless render timer for 60Hz, but the current benchmark run observed 150 actual timer events during the five-second measurement interval (about 30Hz), alongside 150 scene draws. Thus this run does not establish that the native desktop renderer discards half of its display opportunities. The requested input cadence remains 60Hz. The root of the headless timer cadence itself is not yet established.

The resize benchmark now subscribes to the existing timer without replacing or forcing it, reports availability, event count, rate and intervals, and unsubscribes in `finally`. Observation is restricted to the timed workload, excluding warmup and drain. Access is benchmark-only reflection against the pinned framework because Avalonia removes these private APIs from its reference assembly. `evidence/kestrel/headless-render-clock-observed.json` contains the clean-exit run and source reference. The physical 60fps gate remains unqualified; the separate native-window Kestrel publication deficit remains actionable.
