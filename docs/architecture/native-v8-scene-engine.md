# Native V8 + immutable scene engine

**Status:** Accepted direction; compatibility and packaging remain incremental
**Date:** 2026-07-20

## Purpose

The native mode moves V8, DOM/CSS state, layout, input dispatch, and scene construction
off the Avalonia UI thread. Its goal is to remove repeated V8-to-managed host-object
dispatch from hot paths while keeping the managed engine as a supported compatibility
implementation and differential oracle.

WebScene supplies browser-like primitives. Product libraries, bootstrap scripts, assets,
facades, data adapters, and exact-reference integration tests belong to the consuming
application.

## Process model

```text
Avalonia UI thread                    Native engine thread
------------------                    --------------------
surface size + scale  ------------->  V8 isolate and contexts
pointer/key/IME input  ------------>  DOM events and microtasks
focus/capture replies <------------>  CSS, layout, Canvas and SVG state
                                       immutable scene builder
                                                |
                                                v
                                      atomic latest SceneDiff
                                                |
                                                v
Avalonia compositor/render thread acquires one immutable native scene handle,
projects its fixed-layout tables as read-only spans, draws affected retained layers,
then releases the handle.
```

The native engine is the sole writer of V8 handles, live DOM/CSS state, layout state,
Canvas/SVG builders, and unpublished scene arenas. Avalonia objects and renderer-owned
Skia/GPU resources never enter the engine thread. The renderer never receives live DOM
objects.

## Scene publication

The ABI exposes an opaque, reference-counted scene handle. Managed code receives a
pointer to immutable tables; it does not deserialize a command packet or recreate a
managed visual per DOM node.

Each `SceneDiff` contains:

- the renderer's acknowledged base revision and the new revision;
- damage rectangles;
- replacement descriptors for changed retained layers;
- stable layer ordering/removal operations;
- resource IDs and generation changes;
- per-Canvas generation/checkpoint information.

Publication is latest-wins. The engine coalesces changes against the renderer's last
acknowledged revision, so skipped intermediate frames are safe. If the base is stale,
resources were lost, or retained history exceeds its budget, the renderer requests a
checkpoint. It never applies an incompatible delta speculatively.

Immediate drawing can still use partial invalidation. The compositor retains pixels
outside the damage region; within it, the adapter redraws every current retained layer
that intersects the region. Drawing only newly appended commands is incorrect for
moves, removals, clears, opacity, clipping, and overlap.

## Input and scheduling

Avalonia owns platform hit testing for the host surface, focus, pointer capture, IME,
and cursor presentation. It sends ordered primitive input records to a bounded native
queue. Move and wheel events may be coalesced without crossing button, capture, or key
transitions. Down/up/cancel ordering is never coalesced.

Avalonia also submits the compositor's monotonic frame timestamp through
`WEBSCENE_INPUT_FRAME`. Each host frame releases the `requestAnimationFrame` callbacks
that were pending at its start; callbacks queued by those callbacks wait for a later
frame. Consecutive frame records are latest-wins, and the native runtime retains a
60 Hz timer only as a fallback for headless or offscreen hosts that do not provide
frame input.

The engine drains input, runs V8 tasks and microtasks, updates style/layout, and publishes
at most the useful latest scene for a frame. Managed application API calls are marshalled
to this same engine queue and may continue to use ClearScript in managed mode; native
mode exposes an application-neutral command/evaluation boundary.

## Resources and readiness

The host supplies an explicit resource root through
`webscene_engine_set_resource_root`. The generic resolver permits files below that root,
supports relative and absolute resource URLs, and rejects parent traversal. WebScene does
not embed a product asset location or reference server URL.

Components signal readiness through `globalThis.__webSceneComponentReady`. Product-owned
facades may translate their library's readiness callback to that generic signal and can
retain additional product diagnostics outside WebScene.

## Compilation cache

Both engines retain the persistent compilation-unit cache contract: key by engine/build
identity plus source identity, validate cached data, recover from corruption, and report
compiled versus reused units. Managed mode keeps its current cache. Native mode implements
the same observable policy within the native owner so source compilation does not cross
the hot boundary.

## Compatibility strategy

Managed and native engines implement one test adapter contract:

- load prepared HTML at a fixed viewport;
- execute/evaluate JavaScript and return JSON state;
- pump tasks until a predicate or timeout;
- inject pointer, wheel, keyboard, and resize input;
- settle and capture a deterministic BGRA frame;
- expose diagnostics and dispose engine-owned resources.

Reduced HTML/CSS contracts and the curated WPT profile execute through both adapters.
Product repositories own their full-library assets, bootstrap, screenshots, interaction
tests, and performance gates. A native failure is recorded as a parity gap; tests do not
silently switch engines.

## Renderer strategy

The first production adapter should use an Avalonia compositor custom visual and direct
scene traversal. This provides windowing, input, scheduling, clipping, text integration,
and backend portability without rebuilding a large Avalonia visual tree. Backend caches
are keyed by stable native resource ID/generation and may hold compiled paths, glyph
runs, images, gradients, and GPU objects.

A direct GPU backend may be added behind the same scene-reader contract. A shared texture
is appropriate only when a native renderer is independently justified; it would otherwise
duplicate a substantial 2D drawing and text stack.

## Release gates

Native mode is production-ready per capability group, not as an all-or-nothing browser:

1. scene ABI validation, lifetime and stale-base recovery;
2. managed/native shared conformance and deterministic pixel fixtures;
3. pointer capture, release, wheel, keyboard, focus and IME ordering;
4. bounded queues, retained-memory plateaus and renderer resource eviction;
5. warm/cold compilation-cache correctness and diagnostics;
6. resize and interaction frame pacing under a consuming product workload;
7. RID package matrix, signing, notarization and crash-symbol delivery.

The managed mode remains supported while native capability groups are promoted.

The initial automated package matrix builds and exercises macOS ARM64, Linux x64, and
Windows x64. Relocatable runtime packaging and required conformance execution are in
place; code signing, macOS notarization, crash-symbol publication, and the additional
modeled RIDs remain release gates.
