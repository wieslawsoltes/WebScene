# Runtime architectural upgrade shortlist

**Status:** Candidates evaluated; measured rollout order recorded below

**Date:** 2026-07-31

## Purpose

This document records the small set of architectural upgrades that are worth
investigating after reviewing Lightpanda's browser architecture and comparing it with
WebScene's managed and native engines.

An idea belongs here only if it has a credible path to at least one of these outcomes:

- reuse a maintained standards component and delete WebScene implementation code;
- improve compatibility across a broad standards area rather than fixing isolated tests;
- materially reduce peak or retained memory;
- materially improve startup or steady-state performance.

The recommendations are deliberately narrower than "make WebScene a browser." WebScene
should retain its CSS cascade, layout, scene, Canvas, SVG, input, and renderer architecture.
Lightpanda has no graphical rendering engine and its synthetic layout is not a replacement
for those systems.

## Current baseline

The relevant baseline is:

- native runtime release packages use V8 15.3.10;
- the native engine already shares a V8 isolate across engine contexts;
- the managed compatibility engine uses ClearScript and its V8 14.7.173.23 compatibility
  branch;
- managed HTML parsing uses AngleSharp;
- native HTML parsing, CSS syntax parsing, selector compilation, and V8 Web API binding
  installation are handwritten in `webscene_v8_runtime.cpp`;
- the native runtime currently contains approximately 30,000 lines in that translation
  unit and more than 300 explicit V8 template/property installation calls;
- the curated WPT component profile contains testharness tests, product-neutral contracts,
  and pixel reftests, but it intentionally covers only the promoted component profile.

The following thresholds define "material" for these investigations. A proposal need not
meet every threshold, but it must meet at least one without regressing the others:

| Outcome | Investigation gate |
| --- | --- |
| Maintained code | Delete at least 500 handwritten native lines in the affected subsystem, or prevent a comparable amount of projected binding/parser growth. Generated output is not counted as maintained source. |
| Compatibility | Produce a broad, measurable pass-rate improvement in a targeted upstream WPT area with no regression in the required profile. |
| Memory | Reduce peak RSS by at least 15%, or the post-GC retained plateau by at least 25%, in a representative multi-component lifecycle soak. |
| Performance | Improve cold startup/context creation by at least 20%, or representative steady-state CPU time by at least 10%, without moving work to the UI thread. |

These are adoption gates, not predicted results.

## Recommendation matrix

| Priority | Recommendation | Reuse and code reduction | Broad compatibility | Memory | Performance |
| --- | --- | --- | --- | --- | --- |
| 1 | Upstream standards parsing module | **High** | **Very high** | Neutral/possible | Possible |
| 2 | Generated Web API bindings plus a V8 embedder snapshot | **High** | **High** | Possible | **High for startup/context creation** |
| 3 | V8-aware native memory ownership and accounting | Moderate | Neutral | **High potential** | Moderate potential |
| 4 | Broad upstream WPT discovery lane | Reuses test infrastructure | **High over time** | None | None |

## Measured outcome (2026-08-01)

The isolated evaluations are complete. The detailed samples, known baseline failures, raw
artifact paths, and decision rationale are in
[`runtime-upgrade-evaluation-log.md`](runtime-upgrade-evaluation-log.md).

| Candidate | Decision | Gate cleared | Important cost or limit |
| --- | --- | --- | --- |
| `html5ever` 0.39.0 | Adopted behind a rollout switch | Broad HTML compatibility and upstream parser reuse | Performance neutral; approximately +0.7% peak RSS |
| Servo `cssparser` 0.37.0 | Rule in; streaming ABI adopted | CSS Syntax compatibility, 3/11 to 11/11 focused assertions; 23.8%-34.9% faster than owned adapter | Process RSS neutral; zero Rust CSS allocations/retention; upstream crate unmodified |
| Generated WebIDL catalog | Rule in incrementally | Binding compatibility, 2/10 to 9/10 focused assertions | No material code/memory/performance win; prototype attributes need semantic callback refactoring |
| V8 bootstrap snapshot | Rule in first | -27.1% independent context lifecycle, -11.6% shared lifecycle | +0.660% shipped size; per-RID sidecar packaging required |
| Servo `selectors` 0.39.0 syntax | Rule in after ABI cleanup | Selector syntax/specificity, 1/10 to 10/10 focused and 1/9 to 8/9 pinned WPT | +24.7% selector stress; memory neutral; no maintained-code reduction |
| Lexbor CSS 1.4.0 (Lexbor v3.0.0) | Rule out | Required profile neutral, but no improvement over Servo | Focused CSS Syntax 10/11 vs Servo 11/11; +71% to +99% on valid parser fixtures; 3.4x 1 MiB parser-pool memory |

None of these candidates reaches the memory-benefit gate. Memory reduction remains a
separate architectural investigation rather than a claimed side effect of standards
component adoption.

## 1. Upstream standards parsing module

### Implementation status (2026-07-31)

The first `html5ever` spike is implemented behind the compile-time
`WEBSCENE_NATIVE_ENGINE_HTML_PARSER=legacy|html5ever` selection. The default remains
`legacy` until the adoption gates below are evaluated with equivalent V8 runtime
artifacts.

The spike includes:

- a clean-room Rust `staticlib` pinned to `html5ever` 0.39.0 and a committed
  `Cargo.lock`;
- a versioned, exception-safe C tree-sink ABI using borrowed byte slices and opaque
  stable native node handles;
- full-document and context-fragment entry points used by navigation, `innerHTML`,
  `DOMParser`, templates, contextual fragments, and projected iframe bodies;
- explicit node-kind and namespace state, comments, doctypes, quirks reporting,
  template-content fragments, foster parenting, insertion, removal, and reparenting;
- parser timing, callback, parse-error, node-kind, and Rust allocation diagnostics;
- a V8-free correctness suite and 30-iteration p50/p95 microbenchmark;
- a diagnostic-only comment-discard policy implemented in WebScene's sink without an
  `html5ever` fork;
- separate release-build and package paths for `legacy` and `html5ever` artifacts,
  including parser provenance and Rust third-party notices.

Initial macOS arm64 microbenchmark results are diagnostic rather than adoption evidence:

| Fixture | p50 | p95 | Throughput | Native retained node/attribute bytes | Rust peak / retained |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 KiB component markup | 0.076 ms | 0.084 ms | 13.9 MiB/s | 68,624 | 4,538 / 0 |
| 50 KiB component markup | 1.239 ms | 1.380 ms | 39.4 MiB/s | 3,079,976 | 140,112 / 0 |
| 1 MiB component markup | 27.388 ms | 28.886 ms | 36.5 MiB/s | 62,984,768 | 3,638,154 / 0 |

On the 50 KiB fixture, discarding comments reduced native nodes from 3,049 to 2,772 and
retained node/attribute bytes from 3,079,976 to 2,809,624, while changing p50 parse time
by less than 1%. This confirms that the dominant cost is WebScene's approximately 1 KiB
`dom_node`, not temporary Rust parser storage. Comments therefore remain enabled; a
compact non-element node representation is the required mitigation if equivalent
legacy/html5ever runtime measurements breach the 10% retained-memory gate.

The subsequent matched evaluation adopted `html5ever` for compatibility and parser
ownership. Required-profile and ecosystem proofs pass, performance is neutral, and peak
RSS is approximately 0.7% higher. The legacy parser remains only as a rollout comparison
path and should be deleted after packaged-platform coverage is complete.

### Decision

Prototype one separately built Rust static library that provides coarse-grained standards
parsing services to the native engine. Adopt upstream components directly; do not copy
Lightpanda's AGPL integration code.

The first and strongest candidate is Servo's `html5ever`. It should replace all native
HTML tree construction used by document loading, fragments and `innerHTML`, `DOMParser`,
templates, and iframe documents. A small C ABI tree sink should write directly into
`native_document` while normal style, mutation-observer, and layout invalidation are
suspended until the parse transaction commits.

The module should be designed so that later syntax-only services can be added without
creating a second foreign-function boundary:

- `encoding_rs` for standards-compatible byte decoding and encoding labels;
- Servo `cssparser` for stylesheet/declaration tokenization and error recovery;
- a selector-parser spike only if it can emit WebScene's existing compiled selector IR
  without importing a second cascade or layout engine.

CSS parsing must cross the ABI once per stylesheet and return a compact rule/declaration
IR. It must not call across the ABI once per token. WebScene remains authoritative for
the cascade, invalidation, computed values, layout, and rendering.

### Why it qualifies

- It reuses standards implementations maintained outside WebScene.
- It deletes the native tag scanner, entity decoder, stack-based tree builder, and
  eventually substantial CSS tokenizer/error-recovery code.
- `html5ever` supplies HTML tree-builder behavior that isolated patches cannot reproduce
  economically: adoption-agency correction, table foster parenting, fragment context,
  templates, namespaces, quirks mode, raw-text/RCDATA handling, and parse errors.
- A single native parser makes document load and DOM fragment APIs consistent.
- A direct tree sink avoids constructing a duplicate intermediate DOM.

### Required proof

1. Run the existing required profile unchanged through the new parser.
2. Add upstream HTML parsing/tree-construction directories as a discovery denominator.
3. Differentially serialize native, managed/AngleSharp, and Chrome DOMs for reduced cases.
4. Benchmark large document load, repeated `innerHTML`, React mount, allocation volume,
   and peak temporary memory.
5. Delete the old parser after parity. Do not keep a permanent compatibility fallback.

### Risks and boundaries

- Rust becomes a native build dependency, although it is isolated behind one static C ABI.
- DOM mutation timing and custom-element reactions must remain spec-observable even when
  internal invalidation is batched.
- Replacing CSS syntax parsing is a separate decision from replacing HTML parsing.
- Importing a full external style or layout engine would duplicate WebScene's strongest
  subsystem and is out of scope.

## 2. Generated Web API bindings plus a V8 embedder snapshot

### Decision

Replace handwritten V8 template installation with a declarative supported-API catalog and
generated C++ glue. Use maintained WebIDL data, such as the Web Platform `webref/idl`
corpus, as syntax and inheritance input, while keeping an explicit WebScene exposure file
that says which interfaces and members are actually implemented.

The generator should emit:

- function templates, prototype inheritance, constructors, methods, and accessors;
- property attributes and Window/Worker exposure sets;
- argument/result conversions and WebIDL exception rules;
- interface brand checks and illegal-invocation behavior;
- `SameObject` wrapper identity and native unwrapping metadata;
- feature inventory consumed by compatibility reporting.

Once the generated surface is stable, build a V8 custom startup snapshot containing the
templates, prototype graph, standard globals, and immutable bootstrap code. Context
creation should restore that state and attach only context-specific native objects.

The snapshot must be keyed by V8 revision, native ABI version, target architecture, build
configuration, and Web API catalog hash. It complements the existing JavaScript
compilation cache; it does not replace it.

### Why it qualifies

- It replaces hundreds of repetitive, hand-maintained V8 installation calls with one
  auditable source of truth.
- WebIDL-derived inheritance, attributes, arity, exposure, and conversion behavior remove
  entire classes of compatibility mistakes.
- New DOM coverage grows primarily as implementation code, not repeated binding plumbing.
- A custom snapshot can avoid rebuilding the same prototype/template graph for every
  context and may allow more immutable startup data to be shared.
- The existing shared isolate makes per-context template/bootstrap work—not isolate
  creation—the relevant optimization target.

### Required proof

1. Generate one vertical interface family first: `EventTarget` -> `Node` -> `Element` ->
   `HTMLElement`.
2. Compare property descriptors, prototype identity, illegal invocation, exception types,
   and wrapper identity against Chrome and the existing WPT profile.
3. Show a net reduction in handwritten binding code before expanding the generator.
4. Add the snapshot only after generated and non-generated contexts are behaviorally
   identical.
5. Benchmark process cold start, first engine creation, subsequent context creation,
   snapshot size, RSS, and package size.

### Risks and boundaries

- Generated source can reduce maintenance while increasing binary size; both must be
  measured independently.
- A generator that merely reproduces bespoke callback code without deleting old paths is
  not a successful migration.
- Snapshots make native callback addresses and startup data part of the build contract.
  Invalid or mismatched snapshots must fail closed, not silently load.

## 3. V8-aware native memory ownership and accounting

### Decision

Make native memory retained by JavaScript visible to V8 and align allocation lifetime with
document, context, and wrapper lifetime.

Use V8's `AdjustAmountOfExternalAllocatedMemory` for memory whose reachability is governed
by V8 wrappers. Accumulate signed deltas and report them at event-loop or transaction
boundaries rather than on every allocation. Candidate categories include:

- DOM nodes and attributes retained only through wrappers;
- style objects, compiled selectors, and per-document caches;
- Canvas backing data and other JS-owned buffers not already represented as V8 backing
  stores;
- wrapper sidecars, listener tables, observer records, and retained native handles.

Do not report renderer-owned resources that V8 collection cannot reclaim, and do not
double-count ArrayBuffer backing stores already known to V8.

Pair accounting with explicit lifetime regions:

- a context/document region released as a unit;
- temporary parsing/style compilation regions released after commit;
- small pinned regions for data whose final owner is a weak V8 wrapper;
- bounded pools with high-water metrics and trimming on context disposal, invisibility,
  explicit memory pressure, and oversized misses.

This should extend the engine's existing arena, result-pool, capacity-compaction, and
shared-isolate metrics rather than introduce a parallel memory subsystem.

### Why it qualifies

- V8 currently cannot make good GC timing decisions from its heap size when substantial
  reclaimable memory lives in native DOM/style objects.
- Whole-region disposal removes fragmentation and object-by-object teardown work.
- Bounded reuse can reduce allocator traffic without allowing pools to become a new
  retained-memory leak.
- The combination targets both lifecycle plateaus and GC/allocator CPU cost.

### Required proof

1. Establish per-category live, retained-pool, and V8-reported byte metrics before changing
   GC behavior.
2. Run repeated create/mount/interact/unmount cycles with 1, 4, 16, and 32 components.
3. Record peak RSS, post-GC plateau, context-disposal latency, GC pause distribution,
   allocation rate, and pool high-water values.
4. Verify that external-memory deltas return to zero after the last owning context and
   result lease are released.
5. Reject the change if it merely trades memory for excessive GC frequency or frame
   latency.

### Risks and boundaries

- External-memory reporting does not reduce genuinely live memory.
- Incorrect ownership or double counting can trigger pathological GC behavior.
- Arena allocation is appropriate only where object lifetimes are sufficiently aligned;
  long-lived exceptions must not pin an entire document-sized region accidentally.

## 4. Broad upstream WPT discovery lane

### Decision

Keep the curated component profile as the release gate and add a separate, non-gating WPT
discovery lane driven by the upstream `MANIFEST.json` and stock WPT HTTP server.

Initially shard the directories most likely to improve WebScene components:

- `dom`, `html`, and `selectors`;
- `cssom`, targeted CSS syntax, and CSS conditional rules;
- UI Events and input-related testharness tests;
- URL, encoding, Fetch, Workers, and storage only as their required server/origin
  infrastructure becomes available.

Record file and subtest pass/fail/timeout/crash totals, duration, engine revision, and the
first failing assertion. Preserve results between runs and report deltas by standards
area. Promote stable relevant cases into the curated required/candidate profile rather
than making the entire upstream suite a release gate.

Reusable pieces include the WPT manifest and server themselves and the Apache-licensed
Lightpanda demo runner's process-pool, crash-restart, memory-limit, and no-progress
watchdog patterns. WebScene should adapt those patterns to its existing adapter rather
than add CDP solely to run tests.

### Why it qualifies

- It turns compatibility investment from anecdotal API additions into measurable area
  coverage.
- It supplies the denominator needed to judge whether adopting `html5ever`, `cssparser`,
  or generated bindings produced a broad improvement.
- It reuses upstream tests and infrastructure rather than authoring equivalents locally.
- It preserves WebScene's stronger renderer-specific reftest gate.

### Required proof

1. Produce deterministic results for a small directory across repeated runs.
2. Separate unsupported harness infrastructure from product failures.
3. Keep results comparable across native and managed engines and pinned WPT revisions.
4. Require every runtime adoption above to cite before/after WPT deltas.

This recommendation improves compatibility over time but does not itself improve runtime
memory or performance. It is included because it is the evidence system needed to accept
the other changes safely.

## Explicit non-recommendations

The following are not justified by the four outcome filters:

- **Do not import Lightpanda's DOM, CSS, or bridge implementation.** The browser code is
  AGPL-3.0-only by default, and its automation-oriented synthetic layout is below
  WebScene's current renderer requirements. Reimplement useful patterns or adopt their
  permissively licensed upstream dependencies directly after license review.
- **Do not replace WebScene layout with Lightpanda layout.** Lightpanda has no graphical
  renderer and approximates geometry for automation.
- **Do not add another JavaScript engine.** WebScene native packages already use V8
  15.3.10, newer than the reviewed Lightpanda revision.
- **Do not propose shared-isolate hosting as new work.** The native runtime already shares
  an isolate across contexts and has active/peak context metrics and lifecycle tests.
- **Do not add CDP only for WPT.** It expands the product surface without improving the
  component runtime; use the existing test adapter.
- **Do not adopt a full external CSS cascade/layout stack.** The evaluated syntax and
  selector parser islands improve compatibility while preserving WebScene's compiled rule,
  cascade, matching, invalidation, layout, and rendering model. They do not reduce current
  maintained source and must not be described as code-reduction wins.

## Recommended sequence

1. Package the measured V8 bootstrap snapshot with strict fingerprints for each supported
   RID, then enable it by default.
2. Complete packaged-platform rollout of `html5ever`, then remove the legacy HTML parser.
3. Use the completed borrowed-slice callback ABI, expand ecosystem coverage, promote the
   standards module, and remove the syntax fallbacks. Do not fork `cssparser`; its public
   parser traits were sufficient. Next remove the remaining C++ syntax-IR-to-runtime copy.
4. Continue migrating operations and interface structure to generated WebIDL bindings;
   refactor semantic getter/setter callbacks before moving attributes to prototypes.
5. Introduce external-memory accounting and lifetime regions category by category, using
   the lifecycle soak to target the still-unmet memory gate.

Each stage must remove its superseded path. Keeping both implementations indefinitely
would increase the codebase and invalidate the primary reason for adopting maintained
components.

## Review basis

This shortlist was prepared from a source-level comparison of:

- Lightpanda browser commit `392bb4c772446c036e3ee11357205f806f331f5d`;
- Lightpanda demo/WPT runner commit `25095f3ca042f8b80b79d83e8842f30154af80a0`;
- WebScene native runtime, packaging, architecture records, and WPT component profile at
  the date above.

The comparison is architectural evidence, not permission to copy code. Every adopted
dependency or reused implementation must receive a separate version, license, security,
packaging, and maintenance review.
