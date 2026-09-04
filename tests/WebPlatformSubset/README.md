# WebScene component compatibility profile

This directory contains WebScene's native-only compatibility profile. It is a bounded
set of browser contracts for trusted, packaged UI components—not a claim of full Web
Platform Test conformance.

For the release-note styling causes, required contracts, native font-cache tests,
and variable-font weight qualification gates, see
[release notes coverage](RELEASE_NOTES_COVERAGE.md).

The profile manifest is `webscene-component-profile.json`. Every entry has one state:

- `required` — reviewed release-gating behavior;
- `candidate` — useful discovery coverage not yet promised;
- `harnessBlocked` — relevant behavior the runner cannot currently execute; or
- `excluded` — behavior intentionally outside the component runtime.

The pinned upstream revision and file hashes make runs reproducible. Local `contracts/`
cover component behaviors that are not conveniently represented by an unchanged
upstream test. Upstream files remain attributable to WPT and are not silently rewritten
into project-owned tests.

## Policy

Keep the required set small enough to be a credible product promise. Expand candidate
and exploratory WPT coverage aggressively to discover gaps, clusters, and regressions.
Promote a test to required only when:

1. the behavior belongs in the published component profile;
2. the test is deterministic in the native runner;
3. the implementation passes on every released RID;
4. failures are diagnosable without a second engine; and
5. the capability and evidence metadata are updated.

Do not import whole WPT directories into the required set. A large pass percentage over
easy tests is less useful than reviewed coverage of the DOM/CSS/input/rendering behavior
real components depend on.

## Runner

The runner has one adapter: the native engine. There is no `--engine` option and no
fallback.

List the manifest without loading a native library:

```bash
dotnet run --project tests/WebPlatformSubset/runner -c Release -- \
  --selection all --list
```

Run required or candidate tests:

```bash
dotnet run --project tests/WebPlatformSubset/runner -c Release -- \
  --selection required \
  --native-library /absolute/path/to/libwebscene_native_engine.dylib \
  --output TestResults/WebPlatformSubset/required

dotnet run --project tests/WebPlatformSubset/runner -c Release -- \
  --selection candidate \
  --native-library /absolute/path/to/libwebscene_native_engine.dylib \
  --output TestResults/WebPlatformSubset/candidate
```

Use `--test <substring>` for a focused document,
`--timeout-seconds <seconds>` to alter the per-document timeout, and
`--native-cache-directory <path>` to isolate V8 cache evidence.

Static reftests and self-verifying visual tests can also collect an independent
Chromium differential or color-oracle result:

```bash
dotnet run --project tests/WebPlatformSubset/runner -c Release -- \
  --selection candidate \
  --native-library /absolute/path/to/libwebscene_native_engine.dylib \
  --chromium-path /absolute/path/to/chrome-or-chromium \
  --output TestResults/WebPlatformSubset/chromium-oracle
```

The ordinary WPT result remains the exact native-test versus native-reference
comparison. The optional Chromium lane separately renders the test and reference,
records whether Chromium considers them identical, retains all four screenshots, and
reports exact native-to-Chromium pixel-difference metrics. Cross-engine differences are
diagnostic rather than gating because font rasterization, platform defaults, and the
presenter stack need not be pixel-identical. A Chromium test/reference mismatch is a
warning that the selected reftest or local browser environment is not a trustworthy
oracle.

Result JSON records the profile, a line-ending-normalized SHA-256 of the exact manifest,
the pinned WPT revision, native ABI/library hash, selection, document status, assertions,
diagnostics, and artifacts. The cross-RID verifier requires every artifact to carry the
hash of the checked-in manifest, so changes to test type, bounds, or metadata cannot be
mistaken for evidence from the same profile. A required failure produces a non-zero exit
code.

## Test types

- `testharness` runs the pinned WPT harness and records its assertions. HTML/XHTML
  documents execute directly; window-targeted `.any.js` sources are wrapped in a
  generated document without rewriting their pinned JavaScript bytes.
- `reftest` settles and captures the native scene through the deterministic renderer,
  then compares document and reference pixels. With `--chromium-path`, it also records
  the independent Chromium self-comparison and cross-engine differential described
  above.
- `visual` runs an unchanged self-verifying WPT against bounded, manifest-authored exact
  color-count plus optional spatial color-gap, connected-component shape, and anchor-
  relative foreground-offset or component-relative exact-color region checks,
  always retains the native screenshot, and applies the same checks independently in
  Chromium when `--chromium-path` is supplied. Every visual entry must include both a
  failure-color bound and a non-blank success condition appropriate to the upstream
  test's authored pass statement.
- `contract` runs a project-owned reduced behavior contract through the same native
  document and JavaScript boundary.

Input tests enqueue pointer, wheel, keyboard, focus, and resize actions through the
native input ABI. They must not call presenter internals directly.

## Retina canvas export regression

`contracts/resize-observer-device-pixel-canvas-export.html` is a project-owned
WPT-style reduction, not an imported upstream WPT. It covers initial and resized
constrained canvas exports plus same-origin iframe measurements. The pane and time
axis take their backing sizes from `devicePixelContentBoxSize`; a detached price
axis uses `devicePixelRatio`. Assertions reject mixed-scale layers, price-axis
spill into the footer, and incorrect export dimensions. The DOM DPR is checked
against the runner's requested scale so a nominal 2x run cannot silently test 1x.

The ordinary component profile includes the 1x control as a candidate. Run the
paired 2x candidate with:

```bash
dotnet run --project tests/WebPlatformSubset/runner -c Release -- \
  --manifest tests/WebPlatformSubset/webscene-retina-canvas-export-profile.json \
  --selection candidate --native-library /absolute/path/to/libwebscene_native_engine.dylib \
  --output artifacts/retina-canvas-export/native-2x

CHROME_BIN=/absolute/path/to/chromium node tests/WebPlatformSubset/chrome/run-contracts.mjs \
  --path contracts/resize-observer-device-pixel-canvas-export.html \
  --device-scale-factor 2 --output artifacts/retina-canvas-export/chrome-2x
```

Repeat the Chrome command with `--device-scale-factor 1` and a fresh output path
for the control. The browser runner sets the physical process scale as well as
CDP viewport metrics: changing emulated DPR alone does not exercise the physical
ResizeObserver box in Chromium. The native adapter likewise sends the manifest
scale through resize input before any document script, rather than only scaling
the screenshot renderer.

The native executable has two focused filters:
`resize-observer-retina-export` and `resize-observer-device-scale`. The latter
drives real host scale transitions 1x → 2x → 1.5x → 1x without changing CSS size,
requiring new physical measurements without duplicate content-box notifications.
Both are registered in the full native suite.

Before the fix, native failed 9 of the original 13 assertions at 2x (including
a 511x611 export instead of 842x1082), and both focused native tests failed.
The fixed implementation supplies separate content, border, and rounded physical
sizes, tracks the selected observation box, and reaches the observation checkpoint
on outer-document resizes even without an iframe. The expanded contract also
covers padding/borders, option replacement, unobserve, invalid options, and
fractional untransformed sizes. Native and Chromium pass 21/21 at both scales;
the two focused native regressions and the full native suite pass. The real
constrained chart export was independently checked at 421x541 (1x) and 842x1082
(2x), without the earlier diagnostic sizing shim. Local evidence is under
`artifacts/resize-observer-export-fix` and
`artifacts/export-diagnosis/457-dpr{1,2}-native-fixed`.

Keep the web contract candidate until cross-RID validation is complete. Candidate
failures are reported in result JSON but do not set the runner's exit code;
the direct native regressions do exit nonzero.

## Go To tab lines and phantom scrolling (#1248)

`contracts/go-to-tabs-overflow.html` is a project-owned WPT-style reduction of
[the tester's follow-up](https://github.com/SandwichTradingDevOps/StackWich/issues/1248#issuecomment-5511713642).
The original responsive Go To test covered widths and grid tracks, but did not
include the production tab wrappers or calendar percentage-height chain.

Two independent causes are covered here:

- Stylesheet margin shorthands must keep nested `calc(max(var(...), ...)*-1)`
  expressions intact. Splitting on spaces changed the tab margins and produced
  separated underline layers.
- Percentage heights in ordinary auto-height parents use intrinsic sizing, not
  the parent's provisional height. The latter enlarged the calendar from 332px
  to 382px and created overflow. Definite percentage chains, grown column-flex
  items, and fixed portals must still provide definite percentage bases.

The direct native filter `go-to-overflow` reads the same fixture, checks repeated
302 → 431 → 302 width changes, verifies both underline paint layers coincide,
rejects painted phantom scrollbars, and ensures genuinely constrained content
can still scroll. It also runs in the full native suite. The web test remains a
candidate pending cross-RID verification.

```bash
dotnet run --project tests/WebPlatformSubset/runner -c Release -- \
  --selection all --test go-to-tabs-overflow \
  --native-library /absolute/path/to/libwebscene_native_engine.dylib \
  --output artifacts/go-to-native

CHROME_BIN=/absolute/path/to/chromium node tests/WebPlatformSubset/chrome/run-contracts.mjs \
  --path contracts/go-to-tabs-overflow.html --output artifacts/go-to-chrome

dotnet run --project samples/NativeTradingViewTerminal -- --headless-proof \
  --native-library /absolute/path/to/libwebscene_native_engine.dylib \
  --width 431 --height 900 --open-overlay go-to --output artifacts/go-to-live
```

Repeat the live proof at width 858 for the desktop layout. It invokes the actual
Go To application handler, then uses native pointer input to select Date → Custom
range → Date, recording screenshots, scroll geometry, and the loaded library's
path/SHA-256. It does not override the application's layout or hide scrollbars.

## Broad discovery

A broader WPT sweep should write separate non-gating artifacts and expectations. It is
for answering questions such as:

- which failure clusters are parser, selector, layout, CSSOM, event, or harness gaps;
- which upstream tests already pass unchanged;
- which harness capabilities block meaningful areas; and
- which behaviors should be promoted because real product workloads need them.

Discovery results must not be merged into the release pass percentage or advertised as
full standards support. The bounded required profile remains the public compatibility
contract.

## 2026-08-12 coverage audit

The pinned profile contains 115 required, 85 candidate, 0 harness-blocked, and 5
excluded documents. A full `osx-arm64` Inspector-flavor audit started at 41/52 candidate
documents and 222/299 candidate subtests. The first focused standards tranche moved
that lane to 47/52 documents and 239/301 subtests. The broadened lane now passes
79/79 documents and 415/415 subtests while the release gate remains 110/110 documents
and 434/434 subtests:

- complex `:is()` alternatives now match full selectors, CSS sibling combinators ignore
  intervening non-element nodes, and the managed selector parser uses the most specific
  functional-selector argument;
- a timer exception reaches an explicitly installed `window.onerror` callback with its
  Error and source location, then later tasks continue; the pre-existing unhandled-error
  failure path is covered independently and remains fail-closed;
- `replaceChild()` now splices `DocumentFragment` children without losing identity or
  order, while programmatic inline scripts execute synchronously exactly once and
  `innerHTML`-created scripts remain inert when moved;
- numeric and dictionary Window scrolling now operates over parsed two-axis document
  overflow, while disconnected elements expose no rendered client rectangles;
- inline unitless and percentage `line-height` values are resolved after the winning
  `font-size`, including later CSSOM font-size changes; and
- generated Web IDL attributes now appear as enumerable, configurable accessors on the
  declaring interface prototype while retaining native receiver-brand checks; and
- `Document.links` is now a stable, branded, live `HTMLCollection` with interleaved
  `A`/`AREA` tree order plus indexed, `item()`, `namedItem()`, ID, and name lookup; and
- the unchanged self-verifying rounded-overflow WPT now executes through a bounded
  red/green visual oracle in both native and Chromium, after removing a phantom inline
  line box from absolute auto-inset static positioning; and
- the unchanged visibility-layout WPT now requires hidden red paint suppression, visible
  orange/blue controls, and the bounded one-inch spatial gap left by the hidden box in
  both native and Chromium; and
- the unchanged list-style shorthand WPT now requires a compact filled square marker on
  a generic `display:list-item` box through independent native and Chromium connected-
  component shape checks; and
- the unchanged inside-marker position WPT now requires the marker and first text to
  share an inline line, treats `<br>` as a forced line break, and independently measures
  the continuation starting left beneath the marker in native and Chromium; and
- the unchanged elliptical-radius shorthand WPT now preserves independent horizontal
  and vertical corner radii through CSSOM, scene transport, Avalonia, and Uno, with
  component-relative edge checks that reject the former scalar projection in both
  native and Chromium; and
- three unchanged window-targeted EventTarget option WPTs now execute through the
  `.any.js` adapter and pass all 20 assertions for pre-invocation `once` removal,
  duplicate identity, passive cancellation, and synchronous AbortSignal cleanup;
  native element pointer and Window resize dispatch independently cover the same
  listener state;
- two unchanged EventListener-object WPTs pass all 7 assertions for function and
  object `this` binding, identity, removal, per-dispatch `handleEvent` lookup, late
  method definition, function precedence, and custom-element self-registration;
  native pointer and Window resize authority independently exercises object listeners;
  and
- synthetic outer-Window resize dispatch now uses the same listener registry as host
  resize input, while a neutral responsive-overlay reduction independently verifies
  compound substring selectors and non-fixed computed position after style removal;
  and
- legacy `Document.createEvent("Event")` now returns an identity-preserving `Event`
  whose `initEvent()` method initializes and resets dispatch flags; a neutral four-
  assertion reduction plus focused native coverage closes Bootstrap Carousel's exact
  gesture-simulator prerequisite without a framework-specific path; and
- bare-fragment anchor reflection, dynamic HTML `hidden` layout, queued/coalesced
  programmatic scroll events, percentage-height overflow ranges, and computed
  `position` lookup now pass 15/15 neutral assertions in Chrome and native; together
  they close all 40 unchanged Bootstrap ScrollSpy cases without a framework-specific
  engine branch; and
- an empty inline reference inside a tall positioned container now retains a zero-size
  rect instead of inheriting the container cross size; three Chrome-authorized neutral
  assertions close all 89 unchanged Bootstrap Tooltip cases without changing the
  existing flex-container path; and
- textarea child text now supplies `defaultValue` and the initial current value, a
  programmatic or user edit sets the dirty-value state, later child/default changes
  preserve dirty current state, and form reset restores the newline-normalized default;
  eight Chrome-authorized neutral assertions close all 4 unchanged jQuery serialization
  cases without a framework-specific path; and
- bounded descendant `:has()`, empty substring attribute selectors, browser-shaped
  document-root ancestry and containment, deep/shallow `Document.importNode()`, and
  disconnected position constants now pass 11/11 neutral Chrome/native assertions and
  close 62 unchanged browser-local jQuery traversal cases; and
- detached authored box styles, shorthand CSSOM values under an overriding important
  rule, resolved table-column geometry, automatic table shrink-to-fit, shared tracks,
  table-cell minimum height, and zero-content border-box controls now pass 6/6 neutral
  Chrome/native assertions and close 29 unchanged browser-local jQuery dimensions
  cases; and
- distinct `CharacterData`, `Text`, `Comment`, `ProcessingInstruction`, and
  `HTMLStyleElement` brands now back constructible text/comment nodes, processing
  instructions, flattened slot queries, and connected `ShadowRoot.styleSheets` identity;
- a real `Attr` wrapper surface, mutation entry points, detached `Document` construction,
  document cloning, initial iframe realms, and a bounded same-origin GET
  `XMLHttpRequest` prerequisite close the selected Custom Elements lifecycle shard;
- the HTML tree-construction contract now verifies foster-parented text immediately
  before its table instead of assuming it precedes unrelated earlier body content; and
- the pinned `check-layout-th.js` adapter now loads its pinned relative support sheet,
  and empty bordered non-stretch flex items retain their intrinsic cross size through
  dynamic `align-items` changes; and
- a Chrome-authorized generic library-primitives contract now requires real Text-node
  replacement, interface-bounded script/form/select members, ordered document lookups,
  detached-document identity, well-formed XML/CDATA behavior, and stable initial plus
  hydrated iframe realm identity.

The focused behavior is also covered by native runtime tests, managed selector tests,
and the generated-binding freshness check. Candidate promotion is intentionally deferred
until the same documents pass on every released RID.

A current-source `osx-arm64` comparison against the unchanged 110-document release-gate
commit (`4e7e4d3`) used eight fresh-process p50 results per variant in balanced
control/candidate/candidate/control and candidate/control/control/candidate orders.
Median p50 moved from 0.707 ms to 0.744 ms (+5.2%) for the 1,000-sample lifecycle
workload and from 34.620 ms to 34.317 ms (-0.9%) for the 30-sample light-DOM selector
workload. The 100-sample generated named-property workload was faster in every paired
regime, with a 29.4% median paired improvement after cold custom-element mutation
reactions became pay-for-use. All remain inside the established 10%
no-meaningful-regression envelope. A separate 20-process diagnostic
found no statistically supported startup or steady-work timing regression and bounded
the added generated interface-template footprint to 49,872 library bytes and 43,212 V8
used-heap bytes per populated view. Compile-time guards now keep the 64-bit cold-path
footprint fixed at a 976-byte `dom_node` and a 304-byte `native_document`.

The subsequent self-verifying visual-WPT/static-position slice was also compared with
its immediate clean parent (`f062650`) in alternating fresh processes. Median p50 moved
from 1.993 ms to 2.012 ms (+1.0%) for 1,000 lifecycle samples, from 34.400 ms to
34.814 ms (+1.2%) for the 50-sample selector workload, and from 3.292 ms to 3.178 ms
(-3.5%) for 100 generated named-property samples. A four-context/2,000-node memory
probe still reports the fixed 976-byte node and no new per-node state.

The visibility-layout/named-color slice was compared with its immediate clean parent
(`76dc8ab`) in six alternating fresh-process runs. Median p50 moved from 0.752 ms to
0.742 ms (-1.3%) for 1,000 lifecycle samples, from 33.049 ms to 33.110 ms (+0.2%)
for the 50-sample selector workload, and from 3.047 ms to 3.012 ms (-1.2%) for 50
generated named-property samples. The original named-color fast-path order is preserved,
and the slice adds no document or per-node state.

The generic list-marker/shape-oracle slice was compared with its immediate clean parent
(`ad13383`) in six balanced fresh-process runs. Median p50 moved from 0.736 ms to
0.738 ms (+0.3%) for 1,000 lifecycle samples, from 32.506 ms to 32.258 ms (-0.8%)
for the 50-sample selector workload, and from 2.965 ms to 2.983 ms (+0.6%) for 50
generated named-property samples. The runtime path is restricted to generic
`display:list-item` boxes and adds no document or per-node state.

The inside-marker/forced-break slice was compared with its immediate clean parent
(`0462020`) in six balanced fresh-process runs. Median p50 moved from 0.737 ms to
0.739 ms (+0.3%) for 1,000 lifecycle samples, from 32.422 ms to 32.254 ms (-0.5%)
for the 50-sample selector workload, and from 3.058 ms to 3.048 ms (-0.3%) for 50
generated named-property samples. The change adds no document or per-node state; the
ordinary inline fast path only gains a tag comparison, while `<br>` takes the existing
general inline-item path and advances one line offset.

The elliptical-radius slice was compared with its immediate clean parent (`5e03240`)
in six balanced fresh-process runs. Median p50 moved from 0.740 ms to 0.736 ms (-0.5%)
for 1,000 lifecycle samples, from 32.313 ms to 32.416 ms (+0.3%) for the 50-sample
selector workload, and from 2.955 ms to 2.972 ms (+0.6%) for 50 generated
named-property samples. A four-context/2,000-node scalar workload retains the fixed
976-byte node and identical attributed node, attribute, pool, wrapper, and scene
storage. Elliptical declarations alone allocate the existing cold textual-style state
and add one fixed-size companion command immediately before each affected rounded
primitive; ordinary circular commands keep the existing representation.

The EventTarget-options slice was compared with its exact clean parent (`d14139a`) in
six balanced fresh-process runs. Median p50 moved from 0.746 ms to 0.748 ms (+0.3%)
for 1,000 startup/lifecycle samples, from 33.481 ms to 33.401 ms (-0.2%) for the
50-sample selector workload, and from 3.068 ms to 3.095 ms (+0.9%) for 50 generated
named-property samples. A four-context/2,000-node workload that registers listeners
retains the fixed 976-byte node and identical attributed node, attribute, pool,
wrapper, and scene storage. Listener options exist only in cold registration records.
Both isolated and shared-isolate lifecycle probes preserve context identity, hidden
timers, release counts, and shared-slot reuse. Three balanced five-second idle runs
move median normalized CPU from 0.4563% to 0.4546%, with no signalled wakes, timers,
animation frames, or additional scene builds.

The EventListener-object slice was compared with its exact clean parent (`03e481c`)
in six balanced fresh-process runs. Median p50 moved from 0.744 ms to 0.745 ms
(+0.1%) for 1,000 startup/lifecycle samples, from 34.123 ms to 34.602 ms (+1.4%)
for the 50-sample selector workload, and from 3.244 ms to 3.258 ms (+0.4%) for 50
generated named-property samples. The listener-heavy four-context/2,000-node probe
retains the fixed 976-byte node and identical attributed node, attribute, pool,
wrapper, and scene storage. The callback persistent handle changes type but not size
inside the existing cold registration record. Both isolated and shared-isolate
lifecycle probes pass. Five balanced five-second idle runs record no signalled wakes,
timers, animation frames, or extra scene builds; median normalized CPU moves from
0.4785% to 0.5260% (+0.0474 percentage points) in the measurement-noise floor.

The unchanged upstream Bootstrap Modal slice and dynamic-transition fix were compared
with their exact clean parent (`ea01e97`) in six balanced fresh-process runs. Median
p50 moved from 0.767 ms to 0.750 ms (-2.2%) for 1,000 startup/lifecycle samples, from
32.107 ms to 31.933 ms (-0.5%) for the 50-sample selector workload, and from 2.866 ms
to 2.973 ms (+3.7%) for 50 generated named-property samples. A four-context/2,000-node
probe retains the fixed 976-byte node and identical attributed node, attribute, pool,
wrapper, and scene storage. Both isolated and shared-isolate lifecycle probes pass.
Five alternating five-second idle runs move median normalized CPU from 0.3274% to
0.2919%, with zero signalled wakes, no Inspector registry, identical 480-byte blank
scenes, and the same 800 timer and 240 animation-frame callbacks completed by each
variant.

The unchanged upstream Bootstrap Offcanvas slice and synthetic-Window-resize fix were
compared with their exact clean parent (`808dd0d`) in six balanced fresh-process runs.
Median p50 moved from 0.750 ms to 0.755 ms (+0.7%) for 1,000 startup/lifecycle samples,
from 33.042 ms to 33.279 ms (+0.7%) for the 50-sample selector workload, and from
3.173 ms to 3.137 ms (-1.1%) for 50 generated named-property samples. A four-context/
2,000-node probe retains the fixed 976-byte node and byte-identical attributed node,
attribute, pool, wrapper, and scene storage. Both isolated and shared-isolate lifecycle
probes pass, including hidden timers, context release, and shared-slot reuse. Five
alternating five-second idle runs move median normalized CPU from 0.2304% to 0.2279%,
with zero signalled wakes, no Inspector registry, identical 480-byte blank scenes, and
the same 800 timer and 240 animation-frame callbacks completed by each variant. The
per-spec task drain exists only in the ecosystem test harness and does not ship in a
host or runtime artifact.

The unchanged upstream Bootstrap Carousel slice and bounded legacy-Event fix were
compared with their exact clean parent (`9261e70`) in six balanced fresh-process runs.
The machine changed frequency substantially during the run, but balanced median p50
moved only from 2.361 ms to 2.371 ms (+0.4%) for 1,000 startup/lifecycle samples, from
33.054 ms to 33.102 ms (+0.1%) for the 50-sample selector workload, and from 2.889 ms
to 2.896 ms (+0.3%) for 50 generated named-property samples. A four-context/2,000-node
probe retains the fixed 976-byte node and byte-identical attributed node, attribute,
pool, wrapper, and scene storage. The two new method templates add no document or node
state; median populated V8 used heap increased by a bounded 44,136 bytes per context
while total incremental process working set per context was 163,840 bytes lower. Both
isolated and shared-isolate lifecycle probes pass. Five alternating five-second idle
runs move median normalized CPU from 0.2469% to 0.2527% (+0.0059 percentage points),
with zero signalled wakes, no Inspector registry, identical 480-byte blank scenes, and
the same 800 timer and 240 animation-frame callbacks completed by each variant. Median
prewarm time moved from 1.484 ms to 1.477 ms (-0.5%); the release library grows by
16,752 bytes.

The unchanged upstream Bootstrap ScrollSpy slice and product-neutral anchor, hidden,
computed-style, overflow-range, and scroll-task fixes were compared with their exact
clean parent (`8794c1b`) in six balanced fresh-process runs. CPU frequency changed
substantially during the lifecycle run, but median p50 remained within the established
envelope at 1.404 ms versus 1.434 ms (+2.2%) for 1,000 startup/lifecycle samples.
Median p50 moved from 31.690 ms to 31.854 ms (+0.5%) for the 30-sample selector
workload and from 2.922 ms to 2.905 ms (-0.6%) for 50 generated named-property
samples. A four-context/2,000-node probe retains the fixed 976-byte node and
byte-identical attributed node, attribute, pool, wrapper, and scene storage; median
populated V8 used heap increased by a bounded 12,264 bytes per context. Both isolated
and shared-isolate lifecycle probes pass, including context release and shared-slot
reuse. Five alternating five-second idle runs move median normalized CPU from 0.2655%
to 0.2470%, with zero signalled wakes, no Inspector registry, identical 480-byte blank
scenes, and the same 800 timer and 240 animation-frame callbacks completed by each
variant. Median prewarm time moved from 1.485 ms to 1.497 ms (+0.8%); the release
library grows by 80 bytes.

The unchanged upstream Bootstrap Tooltip slice and product-neutral empty-inline
cross-size fix were compared with their exact clean parent (`b292adc`) in six balanced
fresh-process runs. Median p50 moved from 0.732 ms to 0.730 ms (-0.3%) for 1,000
startup/lifecycle samples, from 31.829 ms to 31.880 ms (+0.2%) for the 50-sample
selector workload, and from 2.945 ms to 2.923 ms (-0.7%) for 50 generated
named-property samples. A four-context/2,000-node probe retains the fixed 976-byte
node and byte-identical DOM, attribute, pool, wrapper, and scene storage. Both isolated
and shared-isolate lifecycle probes pass. Five balanced five-second idle runs move
median normalized CPU from 0.2522% to 0.2449%, with zero signalled wakes, no Inspector
registry, identical 480-byte blank scenes, and the same 800 timer and 240 animation-
frame callbacks completed by each variant. The representative workload median remains
inside the established envelope at 254.371 ms versus 261.557 ms (+2.8%). Median
prewarm time moved from 1.489 ms to 1.474 ms (-1.0%); the release library grows by 80
bytes with identical Mach-O segment sizes. The existing flex-container path remains
unchanged, and the fix adds no document or per-node state.

The unchanged upstream jQuery serialization slice and product-neutral textarea value-
lifecycle fix were compared with their exact clean parent (`212ae92`) in six balanced
fresh-process runs. Median p50 moved from 0.731 ms to 0.734 ms (+0.3%) for 1,000
startup/lifecycle samples, from 31.842 ms to 31.786 ms (-0.2%) for the 50-sample
selector workload, and from 2.894 ms to 2.926 ms (+1.1%) for 50 generated named-
property samples. A four-context/2,000-node probe retains the fixed 976-byte node and
byte-identical attributed DOM, attribute, pool, wrapper, and scene storage. Both
isolated and shared-isolate lifecycle probes pass. Five balanced five-second idle runs
move median normalized CPU from 0.24717% to 0.24724% (+0.00007 percentage points),
with zero signalled wakes, no Inspector registry, identical 480-byte blank scenes, and
the same 800 timer and 240 animation-frame callbacks completed by each variant. The
representative workload
median moved from 279.264 ms to 246.263 ms (-11.8%), and median prewarm time from
1.474 ms to 1.521 ms (+3.2%). The release library grows by 64 bytes with identical
Mach-O segment sizes. The textarea dirty bit occupies existing padding in the 56-byte
cold form-control record and adds no ordinary document or per-node state.

The unchanged upstream jQuery traversal slice and product-neutral DOM traversal/
cloning fixes were compared with their exact clean parent (`f158f8b`) in six balanced
fresh-process runs. Median p50 moved from 0.737 ms to 0.744 ms (+1.0%) for 1,000
startup/lifecycle samples, from 31.969 ms to 32.441 ms (+1.5%) for the 50-sample
selector workload, and from 3.072 ms to 3.043 ms (-1.0%) for 50 generated named-
property samples. A four-context/2,000-node probe retains the fixed 976-byte node and
byte-identical attributed DOM, attribute, pool, wrapper, and scene storage; incremental
working set per context moved from 22,827,008 to 23,064,576 bytes (+1.0%). Both isolated
and shared-isolate lifecycle probes pass, including hidden timers, context release, and
shared-slot reuse. Five balanced five-second idle runs move median normalized CPU from
0.2557% to 0.2559% (+0.0002 percentage points), with zero signalled wakes, no Inspector
registry/state, identical 480-byte blank scenes, and the same 800 timer and 240 animation-
frame callbacks completed by each variant. The representative workload median remains
inside the established envelope at 289.449 ms versus 299.951 ms (+3.6%). The release
library grows by 64 bytes and the changes add no document or per-node state.

The unchanged upstream jQuery dimensions slice and product-neutral box/table fixes
were compared with their exact clean parent (`24c1879`) in six balanced fresh-process
runs. Median p50 moved from 0.735 ms to 0.731 ms (-0.5%) for 1,000 startup/lifecycle
samples, from 31.673 ms to 31.921 ms (+0.8%) for the 50-sample selector workload, and
remained 2.901 ms for 50 generated named-property samples. A
four-context/2,000-node probe retains the fixed 976-byte node and byte-identical
attributed DOM, attribute, pool, wrapper, and scene storage; incremental working set
per context moved from 22,835,200 to 22,085,632 bytes (-3.3%). Authored table border
state reuses an existing cold style allocation, and resolved track/spacing values exist
only in table layout records. Both isolated and shared-isolate lifecycle probes pass,
including hidden timers, context release, and shared-slot reuse. Five balanced
five-second idle runs move median normalized CPU from 0.2563% to 0.2533% (-0.0030
percentage points), with zero signalled wakes, no Inspector registry/state, identical
480-byte blank scenes, and the same 800 timer and 240 animation-frame callbacks
completed by each variant. The representative workload median moved from 74.535 ms to
79.448 ms (+6.6%); median prewarm time moved from 1.449 ms to 1.455 ms (+0.4%), warm
context creation from 0.0116 ms to 0.0103 ms (-11.3%), and first scene from 0.8305 ms
to 0.8180 ms (-1.5%). The release library grows by 16,784 bytes. No measured timing
regression exceeds the established 10% no-meaningful-regression envelope.

The unchanged upstream jQuery readiness slice and product-neutral timer-error fix were
compared with their exact clean parent (`c9f6a4a`) in six balanced fresh-process runs.
Median-of-medians p50 moved from 0.734 ms to 0.736 ms (+0.2%) for 1,000
startup/lifecycle samples, from 61.265 ms to 63.580 ms (+3.8%) for the 30-sample
selector workload under substantial shared-machine load, and from 3.084 ms to 3.117 ms
(+1.1%) for 50 generated named-property samples. The success path adds no new branch:
the additional work begins only after a timer callback throws and `window.onerror` is
callable. No document, node, timer, or scene state was added, and the release library
grows by 304 bytes. All measured medians remain within the established 10%
no-meaningful-regression envelope.

The unchanged upstream jQuery wrapping slice and product-neutral fragment/script fix
were compared with their exact clean parent (`8036dfe`) in six balanced fresh-process
runs. Median-of-medians p50 moved from 0.732 ms to 0.736 ms (+0.5%) for 1,000
startup/lifecycle samples, from 31.945 ms to 31.955 ms (+0.03%) for the 30-sample
selector workload, and from 2.960 ms to 2.925 ms (-1.2%) for 50 generated named-
property samples. Script execution history occupies existing tail padding, so the
compile-time 976-byte `dom_node` invariant is unchanged; ordinary nodes allocate no
additional state. The optimized Inspector library grows by 272 bytes. All measured
medians remain within the established 10% no-meaningful-regression envelope.

The unchanged upstream jQuery core slice and its generic DOM/XML/iframe fixes were
compared with their exact clean parent (`9ac2c31`) in six balanced fresh-process runs.
Median-of-medians p50 moved from 0.789 ms to 0.770 ms (-2.3%) for 1,000
startup/lifecycle samples, from 34.390 ms to 34.272 ms (-0.3%) for the 30-sample
selector workload, and from 3.288 ms to 3.335 ms (+1.4%) for 50 generated
named-property samples. Four balanced four-context/2,000-row memory probes moved the
median incremental working set from 22,943,744 to 25,169,920 bytes per context (+9.7%)
and post-destroy retention from 91,062,272 to 99,991,552 bytes (+9.8%). The increase is
bounded to the 2,001 real Text nodes now represented by that authored workload;
`dom_node` remains 976 bytes. The optimized Inspector library grows by 49,872 bytes
(0.16%). Isolated and shared-isolate lifecycle probes pass context identity, hidden
timers, context release, and shared-slot reuse. Every measured median remains within
the established 10% no-meaningful-regression envelope.

There are no remaining candidate failures on the local `osx-arm64` Inspector artifact.
Promotion remains intentionally separate: the same unchanged bytes still need evidence
from every released RID. No documents remain harness-blocked. The pinned elliptical-
radius, list-marker, visibility-layout, and rounded-overflow documents and the dynamic
flex check-layout document are now all exercised in the candidate lane.

The native-package workflow downloads and aggregates both required and candidate
evidence after all native jobs. The required aggregate is blocking and the verified
release package set depends on it; candidate discovery remains non-release-gating.
`scripts/verify-cross-rid-compatibility.py` rejects missing or duplicate RID artifacts,
manifest-hash/profile/revision/engine drift, denominator or path drift, inconsistent
summaries, and any non-passing document or subtest. A green candidate aggregate is
therefore usable promotion evidence without weakening the required release gate when a
candidate failure is discovered on one platform.

The Custom Elements discovery shard pins unchanged upstream registry, constructor,
attribute-reaction, connection, and disconnection tests alongside two product-neutral
local fixtures. The lifecycle fixture is assertion based; the rendering fixture compares
lifecycle-created light DOM with an inert reference and can use the Chromium oracle.
The recorded 2026-08-02 native discovery baseline was 40/118 upstream/local assertions.
The 2026-08-09 Inspector run passes all 118/118 selected lifecycle assertions: 4/4
registry reverse lookup, 12/12 constructor, 13/13 attribute reactions, and 80/80
connection/disconnection assertions plus the 9/9 local lifecycle contract. The
light-DOM rendering reftest also passes natively and Chromium independently considers
its test and reference identical. The bounded XHR prerequisite is GET-only and exists
to load same-origin XML/HTML lifecycle fixtures; it is not a general networking claim.

## Shadow DOM candidate milestone

The first production-shaped Shadow DOM slice is deliberately one end-to-end vertical
feature rather than a JavaScript-only API facade. The native document owns roots and
slot assignment in an optional side table. A single composed projection then drives
cascade inheritance, layout, paint, geometry, hit testing, focus, custom-element
lifecycle traversal, and composed event paths, while ordinary DOM parent/child queries
stay on the logical tree and outside listeners receive a retargeted host.

The candidate claim covers:

- `attachShadow({mode: "open" | "closed"})`, open-root identity and closed-root
  concealment;
- direct default and named slot assignment, fallback children, `assignedNodes()` and
  `assignedElements()`;
- ordinary shadow-contained selectors, exact `:host`, scoped author rules, and
  inherited values from the host;
- composed layout, paint, geometry, hit testing, focus ownership and a basic
  single-root composed event path; and
- lifecycle-created shadow content and connected custom elements inside a shadow root.

It does not yet claim manual distribution, `slotchange`, functional
`:host()`, `:host-context()`, `::slotted`, `::part`, declarative Shadow DOM,
`adoptedStyleSheets`, complete `delegatesFocus`, complete nested-root and
`relatedTarget` retargeting, cloning/adoption semantics, or the full Web IDL prototype
shape. These remain visible discovery failures rather than being papered over.

Recorded 2026-08-09 evidence is:

- the local primitive contract passes 9/9 assertions;
- the native lifecycle-created rendering fixture exactly matches its inert reference,
  and Chromium independently passes the same test/reference pair;
- unchanged pinned WPT passes 3/3 assertions for
  `Element-interface-shadowRoot-attribute.html`, 18/18 for
  `HTMLSlotElement-interface.html`, and 12/12 for `ShadowRoot-interface.html`; and
- generated binding and native runtime regressions cover distinct character-data brands,
  processing-instruction exclusion from slots, recursive flattening, and connected sheet
  identity. This remains candidate evidence; the required 110-document gate is unchanged.

The implementation is pay for what is used. `dom_node` remains 976 bytes before and
after the milestone. `native_document` grows from 296 to 304 bytes for one nullable
side-table pointer; a light-DOM document allocates no shadow roles or shadow bytes.
Against the clean parent commit, a 500-sample lifecycle load benchmark improved from a
0.690 ms to 0.686 ms median, a 30-sample light-DOM selector workload moved from 32.580
ms to 32.598 ms (+0.055%), and four idle contexts over five seconds moved from 0.95407%
to 0.95514% normalized CPU (+0.00107 percentage points). These figures establish a
no-meaningful-regression baseline; future shadow work must preserve it.

## Reported 7GUIs compatibility reductions

The WebScene branch of the React 7GUIs sample reported three CSS/form adaptations against
1.0.17. Current candidate coverage retains product-neutral reductions for all three:

- a default single `select` is one collapsed native box, projects only its selected label,
  and remains usable through the native pointer and keyboard boundary;
- ordinary form grids support fixed, automatic, fractional, and bounded `minmax()` tracks,
  gaps, source-order placement, and numeric row/column spans; and
- mixed inline timer runs retain source order and one shared line box. This last case was
  already repaired by the later inline-boundary whitespace work and needed regression
  coverage rather than another engine path.

These are candidate additions. The 110-document required profile remains unchanged and
passes 110/110 after the layout changes. Select and Grid state stays behind existing
optional form/style records; ordinary DOM nodes gain no fields or presenter controls.
Against the clean parent, three alternating same-machine runs put the 500-sample lifecycle
median-of-medians at 0.715/0.702 ms (parent/current) and the 30-sample light-DOM selector
workload at 32.465/32.648 ms (+0.56%). This is within the established no-meaningful-
regression envelope while the feature paths themselves remain pay for what is used.
