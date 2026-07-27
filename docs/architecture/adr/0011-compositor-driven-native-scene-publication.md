# ADR 0011: Native scenes are published through a compositor mailbox

- **Status:** Accepted
- **Date:** 2026-07-24

## Context

The native engine produces immutable scenes on its document worker while Avalonia
presents them on the render/compositor thread. The original bridge posted a
high-priority UI-dispatcher operation for every published scene before forwarding the
work to the compositor.

That extra hop preserved thread affinity, but it made scene throughput depend on the
application UI queue. A busy application, live window resize, or several active WebScene
documents could queue scene notifications behind input and layout work. The resulting
latency was visible in the TradingView terminal as delayed dialogs, broken-feeling pointer drags, and
chart updates that caught up only after resize ended, even though JavaScript and scene
production remained off the UI thread.

## Decision

Composition-backed native surfaces use a lock-free publication mailbox shared by the
engine callback and the compositor handler:

1. The engine worker records each publication in the mailbox without synchronously
   entering the UI thread.
2. The compositor consumes pending publications at animation-frame boundaries,
   acquires the newest immutable native scene, acknowledges superseded work, and
   presents the latest valid revision.
3. Ordinary publications after the first rendered scene do not post an Avalonia
   dispatcher operation.
4. First presentation and a scene that consumes the latest live-resize input may
   request one coalesced UI-to-compositor wake. This is a correctness escape hatch for
   platforms such as macOS whose nested live-resize loop can temporarily suspend the
   normal compositor clock.
5. The wake gate remains closed until the compositor receives the message. Publications
   arriving between the UI commit and render-side receipt therefore cannot be mistaken
   for work already covered by that wake.
6. Cursor changes remain UI-thread operations because they mutate an Avalonia control.
   The non-composition fallback also retains UI-thread visual invalidation.
7. Detach, engine replacement, and compositor recreation reset the mailbox and wake
   gate, request a full scene checkpoint, and prevent stale publications from crossing
   a surface lifetime boundary.

Scene revisions, acknowledgement, input-consumption ordering, first-frame liveness,
cooperative resize, and the non-composition fallback remain observable compatibility
requirements. The optimization changes scheduling, not document or rendering
semantics.

## Rejected alternatives

- **One UI-dispatch post per scene:** simple, but couples render latency and
  multi-document throughput to the application UI queue.
- **Render directly in the engine callback:** violates compositor/render-thread
  ownership and graphics-context affinity.
- **Compositor polling with no exceptional wake:** removes UI traffic but can stall
  first presentation or live resize when a platform nested event loop suspends
  animation callbacks.
- **One Avalonia timer per document:** adds another UI-thread scheduler and scales
  poorly with concurrent documents.

## Consequences

- Scene publication no longer competes with normal UI input and layout for one
  high-priority dispatcher message per native frame.
- Multiple native documents can publish independently while Avalonia's compositor
  coalesces presentation to display cadence.
- Intermediate scenes may be superseded before presentation; the newest valid scene
  and its consumed-input sequence are authoritative.
- Tests must prove concurrent mailbox accounting, wake coalescing, first-frame
  liveness, continuous resize presentation, input continuity, and bounded dispatcher
  wake counts.
- Diagnostics expose pending publications and compositor wake counts so a regression
  back to per-scene UI dispatch is measurable.
- If a platform cannot provide compositor animation callbacks, it uses the existing
  UI-thread invalidation fallback rather than weakening compositor thread ownership.
