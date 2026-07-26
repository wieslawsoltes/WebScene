# HtmlML Web Platform subset

This directory implements the curated standards and connected-component test lane.
It is deliberately a bounded **HtmlML component profile**, not a claim that HtmlML is
a general-purpose browser or that every value of a listed CSS property is supported.

## Scope rule

A WPT case enters this directory only when it maps to an observed connected-component
capability or a prerequisite needed to make that capability correct. The profile
uses four explicit states in `htmlml-component-profile.json`:

- `required`: published HtmlML component-profile behavior; failures make the runner fail;
- `candidate`: relevant tests being evaluated, but not yet part of the support claim;
- `harnessBlocked`: relevant tests that need an adapter facility rather than a product fix;
- `excluded`: intentionally unsupported areas and the reason they are out of scope.

This keeps the suite narrow. Do not import a WPT directory, implement a feature just
because WPT contains tests for it, or turn candidate failures into blanket expected
failures. New product defects first get a small product-neutral contract, then the
nearest useful WPT case, then an application-owned parity check.

## CSS semantic suites

The candidate lane includes focused upstream CSS Cascade, CSSOM, and CSS Custom
Properties documents in addition to the required component profile. These cases
exercise precedence, CSS-wide keywords, inline declaration mutation, computed-value
serialization, and custom-property inheritance independently of any hosted product.
Run them through both adapters with `--selection candidate`; candidate failures are
reported but do not silently weaken the required profile.

Current evidence (2026-07-26): the complete required profile contains 110 documents
and 433 assertions. The latest promotion requires selector APIs, element collections,
and `parentElement` to preserve JavaScript node identity, uniqueness, and tree order.
It was reduced from impossible duplicate structural paths in a hosted-component
inventory, authorized unchanged in Chrome, and then passed unchanged in both HtmlML
adapters. This rules out a general core identity defect and keeps transient-locator
handling in the private consumer. The preceding promotion requires rendered `innerText`
block boundaries while preserving adjacent inline concatenation. It was reduced from a
TradingView semantic-inventory accessible-name discrepancy, authorized unchanged in
Chrome, and then passed unchanged in both HtmlML adapters. The earlier promotion requires
stylesheet `visibility` inheritance,
layout retention, focus rejection, hit-test rejection, and explicit descendant
computed/interaction restoration; Chrome and native already passed, while the managed
interaction layer now
filters inherited-hidden controls from focus and DOM hit testing. The preceding
Chrome-authorized `transform:none` transition contract discovered by the normalized
TradingView graph requires forward and reverse identity interpolation through multiple
observable frames. The native computed-style serializer reports the painted transition
matrix instead of snapping to the target transform. The native candidate profile passes
28/28 documents and 81/81 assertions; managed passes 14/28 documents and 56/81
assertions. The latest promotion adds the corrected Chrome-authorized overflow viewport
contract and independent managed/native scrollbar paint authority. The preceding
promotion adds the 8-assertion Chrome-authorized font-shaping
and inline-text contract, including managed geometry and raster authority for bounded
nowrap word spacing plus direct/portable collapsed-whitespace regressions. A complete
required rerun caught and prevented an overbroad space advance under `font-size: 0`
through the existing subpixel-table reftest before promotion. An earlier promotion
adds a 6-assertion Chrome-authorized custom-property
CSSOM/lifecycle reduction and the unchanged 73-assertion `variable-definition.html`
WPT. Both adapters pass both documents completely, and the originating unchanged
jQuery custom-property case is green. The complete pinned selector-escape WPT is now
required after native adopted a lossless WTF-8 representation for Web IDL DOMString
values and passed all 68/68 cases, including lone-surrogate distinction. Both adapters
pass the unchanged upstream horizontal
flex-wrap reftest after per-line flexible sizing, wrapped cross-axis stretch,
single-authority block margins, and its bounded consecutive-float reference
construction were implemented, so the case is required. Neutral DOM/Web API
reductions derived from version-pinned jQuery 4.0.0, Bootstrap 5.3.8, and React DOM
19.2.8 consumer fixtures are also required. The latest promotions preserve the
original JavaScript Event object and arbitrary payload accessors through dispatch,
provide rooted IntersectionObserver delivery over bounded scrolling with moving client
but invariant offset geometry, reflect fragment-anchor hash state, and preserve
`Element.classList` same-object identity and live method shadowing. The newest
23-assertion promotion adds shared DOM/parser/attribute/form/CSSOM behavior discovered
by jQuery's unchanged attributes suite, including Web IDL checked/selected conversion
and class-token attribute-presence invalidation. They pass direct
Chrome authority, managed, native, and their originating Bootstrap carousel/ScrollSpy/
Toast, Collapse, and Dropdown compositions. The Dropdown promotion adds connected
stylesheet lifecycle and rendered visibility geometry, document-root and reentrant
click delegation, `MouseEvent` construction/cleanup, element-sibling navigation, and
compound selector continuation after functional pseudos. A retained-runtime reduction
also added variadic `ParentNode.append()`, string-to-Text conversion, fragment
flattening, and correct first/last element-child filtering. The managed aggregate
passes all 107/107 required documents and 424/424 assertions. The inherited-line-height input primitive closes the former dialog
height failure, and live temporal computed-style values plus coalesced rendering-frame
delivery pass the unchanged Chrome-grounded rotation cadence contract in ten consecutive
fresh processes. Its candidate aggregate is 14/28 documents and 56/81 assertions. See
`artifacts/web-platform-required-ecosystem-v14-promoted-20260722/`,
`artifacts/web-platform-required-ecosystem-v14-managed-complete-v5-20260722/`,
`artifacts/web-platform-rotation-cadence-managed-live-style-v3-{1..10}-20260722/`,
`artifacts/web-platform-candidate-ecosystem-v14-promoted-20260722/`,
`artifacts/web-platform-candidate-ecosystem-v14-chrome-contracts-v2-20260722/`, and
`artifacts/web-platform-required-v19-bootstrap-dropdown-promoted-20260723/`,
`artifacts/web-platform-required-v78-dom-attribute-promoted-20260723/`,
`artifacts/web-platform-required-used-values-promoted-v1-20260723/`,
`artifacts/web-platform-candidate-v80-cssom-inline-20260723/native/`,
`artifacts/web-platform-candidate-v80-cssom-inline-managed-v3-20260723/`,
`artifacts/web-platform-required-selector-window-hidden-promoted-v1-20260723/managed/results.json`,
`artifacts/web-platform-required-selector-window-hidden-promoted-native-v2-20260723/results.json`, and
`artifacts/ecosystem-consumers-selector-window-hidden-v1-20260723/ecosystem-results.json`,
plus the current
`artifacts/web-platform-required-hidden-subtree-z-index-promoted-v1-20260723/`,
`artifacts/web-platform-hidden-subtree-z-index-chrome-final-v1-20260723/`, and
`artifacts/ecosystem-consumers-hidden-subtree-z-index-final-v1-20260723/ecosystem-results.json`.
The native input lifecycle contract now has a fourth assertion requiring
`stopImmediatePropagation()` to stop later same-target and ancestor native mouseup
recognizers. Native passes 4/4 in
`artifacts/web-platform-native-input-propagation-v2-20260723/results.json`; managed
passes the new propagation assertion and 3/4 overall while retaining its previously
recorded testdriver movement gap.
The ecosystem denominator is now 18 documents and 450 assertions: Chrome, managed,
and native each pass 450/450. Its official-source slice runs 424
unchanged cases—253 from Bootstrap 5.3.8 and 171 from jQuery 4.0.0's hash-pinned
callbacks, attributes, and CSS unit files—in addition to the three composition fixtures.
The manifest inventories all 14 Bootstrap, 24 jQuery, and 128 React DOM test files. The
remaining 7, 21, and 128 files stay explicitly harness-blocked rather than disappearing
from the denominator.

The jQuery CSS expansion produced two product-neutral four-assertion CSS contracts.
Chrome, managed, and native each pass the declaration contract's distinction between supported declaration
IDL properties and JavaScript expandos, invalid-value retention, empty-string removal,
and disconnected computed-style behavior. They also pass connected percentage inset
and margin pixel serialization, opposing auto-margin distribution, and fractional
client geometry. A third five-assertion contract covers default display restoration,
detach/reattach cascade state, tiny rendered boxes, synchronous display changes, and
ancestor box suppression. All three contracts are now required; that profile passed
86/86 documents and 273/273 assertions in both adapters. A subsequent SCRIPT raw-text,
selector-error, Window.name, and hidden-input tranche enlarges the required profile to
90/90 documents and 286/286 assertions in both adapters. A further six-assertion
Grid placement CSSOM contract promotes `grid-area`, `grid-row`, `grid-column`, their
four placement longhands, cascade precedence, unitless line values, and removal
invalidation. Chrome and both adapters pass it, enlarging the profile at that promotion
to 91/91 documents and 292/292 assertions and closing the unchanged jQuery Grid
assertion in each adapter. The earlier tranche closes the unchanged
jQuery toggle and `:visible`/`:hidden` failures through shared loader, selector,
Window-global, and HTML rendering primitives. The initial declaration fix moved
eight upstream CSS assertions to pass in each adapter without adding jQuery-specific
behavior. Distinct native inline/inline-block/list-item values and ancestor-aware offset
geometry close eight more unchanged native assertions. The latest auto-margin tranche
adds the unchanged pinned CSS2 `auto-margins-used-values.html` WPT (6/6 in both
adapters) and a dynamic CSSOM, geometry, and containing-block mutation contract
(4/4 in Chrome and both adapters). It enlarges the required profile to 93/93 documents
and 302/302 assertions and improves the unchanged ecosystem lane to managed 445/450
and native 444/450 without new failures. The runner now preserves BODY `onload`
startup generically, allowing future check-layout WPTs to execute without changing
their pinned bytes. The subsequent custom-property tranche adds the unchanged
`variable-definition.html` WPT (73/73 in both adapters) and a 6-assertion
Chrome-authorized CSSOM/lifecycle reduction. It expands the required profile to 95/95
documents and 381/381 assertions and improves the unchanged ecosystem lane to managed
446/450 and native 448/450. At that checkpoint four managed and two native failures
remained in the composition denominator. Grid evidence:
`artifacts/web-platform-required-grid-placement-promoted-final-v1-20260723/` and
`artifacts/ecosystem-consumers-grid-placement-final-v1-20260723/`. Earlier evidence:
`artifacts/web-platform-cssom-inline-validity-chrome-v1-20260723/`,
`artifacts/web-platform-cssom-inline-validity-managed-v2-20260723/`, and
`artifacts/web-platform-cssom-inline-validity-native-v1-20260723/`, plus
`artifacts/web-platform-css-used-values-{chrome-v1,managed-v6,native-v2}-20260723/` and
`artifacts/web-platform-required-used-values-promoted-v1-20260723/`, plus
`artifacts/web-platform-display-restoration-{chrome-v1,native-v3}-20260723/` and
`artifacts/web-platform-required-display-restoration-promoted-v1-20260723/`.
Auto-margin evidence:
`artifacts/web-platform-required-auto-margin-promoted-v1-20260723/`,
`artifacts/web-platform-auto-margins-wpt-htmlml-v2-20260723/`, and
`artifacts/ecosystem-consumers-auto-margin-native-fixed-v1-20260723/`.
Custom-property evidence:
`artifacts/web-platform-required-custom-property-promoted-v1-20260723/`,
`artifacts/web-platform-custom-property-cssom-chrome-v3-20260723/`, and
`artifacts/ecosystem-consumers-custom-property-fixed-v2-20260723/`.

The latest box-edge tranche adds two Chrome-authorized product-neutral contracts and
nine assertions. Padding shorthand/longhand mutation, declaration removal, border
shorthand/style/width/physical-side composition, content-box computed dimensions,
and synchronous border-box geometry now pass Chrome, managed, and native. The
unchanged jQuery numeric box-edge allowlist consequently passes completely in native,
raising that ecosystem lane to 449/450; only computed height retention for a text input
inside a `display:none` subtree remains. Evidence:
`artifacts/web-platform-required-box-edges-promoted-v1-20260723/`,
`artifacts/web-platform-cssom-box-edges-chrome-final-v3-20260723/`,
`artifacts/web-platform-cssom-box-edges-htmlml-final-v3-20260723/`, and
`artifacts/ecosystem-consumers-box-edges-final-v4-20260723/`.

The next staged reduction adds four assertions that separate computed dimensions from
box generation beneath `display:none`, followed by three assertions for numeric
`zIndex` IDL assignment, connection/recascade precedence, negative values, and inline
removal. Direct Chrome and both adapters pass all seven. Correct observable z-index
then exposed a retained-canvas composition error; descendants now order inside the
chart host without lifting that complete host above later overlay DOM. Those neutral
fixes close the final native jQuery CSS failure, so the unchanged native ecosystem
aggregate reaches 450/450 while the four managed failures remain visible. Evidence:
`artifacts/web-platform-hidden-subtree-z-index-chrome-final-v1-20260723/`,
`artifacts/web-platform-required-hidden-subtree-z-index-promoted-v1-20260723/`, and
`artifacts/ecosystem-consumers-hidden-subtree-z-index-final-v1-20260723/`.

A further four-assertion reduction distinguishes `getPropertyValue()` fallback from
CSSStyleDeclaration named-property exposure. Chrome and both adapters return an empty
string from method lookup for an unknown property, expose supported camel/kebab aliases,
and keep unsupported and custom names out of named access. It closes exactly the
originating unchanged managed jQuery assertion, improving that aggregate to 447/450.
Evidence:
`artifacts/web-platform-computed-style-named-properties-chrome-v1-20260723/`,
`artifacts/web-platform-required-computed-style-named-properties-v1-20260723/`, and
`artifacts/ecosystem-consumers-computed-style-named-properties-v3-20260723/`.

A further four-assertion lifecycle reduction requires detached computed-style reads to
be empty and prevents their snapshots from surviving reattachment. Chrome and both
adapters recascade stylesheet display:none after connection and suppress the hidden
box. This closes exactly the originating managed jQuery detached show/attach assertion,
improving the ecosystem aggregate to 448/450. Evidence:
`artifacts/web-platform-detached-computed-reattach-chrome-v1-20260723/`,
`artifacts/web-platform-required-detached-computed-reattach-v1-20260723/`, and
`artifacts/ecosystem-consumers-detached-computed-reattach-v1-20260723/`.

A four-assertion iframe lifecycle reduction now requires a connected source-less iframe
to expose its synchronous initial `about:blank` Window and Document with stable
`contentWindow.document`, `contentDocument`, `defaultView`, and `frameElement` identity.
It also requires bounded `document.open()/write()/close()` body replacement and a
computed-style read from a hidden frame. Chrome, managed, and native pass 4/4, and the
originating unchanged jQuery frame-element assertion now passes managed. Evidence:
`artifacts/web-platform-iframe-document-chrome-v1-20260723/`,
`artifacts/web-platform-iframe-document-htmlml-v5-20260723/`,
`artifacts/web-platform-required-iframe-document-final-serial-v1-20260723/`, and
`artifacts/ecosystem-consumers-iframe-document-v1-20260723/`.

A further four-assertion font-relative box reduction requires opposing percentage
insets to retain independent used values; dynamic CSSOM percent-to-em replacement and
four-value `inset` to resolve from the element's inherited font; ancestor font-size
mutation to invalidate computed geometry; and width, min-height, padding, gap, and
flex-basis to expose consistent pixel values. Chrome and both adapters pass 4/4.
Native layout and paint now resolve `em`/`rem` from the owning element/root context,
and inline inset/flex-basis state survives connection and recascade. This closes the
final managed jQuery CSS assertion, so all three engines pass the unchanged 450/450
ecosystem denominator. Evidence:
`artifacts/web-platform-font-relative-box-chrome-v2-20260723/`,
`artifacts/web-platform-required-font-relative-promoted-v2-20260723/managed/`,
`artifacts/web-platform-required-font-relative-promoted-v1-20260723/native/`, and
`artifacts/ecosystem-consumers-font-relative-box-final-v1-20260723/`.

The strict managed rotation-cadence contract passes five consecutive serial runs at
this checkpoint. A deliberately concurrent managed/native run exposed one 53ms host
scheduling stall, so competing-workload frame-loss distribution remains a separate
reliability milestone; the 25ms/14-intermediate-frame required threshold is unchanged.
The first full managed run after the font-relative promotion recorded one 28.715ms
gap; the fresh serial retry passed 103/103 documents and 413/413 assertions. Both
results remain evidence so the clean retry does not erase the reliability observation.
Evidence: `artifacts/web-platform-rotation-cadence-managed-serial-v1-20260723/`.

The full current required-plus-candidate selection contains 134 documents and 504
assertions after adding product-neutral media
query, selector, URL-backed SVG background-image, `font: inherit`, and `all: unset`
contracts plus subtree-opacity, SVG/currentColor menu-chevron, submenu pointer-lifecycle,
and cursor/external-navigation compositions derived from runtime feature use, plus
the DOM mutation, vacated-pane rendering, and detached-canvas scene-publication reductions.
Fresh native evidence passes all 134/134 documents and 504/504 assertions across those
two denominators; managed passes the complete required 106/423 plus 14/28 candidate
documents and 56/81 candidate assertions. The current artifacts are
`artifacts/web-platform-required-overflow-scroll-promoted-v1-20260723/{managed,native}/results.json`
and
`artifacts/web-platform-candidate-overflow-scroll-promoted-v1-20260723/{managed,native}/results.json`.
The required font,
all-reset, and positioned/rounded/stacked pseudo-element contracts pass both adapters.
The required pseudo reftest is backed by managed raster and native scene-order/layer
regressions so a shared reference-construction error cannot create a false pass. The
font and all-reset contracts each pass 2/2 through both adapters. The background primitive
passes 2/2 natively, while managed currently passes only its layout-independence
assertion and remains an explicit candidate exception. Evidence:
`artifacts/web-platform-font-inherit-{v1,managed-v1}/results.json`,
`artifacts/web-platform-all-unset-{native-v2,managed-v4}/results.json`, and
`artifacts/web-platform-css-background-v2/{native,managed}/results.json`. The opacity
candidate passes exact pixels natively through isolated scene-group commands; managed
Avalonia 11 still distributes opacity across subtree draw operations and remains a
reported candidate failure. Evidence: `artifacts/web-platform-opacity-native-v3/results.json`.

The required inline-loader alignment contract is a product-neutral reduction of an
automatically discovered composition. It certifies consecutive auto-height row flow,
flex cross-axis centering, an 18×4 three-dot inline box, 4×4 dot geometry,
`vertical-align: middle`, generated inline content, indefinite percentage-height
resolution, and structural-selector recascade for sibling margins. Both adapters pass;
evidence is in
`artifacts/web-platform-subset-inline-loader-alignment-final-20260722/{managed,native}/results.json`.

The required market-status reduction separately certifies generated whitespace advance
between true inline fragments, adjacent-fragment placement, percentage-translated marker
centering, and first-baseline alignment between padded text and a non-text flex item.
Chrome rejected the original `inline-block` whitespace assertion because trailing
collapsible whitespace inside an atomic inline-level box correctly collapses; the
incorrect artifact is retained, and only the corrected Chrome-valid contract was
promoted. Managed and native now pass it alongside the established loader contract.
Evidence: `artifacts/web-platform-inline-status-chrome-v1-20260723/results.json`,
`artifacts/web-platform-inline-status-chrome-v2-20260723/results.json`,
`artifacts/web-platform-inline-status-managed-regression-v2-20260723/results.json`, and
`artifacts/web-platform-required-inline-status-promoted-v2-20260723/{managed,native}/results.json`.

The required font-shaping and inline-text contract measures typography independently
without consumer-specific selectors. Chrome, managed, and native pass 8/8 assertions for computed
family/size/weight/line-height, host font matching, kerning and combining clusters,
letter spacing, bounded nowrap word spacing, collapsed whitespace advance, shaped
wrapping, complex-script geometry, and inline indicator centering. Managed raster
authority proves that authored word spacing moves painted glyphs rather than changing
geometry alone. Direct and portable layout regressions require one semantic collapsed
advance between inline siblings and no additional advance when inherited
`font-size: 0` suppresses the whitespace node. Evidence:
`artifacts/web-platform-font-shaping-chrome-v1-20260723/results.json`,
`artifacts/web-platform-font-shaping-managed-fixed-v2-20260723/results.json`,
`artifacts/web-platform-font-shaping-native-current-v1-20260723/results.json`, and
`artifacts/web-platform-required-font-shaping-promoted-v2-20260723/{managed,native}/results.json`.
Downloadable WPT fonts/Ahem, word-spaced wrapping and RTL paint, and exact text raster
parity remain explicit work rather than being inferred from the bounded geometry.

The required overflow viewport contract is a product-neutral reduction of the reported
missing-scrollbar and unbounded-scrolling failures. Chrome and both adapters agree on a
finite extent, clamping at both boundaries, queued scroll events only when the offset
changes, descendant hit-test clipping, and synchronous re-clamping when content shrinks
or expands before an immediate `scrollTop` assignment. Chrome rejected the original
candidate because its vertical scrollbar caused horizontal overflow, its positioned
tail legitimately preserved the shrunken extent, and it expected synchronous scroll
events; those rejected bytes are retained and only the corrected browser-valid contract
was promoted. Managed raster and native scene tests independently require a proportional
visible overlay thumb at the start and bounded maximum. Evidence:
`artifacts/web-platform-overflow-scroll-chrome-v1-20260723/results.json`,
`artifacts/web-platform-overflow-scroll-chrome-v2-20260723/results.json`,
`artifacts/web-platform-overflow-scroll-managed-fixed-v2-20260723/results.json`,
`artifacts/web-platform-overflow-scroll-native-current-v2-20260723/results.json`, and
`artifacts/web-platform-required-overflow-scroll-promoted-v1-20260723/{managed,native}/results.json`.
Scrollbar dragging and styling, scroll snapping, smooth scrolling, overscroll behavior,
nested chaining, RTL `scrollLeft`, and platform auto-hide timing remain outside this
bounded claim.

CSS transitions and CSS Animations are measured separately. The required transition
lane covers transform, opacity and color interpolation, delay, temporal computed paint,
events, cancellation, reversal, and bounded rotation cadence in both adapters. Native
also passes the candidate staggered-opacity and continuous-rotation `@keyframes`
contracts. Generic hosted exploration—not a product selector—automatically found the
three staggered loader delays and rotating spinner and retained changing
initial/intermediate/later paint samples for all eight animation edges. Evidence:
`artifacts/web-platform-subset-opacity-keyframes-final-20260722/results.json`,
`artifacts/web-platform-subset-keyframes-final-20260722/results.json`, and
Consumer-specific exploration evidence is maintained outside this repository.

The candidate rounded-spinner contract is product-neutral: it asserts a square border
box, four non-zero percentage radii, uniform border widths, and two per-side colour
groups without selecting a consumer-specific class. Both adapters pass 1/1 document and 2/2
assertions. A native scene regression is the paint authority: it requires two rounded
stroke commands whose side flags cover the complete ring, and requires the sampled
CSS keyframe angle to reach a native rotation transform. A separate zero-command engine
regression rejects synthetic benchmark rectangles in the pre-document startup scene.
Evidence: `artifacts/web-platform-spinner-v3-20260722/{managed,native}/results.json`.

The candidate segmented-rounded-border contract independently covers adjacent boxes
whose touching border sides are zero-width. Both adapters pass its 2/2 geometry and
computed-style assertions. Native paint authority additionally distinguishes an open
joined edge from a multi-colour corner partition and requires the two border centerlines
to meet at the exact same coordinate. This prevents a settled component screenshot from
hiding a one-pixel split between title and action-button outline segments. Evidence:
`artifacts/web-platform-border-join-v3-20260722/{managed,native}/results.json` and
`test_segmented_rounded_borders_share_an_unclipped_join`.

The W3C CSS Validator is a stylesheet syntax checker, not a layout-engine conformance
suite. It is useful for authoring diagnostics but is not included in HtmlML's engine
pass ratio: valid CSS can still be cascaded, computed, laid out, or painted incorrectly.
Engine-specific Gecko, WebKit, and Blink layout tests are discovery sources; a relevant
case is reduced to an upstream WPT or a product-neutral contract before it becomes an
HtmlML certification gate.

First-party documents under `contracts/` cover behavior that spans several standards
or requires repeated lifecycle phases, such as responsive resize and resize-back.
The candidate Canvas contract is generated from the observed runtime feature denominator
and executes the chart-shaped Canvas 2D/Path2D state composition in both adapters.
The media-query and selector contracts similarly cover only the exact runtime-observed
families and expose multiple named assertions rather than relying on document count alone.
The unchanged contract document runs through `managed`, `native`, or `both` using the
same process-isolated engine adapters as the WPT selection.

## Pinned upstream content

`upstream/` contains unmodified files from the official WPT repository at the exact
revision in `upstream-revision.txt`. The original paths are retained. The upstream
BSD-3-Clause license is stored at `upstream/LICENSE.md`. `upstream-files.json` records
the SHA-256 digest of every vendored file so accidental edits are reviewable.

The runner transforms documents only in memory and presents the resulting test to
one of two first-class engine adapters:

- `managed` uses the existing ClearScript/Avalonia DOM and remains the compatibility oracle;
- `native` uses the off-thread V8/native DOM and reads its immutable scene directly;
- `both` runs the identical selection through both adapters and writes separate artifacts.

There is no fallback from native to managed. An unsupported native facility is
reported as a native failure or harness error so the matrix remains useful.

The document preparation then:

- the pinned `testharness.js` is inlined to avoid a general HTTP server;
- the pinned `check-layout-th.js` helper is inlined unchanged for selected geometry assertions;
- selected relative classic scripts are inlined only when their resolved path is present in the pinned upstream provenance manifest;
- the visual `testharnessreport.js` UI is removed;
- selected element-origin `test_driver.Actions().pointerMove()` calls are
  translated to native headless pointer-boundary events;
- selected `test_driver.click(element)` and `test_driver.send_keys(element, Tab)`
  calls are translated to Avalonia pointer and keyboard focus modalities plus
  their corresponding routed input events;
- incidental legacy window-named element references in the selected hover case
  are normalized in memory to explicit `getElementById` lookups;
- XHTML reftest CDATA wrappers are removed in memory because the local blob
  loader currently uses the HTML parser rather than an XML MIME path;
- a result callback records stable JSON for the host;
- managed documents run in a fresh trusted local V8 iframe context; native
  documents run in a fresh native engine instance against the same prepared source.

Do not edit vendored cases to make HtmlML pass. Update them only by reviewing a new
explicit WPT revision and refreshing the provenance metadata.

## Running

From the repository root, run the managed compatibility lane with:

```sh
HTMLML_CLEARSCRIPT_NATIVE=/Volumes/SSD/tmp/HtmlML-ClearScript-751/bin/Release/Unix/ClearScriptV8.osx-arm64.dylib \
HTMLML_CLEARSCRIPT_RID=osx-arm64 \
dotnet run --project tests/WebPlatformSubset/runner/HtmlML.WebPlatformSubset.Runner.csproj \
  -c Release -- --engine managed --selection required
```

After building the native spike, run the exact same profile through both engines:

```sh
dotnet run --project tests/WebPlatformSubset/runner/HtmlML.WebPlatformSubset.Runner.csproj \
  -c Release -- \
  --engine both \
  --native-library "$PWD/artifacts/native-engine-probe-v8/libhtmlml_native_engine.dylib" \
  --native-cache-directory "$PWD/artifacts/native-engine-probe-v8/code-cache" \
  --selection required \
  --output "$PWD/TestResults/WebPlatformSubset-engine-matrix"
```

Use `libhtmlml_native_engine.so` on Linux and `htmlml_native_engine.dll` on Windows.
The two result files are written below `managed/` and `native/`; each records its
engine identity. The native cache option preserves the spike's persistent V8
compilation-unit cache in the test lane.

Useful diagnostic forms:

```sh
# See the manifest selection without loading V8.
dotnet run --project tests/WebPlatformSubset/runner/HtmlML.WebPlatformSubset.Runner.csproj \
  -c Release -- --selection all --list

# Run one family or path substring.
dotnet run --project tests/WebPlatformSubset/runner/HtmlML.WebPlatformSubset.Runner.csproj \
  -c Release -- --selection all --test css-transforms

# Report candidate behavior without making candidate failures gate the command.
dotnet run --project tests/WebPlatformSubset/runner/HtmlML.WebPlatformSubset.Runner.csproj \
  -c Release -- --selection candidate
```

Before promoting a first-party neutral contract, run the unchanged document directly
in Chrome as an independent browser authority. The output directory must be new:

```sh
node tests/WebPlatformSubset/chrome/run-contracts.mjs \
  --path contracts/dom-event-expando-dispatch.html \
  --path contracts/intersection-observer-scroll-root.html \
  --output "$PWD/TestResults/WebPlatformSubset-chrome-contracts"
```

The runner records Chrome's exact version, document and assertion denominators,
runtime exceptions, diagnostics, and per-contract results. This direct check prevents
a framework composition that happens to work in Chrome from silently authorizing a
neutral contract whose asserted semantics differ from Chrome.

For a single engine, the stable result file is written to
`TestResults/WebPlatformSubset/results.json`.
Failed reftests also produce `actual.png`, `reference.png`, and `diff.png`. Required
failures return a nonzero exit code; candidate-only failures remain report-only.
Native result documents use the versioned `htmlml-wpt-subset-result-v2` contract and
embed the native engine ABI and library SHA-256. Certification accepts the artifact
only when that identity exactly matches the supplied hosted-component evidence and
when its result paths are the exact required set from the audited profile. Matching
aggregate counts, a stale native result, or a substituted test document cannot satisfy
the standards gate.

## Current adapter boundary

Both engines consume the same prepared, pinned WPT source. The assertion adapter
supports self-contained local `testharness.js` documents and
resolves relative stylesheet links against each test's directory when the target is
present in the pinned upstream provenance manifest. The
rendering adapter supports exact-match local reftests at an 800×600, DPR 1 viewport.
It does not currently implement the WPT HTTP server, general testdriver/WebDriver
actions, remote origins, physical-device input, fonts, or all shared WPT helper scripts.

The native adapter currently supports DOM load/evaluation, V8 script execution,
pointer move/click, keyboard dispatch, frame pumping, and immutable-scene capture.
Its selected testdriver translation currently exposes Tab focus movement; richer keyboard
sequences are covered by the hosted-component scenarios until they are deliberately added
to this bounded WPT adapter. Native testharness load/timer completion, full canvas replay,
full SVG replay, and text/font pixel parity remain adapter work; their failures must not
be converted to managed fallback behavior.

The next high-value adapter addition is the small flexbox support stylesheet needed
by a selected dynamic flex alignment case. Input actions are intentionally limited
to the element-origin mouse move, primary click, and WebDriver Tab key used by the
selected hover and focus-visible cases; broader action sequences remain out of scope.
