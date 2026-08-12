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

Current Chrome and native evidence (2026-08-12): both engines pass 24/24 consumer
documents and 699/699 selected assertions. Historical three-engine evidence from 2026-07-23 had
Chrome, the former managed adapter, and native at its then-current denominator. Twenty-one
documents execute 673 unchanged official-source cases: all 502
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
behavior. Native consequently passed all 450/450 assertions at that checkpoint. The computed
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
Chrome, managed, and native consequently passed the complete 450/450 denominator.
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

Thirteen documents now execute all 502 cases from Bootstrap 5.3.8's unmodified
`alert.spec.js`, `base-component.spec.js`, `button.spec.js`, `carousel.spec.js`, `collapse.spec.js`,
`dropdown.spec.js`, `jquery.spec.js`, `modal.spec.js`, `offcanvas.spec.js`, `popover.spec.js`,
`scrollspy.spec.js`, `tab.spec.js`, and
`toast.spec.js` with their unmodified fixture
helper. `upstream-sources.json` pins and inventories all 14 Bootstrap unit files, all
24 jQuery QUnit unit files, and all 128 React DOM Jest files at their exact official
tags and commits. It selects thirteen Bootstrap files and three jQuery files, leaving 1,
21, and 128 files respectively classified as harness-blocked. Vendored selected bytes,
licenses, and support files carry SHA-256 pins, and the build fails if those bytes
drift. Evidence: `artifacts/ecosystem-consumers-chrome-jquery-css-v1-20260723/`,
`artifacts/ecosystem-consumers-managed-jquery-css-v3-disconnected-20260723/`, and
`artifacts/ecosystem-consumers-native-jquery-css-v2-cssom-20260723/`. Latest aggregate
evidence is
`artifacts/ecosystem-consumers-font-relative-box-final-v1-20260723/ecosystem-results.json`;
the matching Chrome, managed, and native per-engine results are retained beneath that
evidence directory.

The modal tranche adds 60 unchanged assertions for visibility, focus trapping,
backdrop and keyboard policy, scroll locking, resize, data APIs, transition lifecycle,
ARIA state, and instance disposal. Its one native divergence reduced to the neutral
`dynamic-transition-style-task-order.html` contract: `innerHTML`-parsed transition
longhands survive recascade and computed time values serialize in seconds, while a
synthetic click retains ordinary timer task ordering. The engine fix preserves inline
origin and per-longhand `!important` precedence without adding hot DOM-node storage.

The adjacent offcanvas tranche adds 50 unchanged assertions for configuration,
responsive resize dismissal, focus trapping, backdrop and keyboard policy, scroll
locking, transitions, data APIs, ARIA state, jQuery dispatch, and disposal. A generic
Jasmine task boundary prevents already-queued transition cleanup from contaminating the
next spec's spies. Its one native divergence reduced to
`responsive-overlay-resize-dispatch.html`: JavaScript synthetic resize dispatch now
uses the same outer-Window listener registry as host resize input, including once,
passive, AbortSignal, capture, and object-listener semantics.

The Carousel tranche adds all 66 unchanged assertions for navigation, keyboard and RTL
direction, touch and pointer swipes, interval cycling, pause/resume, wrapping,
visibility policy, indicators, slide events, delegated data APIs, jQuery dispatch, and
disposal. It uses Bootstrap's exact `hammer-simulator` test prerequisite at a locked
version. The native-only divergence reduced to `document-create-event-init.html`:
the bounded legacy `Document.createEvent("Event")` path returns a real `Event`, and
`Event.initEvent()` initializes and reinitializes its type, bubbling, cancelability,
propagation, and cancellation state. Unsupported legacy interface names fail with
`NotSupportedError`; no Carousel-specific runtime path was added.

The adjacent ScrollSpy tranche adds all 40 unchanged assertions for target discovery,
forward and backward bounded scrolling, hidden-section filtering, active navigation,
smooth scrolling, Unicode fragments, data APIs, lifecycle, and jQuery dispatch. Four
Chrome-authorized product-neutral reductions close the native divergences: bare fragment
URLs expose an empty anchor `hash`; the standard `hidden` attribute suppresses and
dynamically restores layout; programmatic offset changes queue and coalesce one scroll
event while retaining immediate geometry and unchanged-offset idempotence; and
`CSSStyleDeclaration.getPropertyValue("position")` exposes the connected scroller's
computed position so child-relative offsets are not mixed with document-relative ones.
No ScrollSpy-specific runtime path was added.

The adjacent jQuery-integration tranche adds both unchanged assertions for all twelve
Bootstrap component plugin registrations and namespaced jQuery event delivery through
the Alert data API. Its adapter establishes jQuery before loading the original source
and waits for the same `DOMContentLoaded` boundary Bootstrap uses to install plugins,
eliminating browser-scheduling dependence without modifying the upstream test. Chrome
and native both pass, and there are no production source or artifact changes in this
tranche.

The Popover tranche adds all 31 unchanged assertions for content/title resolution,
template reuse, custom classes, manual and multi-trigger show/hide behavior, instance
lifecycle, and the jQuery interface. Discovery exposed an adapter-ordering divergence:
the bounded inter-spec task drain ran before `afterEach`, allowing a resolved fixture's
queued transition lifecycle to restart and overlap the next spec. Cleanup now runs at
Jasmine's actual boundary before the drain, while spies remain installed until queued
cleanup finishes. Chrome and native pass the full lane after the change; no Popover-
specific or production runtime path was added, and pixel parity remains outside this
functional claim.

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
