# WebScene component compatibility profile

This directory contains WebScene's native-only compatibility profile. It is a bounded
set of browser contracts for trusted, packaged UI components—not a claim of full Web
Platform Test conformance.

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

- `testharness` runs the pinned WPT harness and records its assertions.
- `reftest` settles and captures the native scene through the deterministic renderer,
  then compares document and reference pixels. With `--chromium-path`, it also records
  the independent Chromium self-comparison and cross-engine differential described
  above.
- `visual` runs an unchanged self-verifying WPT against bounded, manifest-authored exact
  color-count and optional spatial color-gap checks, always retains the native
  screenshot, and applies the same checks independently in Chromium when
  `--chromium-path` is supplied. Every visual entry must include both a failure-color
  bound and a non-blank success condition appropriate to the upstream test's authored
  pass statement.
- `contract` runs a project-owned reduced behavior contract through the same native
  document and JavaScript boundary.

Input tests enqueue pointer, wheel, keyboard, focus, and resize actions through the
native input ABI. They must not call presenter internals directly.

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

## 2026-08-09 coverage audit

The pinned profile contains 110 required, 55 candidate, 3 harness-blocked, and 5
excluded documents. A full `osx-arm64` Inspector-flavor audit started at 41/52 candidate
documents and 222/299 candidate subtests. The first focused standards tranche moved
that lane to 47/52 documents and 239/301 subtests. The broadened lane now passes
55/55 documents and 311/311 subtests while the release gate remains 110/110 documents
and 434/434 subtests:

- complex `:is()` alternatives now match full selectors, CSS sibling combinators ignore
  intervening non-element nodes, and the managed selector parser uses the most specific
  functional-selector argument;
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
  dynamic `align-items` changes.

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

There are no remaining candidate failures on the local `osx-arm64` Inspector artifact.
Promotion remains intentionally separate: the same unchanged bytes still need evidence
from every released RID. Three self-verifying visual documents remain harness-blocked:
two list-marker documents are not yet pinned locally, while the elliptical-radius case
deliberately exceeds the currently claimed scalar corner projection. The pinned
visibility-layout and rounded-overflow documents and the dynamic flex check-layout
document are no longer in that set.

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
