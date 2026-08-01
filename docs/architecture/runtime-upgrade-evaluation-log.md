# Runtime upgrade evaluation log

**Status:** Evaluation complete; staged rollout work remains

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
5. Lexbor CSS as a C alternative to Servo `cssparser`, using the same WebScene IR boundary.

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

**State:** Ruled in for selector-syntax compatibility; rollout remains conditional

**Prototype boundary:** Accept only if the external parser can emit WebScene's existing
compiled selector IR. Do not import a second cascade, invalidation system, style tree, or
layout engine.

**Required evidence:**

- Targeted Selectors WPT improvement and required-profile parity.
- Net deletion of selector parsing/compilation code.
- Selector compilation, matching, invalidation, memory, and package-size measurements.

**Decision:** **Rule in the syntax parser and its specificity calculation for
compatibility.** The prototype clears the compatibility gate, preserves exact required-
profile behavior, and emits WebScene's existing flat compiled-selector IR without adding
another matcher, cascade, invalidation system, style tree, layout engine, or renderer. It
does not clear the maintained-code, memory, or performance gates. Keep the A/B switch
until packaged ecosystem coverage is broader and optimize the owned Rust-to-C++ result
before making it the sole path. The 24.7% parser-heavy stress regression is an explicitly
accepted compatibility tradeoff; it is 7.129 ms across 8,000 selector compilations and is
not visible in the matched required profile or Monaco proof.

**Current findings:**

- The prototype pins Servo `selectors` 0.39.0 (MPL-2.0) beside the existing `cssparser`
  0.37.0 dependency. It parses a complete selector list in one Rust call and returns
  serialized selectors, compounds, combinators, and packed specificity through a flat
  versioned C ABI. Stylesheet parsing reuses that result instead of parsing twice.
- The focused standards contract improves from 1/10 assertions under the legacy parser to
  10/10; unchanged Chrome 151 also passes 10/10. It covers complete-list invalidation,
  CSS comments, `:is()`, `:not()`, and
  `:where()` specificity, malformed combinators and attribute operators, and selector
  tokenization used by `querySelector`.
- Two pinned upstream specificity tests improve from 1/9 to 8/9 subtests. In particular,
  `not-specificity.html` improves from 1/8 to 8/8. Both variants still fail the complex
  sibling-sensitive `:is()` fixture because the existing matcher/DOM sibling semantics do
  not yet implement it; replacing syntax parsing cannot repair that separate layer.
- The broader existing selector profile is exactly neutral at 13/14 documents and 126/126
  subtests, with the same hover timeout. The required profile is also exactly neutral at
  104/110 documents and 431/433 subtests, with the same five failures and one timeout.
- On macOS arm64, AppleClang 21, V8 15.3.10-WebScene, Release/certification, html5ever,
  cssparser, generated bindings, and no snapshot, five processes with five warmups and 20
  samples measured 28.886 ms legacy versus 36.015 ms Servo median p50 for four STYLE
  reparses containing 2,000 selectors each: +24.7%, or +7.129 ms for 8,000 selectors.
- Median peak RSS in that stress is 52,854,784 versus 52,871,168 bytes (+0.031%), so the
  result is memory-neutral. Linked libraries are 58,610,832 versus 58,710,176 bytes:
  +99,344 bytes, or +0.1695%.
- Matched Monaco headless proofs produced all four expected captures. Control versus Servo
  measured 16.32 versus 16.19 seconds wall, 5.46 versus 5.50 user CPU seconds, and 255.8
  versus 256.2 MB peak RSS in one run. This is a non-regression observation, not a benefit;
  initial and folded captures are byte-identical and visual inspection of the remaining
  captures shows the same editor state.
- The candidate cannot delete 500 maintained lines. The replaceable legacy validation,
  list splitting, specificity, and compilation surface is roughly 500 lines at most, while
  the new Rust adapter and C++ ABI/wrapper are larger. Adopt it only for compatibility,
  then reduce copying and delete the fallback after packaged rollout evidence is complete.

### 5. Lexbor CSS

**State:** Ruled out in favor of Servo `cssparser`

**Prototype boundary:** Parse the same stylesheet/declaration inputs and project into the
same flat WebScene rule/declaration IR. Keep WebScene's selector compiler, cascade,
invalidation, layout, and renderer unchanged.

**Decision:** **Rule out Lexbor's high-level CSS stylesheet API for this boundary.** It
does not clear a code, compatibility, memory, or performance gate against the already
selected Servo parser. Do not retain its runtime switch or its 443-line adapter. The
useful lesson is its callback/direct-consumer ownership model: apply that shape to the
Servo wrapper instead of changing standards parsers.

**Current findings:**

- The isolated prototype used upstream Lexbor v3.0.0 at commit
  `2ae88a1c6b5261830eff73ee12bb3cdf805f3cfe` (Apache-2.0), macOS arm64,
  AppleClang 21, Release. Only the separately built `core` and `css` static modules were
  linked. The evaluated Lexbor CSS/core source surface is about 42,461 C lines.
- Lexbor does not natively understand all rule-container at-rules at its typed stylesheet
  layer. In particular, generic `@supports` block parsing misclassified
  `button:focus-visible { ... }` as a declaration. Recovering parity required adapter code
  to locate raw blocks and invoke Lexbor's rule-list parser again for `supports`, `layer`,
  `container`, and keyframes. Lexbor also did not accept whitespace between `!` and
  case-insensitive `important` without adapter normalization.
- Even with that glue, the focused CSS Syntax contract is 10/11 versus Servo's 11/11.
  Lexbor consumes the valid `height: 19px` following a malformed declaration instead of
  recovering at the next semicolon. The broader required profile is neutral at 104/110
  documents and 431/433 subtests, so the focused denominator exposes a real regression
  hidden by the broad suite.
- The matched end-to-end microbenchmark includes parser work and projection into C++ IR,
  with five processes, five warmups, and 30 samples. Median p50 is 0.0107 versus 0.00538 ms
  at 1 KiB (+99%), 0.408 versus 0.238 ms at 50 KiB (+71%), and 8.495 versus 4.742 ms at
  1 MiB (+79%) for Lexbor versus Servo. The malformed fixture is 0.00263 versus
  0.000875 ms (+200%).
- Lexbor's measured stylesheet pools reserve 192,528 bytes for the 1 KiB fixture and
  8,262,264 bytes for 1 MiB, versus Servo's owned IR retaining 2,452 and 2,423,899 bytes.
  This is about 79x and 3.4x respectively. The pool figure is reserved parser capacity,
  while the Servo figure is allocator-observed live ownership, so process RSS is the
  decisive cross-allocator check.
- Five focused runtime processes measured median peak RSS of 188,366,848 bytes for Lexbor
  versus 186,515,456 bytes for Servo (+0.99%). Warm wall time was 0.27 versus 0.25 seconds
  and user CPU 0.22 versus 0.20 seconds. The linked runtime grew from 58,610,832 to
  58,842,608 bytes (+231,776 bytes, +0.395%). These are bounded, but none is a benefit.
- The prototype adapter reached 443 lines versus 108 for the current Servo C++ wrapper and
  still needed WebScene-specific error recovery and at-rule grammar policy. Retaining it
  would increase maintained code and introduce a second dependency without broadening
  compatibility.

## Final recommendation order

1. Ship the V8 bootstrap snapshot first after per-RID sidecar packaging is complete. It is
   the only candidate that clears a material performance gate.
2. Continue the adopted `html5ever` path. It is the broadest parser-compatibility and
   implementation-ownership improvement, but not a memory or performance win.
3. Promote Servo `cssparser` and selector parsing together as the standards parsing
   module after replacing their owned-result ABI with a callback/streaming adapter. Try an
   in-repository wrapper first; `cssparser` itself need not be forked unless its public
   parser traits prove insufficient. Both are compatibility investments;
   neither reduces memory, runtime, or current maintained source by itself.
4. Expand generated WebIDL bindings incrementally for operations, prototype inheritance,
   brands, constructors, and wrapper selection. Refactor semantic attribute callbacks
   before moving accessors to prototypes; do not generate duplicate method bodies.
5. Treat significant memory reduction as unsolved. None of these candidates reaches the
   15% peak-RSS or 25% retained-plateau gate, so memory work needs a separate ownership,
   compact-node, interning, or V8 external-memory investigation.
6. Do not adopt Lexbor CSS as a parallel or replacement parser. Its only attractive idea
   here is direct result consumption, which should be applied to the selected Servo path.

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

### 2026-08-01 — Servo selector parser decision

- Prototype base: `1564d75`; matched macOS arm64 Release/certification builds against V8
  15.3.10-WebScene with html5ever, cssparser, generated bindings, pointer compression and
  shared cage, and no startup snapshot. The only intended variant is
  `WEBSCENE_NATIVE_ENGINE_SELECTOR_PARSER=legacy|servo`.
- The adapter consumes Servo's syntax tree once per complete selector list and projects it
  into the existing compound/combinator/specificity representation. A first adapter parsed
  each stylesheet selector twice; reusing the parsed IR reduced its one-shot stress result
  from about 52 ms to the final 36 ms before comparative measurements were recorded.
- Focused contract: 1/10 legacy versus 10/10 Servo. Pinned upstream specificity WPT: 1/9
  versus 8/9. Chrome 151 independently passes the unchanged focused contract at 10/10.
  Existing selector profile and the complete required profile remain exactly neutral.
  The final rebuilt-binary parity runs are in `final-required-control` and
  `final-required-servo`; focused and discovery results are under
  `artifacts/selector-evaluation/contract-*`, `wpt-specificity-*`, and `selectors-*`.
- The matched benchmark used five processes per variant, five in-process warmups and 20
  samples, with four reparses of a 2,000-selector STYLE element, 128 DOM subjects, and a
  comment-tokenized `querySelector`. Servo is 24.7% slower, process RSS is +0.031%, and the
  library is +0.1695%. Raw data and build metadata are in
  `artifacts/selector-evaluation/benchmark-results.json`.
- Matched Monaco runs complete with the same visible editor state. Raw timing and captures
  are in `artifacts/selector-evaluation/monaco-control.time`, `monaco-servo.time`,
  `monaco-control-matched`, and `monaco-servo`.
- Decision: rule in for broad selector-syntax and specificity compatibility, accepting the
  bounded parser-heavy regression. Do not claim code, memory, or performance benefits and
  do not expand the boundary into Servo matching, cascade, invalidation, or layout.

### 2026-08-01 — Lexbor CSS comparison

- Cloned Lexbor v3.0.0 as an uncommitted neighboring source checkout and built only its
  `core` and `css` static targets. The rejected adapter and runtime switch were removed
  after measurement so they do not become a second maintained implementation.
- Raw ignored evidence is under `artifacts/lexbor-css-evaluation`: five-process parser
  microbenchmarks, five focused process RSS samples, the 10/11 CSS Syntax result, the
  104/110 required-profile result, and the full Lexbor runtime build.
- The benchmark boundary was corrected for both variants to include C++ IR materialization;
  previous Servo microbenchmarks stopped after the Rust FFI call. Allocation metric names
  are now backend-neutral in preparation for the streaming ABI experiment.
- Decision: rule out Lexbor. It is slower and more memory-hungry on the matched parser
  fixtures, regresses malformed-declaration recovery, increases binary and adapter size,
  and offers no compatibility improvement over Servo. Next evaluate a streaming Servo
  adapter that emits directly into WebScene-owned vectors.
