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
