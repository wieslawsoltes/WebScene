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
