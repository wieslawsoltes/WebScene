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

Static reftests can also collect an independent Chromium differential:

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

Result JSON records the profile, pinned WPT revision, native ABI/library hash, selection,
document status, assertions, diagnostics, and artifacts. A required failure produces a
non-zero exit code.

## Test types

- `testharness` runs the pinned WPT harness and records its assertions.
- `reftest` settles and captures the native scene through the deterministic renderer,
  then compares document and reference pixels. With `--chromium-path`, it also records
  the independent Chromium self-comparison and cross-engine differential described
  above.
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

The pinned profile contains 110 required, 52 candidate, 6 harness-blocked, and 5
excluded documents. A full `osx-arm64` Inspector-flavor audit started at 41/52 candidate
documents and 222/299 candidate subtests. Focused standards fixes moved that lane
to 47/52 documents and 239/301 subtests while the release gate remained 110/110
documents and 434/434 subtests:

- complex `:is()` alternatives now match full selectors, CSS sibling combinators ignore
  intervening non-element nodes, and the managed selector parser uses the most specific
  functional-selector argument;
- inline unitless and percentage `line-height` values are resolved after the winning
  `font-size`, including later CSSOM font-size changes; and
- generated Web IDL attributes now appear as enumerable, configurable accessors on the
  declaring interface prototype while retaining native receiver-brand checks; and
- distinct `CharacterData`, `Text`, `Comment`, `ProcessingInstruction`, and
  `HTMLStyleElement` brands now back constructible text/comment nodes, processing
  instructions, flattened slot queries, and connected `ShadowRoot.styleSheets` identity.

The focused behavior is also covered by native runtime tests, managed selector tests,
and the generated-binding freshness check. Candidate promotion is intentionally deferred
until the same documents pass on every released RID.

The remaining five candidate failures are kept visible and rank the next work:

1. Custom Elements constructor timing, Attr mutation, cloning, alternate-document
   realms, iframe documents, and XHR-backed document prerequisites account for four
   documents and 61 failed assertions.
2. The HTML tree builder still misses foster-parented table text ordering in one local
   adoption-agency/table-construction assertion.
3. Six relevant visual/check-layout documents remain harness-blocked; `check-layout-th.js`
   support is the highest-value runner expansion because it unlocks upstream flex
   relayout evidence without converting the test into a local contract.

The Custom Elements discovery shard pins unchanged upstream registry, constructor,
attribute-reaction, connection, and disconnection tests alongside two product-neutral
local fixtures. The lifecycle fixture is assertion based; the rendering fixture compares
lifecycle-created light DOM with an inert reference and can use the Chromium oracle.
The recorded 2026-08-02 native discovery baseline is 40/118 upstream/local assertions
with the registry reverse-lookup document and local lifecycle document passing. The
light-DOM rendering reftest also passes natively and Chromium independently considers
its test and reference identical. Remaining failures are retained as discovery data,
not hidden by expectations: they cluster around Attr mutation APIs, clone upgrade,
alternate document realms, and unrelated iframe/XHR harness prerequisites.

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
