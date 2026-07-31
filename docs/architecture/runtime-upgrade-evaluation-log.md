# Runtime upgrade evaluation log

**Status:** Active

**Started:** 2026-08-01

## Objective

Rule in or rule out each remaining runtime upgrade with isolated prototypes and
measured evidence. A decision must not be inferred from another candidate: adopting a
CSS tokenizer does not imply adopting an external selector engine, and generating V8
bindings does not imply that a startup snapshot is worthwhile.

Candidates:

1. Servo `cssparser` for stylesheet and declaration syntax.
2. Generated WebIDL-to-V8 bindings.
3. A V8 embedder startup snapshot after binding parity.
4. Servo selector parsing, only if it can emit WebScene's existing selector IR without
   importing another cascade or layout engine.

## Decision gates

A candidate is ruled in only when it meets at least one material-benefit gate and stays
within every non-regression limit. Otherwise it is ruled out or retained as conditional
research.

| Area | Material-benefit gate | Non-regression limit |
| --- | --- | --- |
| Maintained code | Delete at least 500 handwritten native lines, or prevent comparable projected growth | No permanent duplicate implementation after adoption |
| Compatibility | Broad improvement in the targeted upstream WPT denominator | No required-profile regression |
| Memory | At least 15% lower peak RSS or 25% lower post-GC retained plateau | No more than 10% regression on representative workloads |
| Performance | At least 20% faster cold startup/context creation or 10% lower representative steady-state CPU | No more than 10% regression without a separately accepted compatibility tradeoff |
| Distribution | — | Package-size and toolchain costs must be reported explicitly |

Every measurement must record the commit, V8 revision, compiler/build mode, target,
fixture, warm/cold cache state, sample count, and raw artifact location.

## Baseline

- Commit baseline: `38d72dc` (`Integrate html5ever native HTML parser`).
- Native JavaScript engine: V8 15.3.10 with pointer compression and shared cage.
- Target used for the initial measurements: macOS arm64, Release, certification enabled.
- HTML parsing: `html5ever` 0.39.0 through a coarse Rust static-library C ABI.
- Parser correctness suite passes.
- TradingView proof passes with 1,627 elements, eight canvases, live WebSocket data, and
  pointer/click delivery to the chart canvas.
- `html5ever` parser microbenchmark: approximately 1.25 ms p50 for 50 KiB and 28 ms p50
  for 1 MiB; Rust retains zero bytes after parsing.
- Matched TradingView runs showed approximately +0.7% peak RSS for `html5ever` versus
  legacy and no repeatable end-to-end performance regression. Network variance prevents
  treating the observed speed difference as a benefit.
- Known suite baselines must be preserved during comparisons: the legacy build currently
  stops at an SVG `currentColor` fixture, while the `html5ever` build reaches a later
  certification feature-inventory assertion.

## Candidate scorecards

### 1. Servo cssparser

**State:** Queued

**Prototype boundary:** Replace CSS tokenization, block/function handling, declaration
splitting, and error recovery. Emit a compact rule/declaration IR in one coarse call.
WebScene remains authoritative for selectors initially, cascade, invalidation, computed
values, property semantics, layout, and paint.

**Required evidence:**

- Lines of handwritten tokenizer/parser code deleted.
- Required-profile result and targeted upstream CSS Syntax denominator.
- Stylesheet, inline declaration, repeated CSSOM mutation, TradingView, and Monaco timing.
- Peak temporary parser memory, process RSS, retained plateau, and package-size delta.

**Decision:** Pending.

### 2. Generated WebIDL-to-V8 bindings

**State:** Queued after the CSS syntax spike

**Prototype boundary:** Generate the `EventTarget` -> `Node` -> `Element` ->
`HTMLElement` interface family from pinned WebIDL input plus an explicit WebScene
exposure manifest. Keep native method behavior handwritten.

**Required evidence:**

- Net handwritten binding-code reduction, excluding generated output.
- Property descriptors, prototype identity, brand checks, illegal invocation,
  conversions, exceptions, and wrapper identity against Chrome and the required profile.
- Context-creation time, binary size, and RSS before considering snapshots.

**Decision:** Pending.

### 3. V8 embedder startup snapshot

**State:** Conditional on generated-binding behavioral parity

**Prototype boundary:** Snapshot only immutable bootstrap code and the generated
prototype/template graph. Attach document/context-specific native state after restore.

**Required evidence:**

- First and subsequent context-creation latency.
- Process cold start, snapshot generation/load time, RSS, and package-size delta.
- Strict mismatch rejection keyed by V8 revision, ABI, architecture, configuration, and
  binding-catalog hash.

**Decision:** Pending prerequisite.

### 4. Servo selector parsing

**State:** Conditional on the CSS IR experiment

**Prototype boundary:** Accept only if the external parser can emit WebScene's existing
compiled selector IR. Do not import a second cascade, invalidation system, style tree, or
layout engine.

**Required evidence:**

- Targeted Selectors WPT improvement and required-profile parity.
- Net deletion of selector parsing/compilation code.
- Selector compilation, matching, invalidation, memory, and package-size measurements.

**Decision:** Pending prerequisite.

## Evidence journal

### 2026-08-01 — Evaluation opened

- Created separate scorecards and shared quantitative gates.
- Selected `cssparser` syntax/declaration parsing as the first experiment because it can
  reuse the existing Rust build and coarse ABI without changing WebScene's cascade.
- Deferred the V8 snapshot until a generated binding slice reaches behavioral parity.
- Made selector parsing an independent conditional decision.
