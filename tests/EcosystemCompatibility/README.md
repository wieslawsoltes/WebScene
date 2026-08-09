# WebScene ecosystem-consumer compatibility lane

This non-gating discovery lane composes the certified browser primitives through
real, version-pinned JavaScript component stacks. It complements WPT; it does not turn
a framework pass into a standards claim.

The first bounded profile contains:

- jQuery 4.0.0: selectors/traversal, DOM mutation and deep cloning,
  attributes/properties, CSSOM, delegated events, single/multiple-value forms, and
  Deferred callbacks;
- Bootstrap 5.3.8: tabs, collapse, dropdown/Popper placement, modal lifecycle,
  tooltip, popover, offcanvas, ScrollSpy, carousel transitions, custom events,
  classes, and ARIA state;
- React DOM 19.2.8: `createRoot`, reconciliation, synthetic events, keyed reorder,
  controlled inputs, portals, batching, transitions, Suspense resolution, hydration
  reuse, and unmount cleanup.

Current native evidence (2026-08-09): the native engine passes 18/18 consumer documents
and 450/450 selected assertions. Historical three-engine evidence from 2026-07-23 had
Chrome, the former managed adapter, and native at the same denominator. Fifteen
documents execute 424 unchanged official-source cases: all 253
selected Bootstrap cases, all 51 dynamically registered cases from jQuery 4.0.0's
unmodified `callbacks.js`, 65 selected browser-local cases from its unmodified
`attributes.js`, and 55 selected browser-local cases from its unmodified `css.js`.
The remaining 26 assertions are the three owned composition fixtures.

The four new CSS shards preserve the exact upstream `css.js`, official fixture markup,
and official `testsuite.css`; only two Shadow DOM and three separately served iframe/
zoom cases are explicitly harness-blocked. They independently exercise declaration
validity and feature detection, computed style, detached construction, show/hide/toggle,
cascade display, relative values, unit conversion, box geometry, custom properties,
and rendered visibility. Reducing the first failures produced the product-neutral
`cssom-inline-declaration-validity.html` contract and fixed eight upstream assertions
in each WebScene adapter. A subsequent display-lifecycle reduction closes eight more
unchanged native assertions by distinguishing inline, inline-block, and list-item and
making offset geometry ancestor-aware. SCRIPT raw-text tokenization, selector API
SyntaxError semantics (including borrowed prototype methods), Window.name, and hidden
input rendering then close the unchanged toggle and `:visible`/`:hidden` failures in
both adapters. A neutral six-assertion Grid placement CSSOM reduction subsequently
closes the unchanged `grid-area`/`grid-row-start` assertion in both adapters through
cascade-correct shorthand expansion and computed serialization. The exact pinned CSS2
`auto-margins-used-values.html` WPT and a dynamic CSSOM/geometry reduction then close
the originating computed-margin case, percentage-used-value checks in both adapters,
and native negative margin assignment. A pinned 73-assertion CSS Variables WPT plus a
six-assertion Chrome-authorized reduction then close the complete unchanged jQuery
custom-property case. The shared fixes preserve case-sensitive names, CSS-token
whitespace, importance and overwrite rules, detached STYLE activation, and the
CSSStyleDeclaration named-property fallback. The subsequent box-edge reduction covers
padding shorthand/longhand mutation and removal, border
shorthand/style/width/physical-side composition, content-box computed dimensions, and
synchronous border-box geometry. Chrome and both WebScene adapters pass its nine
assertions, and the complete unchanged jQuery numeric box-edge allowlist now passes
natively. The next two neutral reductions distinguish computed dimensions from
suppressed geometry beneath `display:none` and preserve numeric z-index CSSOM values
through connection, stylesheet recascade, negative mutation, and removal. Direct
Chrome and both adapters pass all seven assertions. Correct z-index then exposed and
fixed retained-canvas host grouping without introducing application-specific runtime
behavior. Native consequently passes all 450/450 selected assertions. The computed
CSSStyleDeclaration named-property reduction then closes the managed
unsupported-property return assertion by distinguishing method fallback from supported
IDL aliases. The detached computed-style reduction then makes snapshot reuse sensitive
to tree connectivity, so disconnected empty values cannot survive reattachment and
stylesheet display:none recascades correctly. The subsequent initial-iframe reduction
provides a synchronous source-less `about:blank` Window/Document, stable cross-realm
`contentDocument`/`defaultView`/`frameElement` identity, bounded
`document.open()/write()/close()` body replacement, and hidden-frame computed style.
It closes the unchanged frame-element CSS assertion. At that checkpoint one managed
failure remained as visible discovery debt in bounded relative unit conversion. The subsequent
Chrome-authorized font-relative box reduction resolves that final assertion without
jQuery-specific behavior: opposing percentage insets remain independent, dynamic
percent-to-em replacement and four-value `inset` follow inherited font-size mutation,
and width, min-height, padding, gap, and flex-basis expose consistent pixel values.
Chrome, managed, and native consequently pass the complete 450/450 denominator.
Evidence:
`artifacts/ecosystem-consumers-font-relative-box-final-v1-20260723/` and
`artifacts/web-platform-required-font-relative-promoted-v1-20260723/`.
The connected used-value reduction now independently passes Chrome, managed, and
native for percentage insets/margins, opposing auto margins, and fractional geometry
and is required in the component profile. The Grid reduction is also required after
Chrome, managed, and native passed 6/6; the unchanged jQuery totals moved by exactly
one assertion in each adapter, providing an end-to-end discovery-to-fix proof while
leaving unrelated composition failures visible.
The auto-margin promotion is required after Chrome passed the 4/4 neutral composition
and both adapters passed both it and all 6/6 assertions in the unchanged pinned WPT.
The generic WPT adapter also gained BODY `onload` startup support so future
check-layout documents can run unchanged.

Seven documents execute all 253 cases from Bootstrap 5.3.8's unmodified
`alert.spec.js`, `base-component.spec.js`, `button.spec.js`, `collapse.spec.js`,
`dropdown.spec.js`, `tab.spec.js`, and `toast.spec.js` with their unmodified fixture
helper. `upstream-sources.json` pins and inventories all 14 Bootstrap unit files, all
24 jQuery QUnit unit files, and all 128 React DOM Jest files at their exact official
tags and commits. It selects seven Bootstrap files and three jQuery files, leaving 7,
21, and 128 files respectively classified as harness-blocked. Vendored selected bytes,
licenses, and support files carry SHA-256 pins, and the build fails if those bytes
drift. Evidence: `artifacts/ecosystem-consumers-chrome-jquery-css-v1-20260723/`,
`artifacts/ecosystem-consumers-managed-jquery-css-v3-disconnected-20260723/`, and
`artifacts/ecosystem-consumers-native-jquery-css-v2-cssom-20260723/`. Latest aggregate
evidence is
`artifacts/ecosystem-consumers-font-relative-box-final-v1-20260723/ecosystem-results.json`;
the matching Chrome, managed, and native per-engine results are retained beneath that
evidence directory.

Every selected, harness-blocked, or excluded upstream file remains listed in
`upstream-sources.json` and summarized in `ecosystem-profile.json`. A failure must be
reduced to an upstream WPT or a product-neutral WebScene contract before changing an
engine primitive.

Install the exact lock and run Chrome plus the native WebScene engine:

```sh
cd tests/EcosystemCompatibility
npm ci
npm test -- \
  --engine all \
  --native-library /absolute/path/to/libwebscene_native_engine.dylib \
  --output /absolute/path/to/new/evidence-directory
```

The output contains raw per-engine documents plus
`ecosystem-results.json`, whose separate selected, runnable, excluded, passed, and
failed denominators make discovery gaps visible. Discovery failures do not weaken the
required WPT profile and do not become release gates until explicitly promoted.
