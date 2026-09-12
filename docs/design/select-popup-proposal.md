# Proposal: real dropdowns for HTML select

Status: **Proposed; awaiting design agreement.**

Tracks [issue #44](https://github.com/SceneTech/WebScene/issues/44).
This document is a design proposal, not an implementation. It changes no runtime
behavior and does not close that issue. Source inspection used WebScene revision
`e6e7fe20eb98048b9b0f19dd5d0676e5e378505c` on 2026-09-11.

## Recommended decision

Implement the popup for an ordinary collapsed `<select>` inside WebScene, with a
shared native C++ interaction controller and an engine-owned scene presenter.
Use a private, build-time-compiled HTML/CSS template for the popup where it can
reuse the existing compiler and common document primitives without reversing
library dependencies. No JavaScript is required by the control.

Keep behavior separate from presentation, so a host-native presenter can be
added later without changing selection semantics. Do not ship a second presenter
or publish a general control-library/template API in this first feature.

Applications keep using standard markup:

```html
<label for="view">View</label>
<select id="view">
  <option value="top">Top</option>
  <option value="front">Front</option>
  <option value="iso">Isometric</option>
</select>
```

No Kestrel-specific replacement, custom element, JavaScript shim, or application
registration is required. The result should also work for other applications
using the same HTML controls.

## What the current source establishes

- [The tracked defect](https://github.com/SceneTech/WebScene/issues/44) identifies
  `apply_non_checkable_form_control_click_default` in
  `experiments/WebScene.NativeEngine.Probe/native/webscene_v8_runtime_tasks.inc`
  as the deliberate click-to-advance fallback, and identifies the collapsed-only
  painter in `webscene_native_dom_scene.inc`.
- [The existing input contract](../../tests/WebPlatformSubset/contracts/html-single-select-input.html)
  expects a click to advance the value immediately and produce `click`, `input`,
  and `change`. That shortcut-specific assertion must be replaced, not treated
  as proof of browser dropdown behavior.
- [The direct native document API](../../src/WebScene.NativeWeb/src/native_web.cpp)
  already handles select values and bounded ArrowUp/ArrowDown/Home/End selection
  in C++. Its activation path is separate from the V8 runtime. Fixing only the
  V8 path would not give native applications the same popup behavior.
- [Shared form helpers](../../experiments/WebScene.NativeEngine.Probe/native/webscene_native_form_state.h)
  already hold common option collection, selectedness, value lookup, and value
  assignment logic. New interaction logic should extend the shared layer rather
  than create another authoritative selection model.
- [The compiled Kestrel sample](../../samples/NativeKestrel/hybrid/README.md) is an
  integration consumer. The engine fix must not require rewriting its controls.

## Alternatives and tradeoffs

| Approach | Benefit | Cost or limitation | Recommendation |
| --- | --- | --- | --- |
| Host-native option picker | Platform conventions, platform picker accessibility, and potentially an out-of-window popup surface | Each host must implement and qualify presentation, geometry conversion, cancellation, focus, threading, and lifetime; platform appearance may not follow app CSS | Preserve as a future option, not the only implementation |
| Shared scene-rendered popup | One interaction/presentation path across native and hybrid apps; reproducible headless input and rendering tests; application-consistent appearance | WebScene must implement keyboard navigation, scrolling, layering and accessibility semantics; an in-surface popup cannot extend outside that surface | Recommended default |
| Public XAML-like control/template framework | Broad future component reuse | Makes one missing HTML feature depend on a public styling, component, binding and lifecycle design | Defer |

These are design tradeoffs, not benchmark results. A host-native presenter would
be a legitimate alternative when platform-native appearance or out-of-surface
presentation is a product requirement. It is not equivalent to delegating the
DOM control itself to an OS menu.

The HTML Standard defines option state and notifications without requiring one
OS widget implementation. Its newer base-appearance select work also separates
picker presentation from the underlying select. That is a useful architectural
precedent, not a claim that WebScene implements customizable select or its CSS.
See the [select specification](https://html.spec.whatwg.org/multipage/form-elements.html#the-select-element)
and [Open UI explainer](https://open-ui.org/components/customizable-select.explainer/).

## Ownership and dependency boundary

```text
Application <select> / <option> / <optgroup>
                    |
        Shared WebScene select controller
        - authoritative DOM option state
        - pending highlight and interaction lifetime
        - validated commit / cancel
                    |
             private presenter boundary
                    |
       Built-in compiled-template scene popup
                    |
          normal host scene composition
```

The shared controller belongs below both the direct native document API and the
V8 adapter. It must not depend on V8, AppScene, Avalonia, Uno, or an OS UI toolkit.
Those adapters translate input and dispatch their appropriate existing events;
they must not each implement their own selection algorithm.

WebScene continues to own DOM, form behavior, layout and popup scene content.
AppScene and other hosts continue to own windows, platform input and composition.
A future native presenter would implement a narrow service contract defined by
WebScene; WebScene must not acquire a build-time dependency on AppScene.

The common engine must not instantiate a second high-level native document to
render its own control. If existing template output is tied to the upper
`native_web::document` facade, introduce only the small shared build adapter
needed to target common document primitives, or initially use those primitives
directly. Do not introduce a Core-to-NativeWeb dependency cycle merely to use a
particular template or module format. C++ module packaging is an implementation
choice; it is not the control architecture.

## Internal template, not an application-authored replacement

Keep the select and its actual options in the authored DOM. The popup visuals
are an engine-owned representation of those options, not new authored children
that change `select.options`, form submission, DOM queries or application
mutation observers.

The private visual tree needs explicit ownership, style isolation, input
retargeting and accessibility mapping. Calling it a template does not create
those guarantees automatically. This proposal does not require general public
Shadow DOM support or a new public component system.

Bind option labels as text, including the applicable `label` attribute and
optgroup labels. Do not assemble HTML strings from option text. Preserve option
identity even when labels or values are duplicated. Respect the control's
relevant computed appearance, typography and direction through an explicit
styling contract; arbitrary application `div` or `button` rules must not break
internal popup rows.

## Interaction contract to implement

Scope is a single-selection, collapsed select: no `multiple` and an effective
display size of one. Do not route `size > 1` listboxes into a dropdown, and do not
regress existing multiple-selection state. Editable comboboxes and datalist are
not part of this feature.

Opening a picker must not select the next item. Keep the live selection separate
from the popup's pending highlighted option. Primary pointer selection and
keyboard acceptance commit a valid option; Escape cancellation changes nothing.
Selecting the already-selected option must not invent a change. Compare option
identity/selectedness, not just the value string, because distinct options can
have identical values.

Support keyboard opening, arrows, Home/End, page navigation, type-ahead, scrolling
and bringing the highlighted option into view. Disabled selects, disabled
options and disabled optgroups must remain noninteractive as appropriate. Empty
and all-disabled lists must be safe. Keep selected and highlighted presentation
conceptually distinct.

For the proposed scene profile, outside-click dismissal cancels pending choice
and consumes the dismissing pointer gesture so it cannot accidentally operate a
CAD canvas underneath. Focus should remain associated with the select while
open and be restored only when that remains valid; never steal focus from a
replacement dialog or a newly focused control after application callbacks.

Before changing event assertions, record browser/platform traces for opening
(pointerdown versus pointerup), closed-arrow changes, Tab, Enter/Space,
Alt+arrows/F4, outside dismissal, focus and event timing. Document deliberate
profile differences. The [WAI select-only combobox example](https://www.w3.org/WAI/ARIA/apg/patterns/combobox/examples/combobox-select-only/)
is useful guidance, but explicitly differs from native select in some behavior;
it is not a universal browser conformance oracle.

Honor canceled default actions and distinguish routed user input from synthetic
`.click()`/`dispatchEvent`. This feature must not claim to implement `showPicker()`
unless its separate activation and error contract is implemented and tested.
Programmatic value/selectedIndex updates must not themselves synthesize user
input/change events. Commit through the shared selection machinery, then issue
appropriate input/change notifications through the owning adapter, with
reentrancy-safe lifetime checks.

## Popup lifetime and mutation safety

Use stable document/control/option identities and a popup generation, not a raw
pointer or an unchecked selected index. All DOM access stays on the document's
owner thread; queued input and any future host completion are validated there.

For the first implementation, cancel the popup on relevant option-list, label,
selectedness, disabled-state or control-visibility mutations rather than leave
an obsolete interactive snapshot visible. That policy must cover both C++ and
JavaScript mutations, navigation, removal, disposal, form reset and reopening.
Unrelated document mutations must not continually dismiss the popup.

Late input/completion from a canceled generation must be ignored. Revalidate
membership, enabled state and control lifetime before committing, and recheck
lifetime after application event callbacks. Cancellation must not restore an
old snapshot over a newer application-assigned value. There should be at most
one active select popup per presented document/host interaction context, with
explicit nesting behavior around dialogs and embedded documents.

## Geometry and composition

A popup must be in an engine-owned top-layer/overlay pass, not an ordinary child
with a very large `z-index`. It must escape ancestor overflow clips and stack
correctly above WebGPU/video content while respecting modal/inert boundaries.
Existing overlay behavior can be reused after its ownership and ordering rules
are checked; do not assume every fixed-position element already satisfies this.

Anchor to the final transformed control bounds in viewport logical coordinates.
Apply display scale exactly once at the host boundary. Measure the popup, choose
above/below placement, clamp it to the available presentation surface, and bound
its height with scrolling. Recompute geometry on scrolling, resize, display-scale
changes or anchor movement; cancel when the anchor is no longer present/visible.
RTL alignment must be intentional.

The initial scene popup stays inside its WebScene presentation surface. Escaping
an embedded view or native window would require a separate host popup surface
and is not supplied by this design. Preserve GPU image ownership and retirement;
no CPU readback, separate webview, or special video overlay should be needed to
paint the dropdown.

## Accessibility is required work, not a free consequence of HTML

Represent the labeled combobox, expanded/collapsed state, associated listbox,
active option, selected options, disabled state and groups in the engine's
accessibility model. Retarget private template visuals to those semantics.

A native application does not gain OS accessibility simply by adding ARIA to a
private template. Each supported host must expose and verify the corresponding
platform accessibility information/actions. Record missing host bridges as
explicit gaps; do not call the control fully accessible or close all of #44 on
the strength of keyboard tests or a scene screenshot. A native-menu presenter
would still need an accessible owning select and correct focus integration.

## Implementation and acceptance plan

1. Add shared controller/model tests and replace the click-to-cycle contracts.
2. Implement the private scene presenter and overlay/input integration for both
   native and hybrid routes, without an application opt-in.
3. Add engine/host interaction tests, then qualify unchanged Kestrel behavior.
   Keep #44 open until its acceptance requirements are actually evidenced.

Required test coverage:

- Real open, non-adjacent choice, same-option choice and cancellation; correct
  selectedness/value/index and event order/count in both adapters.
- Prevented defaults; disabled select/option/optgroup; duplicate values; empty
  lists; type-ahead and keyboard scrolling; retained listbox/multiple behavior.
- Removal/reopen/navigation/disposal, mutation during an open popup, and
  application handlers that mutate or remove nodes during commit.
- Nested overflow and transforms, viewport edges, RTL, high DPI, resize,
  scrolling, modal/inert content, and no pointer/wheel leakage into the canvas.
- Headless pointer/key tests that actually open and operate the same scene
  popup, not a fake presenter or programmatic value assignment alone.
- Scene output plus real host composition above WebGPU/video, focus and
  platform accessibility checks. Headless success does not establish those.
- Unchanged Kestrel controls: open the list, choose a non-adjacent view/style or
  property option, and verify the resulting application behavior.

Use relevant WPT/browser comparisons for DOM semantics. Keep engine-specific
popup affordance tests in the native/framework harness; a WebScene-only contract
must not be relabeled as upstream browser conformance. Report platforms actually
run separately from portable code/build support.

## Decision requested

Approve **shared scene-rendered select dropdowns with native C++ behavior and a
private compiled visual template**, leaving host-native presentation and a public
control library for later. The main product tradeoff to accept now is consistent,
app-themed, in-surface popup rendering rather than an OS-native popup by default.
