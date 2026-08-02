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

It does not yet claim nested/flattened or manual distribution, `slotchange`, functional
`:host()`, `:host-context()`, `::slotted`, `::part`, declarative Shadow DOM,
`adoptedStyleSheets`/`styleSheets`, complete `delegatesFocus`, complete nested-root and
`relatedTarget` retargeting, cloning/adoption semantics, or the full Web IDL prototype
shape. These remain visible discovery failures rather than being papered over.

Recorded 2026-08-02 evidence is:

- the local primitive contract passes 9/9 assertions;
- the native lifecycle-created rendering fixture exactly matches its inert reference,
  and Chromium independently passes the same test/reference pair;
- unchanged pinned WPT reaches 2/3 assertions for
  `Element-interface-shadowRoot-attribute.html`, 13/18 for
  `HTMLSlotElement-interface.html`, and 8/12 for `ShadowRoot-interface.html`; and
- the unchanged failures identify prototype placement, absent `Text`,
  `createProcessingInstruction` and `StyleSheetList` surfaces, plus flattened nested
  slots. They are candidate discovery data; the required 110-document gate is
  unchanged.

The implementation is pay for what is used. `dom_node` remains 976 bytes before and
after the milestone. `native_document` grows from 296 to 304 bytes for one nullable
side-table pointer; a light-DOM document allocates no shadow roles or shadow bytes.
Against the clean parent commit, a 500-sample lifecycle load benchmark improved from a
0.690 ms to 0.686 ms median, a 30-sample light-DOM selector workload moved from 32.580
ms to 32.598 ms (+0.055%), and four idle contexts over five seconds moved from 0.95407%
to 0.95514% normalized CPU (+0.00107 percentage points). These figures establish a
no-meaningful-regression baseline; future shadow work must preserve it.
