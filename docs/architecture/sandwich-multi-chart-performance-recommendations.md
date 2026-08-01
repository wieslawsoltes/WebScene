# Sandwich multi-chart performance recommendations

**Status:** Implementation roadmap

**Date:** 2026-08-01

**Baseline release:** WebScene 1.0.16

## Purpose

This document is a self-contained brief for an agent improving WebScene for a
large, continuously updating browser application embedded in an Avalonia
desktop application. Sandwich's four-chart benchmark is the reference
workload, but every WebScene change must remain generic. Do not add
TradingView assets, API definitions, URL checks, selector checks, or other
site-specific behavior to WebScene.

The goal is to reduce total process CPU and memory while preserving browser
semantics, visual correctness, input behavior, reparenting, and the absence of
native-child-window airspace restrictions.

## Known evidence

Treat these measurements as the starting point, not as results to reproduce
selectively:

- The deterministic four-chart workload sends one real-time bar every 250 ms
  to each chart through the production generated interop and datafeed path.
- Published WebScene 1.0.14 measured `0.35918` CPU core and `630.40 MiB` mean
  RSS in that workload.
- Two published WebScene 1.0.15 runs averaged `0.32328` CPU core and
  `633.20 MiB` mean RSS: about 10% lower CPU with effectively unchanged RSS.
- A live-data Time Profiler sample attributed approximately `0.0519` core to
  WebScene native/V8, `0.0207` to system work, `0.0100` to Avalonia/Skia,
  `0.0077` to tiered JIT, and `0.0064` to JavaScript. Thread-pool spinning was
  negligible in that capture.
- The Servo selector cache is present. A focused repeated-selector benchmark
  improved from `117.830 ms` to `29.208 ms`, a 75.2% reduction (`4.0x`).
- The WebScene 1.0.16 document-property/named-property correction improved its
  focused benchmark from `68.804 ms` to `3.081 ms`, a 95.5% reduction
  (`22.3x`). Its whole-application benefit has not yet been established.
- The generated binary ABI previously reduced CPU by 29.5% and managed
  allocation by 96.7% against the removed JSON control in a focused
  four-engine callback workload. It is already the intended hot path.
- The native engine already shares one V8 isolate across engine contexts. Do
  not propose “use a shared isolate” as though it were absent. Investigate
  what remains duplicated per context.
- The compositor mailbox already coalesces immutable scene publication at
  compositor boundaries. Preserve ADR 0011.
- An earlier partial-damage experiment produced black regions and flicker.
  Clipping a fresh frame to a damage rectangle is incorrect unless undamaged
  pixels are retained.

Related evidence:

- [TradingView-shaped four-chart generated binary interop](tradingview-four-chart-binary-interop-results.md)
- [ADR 0011: compositor mailbox](adr/0011-compositor-driven-native-scene-publication.md)
- [ADR 0012: pooled binary interop](adr/0012-pooled-binary-native-javascript-interop.md)
- [Runtime architectural upgrade shortlist](runtime-architectural-upgrade-shortlist.md)

## Non-negotiable constraints

1. Do not add consumer-specific code to WebScene. Optimize standards-level
   operations and generic runtime behavior.
2. Do not share a Window, Document, JavaScript global realm, DOM, storage
   namespace, or mutable application state between documents.
3. Do not move engine work onto Avalonia's UI thread.
4. Do not introduce a native child window or an airspace-restricted browser
   surface.
5. Do not report a performance win from live market data alone. Use the
   deterministic workload and matched builds.
6. Do not trade correctness for partial rendering. Damage must never create
   stale, transparent, or black pixels.
7. Reparenting a WebScene control must preserve the engine/context whenever
   ownership and lifetime still permit it. It must not reload the page merely
   because its Avalonia visual parent changed.
8. Keep normal diagnostics disabled or below 1% overhead. Detailed counters
   may be enabled explicitly for profiling.
9. One work package per commit. Include tests and before/after evidence in the
   same change or an immediately following documentation commit.

## Measurement protocol

Complete this protocol before optimizing and after every candidate change.

### Workloads

Measure all of the following using Release builds:

| Workload | Purpose |
| --- | --- |
| Empty benchmark window | Fixed Sandwich/Avalonia overhead |
| 1 deterministic chart | Per-document startup and steady state |
| 2 deterministic charts | Scaling and shared-resource effects |
| 4 deterministic charts | Acceptance workload |
| 4 hidden after ready | Timer/background and visibility policy |
| 4 charts, reparent cycle | Lifetime correctness and leaks |

The Sandwich harness is enabled with `--chart-benchmark-2x2`; deterministic
data is selected with `SANDWICH_CHART_BENCHMARK_REPLAY=1`. It is maintained in
the consumer repository and must not be copied into WebScene.

### Collection

- Wait until every chart reports ready.
- Warm for at least 30 seconds.
- Collect at least 60 one-second samples for CPU and RSS.
- Use ABBA ordering for two variants: control, candidate, candidate, control.
- Record median and mean CPU, mean RSS, RSS range, engine/context count,
  rendered scene count, published/superseded scene count, diff count, and
  market update count.
- Reject a run when delivered update or rendered-scene work differs
  materially between variants.
- Use the same screen state, window size, power state, build configuration,
  package layout, and sampling tool.
- Account for all processes if a comparison includes a multi-process browser.

### Adoption thresholds

A runtime optimization should normally achieve at least one of:

- 10% lower four-chart steady-state CPU;
- 10% lower total four-chart RSS or 15% lower incremental RSS per chart;
- 20% lower cold context startup;
- removal of at least 90% of allocation on a hot boundary;
- a material standards-correctness improvement with no measurable regression.

Smaller improvements may be retained when they remove an identified
pathological hot path, are independently proven, and have negligible risk.

## Work package 1: unified low-overhead diagnostics

Implement this first. Optimization without workload accounting has already
produced misleading conclusions.

Expose process and per-context counters for:

- timers scheduled, fired, cancelled, late, and coalesced;
- `requestAnimationFrame` requested, invoked, and skipped;
- microtask checkpoints;
- style invalidations, style recalculations, layouts, paints, and their time;
- DOM mutations and MutationObserver/ResizeObserver deliveries;
- scene builds, no-op builds, publications, presentations, superseded scenes,
  full checkpoints, and damage area;
- compositor wake requests and actual wakes;
- generated binary calls/callbacks, arbitrary evaluation calls, JSON
  materializations, bytes encoded/decoded, leases, and pool misses;
- selector cache, script cache, resource cache, font cache, and decoded-image
  cache hits/misses;
- V8 heap used/committed, native DOM bytes, scene bytes, mapped resource bytes,
  decoded image bytes, and retained interop arenas.

Counters must be readable without parsing log text. Prefer a snapshot API with
monotonic integers and durations. Add a reset/baseline facility for tests, but
do not make production correctness depend on reset.

Acceptance:

- Disabled overhead is unmeasurable; enabled overhead is below 1% in the
  four-chart benchmark.
- Counter invariants are tested, including publication/presentation accounting
  and zero outstanding interop leases after disposal.
- The benchmark can prove that two compared runs performed equivalent work.

## Work package 2: timer, animation-frame, and compositor scheduling

This is the highest-probability CPU investigation.

Profile callback frequency and determine which callbacks produce a style,
layout, scene, or visible change. Then evaluate generic changes such as:

- one process-level scheduler coordinating due work across contexts while
  preserving each context's event-loop ordering;
- aligning animation-frame delivery with the Avalonia compositor cadence;
- issuing at most one pending compositor wake per surface;
- suppressing scene construction/publication when no visual state changed;
- avoiding repeated clock polling when the next timer deadline is known;
- standards-compatible timer clamping and coalescing;
- throttling background documents only when host visibility is genuinely
  false, with immediate recovery when visible;
- preventing hidden documents from producing presentation work while still
  running required non-visual JavaScript at the appropriate policy rate.

Do not merge event loops or mutable realms merely because contexts share an
isolate. Preserve timer order, microtask checkpoints, promise behavior, input
ordering, and visibility events.

Acceptance:

- Deterministic updates delivered and visible results are unchanged.
- Pointer interaction, dialogs, chart drag, resize, and first-frame liveness
  remain correct.
- Hidden/visible transitions have automated coverage.
- Four-chart CPU improves by at least 10%, or the investigation is documented
  and closed with evidence identifying the next dominant cost.

## Work package 3: eliminate repeated no-op DOM/style/layout work

Use profiles and the new counters to rank operations. Do not introduce broad
memoization without invalidation rules.

Investigate:

- native accessors for frequently used standard Document/Window properties so
  misses do not fall through to whole-DOM named-property scans;
- O(1) ID/name indexes with correct mutation and tree-adoption invalidation;
- computed-style reuse keyed by the actual cascade dependencies;
- narrower style invalidation for class, attribute, state, and subtree changes;
- avoiding layout when changed properties cannot affect geometry;
- batching observer delivery at the proper microtask boundary;
- deduplicating repeated identical ResizeObserver results;
- preserving and extending the compiled selector cache without reparsing for
  validation;
- cache hit-rate and retained-size limits to prevent unbounded growth.

Start with the top symbolized operation from a deterministic Time Profiler
capture. Add a focused microbenchmark and a standards regression test before
changing it.

Acceptance:

- The focused operation improves materially, preferably at least 2x.
- Required DOM/CSS tests and relevant WPT subsets pass.
- The deterministic four-chart run does not regress CPU or RSS.

## Work package 4: reduce per-context memory duplication

The isolate is already shared. Build a 0/1/2/4-context memory model before
changing ownership. Report fixed process cost and incremental cost per context.

Attribute retained memory to:

- V8 context heaps and embedder/native objects;
- bootstrap/prototype data not covered by the existing snapshot;
- compiled scripts and code cache;
- DOM nodes, attributes, strings, style data, and layout boxes;
- immutable and in-flight scenes;
- mapped/decoded resources;
- fonts, glyphs, typefaces, images, and canvas surfaces;
- callback registrations, handles, and interop arenas.

Safe sharing candidates include immutable snapshot pages, compiled script
cache entries keyed by source and runtime identity, memory-mapped resource
bytes, immutable font/typeface objects, decoded immutable images, and bounded
compiled-selector entries. Mutable DOM, layout, canvas, storage, event, and
application objects must remain context-owned.

Add lifecycle so shared caches release or evict data. Run repeated create,
reparent, detach, destroy, and GC/idle cycles to find retained contexts.

Acceptance:

- At least 15% lower incremental RSS per chart or 10% lower total four-chart
  RSS, with no context leakage after the lifecycle soak.
- Cache ownership is thread-safe and bounded.
- Separate documents cannot observe or mutate each other's state.

## Work package 5: verify generated binary interop end to end

The binary ABI is implemented; this package is an audit and hardening task, not
a request to design another transport.

- Prove that schema-known generated calls and callbacks never fall back to
  JavaScript source generation or JSON.
- Count arbitrary evaluation separately from generated operations.
- Report bytes/call, pool hit/miss, leases, operation slots, callback latency,
  and managed allocation.
- Use borrowed/leased results for large arrays when callers can consume them
  synchronously without materialization.
- Ensure callback targets and retained handles become invalid safely during
  detach, reparent, navigation, and engine destruction.
- Add an optional benchmark assertion that fails if a generated hot operation
  materializes JSON.

Acceptance:

- Normal generated void callbacks remain near allocation-free and leave zero
  outstanding leases.
- Four-context callback stress passes cancellation/disposal races.
- The consumer's market-data and broker callback path is confirmed binary by
  counters, without adding its API files to this repository.

## Work package 6: asset, font, and image reuse

Network and parsing startup work may be duplicated even when the V8 isolate is
shared. Measure before implementing.

Investigate process-wide immutable reuse for:

- mapped response bodies and packaged resources;
- V8 code-cache entries;
- CSS source/token storage where ownership permits it;
- font files, typefaces, and glyph caches;
- decoded immutable images and SVG resources.

Keys must include all data that affects correctness: URL, origin/security
policy, response validators, runtime/ABI version, content hash, decoding
options, device scale where relevant, and font variation/style.

Acceptance:

- Context isolation and cache invalidation tests pass.
- Cold and warm startup are measured separately.
- Retained shared data is bounded and included in memory diagnostics.

## Work package 7: composition and damage, evidence gate

Do this only after deterministic profiling shows composition, rasterization,
pixel upload, or whole-surface redraw is a material remaining cost. The prior
profile placed Avalonia/Skia well below native/V8 CPU, so this is not currently
the first target.

First add evidence:

- signposts around scene traversal, Skia recording/rasterization, texture or
  pixel upload, and compositor presentation;
- bytes uploaded/copied per frame;
- full surface area versus damaged area;
- GPU/Metal and CPU time;
- frame drops and presentation latency.

If partial damage is pursued, use a correctness-preserving design:

1. Keep a retained backing store or retained compositor primitives.
2. Repaint damage into preserved content without clearing undamaged pixels.
3. Promote to a full checkpoint after attach, reparent, resize, scale change,
   compositor recreation, surface loss, or uncertain dependency.
4. Include effects whose pixels extend beyond local geometry: shadows,
   filters, clips, transforms, antialiasing, text, canvas, and SVG.
5. Validate with deterministic pixel comparisons and forced random damage.

A shared texture/IOSurface path is only a candidate after tracing proves that
CPU copies or uploads are significant. It must preserve Avalonia composition,
transforms, clipping, opacity, input, and airspace behavior. Do not replace the
renderer or embed a native child window as part of this experiment.

Acceptance:

- No black rectangles, stale pixels, flicker, or resize regression.
- Pixel tests cover damage and forced full-frame fallback.
- At least 10% whole-application CPU reduction or a separately material GPU/
  latency improvement justifies the added complexity.

## Work package 8: lifecycle, reparenting, and visibility soak

Treat this as a cross-cutting acceptance package.

Automate:

- attach, ready, detach, reattach to the same window;
- move between Avalonia parents;
- move to a floating window and back;
- minimize, hide, show, occlude where observable, and restore;
- resize and device-scale changes;
- compositor loss/recreation;
- dispose while callbacks, timers, promises, and scene publications are live.

Verify that compatible reparenting retains engine identity, document state,
subscriptions, storage, and generated handles. When recreation is genuinely
required, fail or checkpoint explicitly rather than silently presenting a
partially valid scene.

Acceptance:

- No reload during compatible reparenting.
- No runtime exception, missing market updates, blank scene, or stale input.
- Context and native resource counts return to baseline after the soak.

## Consumer-side recommendations

These belong in Sandwich or its served application, not in WebScene:

- use supported application configuration to disable unused browser-app
  features, studies, services, and persistence traffic;
- avoid high-frequency polling shims when WebScene provides the standard API;
- preserve deterministic replay as the performance acceptance workload;
- distinguish initialization time from steady-state measurement;
- audit direct remote hosting versus a small local bootstrap page separately;
- verify that all generated hot APIs use binary codecs and callbacks.

Do not patch third-party served JavaScript merely to hide a WebScene standards
gap. Implement the generic missing standard behavior in WebScene and add a
reduced regression test.

## Recommended execution order

1. Re-establish the 1.0.16 deterministic 0/1/2/4-chart baseline.
2. Implement unified diagnostics.
3. Profile and optimize scheduling/no-op frames.
4. Profile and optimize the hottest DOM/style/layout operation.
5. Attribute and reduce per-context memory.
6. Audit binary interop and shared immutable assets.
7. Re-profile. Pursue composition/damage only if it is now material.
8. Run lifecycle, pixel, and package acceptance before version bump/publish.

## Agent completion report

An implementing agent must finish with:

- commits and changed file list;
- exact control and candidate revisions;
- exact build configuration and native library/package hashes;
- commands used to reproduce tests and benchmarks;
- ABBA CPU/RSS results and work counters;
- focused benchmark and profile attribution;
- correctness, lifecycle, and visual-test results;
- rejected hypotheses and why they were rejected;
- remaining risks and the next highest measured hotspot.

Do not describe a microbenchmark improvement as an application improvement
unless the deterministic four-chart acceptance run supports it.
