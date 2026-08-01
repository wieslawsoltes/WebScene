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

### 2026-08-01 — cssparser isolated IR prototype

- Working tree based on decision-log commit `0b18a34`; V8 remains pinned at 15.3.10,
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

- Built matched macOS arm64 Release/certification variants against V8 15.3.10, pointer
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

- Prototype base: `0fa1d52`; V8 15.3.10, macOS arm64, AppleClang 21, Release with
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
  `PropertyCallbackInfo` in V8 15.3 exposes `Holder()` but not the original receiver.
  Instance accessors therefore retain current semantics and brand guards, while generated
  method templates can use `Signature` for correct receiver enforcement.
- Decision: rule in the generated operation/prototype catalog for compatibility, retain the
  A/B switch during ecosystem validation, and treat prototype attribute descriptors as a
  separately gated semantic-callback refactor rather than hiding duplicate behavior in the
  generator.
