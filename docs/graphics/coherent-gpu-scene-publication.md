# Coherent GPU scene publication

Status: submission completion-ticket foundation implemented; scene capture/commit
integration and end-to-end coherence remain unimplemented and unqualified. This preserves epic #22 scope and its Dawn/Skia
GPU-resident route.

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
