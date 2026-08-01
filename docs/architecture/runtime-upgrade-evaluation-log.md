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
- Release configuration: V8 15.3.10 with pointer compression and shared cage. The local
  artifact inherited by the experiments was subsequently verified at runtime as
  `14.7.173.23-HtmlML`; results below now distinguish that stale artifact from the release
  target instead of inferring the engine version from build configuration.
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

**State:** Ruled in for compatibility; adoption cleanup remains before making it default

**Prototype boundary:** Replace CSS tokenization, block/function handling, declaration
splitting, and error recovery. Emit a compact rule/declaration IR in one coarse call.
WebScene remains authoritative for selectors initially, cascade, invalidation, computed
values, property semantics, layout, and paint.

**Required evidence:**

- Lines of handwritten tokenizer/parser code deleted.
- Required-profile result and targeted upstream CSS Syntax denominator.
- Stylesheet, inline declaration, repeated CSSOM mutation, TradingView, and Monaco timing.
- Peak temporary parser memory, process RSS, retained plateau, and package-size delta.

**Decision:** **Rule in.** The compatibility gate is met and representative non-regression
limits hold. The maintained-code gate is not met. Before the prototype becomes the only
implementation, replace the owned Rust-plus-C++ copy with a borrowed or streaming ABI where
practical, delete the legacy splitter/walker, and rerun the same evidence on packaged builds.

**Current findings:**

- The directly replaceable handwritten syntax surface is only about 205 lines:
  `matching_css_brace`, `parse_css_declarations`, `parse_css_rules`, and the
  comment-removal loop in `add_stylesheet`. Selector-list splitting remains outside this
  candidate. This is below the 500-line maintained-code gate by itself.
- The isolated prototype pins `cssparser` 0.37.0 in the same Rust static library as
  `html5ever`; it makes one parse call, exposes a flat owned rule/declaration IR, and
  immediately frees Rust storage after the C++ copy.
- Syntax fixtures pass for nested blocks and functions, delimiters in quoted strings and
  URLs, comments containing delimiters, nested media/supports rules, font descriptors,
  keyframes, ASCII-insensitive ordinary property names and `!important`, case-sensitive
  custom properties, malformed-declaration recovery, and invalid UTF-8 rejection.
- On macOS arm64 with AppleClang 21, Rust Release (`opt-level=3`, LTO), five warmups and
  30 warm samples, the prototype parsed the generated 50 KiB stylesheet at 0.406 ms p50
  (120 MiB/s) and the 1 MiB stylesheet at 4.210 ms p50 (238 MiB/s).
- The owned 1 MiB IR peaked at 4,120,235 Rust bytes and retained 2,423,899 bytes until
  copied. This is a warning, not yet a process-memory regression result: a streaming or
  borrowed final ABI could remove most of that temporary ownership if live measurements
  justify adoption.
- The crate is now Cargo-feature-gated, so a legacy CSS build does not link it. Matched
  linked engines measured 58,463,056 bytes (legacy) and 58,550,160 bytes (`cssparser`):
  +87,104 bytes, or +0.149%.
- The focused CSS Syntax projection improves from 3/11 passing assertions under the legacy
  splitter to 11/11 with `cssparser`. It covers strings, functions, data URLs, nested
  component values, comments, ASCII-insensitive names, `!important`, malformed recovery,
  dynamic STYLE reparsing, and a 2,000-rule final-rule recovery check.
- The matched required profile is exactly neutral: both builds pass 104/110 documents and
  431/433 subtests with the same five failures and one timeout.
- Five process samples of the 2,000-rule contract measured median peak RSS of 184,958,976
  bytes (legacy) and 186,531,840 bytes (`cssparser`), a +0.85% delta. This is below the 10%
  non-regression limit; it is not a memory benefit.
- The 107,644-byte repeated STYLE mutation has a 1.868 ms legacy warm median versus
  4.191 ms for the owned-IR prototype. The +124% parser-stress cost is an explicitly
  accepted compatibility tradeoff: the legacy run is faster because it misparses the
  delimiter-bearing declarations. Required-profile wall time was neutral (51.23 versus
  50.75 seconds), and the absolute stress delta is 2.32 ms per 108 KiB reparse.
- TradingView and Monaco headless proofs both pass. Single matched runs showed no RSS or CPU
  regression: TradingView was 410.6 versus 408.9 MB peak RSS and 9.31 versus 7.54 user CPU
  seconds; Monaco was 282.5 versus 258.4 MB and 9.42 versus 7.98 user CPU seconds. Because
  the `cssparser` run was second and network/build caches differ, these are non-regression
  observations, not claimed benefits.
- The prototype adds substantially more maintained adapter/IR code than the roughly 205
  handwritten lines it can delete, so it is adopted for standards compatibility, not code
  reduction. Selector parsing remains a separate decision.

### 2. Generated WebIDL-to-V8 bindings

**State:** Ruled in for method/prototype compatibility; accessor migration remains
conditional before making it default

**Prototype boundary:** Generate the `EventTarget` -> `Node` -> `Element` ->
`HTMLElement` interface family from pinned WebIDL input plus an explicit WebScene
exposure manifest. Keep native method behavior handwritten.

**Required evidence:**

- Net handwritten binding-code reduction, excluding generated output.
- Property descriptors, prototype identity, brand checks, illegal invocation,
  conversions, exceptions, and wrapper identity against Chrome and the required profile.
- Context-creation time, binary size, and RSS before considering snapshots.

**Decision:** **Rule in the generated catalog and interface-template chain for operations,
constructors, brands, and wrapper selection.** It meets the compatibility gate with exact
required-profile parity and moves the focused browser-shaped denominator from 2/10 to
9/10. It does not meet the code-reduction, memory, or performance benefit gates. Keep the
legacy A/B switch until the generated path has packaged ecosystem coverage. Do not claim
full WebIDL parity: V8 15.3 native property callbacks cannot expose the original receiver
when an accessor is installed on a prototype, so attributes remain generated instance
accessors and fail browser descriptor placement. Migrating attributes requires shared
semantic getter/setter functions callable from V8 `FunctionCallback` accessors; duplicating
76 implementations merely to move descriptors is rejected.

**Current findings:**

- The prototype pins build-time-only `@webref/idl` 3.82.1 (MIT) and `webidl2` 24.5.0
  (W3C), validates an explicit 123-member exposure manifest against the standards corpus,
  and commits reproducible generated C++. Normal builds have no Node, npm, network, or new
  runtime dependency; `npm run check --prefix tools/webidl-v8-bindings` detects drift.
- The legacy element catalog is 271 handwritten installation lines and duplicates about
  50 constructor/alias lines across top-level and frame contexts. The new 192-line exposure
  manifest plus 227-line generator does not yield a 500-line net maintained-code reduction
  for this slice. The 2,194-line generated output is excluded from the handwritten count.
- The generated V8 chain creates distinct `EventTarget`, `Node`, `Element`, and
  `HTMLElement` templates, selects Node wrappers for text/fragments, Element for SVG, and
  HTMLElement for HTML, installs operations on the declaring prototype with WebIDL
  `.length`, V8 receiver signatures, illegal constructors, and per-object standalone
  EventTarget listener identity. Wrapper identity remains native and stable.
- Chrome passes 10/10 focused assertions. The matched legacy engine passes 2/10; the
  generated engine passes 9/10. The generated path fixes constructor/prototype identity,
  HTML/text/SVG brands, operation placement and arity, illegal invocation, illegal
  constructors, standalone EventTarget isolation, and wrapper identity. Both WebScene
  variants fail only the focused prototype-accessor descriptor assertion in the generated
  path; legacy has seven additional failures.
- The final required profile is exactly neutral at 104/110 documents and 431/433 subtests,
  with the same five failures and hover timeout as the cssparser control. An early catalog
  revision exposed a real `DocumentFragment.append` regression (103/110, 430/433); moving
  the already-supported ParentNode operations to the shared Node layer restored exact
  parity before the decision.
- Matched Release/certification binaries are 52,399,408 bytes (legacy) and 52,460,048 bytes
  (generated): +60,640 bytes, or +0.116%.
- Across five processes, each with five warmups and 30 samples, the median ready-runtime
  lifecycle p50 is 0.866 ms legacy versus 0.891 ms generated for independent isolates
  (+2.9%), and 0.364 versus 0.379 ms with a shared isolate (+4.1%). Median peak RSS is
  33,112,064 versus 33,243,136 bytes (+0.4%) isolated and 43,057,152 versus 42,811,392
  bytes (-0.6%) shared. These are neutral non-regressions, not benefits.

### 3. V8 embedder startup snapshot

**State:** Ruled in for startup/context-creation performance; packaged rollout remains

**Prototype boundary:** Snapshot immutable bootstrap code first. Keep the generated
prototype/template graph out of the initial snapshot because its native callbacks would
require a stable external-reference table; attach all document/context-specific native
state after restore.

**Required evidence:**

- First and subsequent context-creation latency.
- Process cold start, snapshot generation/load time, RSS, and package-size delta.
- Strict mismatch rejection keyed by V8 revision, ABI, architecture, configuration, and
  binding-catalog hash.

**Decision:** **Rule in for performance.** Against the configured V8 15.3.10 release build,
the prototype clears the 20% context-creation gate for independent isolates and the 10%
steady-lifecycle gate for a shared isolate, while preserving exact required-profile parity.
Keep the option off by default until every packaged RID includes and verifies its matching
snapshot sidecar. This is not a memory or code-reduction win.

**Current findings:**

- The opt-in `WEBSCENE_NATIVE_ENGINE_V8_SNAPSHOT=bootstrap` build extracts and snapshots
  36,064 bytes of immutable WebSocket, editor-platform, fetch, and IntersectionObserver
  JavaScript. DOM wrappers, native callbacks, context embedder slots, and the differing
  top-level/frame Blob and MessageChannel programs remain context-specific.
- With V8 15.3.10-WebScene, the build-time snapshot is 403,016 bytes. The snapshot engine
  library is 16,368 bytes smaller because the four raw programs are dead-stripped, making
  the complete shipped delta +386,967 bytes (+0.660%). The builder is not shipped.
- The metadata fingerprint covers the complete V8 version header, target CPU, pointer
  compression/shared cage/direct-handle/size flags, bootstrap SHA-256, and generated
  binding-catalog SHA-256. Missing or unequal metadata is rejected before isolate creation;
  the missing-sidecar probe exits cleanly with an engine evaluation failure.
- On macOS arm64, AppleClang 21, V8 15.3.10-WebScene, Release/certification, generated
  bindings, html5ever and cssparser, five processes with five warmups and 30 samples
  measured independent-isolate lifecycle p50 at 1.036 ms control versus 0.755 ms snapshot
  (-27.1%). Shared-isolate p50 was 0.397 versus 0.351 ms (-11.6%).
- Twenty cold processes measured 26.702 versus 25.671 ms median wall time (-3.9%) and
  3.080 versus 2.214 ms first measured context (-28.1%). Snapshot generation took 31.5 ms
  median, excluding its one-time first-process outlier.
- Median RSS changed by -1.1% for repeated isolated lifecycles, -2.8% for shared-isolate
  lifecycles, and -1.4% for a cold process. These are safe non-regressions, not a memory
  benefit.
- The complete required profile is exactly neutral at 104/110 documents and 431/433
  subtests with the same five failures and hover timeout. Both parser suites pass; the
  native suite reaches the known SVG `currentColor` fixture failure.
- Raw release evidence is in
  `artifacts/snapshot-evaluation/benchmark-results-v8-15.3.10.json`,
  `artifacts/snapshot-evaluation/required-15.3-control/results.json`, and
  `artifacts/snapshot-evaluation/required-15.3-snapshot-awake/results.json`. The earlier
  14.7 cross-check remains in `benchmark-results-final.json`.

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

### 2026-08-01 — cssparser isolated IR prototype

- Working tree based on decision-log commit `0b18a34`; the release configuration targets
  V8 15.3.10, while the local artifact used here was later identified as 14.7.173.23,
  although the isolated parser test and benchmark deliberately do not link V8.
- Build: macOS arm64, AppleClang 21 C++20 Release and Rust 1.90.0 release with LTO.
- Fixtures: generated component stylesheet at 1 KiB, 50 KiB, and 1 MiB plus focused
  malformed and nested-syntax cases. Cache state: five in-process warmups followed by 30
  warm samples.
- Raw build and executables: `artifacts/build-cssparser-spike`.
- Results: parser tests pass; 1 KiB 0.0116 ms p50, 50 KiB 0.406 ms p50, 1 MiB
  4.210 ms p50. The 1 MiB output contains 8,066 flat rules and 16,132 declarations.
- Next gate: wire the same IR behind a build switch, run required-profile parity and
  representative live workloads, and measure process RSS including the temporary C++
  copy. Do not adopt the owned IR solely from these microbenchmark results.

### 2026-08-01 — cssparser live A/B decision

- Built matched macOS arm64 Release/certification variants against the locally cached V8
  14.7.173.23 artifact, pointer
  compression, and the shared cage. The only intended variant was
  `WEBSCENE_NATIVE_ENGINE_CSS_PARSER=legacy|cssparser`; the Cargo feature gate prevents
  the control from linking `cssparser`.
- Required profile: exact parity at 104/110 documents and 431/433 subtests. Raw results:
  `artifacts/cssparser-evaluation/required-profile-legacy-control` and
  `artifacts/cssparser-evaluation/required-profile-cssparser-final`.
- CSS Syntax projection: legacy 3/11, `cssparser` 11/11. Raw results:
  `artifacts/cssparser-evaluation/syntax-final-legacy` and
  `artifacts/cssparser-evaluation/syntax-final-cssparser`.
- The first live run exposed an incorrect Rust `str::trim` use that treated vertical tab as
  CSS whitespace. Replacing it with the five CSS whitespace code points restored the
  existing 6/6 custom-property contract before the final required-profile run.
- Memory raw logs: `artifacts/cssparser-evaluation/memory`. Linked engines differ by
  87,104 bytes. The isolated 1 MiB IR still identifies the owned-copy boundary as the
  first optimization target.
- TradingView proofs and screenshots: `artifacts/cssparser-evaluation/tradingview`.
  Monaco initial/selected/edited/folded screenshots: `artifacts/cssparser-evaluation/monaco`.
  Both variants completed their behavioral gates.
- Decision: rule in for the demonstrated syntax compatibility gain. Do not bundle Servo
  selectors or another cascade/layout engine as part of this decision.

### 2026-08-01 — generated WebIDL-to-V8 binding slice

- Prototype base: `0fa1d52`; locally cached V8 14.7.173.23, macOS arm64, AppleClang 21,
  Release with
  certification, html5ever, and cssparser. Build switch:
  `WEBSCENE_NATIVE_ENGINE_DOM_BINDINGS=legacy|generated`.
- Inputs and generator: `tools/webidl-v8-bindings`; generated output:
  `experiments/WebScene.NativeEngine.Probe/native/generated/webscene_dom_bindings.inc`.
  The pinned WebRef corpus is generation-time validation, not shipped code.
- Focused raw results: `artifacts/webidl-evaluation/contract-legacy-eventtarget`,
  `artifacts/webidl-evaluation/contract-generated-eventtarget`, and
  `artifacts/webidl-evaluation/contract-chrome-final.html`.
- Required-profile raw result: `artifacts/webidl-evaluation/required-generated-eventtarget`;
  matched control: `artifacts/cssparser-evaluation/required-profile-cssparser-final`.
- Timing/RSS raw samples: `artifacts/webidl-evaluation/benchmarks`. Fixture: create engine,
  wait for a successful V8 evaluation, release the result, and destroy the engine; five
  process samples per variant/mode, five warmups and 30 recorded lifecycles per process.
- The V8 accessor-receiver constraint was found by compiling the prototype, not inferred:
  `PropertyCallbackInfo` in the evaluated V8 14.7 exposes `Holder()` but not the original
  receiver. This constraint must be rechecked against the 15.3 release headers.
  Instance accessors therefore retain current semantics and brand guards, while generated
  method templates can use `Signature` for correct receiver enforcement.
- Decision: rule in the generated operation/prototype catalog for compatibility, retain the
  A/B switch during ecosystem validation, and treat prototype attribute descriptors as a
  separately gated semantic-callback refactor rather than hiding duplicate behavior in the
  generator.

### 2026-08-01 — V8 bootstrap snapshot prototype

- Prototype base: `324e38d`; matched macOS arm64 Release/certification builds with
  generated bindings, html5ever, cssparser, pointer compression, and shared cage.
- Runtime version inspection corrected the evaluation artifact from the assumed 15.3.10
  to `14.7.173.23-HtmlML`. Repository release scripts and package verification target
  15.3.10, so this checkpoint is provisional rather than silently relabeling the data.
- The default-context snapshot contains four immutable bootstrap programs and accepts the
  existing named-window global template when each context is restored. Native callback
  functions and document/frame state are attached afterward; no external-reference table
  is required for this boundary.
- Raw A/B results: `artifacts/snapshot-evaluation/benchmark-results-final.json`. Five warm
  processes per mode used five warmups and 30 samples; cold results used 20 fresh
  processes. Independent-isolate lifecycle improves 30.2%, but shared-isolate lifecycle,
  first context, process cold start, and RSS do not meet material-benefit gates.
- Required-profile result: `artifacts/snapshot-evaluation/required-snapshot/results.json`,
  exactly matching the generated-binding control at 104/110 documents and 431/433
  subtests.
- Decision: provisional rule-in only for independent-isolate churn. Keep the option off by
  default and reproduce on the V8 15.3.10 release artifact before adoption.

### 2026-08-01 — V8 15.3 release snapshot validation

- Built a clean macOS arm64 V8 15.3.10-WebScene monolith with the release configuration
  (pointer compression, shared cage, static monolith, no external startup data) and made
  matched control/snapshot runtime builds from commit `03460aa`.
- Raw A/B results: `artifacts/snapshot-evaluation/benchmark-results-v8-15.3.10.json`.
  Five processes per mode used five warmups and 30 samples; cold results used 20 fresh
  processes. Independent-isolate lifecycle improves 27.1%, shared-isolate lifecycle 11.6%,
  and first measured context 28.1%. Cold process wall time improves 3.9%.
- RSS improves only 1.1% to 2.8%, below the memory-benefit gate. The complete shipped
  snapshot sidecar and metadata add 0.660% after dead-stripping the raw bootstrap programs.
- Matched required-profile runs are exactly neutral at 104/110 documents and 431/433
  subtests, with the same five failures and hover timeout. A first snapshot run crossed a
  macOS host-sleep interval; the clean awake rerun completed in 47.45 seconds versus
  52.35 seconds for control and is used only as a compatibility check, not a speed claim.
- Rechecking V8 15.3 headers confirms that `PropertyCallbackInfo` still exposes only
  `Holder()` to embedders despite documentation mentioning `info.This()`. The generated
  WebIDL attribute-accessor constraint therefore remains unchanged.
- Decision: rule in for startup/context-creation performance. Retain the build switch until
  snapshot sidecar packaging and fingerprint validation cover all supported RIDs.
