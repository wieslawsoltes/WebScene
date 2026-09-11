# Remaining 55 CSS constructs: integrated implementation plan

Status: active. Requested as one integrated implementation pass, without stopping
for approval or treating individual commits as completion. Preserve original
Kestrel HTML/CSS and use independent fixtures to develop each feature family.
Baseline: native-web-css-checkpoint.txt, 397 rules / 1475 declarations, 55 distinct
unsupported constructs. These are distinct source usages, not 55 separate engines.

## Architecture and boundaries

### CSS architecture review

The compiler audit currently reports 30 distinct unsupported source constructs,
not 30 isolated parser changes or a percentage of browser compatibility. Continue
using the original stylesheet as evidence, but separate compiler emission gaps
from missing native layout, paint and interaction behavior.

The preferred direction is to reuse WebScene's existing Rust CSS parser and native
cascade for both parsed and compiled documents. Build-time preparation should
emit stylesheet data into generated C++ modules, consumed by the same native
engine. Do not expand a second implementation of CSS semantics as the long-term
architecture. The extracted prepared_stylesheet, stylesheet_owner and
native_style_session now connect to native_web through an explicitly selected
stylesheet resolver (see the integration checkpoint below). The default application
still uses typed rules; the shared adapter does not prove parser-free execution. Declaration values,
media conditions and functional selector arguments still contain text interpreted
by the shared machinery; emitting these records alone would not satisfy the
no-runtime-CSS-parsing requirement.

An embedded-CSS runtime-parser route is a proposed intermediate comparison mode,
not a change to the final acceptance contract. It can establish which original UI
features the shared engine actually renders before completing build-time lowering.
Keep HTML construction and dynamic templates compiled in every mode. Keep the
default application parser-free until an alternative mode is explicitly selected.

Next integration gates:

1. Define the prepared-style handoff and connect native document mutation, focus,
   hover, preferences, viewport changes and disposal to the shared style session.
2. Compare parsed preparation and generated preparation on identical native trees,
   including dynamic updates; retain unsupported-feature diagnostics.
3. Complete typed value/selector/media preparation wherever shared execution still
   reparses text, then verify the packaged binary's parser dependencies.
4. Address actual engine gaps once for both authoring paths, and test original
   Kestrel behavior and presented frame performance separately from syntax coverage.

WebScene owns compiler lowering, native style/cascade/layout/paint, animation and
DOM APIs. Foco supplies cursor/window/input, preference projection, compositor and
frame scheduling. Reuse native engine behavior where verified; accepted syntax
without observable behavior does not count as support. Emit typed expressions,
tracks, keyframes and paint records; no runtime CSS or HTML parsing. Keep generated
C++ modules and compiled template APIs. Native C++ remains the application model.

## Dependency-ordered implementation batches

1. **Typed values and paint data (P).** Extend the existing expression evaluator
   to two-color sRGB mixing and color-valued functions in shadows/gradients. Compile
   gradient stops and angles, preserving variable dependency recomputation.
   Add dashed borders, inset shadows, outline and outline-offset to native style
   writers and paint geometry. Define clipping, spread, negative offsets and
   currentColor behavior; keep outlines outside layout sizing. Add brightness and
   backdrop blur using existing renderer effects if available, otherwise extend
   native scene/compositor effect records. Validate paint order, clipping, opacity
   and resource disposal. Do not substitute solid fills or ignore effects.
2. **Selectors and generated boxes (S).** Extend compiled selectors to target
   before/after/placeholder/backdrop style records. Compile content string values
   (including empty strings), maintain originating-element specificity/inheritance
   and pseudo-state updates. Generate before/after boxes through native engine
   layout and paint, placeholder through native editable-control rendering, and
   backdrop through dialog top-layer ownership. Check empty generated boxes,
   positioning, hit testing, modal lifecycle and removal.
3. **Layout and text (L).** Add typed grid line/span placement including negative
   end lines; resolve 1/-1 against explicit track count rather than hardcoded spans.
   Bridge collapsed table borders and verify shared edge conflict resolution.
   Implement anywhere wrapping and ellipsis in native shaping/layout/paint, covering
   intrinsic widths, clipping, Unicode, resize and mutation. Implement font-synthesis
   policy in shaping/font selection; none must disable synthetic faces.
4. **Interaction and control presentation (I).** Compile cursor keywords and expose
   inherited resolved cursor to Foco pointer hit testing. Add selection policies,
   touch-action gesture policy, vertical resize handles, scrollbar width/color,
   accent-color and color-scheme. Implement or connect native control rendering and
   event paths, keyboard accessibility, disabled states, pointer capture and cleanup.
   Expose host preference/theme data without JS. Verify real host interaction as well
   as native event contracts; storing unused style fields is insufficient.
5. **Conditions and animation (A).** Compile media conditions as typed predicates:
   screen/print and prefers-reduced-motion. Screen is the normal window medium;
   expose print context explicitly rather than deleting print rules. Compile
   keyframes, shorthand timing/duration/iteration/fill and animation:none. Implement
   transitions for fill and none with native frame-clock interpolation, variable
   changes, cancellation and reduced-motion updates. Schedule via existing Foco
   display driver; no independent timer. Inspect keyframe declarations and any
   newly exposed unsupported contents recursively.
6. **Integrated verification (V).** Re-run the complete original stylesheet in
   strict mode: zero omissions, no preview flag, and every listed construct below
   has behavior evidence. Compile the original document and all predefined UI
   templates; resolve additional diagnostics rather than hiding them. Rebuild
   no-JS macOS application, verify linkage/assets, run original-browser versus
   native layout and image comparisons at multiple window sizes, light/dark modes,
   hover/focus/selection/dialog states and reduced-motion settings. Inspect real
   cursor, wheel, resize and shutdown behavior. Keep performance measurements
   separate from visual correctness and inspect frame presentation rather than
   submission timing alone.

## Per-construct ledger

Every row starts open. Closure requires the batch implementation plus its concrete
regression and integrated evidence; update this ledger with evidence as work lands.

| ID | Construct | Batch | Status |
|---|---|---|---|
| 01 | `#command-input::placeholder` | S | Open |
| 02 | `#ribbon-tab-list button.active:after` | S | Open |
| 03 | `.document-tab.active:after` | S | Open |
| 04 | `.panel-tabs button.active:after` | S | Open |
| 05 | `.progress-line:after` | S | Open |
| 06 | `.property-section-title:before` | S | Open |
| 07 | `@keyframes progress` | A | Open |
| 08 | `@keyframes toast-in` | A | Open |
| 09 | `@media (prefers-reduced-motion:reduce)` | A | Open |
| 10 | `@media print` | A | Open |
| 11 | `accent-color:var(--accent)` | I | Open |
| 12 | `animation:none` | A | Open |
| 13 | `animation:progress 1.2s infinite ease-in-out` | A | Open |
| 14 | `animation:toast-in .15s ease-out` | A | Open |
| 15 | `backdrop-filter:blur(3px)` | P | Open |
| 16 | `backdrop-filter:blur(9px)` | P | Open |
| 17 | `background:color-mix(in srgb,var(--active) 63%,var(--panel))` | P | Implemented; native paint checks pass; integrated comparison pending |
| 18 | `background:linear-gradient(125deg,var(--panel2),var(--panel))` | P | Implemented for source form; compiler/native/Foco tests pass; integration pending |
| 19 | `background:linear-gradient(145deg,var(--panel2),var(--panel))` | P | Implemented for source form; compiler/native/Foco tests pass; integration pending |
| 20 | `border-collapse:collapse` | L | Open |
| 21 | `border:1px dashed #65c5a4` | P | Implemented; compiler/native/Foco raster tests pass; integration pending |
| 22 | `border:2px dashed var(--accent)` | P | Implemented; compiler/native/Foco raster tests pass; integration pending |
| 23 | `box-shadow:0 0 0 1px color-mix(in srgb,var(--accent) 25%,transparent)` | P | Implemented; compiler/native paint tests pass; integration pending |
| 24 | `box-shadow:inset 0 -2px 0 var(--accent)` | P | Implemented; native command and Foco raster checks pass; integration pending |
| 25 | `box-shadow:inset 0 0 0 1px var(--accent-dim)` | P | Implemented; native command and Foco raster checks pass; integration pending |
| 26 | `box-shadow:inset 2px 0 0 var(--accent)` | P | Implemented; native command and Foco raster checks pass; integration pending |
| 27 | `color-scheme:dark` | I | Open |
| 28 | `color-scheme:light` | I | Open |
| 29 | `content:""` | S | Open |
| 30 | `content:"⌄"` | S | Open |
| 31 | `cursor:col-resize` | I | Open |
| 32 | `cursor:crosshair` | I | Open |
| 33 | `cursor:grabbing` | I | Open |
| 34 | `cursor:move` | I | Open |
| 35 | `cursor:not-allowed` | I | Open |
| 36 | `cursor:pointer` | I | Open |
| 37 | `cursor:row-resize` | I | Open |
| 38 | `dialog::backdrop` | S | Open |
| 39 | `filter:brightness(1.1)` | P | Open |
| 40 | `font-synthesis:none` | L | Open |
| 41 | `grid-column:1/-1` | L | Open |
| 42 | `outline-offset:-2px` | P | Implemented for source form; native geometry tests pass; full semantics/integration pending |
| 43 | `outline:2px solid var(--accent)` | P | Implemented for source form; native geometry tests pass; full semantics/integration pending |
| 44 | `outline:none` | P | Implemented for source form; native geometry tests pass; full semantics/integration pending |
| 45 | `overflow-wrap:anywhere` | L | Open |
| 46 | `resize:vertical` | I | Open |
| 47 | `scrollbar-color:var(--line) transparent` | I | Open |
| 48 | `scrollbar-width:none` | I | Open |
| 49 | `scrollbar-width:thin` | I | Open |
| 50 | `text-overflow:ellipsis` | L | Open |
| 51 | `touch-action:none` | I | Open |
| 52 | `transition:fill .15s` | A | Open |
| 53 | `transition:none` | A | Open |
| 54 | `user-select:none` | I | Open |
| 55 | `user-select:text` | I | Open |

## Completion gates beyond this CSS batch

Finishing these 55 usages does not itself finish the thread goal. After compiler
coverage and browser comparisons, complete Kestrel command/state/geometry/viewmodel
logic in C++, use compiled templates for all dynamic UI, embed resources, and prove
native WebGPU initialization without V8. Benchmark panning and live window resizing
at 60fps with presentation traces and no stretched stale-frame elastic effect.
Keep Foco's optimized display scheduling and GPU surface integration. Do not claim
full Kestrel parity from this CSS inventory or a static diagnostic preview.

## Implementation evidence

- Initial paint batch: native premultiplied-alpha sRGB mixing with typed variable
  colors, default/complementary weights, normalization and alpha scaling when
  weights sum below 100%. Compiler tests cover two variable colors and weight
  grammar; native scene checks cover red/blue 25/75 and 25/25 mixtures, with class
  removal restoring earlier paint. Zero-total weights are rejected. Nested
  color-valued expression trees for gradients/shadows remain pending.
- Literal HSL/HSLA support was completed alongside the initial batch: hue units,
  wrapping, percentage saturation/lightness, alpha and clamping, with native
  paint coverage. This improves the shared color prerequisites but is not counted
  as closure of additional baseline rows.
- The complete 55-row batch is not finished. Keep all subsequent batch gates
  active; do not present these first changes as completion of the request.

- Outline batch: supported source forms lower to typed native outline style;
  signed offsets adjust painted bounds without changing layout. Native contracts
  verify outside/inset geometry, none, and restoration. Rounded outline radii
  incorporate signed offset; rounded pixel comparison, currentColor/default width,
  broader shorthand ordering and pseudo-element outline propagation remain pending.
- Outline follow-through: typed shorthand evaluation now supports component
  reordering, omitted color/width, thin/medium/thick and whole-value variables.
  Duplicate literal components fail compilation; invalid substituted components
  suppress the outline. Native currentColor resolves from the live foreground at
  paint time. Compiler and native contracts pass, including mutation from explicit
  color to default currentColor. Other outline styles, longhands and pseudo boxes
  remain pending; original source-form rows retain their integration gate.
- Shadow host prerequisite: Foco's DOM packet renderer ignored existing kinds
  17/18 (outer shadows). Added ordered/background/foreground handling and Skia mask
  blur. Raster tests verify both variants paint opaque centers and blurred exterior
  alpha. The Native Web Foco configuration now builds/runs that host paint suite
  as native_web_foco_paint; it passes. This fixes existing outer-shadow delivery;
  inset shadow geometry/commands are still pending. A stale standalone Foco build
  was not used for validation; tests ran against current Native Web dependencies.

- Inset shadows: typed inset flag, padding-box clipping and inverse rounded-hole
  shadow commands now connect to Foco's blur renderer. All three baseline inset
  forms compile. Native contracts check background/clip/shadow/restore order;
  Foco raster tests check hard inset edges, a transparent center, soft inward blur
  and no paint outside the clip. Shadow command kinds 17/18 reserve flags bit 0
  for this inverse path; non-Foco consumers need equivalent support before claiming
  embedding parity. Multiple shadows, arbitrary component ordering, elliptical
  corner/border details and browser comparisons remain open integration work.
- Shadow shorthand follow-through: color and inset can precede or follow the
  contiguous length group; omitted color and explicit currentColor resolve from
  the live foreground. Duplicate colors/inset and interrupted length groups are
  rejected, including after typed variable substitution. Compiler and native
  mutation contracts pass; a reordered inset shadow paints blue after a foreground
  change. Multiple shadows and non-pixel shadow lengths remain pending.

- Dashed borders: typed per-side dash state, rounded uniform perimeter and mixed
  straight-edge stroke commands connect to Foco dash path effects. Both baseline
  dashed declarations compile; native mutation checks verify solid resets dash
  state. Foco raster tests cover visible dash/gap runs for background/foreground
  rounded and line commands. DOM kinds 40–43 are documented separately from canvas
  opcodes. Compiler (103 tests), native contracts and Foco paint tests pass.
  Mixed-width rounded corners, elliptical metadata, exact browser dash distribution
  and non-Foco consumer support remain pending; no broad border parity claim.

- Color-valued shadow expressions now reuse shared compiled color-mix lowering.
  Generated code evaluates typed color variables and appends the resulting RGBA
  token to the shadow operands; no CSS string reaches a runtime parser. The exact
  baseline mix-shadow form and an inset two-color variant compile. Native mutation
  coverage verifies a 25% variable red mix emits ff000040 in an inset shadow.
  Compiler (104 tests) and native contracts pass. Nested custom-property function
  trees beyond this direct shadow component remain pending.

- Typed linear-gradient source forms now emit angle and RGBA stop records rather
  than CSS strings. Native contracts verify angle, stop count and live custom-color
  mutation; Foco raster tests verify direction, bounds and malformed-payload safety.
  Compiler (105 tests), native contracts and Foco paint tests pass. Current lowering
  supports equally spaced colors and degree angles; explicit stop positions,
  side/corner directions and broader gradient syntax remain pending. This is not
  full gradient parity or closure of the integrated comparison gate.

## Parser reuse decision (user clarification)

The compiler already links the existing Rust cssparser and selectors library
through the shared parser bridge. The Rust DeclarationParser adapter currently
returns declaration value strings, not a complete typed property-value model.
The 55 diagnostics must not be described as 55 missing Rust parser features.

Before adding further property-specific compiler parsing, classify each gap as
compiler exposure, shared value interpretation, or native/host behavior. Extract
existing runtime property interpretation into a V8-independent shared layer where
possible; have the compiler serialize typed values/expressions from that layer.
Ordinary parsed WebScene content should use the same interpretation and native
style APIs. Keep parser code in build tools for native-only applications.

Existing gradient handling illustrates the boundary: runtime cascade retains the
CSS image string and Foco's draw_dom_gradient interprets that string at paint time.
Reusing Rust syntax parsing alone cannot turn that path into parser-free compiled
paint. Move interpretation ahead of rendering and converge both producers on typed
paint records. Avoid growing a second independently maintained property grammar
in tooling/webscene-uic/main.cpp. Variable-dependent values must retain typed
expression structure, rather than freezing theme-dependent computed styles at build
time. Tests must compare parsed and compiled paths as well as native rendering.

- Reuse audit: collapsed table borders currently suppress spacing in native layout,
  but no shared-edge border conflict resolver was found in the native layout/scene
  implementation. Row 20 remains open; exposing border_collapsed alone would not
  satisfy its behavior gate.
- Shared shadow interpretation: extracted typed component validation and native
  style application into webscene_shadow_value.h. Both compiled variable evaluation
  and runtime CSS shadow tokenization now use that builder. This removes duplicate
  ordering/default-color logic and gives runtime content the same inset/currentColor
  semantics as compiled content. The runtime tokenizer remains separate; shared
  typed property parsing is not complete. Multiple shadows remain unsupported.
- Updated original-stylesheet audit after typed gradients: 397 rules, 1475
  declarations, 43 distinct unsupported constructs (baseline 55). This measures
  compiler acceptance only, not integrated visual parity or feature completion.

## Delivery alternative under consideration

Recommendation following the user's CSS-runtime question: retain compiled HTML
and predefined templates, C++ application code and embedded resources, but extract
and reuse WebScene's CSS runtime as the first delivery path. Parse bundled original
CSS at startup; do not add runtime HTML parsing or require V8. This deliberately
relaxes the original no-runtime-CSS-parser requirement and is a proposed change,
not a claim that the existing acceptance gate has already been met.

First prove a V8-free CSS service against the same native document used by compiled
HTML. Preserve selectors, cascade, variables, media predicates, pseudo styles,
invalidation and animation scheduling. The existing cascade is largely native C++
but lives as methods of the V8 runtime implementation and depends on its state.
Extraction is real engineering work; adding the Rust parser library alone is not
sufficient. Validate original Kestrel CSS without omission before extending the
application port. Shared native/Foco rendering gaps still require implementation.

Later optimize by serializing the shared parser's stylesheet representation at
build time, retaining runtime cascade and dynamic values. This avoids maintaining
an independent CSS implementation and can ultimately remove startup parsing.
Full standards-compliant CSS compilation is not close merely because the
Kestrel-specific unsupported-construct count is falling.

Shared-shadow validation: compiler/native contracts and the focused V8 runtime
scene test pass. The runtime test verifies both existing outer shadow geometry
and inset/currentColor paint. Its standalone harness needed an execute bootstrap
because evaluate alone does not activate native scene publication. Inline recascade
now preserves inset/currentColor flags, and shadow resets clear all shared fields.
The runtime test build used Inspector enabled; the Inspector-disabled build exposed
an existing unguarded call to cancel_detached_frame_context_tasks, still unresolved.

- Reference-runtime build repair: navigation task cancellation is now compiled
  regardless of Inspector support; only its Inspector notification is conditional.
  The Inspector-disabled runtime and test executable build successfully. Focused
  shared-shadow-values and iframe-replacement-layout tests both pass. This resolves
  the build failure noted above and preserves a usable ordinary-runtime reference
  for shared CSS extraction. It does not change the delivery profile decision.

- CSS service extraction: stylesheet declarations, compiled selector structures,
  immutable rule payloads, keyframe storage and movable cascade state now live in
  webscene_css_state.h, independent of V8. The existing runtime uses aliases to
  those shared types; its payload cache retains its existing ownership. The
  compiler build includes the header to verify the V8-free tool boundary. This
  does not yet switch compiler lowering to runtime cascade execution. Both builds,
  compiler/native contracts, shared-shadow-values and iframe-dynamic-recascade
  tests pass. Next extraction boundaries are selector matching, property application
  and cascade/invalidation services, followed by a native-document integration test.

- CSS service extraction: webscene_css_declarations.h now owns the Rust-backed
  declaration parser and runtime declaration normalization. The V8 adapter calls
  this service, retaining the legacy parser fallback. A separate native_web_css_service
  executable links the Rust parser without V8 or the runtime engine and passes
  custom-property case/empty values, escapes, important, nested functions, quoted
  delimiters and malformed-declaration recovery checks. Its link dependencies and
  undefined symbols contain no V8/runtime-engine dependency. Runtime shadow and
  iframe dynamic-recascade checks pass after the extraction. This establishes a
  reusable parser service, not yet a standalone cascade or a working Kestrel CSS
  runtime; selector evaluation and cascade execution remain the next boundary.

- Selector preparation extraction: webscene_css_selectors.h now owns the existing
  identifier/escape handling, compound preparation and Servo-backed selector/list
  preparation. The runtime delegates to it; its legacy selector fallback remains.
  The V8-free CSS service test now links the selector bridge and verifies combinators,
  pseudo-element metadata, escaped identifiers and malformed-selector rejection.
  Runtime positional-selector-siblings, iframe-dynamic-recascade and shared-shadow-values
  tests pass. This moves preparation, not DOM-dependent matching or cascade execution;
  supported selector semantics are unchanged by this extraction.

- Native selector matching extraction: attribute and nth-expression matching now
  live in webscene_css_matching.h and are used by the existing runtime. V8-free
  tests exercise attribute word/language matching, empty substring rejection,
  mutation, signed An+B expressions and malformed expressions. Nth arithmetic uses
  a 64-bit difference to avoid overflow for valid extreme offsets. Service tests
  and runtime positional-selector-siblings/iframe-dynamic-recascade tests pass.
  Full compound matching, document state, combinators and cascade execution still
  require extraction; this does not claim new overall selector compliance.

- Disabled-state selector extraction: the shared matcher now accepts a native
  document to resolve disabled form controls through parent relationships. The
  ordinary runtime delegates to the same function. V8-free service tests use native
  DOM construction to check direct attributes, fieldset inheritance, first-legend
  exemption, attribute removal and select/option inheritance. The service links
  the native DOM library without V8. Service and runtime positional/iframe cascade
  regressions pass. This preserves existing disabled semantics; full compound
  matching and interaction-state dependencies are still not extracted.

- Interaction selector boundary: hover/focus/focus-visible/focus-within now use a
  native interaction_state record and shared document-aware matching. The runtime
  passes its existing hovered/focused nodes and focus modality; no JS handle crosses
  the matching interface. Native tests cover ancestor hover/focus-within, direct
  focus, modality changes, text-control focus visibility and hover removal. Service
  and runtime host-pointer-exit/positional/iframe cascade regressions pass. This
  establishes an input-state boundary but does not yet provide full standalone
  compound matching, a CSS cascade, or native-app interaction integration.

- Inherited selector state: language/direction matching now uses shared native
  document helpers. V8-free tests cover inherited language, case folding, nearer
  overrides, explicit empty language, direction inheritance and default direction.
  The runtime delegates to these helpers; service and runtime positional/iframe
  cascade regressions pass. Existing behavior is preserved; broader language-range
  support and automatic direction detection are not added by this extraction.
  Full compound matching and cascade execution remain open.

- Form/URL selector boundary: selected-option resolution and its traversal helpers
  now live in webscene_native_form_state.h and serve both DOM properties and CSS.
  Shared checked matching consumes live native checkbox/option state. Target
  matching consumes a plain hash string; the V8 adapter still obtains the current
  context's hash. Native service tests cover default/authored option selection,
  live state overriding attributes, checkbox state and empty/matching URL targets.
  Service and runtime pointer/positional/iframe cascade regressions pass. Compound
  matching still contains recursive selector/cache dependencies and is not yet a
  standalone service; no additional browser-compliance claim is made.

- Combinator traversal extraction: native selector traversal now owns child,
  descendant, adjacent-sibling and general-sibling walking. A compound predicate
  is supplied explicitly; the runtime supplies its existing compound matcher.
  Native service tests isolate traversal with a tag predicate, verifying positive
  and negative relationships. Service and runtime positional/iframe/pointer tests
  pass. These tests do not imply the standalone service can evaluate all compound
  selectors yet; that predicate and cascade execution remain open.

- Compound matcher extraction: webscene_css_compound.h now contains the existing
  compound evaluation logic without V8 types. A host contract supplies document,
  input state, target hash, class lookup, text-control classification and recursive
  queries; the runtime delegates while keeping its current caches/query behavior.
  Sibling benchmark counters retain their export access through shared storage.
  A V8-free test host verifies combined class/combinator/focus/hover selectors,
  disabled/target selectors and nested not/is/has cases. Service and runtime
  positional/iframe/pointer/shadow checks pass. This preserves existing selector
  limitations and is not full standards compliance. A production native query host
  and cascade execution are still required before styling the compiled application.

- Native query host: webscene_css_query.h supplies the shared compound/traversal
  engine with native DOM, input IDs, URL hash and class lookup. It preserves query
  scope through descendant traversal and caches prepared syntax, not match results.
  Shared ownership pins syntax during recursive matching even if bounded-cache
  eviction occurs. The service test now uses this host instead of its test adapter,
  including a one-entry cache, scoped queries and class/focus mutation checks.
  Text-control classification is shared with the runtime form helpers. Service and
  runtime pointer/positional/iframe regressions pass. The host borrows its document
  and is intended for its owning thread; its state must be updated by the app/host.
  Stylesheet cascade/style application and app integration remain unfinished.

- Cascade primitives: specificity/source-order sorting and root custom-property
  rebuilding now use shared functions in webscene_css_rule_operations.h. The
  runtime delegates without changing its root-variable policy. Native tests cover
  specificity, source ties, important overrides, media exclusion and shadow scope.
  Service and runtime iframe/shadow/positional regressions pass. Per-element
  inheritance, substitution, declaration application and invalidation are still
  required; these primitives do not constitute a standalone cascade.

- Parsed CSS variable resolver extraction: webscene_css_variables.h now provides
  the existing runtime substitution path to native callers. The runtime delegates
  to it; compiled-only styles retain their typed evaluator. Native tests verify
  ancestor/root lookup, nested fallback, repeated references and mutation; runtime
  dimension-variable-compatibility, dimension-inheritance and iframe cascade tests
  pass. This is behavior-preserving reuse, not full variable compliance: cycle
  semantics, quote/token awareness and the fixed expansion bound remain limitations
  of this existing resolver. Declaration application/cascade integration remains open.

- Custom-property declaration application: inline seeding and stylesheet application
  now share native helpers, preserving the runtime's existing inline/important
  precedence. Native tests verify normal stylesheet declarations cannot replace
  inline values, stylesheet important can replace normal inline, inline important
  remains protected, and removing inline state allows later stylesheet values.
  Service and runtime dimension-variable/inheritance/iframe regressions pass.
  Ordinary style properties, whole-document cascade and app integration remain open.

- Box-property application extraction: canonical property naming, existing value
  component splitting, margin/padding application and per-side margin precedence
  now live in webscene_css_box_values.h. All existing runtime callers delegate to
  the shared implementation. Native tests cover shorthand expansion, automatic
  margin flags, inline-side protection and important-side protection. Service and
  runtime dimension-variable/inheritance/iframe regressions pass. This preserves
  current parser/length/logical-side limitations; it does not claim full box-property
  compliance. Other property families and full cascade orchestration remain open.

- Inset/border application extraction: existing inset shorthand, reset-value
  handling, border width/color/shorthand application and explicit-color recognition
  now live in the shared box-value helpers. Native tests verify inset expansion,
  automatic sides, border widths/colors, currentColor flags and removal. Service
  and runtime tradingview-opacity-border, active-chart-pseudo-border and
  logical-inset-transition tests pass. Existing border-style/logical-direction and
  parsing limitations remain; this is shared behavior, not full border compliance.
  Full declaration dispatch and cascade orchestration are still unfinished.

- Corner-radius extraction: shared box-value helpers now apply circular and
  elliptical radii to ordinary styles and pseudo styles. Native tests cover
  horizontal/vertical shorthand expansion, individual corners and clearing a
  pseudo's elliptical state. Service and runtime elliptical-corner-radii and
  active-chart-pseudo-border tests pass. Parsing/logical-corner limitations are
  unchanged; full declaration dispatch and cascade orchestration remain open.

- Transition configuration extraction: component-list splitting, time conversion,
  timing curves, property-list configuration and transition shorthand now use shared
  native helpers. Runtime adapters delegate without changing frame scheduling.
  Native tests cover property lists, durations, positive/negative delays, linear
  timing and none resets. Service and runtime logical-inset-transition,
  dimension-variable-compatibility and iframe cascade tests pass. Existing timing
  grammar/property-coverage limitations remain; no standalone animation scheduler
  or application cascade integration has been added.

- Grid/animation value extraction: grid placement application and animation
  shorthand now use shared native helpers. Tests preserve area/line metadata and
  reset behavior; this does not fix the runtime's general grid placement limits.
  A new progress-animation test exposed the old time detector accepting any token
  ending in s. Shared time parsing now requires a finite numeric value and complete
  s/ms unit, so progress remains an animation name. Tests cover signed/fractional/
  exponent times and malformed tokens. Native service and runtime compact grid,
  symbol-search grid and logical-inset-transition tests pass. Full animation
  playback and grid layout parity in the standalone app remain unverified.

- Keyframe configuration extraction: runtime CSS now delegates opacity/rotation
  stop selection, timing and animation signatures to the shared native helper.
  The V8-free service test exercises actual native-document interpolation at
  500ms and cancellation after animation:none. The focused host-clock-keyframes
  runtime regression passes, covering transitions, staggered opacity animation,
  continuous rotation and offscreen frame-demand suppression. Its prerequisite
  transition tests are retained because the existing tests share a clock timeline.
  This preserves existing first-animation/property coverage limits; it does not
  establish full animation compliance or Kestrel application parity.

- Pseudo-element value extraction: generated content decoding and the existing
  pseudo-element property dispatcher now live in webscene_css_pseudo_values.h.
  Runtime cascade retains variable resolution and diagnostic reporting, while
  native callers can apply resolved values without V8. Native tests cover escaped
  content, display, padding, currentColor borders, elliptical radii, resets and
  unsupported/partial diagnostics. Runtime active-chart-pseudo-border and
  elliptical-corner-radii regressions pass. Existing content grammar, logical
  direction, paint and typography limits remain; pseudo rule orchestration and
  compiled application integration are not yet complete.

- Stylesheet ingestion extraction: the existing Rust parser event adapter now
  lives in webscene_css_stylesheet_sink.h, parameterized by native storage,
  capability-inventory and diagnostic callbacks. Runtime parsing uses this same
  adapter. The V8-free service test verifies ordinary rules, nested media ancestry,
  important declarations, supported supports conditions, exclusion of container
  rules, keyframe routing and source addresses. Runtime media-query-list,
  host-clock-keyframes and iframe-dynamic-recascade regressions pass. Existing
  at-rule limitations are deliberately retained; this adapter does not implement
  cascade layers, container conditions or a complete native stylesheet owner.

- Keyframe ingestion now calls shared native stop parsing and normalization
  directly, removing another runtime-host dependency from the stylesheet adapter.
  The V8-free service test parses a CSS keyframe block, stores its definition,
  configures a native document and verifies opacity at a host-clock timestamp.
  It also verifies turn-to-degree rotation and implicit initial rotation handling.
  Existing first-declaration, name normalization and restricted property/grammar
  behavior remain; this is reuse of current semantics, not complete CSS animations.

- Rule payload preparation/storage extraction: runtime and native callers can now
  share immutable selector/declaration/media payload construction and weak-cache
  interning through webscene_css_rule_payload.h. The native service test checks
  selector preparation, specificity, identity reuse, declaration isolation and
  releasing/recreating unused payloads. Cache ownership and rule indexing remain
  with their host; this does not yet supply the complete native stylesheet owner.

- Resource resolution extraction: shared native URL and CSS url() rewriting
  helpers now serve the existing runtime as well as native callers. Native tests
  cover stylesheet-relative package-style URLs, root-relative resources, fragments,
  data URLs and malformed input preservation. The runtime relative-stylesheet-resource
  regression passes. This preserves existing URL/tokenization limitations and
  supplies resolution only; embedding bytes, loading fonts/images, and complete
  native stylesheet integration still require their own implementation/validation.

- Rule preparation integration: the Servo runtime path and native stylesheet
  ingestion test now share declaration URL resolution and selector-list expansion
  through webscene_css_rule_preparation.h. Hosts retain diagnostics, resource
  prefetch and rule ownership. Tests cover nested selector-list commas, resolved
  URLs and invalid combinator rejection; parser error recovery remains intact.
  Native service and runtime relative-resource, iframe cascade and positional
  selector regressions pass. Whole-document native cascade remains unfinished.

- Owned stylesheet preparation: webscene_css_stylesheet.h now assembles shared
  parsing, rule preparation, immutable payloads and keyframes into an owned native
  result with diagnostics. Media conditions remain attached for later live
  matching; the caller supplies capability inventory. Native ownership and
  diagnostics tests pass. Running native_web_css_service with the original
  samples/NativeKestrel/reference/src/style.css prepares 407 selector-expanded
  rules and 1520 declarations, with one retained keyframe definition. Its seven
  diagnostics comprise five media conditions (the diagnostic command deliberately
  supplies no media capabilities) and two partially supported keyframe blocks.
  These syntax/preparation counts are not compiler support or visual-parity counts.
  Unsupported animation properties, font registration, property application and
  complete native document cascade remain open. This does not change the
  compiled application's runtime parsing dependencies.

- Media evaluation extraction: shared native environment-based media matching and
  capability inventory now serve the runtime and native preparation diagnostic
  command. Tests verify inclusive Kestrel width/height breakpoints, color scheme,
  screen/print and comma alternatives; runtime media-query-list, reentrant query
  and iframe cascade regressions pass. Original Kestrel CSS still prepares 407
  rules/1520 declarations; all five media conditions are recognized, leaving two
  partial keyframe diagnostics in this preparation stage. Existing numeric-unit,
  grammar and hard-coded input/reduced-motion preference limits are unchanged.
  Prepared rules still need document ownership and resize-triggered cascade.

- Prepared selector consumption: native document queries can match the immutable
  selectors already stored in a prepared stylesheet, avoiding outer-selector
  reparsing. The shared query implementation delegates to this path. Native tests
  exercise authored stylesheet selectors against live class and hover changes and
  verify pseudo-element rules do not accidentally match their originating DOM box.
  The service test passes. Candidate indexing, pseudo rule routing and complete
  document cascade application remain separate unfinished work.

- Declaration precedence metadata extraction: the existing property bit groups
  and alias/shorthand mapping now live in webscene_css_property_mask.h and all
  runtime callers use that shared definition. Native tests cover combined groups,
  aliases, custom-property exclusion and high-bit storage; runtime dimension,
  inheritance, animation and iframe cascade regressions pass. Grouped-field
  precedence limitations remain unchanged. This is a dependency for sharing the
  full declaration dispatcher, not new property coverage or completed cascade.

- Reset application extraction: existing non-important all:unset behavior now
  lives in webscene_css_reset.h, including native defaults and preservation of
  modeled inline/important values, custom properties and pseudo-element state.
  Native tests verify protected width/color and custom/pseudo data survive while
  ordinary height/opacity reset. The focused runtime all-unset regression passes.
  Existing grouped-property/reset coverage limits remain; other reset keywords,
  full declaration dispatch and application cascade integration are still open.

- Box declaration application extraction: resolved dimension, inset, padding,
  margin and gap application now uses webscene_css_box_application.h with an
  owner-supplied precedence predicate. Native tests check inherited dimensions,
  protected inline values, shorthand sides/gaps and reset behavior. Native service
  and runtime dimension inheritance/variables, inset transition and iframe cascade
  regressions pass. Existing parsing, logical-direction and grouped precedence
  limitations are unchanged. Other declaration families and full cascade remain open.

- Grid/flex application extraction: shared layout helpers now prepare the existing
  grid tracks and apply grid/flex declarations, alignment and box sizing with the
  owner-provided precedence predicate. Native layout tests verify fixed-plus-flexible
  columns respond to 500px/700px viewports for both grid and flex. This preserves
  existing repeat/named-line/dense-layout and shorthand limitations; it does not
  claim complete grid/flex compatibility or presented-frame performance. Full
  declaration dispatch and document cascade remain unfinished.
  Runtime compact-go-to-grid, tradingview-symbol-search-grid,
  dynamic-percentage-flex-list and dimension-inheritance regressions pass.

- Structural declaration extraction: display parsing and display, table spacing/
  collapse/layout, positioning, float and z-index application now use shared native
  helpers. Native layout tests verify display:none releases flex space, inherited
  position/z-index values and table style metadata. Runtime property-table spacing,
  compact table, fixed dialog and dimension-inheritance regressions pass. Existing
  keyword/float/table coverage limitations remain; full cascade and application
  integration are not complete.

- Paint application extraction: shadow parsing and background color/image/position/
  size/repeat mutation now use shared native helpers. SVG loading remains a host
  callback. Native tests cover inset/currentColor shadows, resource loading/failure,
  gradient replacement and size resets. They exposed stale background-size second
  components; clearing both components before parsing fixes updates from two values
  to contain or one length. Native service and runtime shadow, relative resource,
  SVG checker and active-chart pseudo-border regressions pass. Existing image,
  gradient, shadow and tokenization limits remain; full cascade is unfinished.

- Visibility/overflow extraction: native helpers now apply overflow, containment,
  visibility, pointer-events and opacity, including computed overflow-axis coupling.
  Native hit tests verify hidden/noninteractive elements are excluded; style tests
  cover clipping/scroll flags and opacity clamping. Native service and runtime
  overflow navigation, virtual-row scroll and pointer-exit regressions pass.
  Existing inheritance/keyword coverage remains; this does not establish complete
  scrolling behavior or presented-frame performance for Native Kestrel.

- Typography extraction: font resolution/shorthand and text, SVG paint, cursor and
  list-style application now use shared native helpers. Native tests verify
  inherited percentage sizing, em spacing and unitless line-height preservation.
  A numeric-weight shorthand test exposed 700 being read as font-size; the parser
  now recognizes modeled 100–900 weights before locating size. Native service and
  runtime dimension/font-relative, SVG typography and reset regressions pass.
  Variable weights, additional font axes, shaping and other existing grammar limits
  remain; this does not establish font-resource packaging or full app parity.

- Unified resolved declaration application: borders/outlines, transforms and
  animation declarations are shared, and webscene_css_application.h now composes
  the native property handlers behind one resolved-declaration entry point used
  by the runtime. A V8-free integration test prepares CSS, matches its selectors
  against a C++-constructed document, applies declarations and verifies flex layout.
  Native decoration/reset tests and runtime keyframes, border, dimension and iframe
  cascade regressions pass. Variable substitution, rule ordering, invalidation,
  pseudo routing and resource ownership remain the caller's responsibility; the
  complete native document cascade and Kestrel integration are not finished.

- Native declaration entry point now combines alias normalization, live custom
  property application/substitution, invalid-variable rejection and resolved style
  application. Runtime application delegates to it while retaining telemetry and
  resource callbacks. Native tests verify variable updates, fallbacks, aliases,
  important protection and resulting geometry. Runtime variable/inheritance,
  iframe cascade and keyframe regressions pass. The ordinary path borrows authored
  declarations; only aliases allocate normalized copies. Existing substitution and
  precedence limitations remain; rule ordering/invalidation are not yet integrated.

- Cascade reset extraction: runtime and native callers now share clearing of
  previous stylesheet state through webscene_css_cascade_reset.h. Native tests
  verify stale dimensions, visibility, custom/pseudo values and important flags
  clear while an inline width survives and determines layout. Runtime dynamic
  iframe cascade, active-class pseudo border, variable dimensions and all-unset
  regressions pass. This preserves existing reset behavior/limitations; candidate
  selection, ordering and the document cascade lifecycle are still unfinished.

- Matched declaration ordering extraction: shared cascade application performs
  custom-property passes before dependent values, then replays dependent inline
  values and restores inline transitions with existing important guards. Native
  tests verify a later matched rule supplies an earlier rule's variable and an
  initially unresolved inline height updates, retaining its inline mask. Runtime
  variable/inheritance, transition/keyframe and iframe cascade regressions pass.
  Candidate selection, final font metrics, pseudo routing and document invalidation
  still need integration; this is not a complete native stylesheet lifecycle.

- Pseudo application extraction: pseudo selector suffix routing, variable-aware
  pseudo property application and scrollbar declarations now use native helpers.
  Tests cover generated text/color from custom properties and important scrollbar
  visibility without hiding the originating element. Native service and runtime
  active pseudo border, scrollbar drag/style and overflow navigation regressions
  pass. Existing suffix grammar, unsupported scrollbar-corner behavior and pseudo
  cascade limitations remain; native document lifecycle integration is unfinished.

- Candidate matching extraction: shared matching now filters ordered candidate
  indices by media and existing shadow-scope policy and separates ordinary/pseudo
  rule targets. Native tests cover specificity ordering, media deactivation and
  live class changes; runtime pseudo border, media, iframe cascade and positional
  selector regressions pass. Results borrow their rule storage until application
  completes. Candidate index construction, font finalization and invalidation still
  need integration into a complete native document stylesheet owner.

- Cascade finalization extraction: native callers and the existing runtime now
  share relative line-height finalization against the winning font size and
  computed layout-style comparison. A regression exposed missing pseudo padding
  in the comparison; all four padding edges now participate so those changes
  invalidate geometry while background-color-only changes remain paint-only.
  Native CSS service tests and runtime paint-only cascade, variable dimensions,
  dimension inheritance and active pseudo-border regressions pass. Both builds
  are current. This does not change compiler support counts or introduce runtime
  CSS parsing into compiled applications. Native stylesheet ownership, candidate
  indexing and invalidation integration remain unfinished; full original Kestrel
  parity and presented 60fps panning/resize remain unverified.

- Selector index extraction: shared native index_selector now selects subject
  ID/class/tag/attribute keys, focus/root buckets and fallback rules, and records
  ancestor attribute dependencies. The ordinary runtime delegates this portion of
  index construction to it. Native tests cover all buckets and distinguish ancestor
  attributes from subject attributes; runtime positional selector, pseudo border,
  iframe recascade and media-query regressions pass after rebuilding. Existing
  selector scanning limitations are preserved. Hover/variable dependency indexing,
  candidate collection and native document invalidation remain to be integrated;
  this does not increase audited compiler coverage or prove Kestrel parity.

- Candidate collection extraction: shared native collect_candidates now combines
  fallback, tag, root alias, ID, attribute, focus and class rule buckets. Runtime
  class lookup allocation policy and benchmark counters remain in its callback;
  sorting/deduplication and full matching remain separate. Native service tests
  cover combined buckets and repeated whitespace-separated classes. Rebuilt runtime
  positional selector, active pseudo border, iframe recascade and media-query
  regressions pass. This preserves existing selection policy; native stylesheet
  ownership/invalidation and full Kestrel parity/performance remain unfinished.

- Native prepared stylesheet ownership: stylesheet_owner now attaches/replaces
  prepared author sheets in stable source order, removes them with index rebuild,
  merges keyframe definitions, collects sorted unique candidates, and refreshes
  media activation from an explicit environment. Prepared data types are separated
  from the parser entry point; the owner performs no CSS text parsing. Native
  tests pass for replacement order, removal/index compaction, repeated class keys,
  viewport activation and unchanged activation. Original Kestrel preparation still
  reports 407 selector-expanded rules, 1520 declarations and two partial keyframe
  diagnostics; these are syntax counts, not visual parity. The owner is not yet
  wired to a native document host. Recascade scheduling, interaction dependencies,
  scoped sheets, resource registration and app integration remain outstanding.

- Native per-node cascade integration: apply_native_cascade combines the prepared
  stylesheet owner, indexed matching, reset, ordinary/inline declarations, font
  finalization, pseudo/scrollbar rules, keyframe configuration and layout-versus-
  paint invalidation. Callers provide root variables, interaction-aware query state,
  resource loading and diagnostics, and must process parents before children.
  Native service tests pass for sheet replacement changing width and removing
  generated content, and removal clearing the previous stylesheet width. No V8 or
  runtime HTML parsing is used. Automatic tree scheduling, root variable refresh,
  scoped sheets, interaction invalidation and application hosting remain unfinished;
  this test proves computed-style updates, not browser parity or frame performance.

- Native document cascade pass: explicit full-document refresh rebuilds active root
  variables and traverses the light DOM parent-before-child using an iterative
  stack. Native tests pass for inherited variable updates, removed variables falling
  back, and media activation changing computed width after a viewport update.
  This is a correctness refresh path for host-scheduled changes, not a per-frame
  operation. Automatic mutation hooks, selective invalidation, shadow scopes,
  parser-free prepared pseudo matching, native app integration and presented-frame
  performance remain open. No original Kestrel parity claim is made.

- Prepared pseudo origin selectors: immutable rule payload preparation now compiles
  the originating selector for pseudo-element rules. Native cascade matching uses
  that representation directly instead of submitting the origin string to the
  query parser. Existing runtime matching callbacks retain their host behavior.
  Native cascade/pseudo tests and rebuilt runtime pseudo border, iframe recascade
  and positional selector regressions pass. Nested functional selector queries
  and generated stylesheet serialization still require work before claiming a
  fully parser-free native CSS application. Full Kestrel parity remains unproven.

- Native style session: a document-borrowing session now owns prepared sheets and
  query state, coalesces explicit invalidations, and refreshes before host layout.
  Sheet replacement/removal, viewport changes, focus/hover and target changes
  schedule refresh; unchanged interaction/viewport inputs do not. DOM mutators
  still must explicitly invalidate. Native tests pass for focus styling, class
  mutation, repeated invalidation coalescing, no-op flush and removal cleanup.
  This is not yet wired to the Foco sample or native DOM mutation hooks, and full
  document refresh is not a suitable unconditional animation-frame operation.
  Resource callbacks, selective invalidation, original Kestrel parity and 60fps
  panning/resize verification remain outstanding.

- Compiled Native Web host invalidation: inspection found every dirty canvas update
  reran the entire compiled declaration cascade. document_state now separately
  tracks style invalidation; canvas-only changes retain computed styles, while DOM,
  attributes, rules, interaction and viewport changes still recascade. Layout and
  scene construction remain unchanged. A declaration-count contract proves canvas
  paint skips the cascade and attribute/resize updates execute it. Compiler and
  contract suites pass. This applies to the current app host but does not yet wire
  the prepared stylesheet session into it or prove 60fps presented performance.

- Native canvas layout reuse: canvas drawing/clearing/external composition now
  marks scene generation rather than global layout dirtiness. The compiled host
  checks both scene generation and layout state, rebuilding display data while
  retaining geometry for canvas-only changes. Style/viewport changes still mark
  layout dirty. A public layout-pass counter supports performance diagnostics;
  contracts verify paint/clear skip layout, paint produces a new scene revision,
  and mutations/resize still run layout. Compiler and contract tests pass. This
  removes measured unnecessary passes but does not establish presented 60fps or
  complete original Kestrel behavior; Foco app-level profiling remains required.

- Foco app validation after canvas reuse changes: rebuilt FocoKestrel in
  artifacts/native-web-foco and ran --exercise-commands --capture. Process exited
  zero after hosted line creation, undo and redo checks, with GPU serial=2. Viewed
  /tmp/kestrel-canvas-reuse.png: native grid, box and created line are visible in
  the simplified POC interface. This is not the original Kestrel UI and serial=2
  is not an FPS measurement. otool lists Dawn and system frameworks, no V8 dylib;
  undefined-symbol scan found no V8/HTML/CSS parser matches (not a complete static
  dependency audit). Full original UI, behavior, packaging and presented 60fps
  panning/resize acceptance remain outstanding.

- Compiler scrollbar-width support: auto/thin/none now emit typed native style
  calls. Native overlay widths are 6px for auto and 4px for thin; none suppresses
  rails while preserving scrolling. Compiler tests reject lengths/unknown/multiple
  keywords, and contracts verify emitted rail geometry and hidden-bar scrolling.
  Compiler and contract suites pass. Fresh original Kestrel audit: 397 rules,
  1475 declarations, **41 distinct unsupported constructs**, down from 43.
  Inherited scrollbar-width, scrollbar-color, original app parity and performance
  remain unfinished; this is not a full CSS Scrollbars conformance claim.

- Compiler scrollbar-color support: two typed colors (including compiled var()
  expressions), auto/initial and inherit/unset now emit native color updates.
  Parent color inheritance is preserved without allocating auxiliary style storage
  for unchanged defaults; invalid computed pairs restore inherited colors.
  Compiler tests cover Kestrel's var(--line) transparent form and reject malformed
  literal pairs; native paint contracts verify custom rail colors. Compiler and
  contract suites pass. Fresh original audit: 397 rules, 1475 declarations,
  **40 distinct unsupported constructs**. currentColor/color-mix forms, forced-color
  handling and browser differential validation remain incomplete. No app parity
  or presented-frame performance claim follows from the audit count.

- Compiler full grid column span: grid-column:1/-1 and auto now emit a typed flag;
  used and intrinsic grid layout consume it without parsing a placement string.
  Contracts verify spanning three tracks and resetting to one track; compiler
  tests cover whitespace and invalid line zero. Compiler/contracts and rebuilt
  runtime compact-go-to/symbol-search grid regressions pass. Fresh original Kestrel
  audit: 397 rules, 1475 declarations, **39 distinct unsupported constructs**.
  General numeric/named grid lines and spans remain unsupported in this compiler
  path. Full original app parity and 60fps acceptance remain unfinished.

- Basic native cursors: compiler supports auto/default, pointer, text, crosshair
  and horizontal/vertical resize keywords with inherit/unset. Native hit-based
  cursor lookup walks ancestors; the Foco view maps supported keywords to host
  cursor types on pointer input and scene refresh. Document inheritance/override
  contracts, 109 compiler tests and FocoKestrel build pass. Diagnostic fixtures now
  use unsupported grabbing instead of newly supported pointer. Fresh original CSS
  audit: **35 distinct unsupported constructs**. Live OS cursor transitions after
  deferred style changes remain to be verified; grabbing/move/not-allowed, custom
  cursor images, full browser parity and performance remain open. Border-collapse
  was rechecked and remains open because shared-edge conflict resolution is absent.

- Cocoa interaction cursors: Foco bf7b065a adds grab/grabbing/not_allowed enum
  values and native open-hand/closed-hand/operation-not-allowed NSCursor mappings.
  Other platform backends explicitly retain arrow fallback. Native Web maps compiled
  grab/grabbing/not-allowed keywords to those values; the FocoKestrel macOS build,
  compiler suite and contracts pass. Diagnostic fixtures now use unsupported zoom-in.
  Fresh original CSS audit: **33 distinct unsupported constructs**. These shapes
  have not been visually verified. Cocoa currently applies cursor selection before
  deferred host-frame style updates, so style-driven cursor changes can lag until
  subsequent input; that ordering remains open along with move/custom-image cursors,
  original Kestrel parity and measured 60fps panning/resize.

- Cocoa deferred cursor ordering: Foco 0a4be755 separates cursor refresh from input
  dispatch and refreshes again after host-frame callbacks/layout in both Cocoa
  presentation paths. Post-frame refresh is restricted to the key window and
  avoids redundant NSCursor updates when the effective kind is unchanged.
  FocoKestrel rebuild and hosted line/undo/redo capture pass (exit zero, serial=2).
  This smoke test does not observe OS cursor state; stationary-pointer visual
  validation remains required. Compiler audit remains 33, and full original
  Kestrel parity and presented 60fps panning/resize remain incomplete.

- Text overflow prerequisite audit: native DOM emits text fragments with spacing
  metadata, but Foco draw_dom_text ignored both spacing fields. Foco 636d421d now
  applies finite letter/word spacing while preserving the existing unspaced font
  run path. Raster tests pass for exact ASCII letter-gap and space advances.
  This matches the native fallback scalar-spacing policy, not full grapheme-aware
  shaping. Ellipsis and anywhere wrapping remain open: both fragment and fallback
  text paint paths require coordinated overflow geometry, and the Foco renderer
  still uses simple text runs rather than a shared full shaper. Compiler audit
  remains 33; original app parity and 60fps acceptance remain unfinished.

- Screen-target media types: the compiler now accepts screen/all/not print as
  active and print/not screen/not all as inactive for the current native screen
  application target. Inactive nested rules retain an impossible media interval;
  nested screen rules cannot reactivate a print subtree. Compiler suite passes,
  with diagnostic fixtures using unsupported speech instead of print. Fresh
  original CSS audit: **32 distinct unsupported constructs**. This is not print
  output support or general media-query grammar; print rendering would require
  a distinct target/runtime mode. Reduced-motion preference and animations remain
  open, as do original Kestrel parity and presented 60fps panning/resize.

- Reduced-motion media conditions: generated rules now carry an optional boolean
  preference, and document.set_reduced_motion schedules recascade when it changes.
  Compiler supports reduce/no-preference in at-rules and stylesheet media attributes;
  nested contradictory conditions remain inactive. Compiler and contract suites
  pass for emitted conditions and runtime activation/deactivation. Fresh original
  CSS audit: **31 distinct unsupported constructs**. Host OS preference propagation,
  preference-change notification and compiled animation/transition behavior remain
  open; accepting the condition does not implement the declarations inside it.
  Original Kestrel parity and presented 60fps acceptance remain incomplete.

- Foco reduced-motion propagation: native views read the attached visual-animation
  service preference and refresh compiled styles when it changes. Foco 1519b2e0
  wakes host frames from scene_publisher::set_reduced_motion only on a changed
  value; Cocoa's existing OS-settings subscriptions feed that service. New hosted
  native_web_foco_motion test passes for wake-up, style activation/deactivation
  and no-op repeated values. This verifies the service-to-view path without
  changing the machine's accessibility settings. Compiled animations/transitions,
  original Kestrel parity and presented 60fps validation remain unfinished; audit
  remains 31 distinct unsupported constructs.

- Native animation clock integration: compiled document cascades now notify the
  existing native animation machines, and document.advance_animations feeds their
  monotonic host clock. Native transition events dispatch with property/elapsed
  metadata. Foco views request host frames while animations remain active and
  stop afterward. Native C++ style authoring exposes a linear opacity transition;
  document tests verify start/midpoint/completion and transitionend, while hosted
  tests verify frame demand ends at completion. Compiler suite also passes.
  Compiler transition/keyframe syntax, richer timings, cancellation/disposal edge
  cases and visual animation parity remain open. Audit stays at 31; this is clock
  plumbing, not proof of Kestrel animation or presented 60fps behavior.

- Compiled opacity transitions: shorthand supports opacity, validated duration and
  optional delay, named easing and none, emitted as typed C++ timing data. A new
  generated C++ module test verifies delayed start, completion and cancellation.
  Native opacity transitions now cancel on transition-property:none even when the
  target is unchanged. Queued events retain frame demand after cancellation;
  hosted tests verify delivery and return to idle. Compiler/contracts/generated
  transition tests, runtime host-clock regression and hosted motion tests pass.
  Fresh original CSS audit: **30 distinct unsupported constructs**. Multiple
  properties, fill transitions, cubic/steps timing, compiled keyframes and broader
  cancellation semantics remain unfinished. No full parity or 60fps claim.

- Animation callback disposal: native documents expose disposal state and clear
  queued transition events and scene output during disposal. Foco drops retained
  composition packets and GPU ownership, avoids advancing disposed documents and
  returns to idle. Generated transition tests verify target removal suppresses
  later queued events; hosted motion tests dispose from transitionend and verify
  no retained stream or frame demand. Native contracts, compiler, transitions and
  hosted motion tests pass. This does not establish general callback-disposal
  safety for every input path, original Kestrel parity or presented 60fps.

- Shared stylesheet document handoff: added an owned stylesheet_resolver seam and
  separate opt-in webscene_native_web_shared_css target. The adapter accepts
  prepared_stylesheet records, projects viewport/hover/focus/reduced-motion state
  and runs the existing native cascade on document invalidation. It retains
  preparation/application diagnostics and is destroyed before native tree cleanup.
  Mixing this backend with typed generated rules throws rather than silently
  discarding one source of styling. Replacing the resolver invalidates styles.
  A generated C++ HTML module and predefined row template now exercise this path:
  initial layout, hover, class/text updates, focus, responsive media, reduced
  motion, insertion/removal, replacement and disposal pass. Canvas-only drawing
  retains the layout-pass count. Six tests pass: shared styles, native CSS service,
  module smoke, transitions, contracts and compiler. This comparison test prepares
  CSS at runtime; it never parses HTML at runtime. The default app remains on the
  typed backend. Shared-engine SVG resource loading, color preference projection,
  remaining interaction states, typed prepared values and actual Kestrel/Foco
  integration remain unfinished. No original-app parity or 60fps claim.

- Build-time shared CSS preparation: webscene-uic --prepare-css input.css
  output.cppm --module module.name now emits a C++ module whose compiled_css::build
  returns prepared_stylesheet data. It uses the existing Rust parser and emits
  prepared selector compounds/pseudo origins, declarations, media conditions,
  keyframes and diagnostics. Output preserves rule order and sorts keyframe names
  for deterministic generation. CMake exposes webscene_prepare_css_module.
  Parsed and generated preparation run through identical compiled-HTML mutation,
  focus/hover, resizing, preference and template tests. Round-trip assertions cover
  selector fields, escaping, declarations, keyframes and diagnostics; compiler
  tests cover deterministic output, invalid module names and input overwrite
  rejection. Shared style, CSS service and compiler suites pass. The unchanged
  original Kestrel stylesheet produces a 348,956-byte module that Clang successfully
  precompiles; its two partial-keyframe diagnostics remain visible. This emission
  does not validate every property's rendering semantics. Textual values, media
  and functional selector arguments still require shared-engine interpretation,
  so this is build-time stylesheet parsing, not the final parser-free backend.
  The packaged Kestrel app has not switched to this path; parity, resources and
  presented 60fps remain unfinished.

- HTML compiler shared-backend selection: --css-backend shared now prepares linked
  and embedded stylesheets into the generated HTML module, preserving stylesheet
  order, source addresses, link dependencies and media attributes. Generated build
  installs the optional shared resolver and accepts a diagnostic report. Inline
  styles on the document root, body, elements and predefined templates are kept
  as authored attributes in this transitional backend. The shared native cascade
  now has an opt-in attribute synchronization path, including inline custom values,
  priority and style removal; the existing V8-style path remains the default for
  other session callers. Typed documents continue to reject textual style writes.
  Tests verify linked/embedded/inline interaction, responsive template dimensions,
  inherited custom-value updates, attribute removal and important precedence.
  Shared document/style, CSS service, contracts and compiler suites pass, including
  backend validation and linked dependency emission. Kestrel's original index.html
  generates and Clang-precompiles with --css-backend shared --preview; preview HTML
  warnings and script omission remain, so this is not a complete original app.
  No default application backend changed. Runtime inline/value interpretation,
  resource integration, actual Foco/Kestrel integration, parity and presented 60fps
  remain unfinished.

- Foco shared-CSS preview integration: NATIVE_WEB_PREVIEW_SHARED_CSS (default OFF)
  selects generated shared styles for the original Kestrel document and ribbon
  templates, links the separate shared adapter and reports native CSS limitations.
  Canvas percentage sizing remains native application logic. Added --capture for
  the GPU preview, requiring a delivered GPU image and shutting down after capture.
  Built the opt-in FocoKestrelPreview and ran it successfully: compositor capture
  /tmp/kestrel-shared-css-preview.png, GPU serial 1, clean exit. Inspected the image:
  original shell, ribbon icons, sidebars, command dock and GPU grid/demo box render.
  Layer/property populations and app behavior are still incomplete. Layout dump
  at 1280x800 and 1280x1000 shows the workbench growing from 463 to 663 pixels while
  fixed top/bottom regions retain size; this is layout evidence, not smooth live
  resize evidence. Input-coalescing check passes. Runtime diagnostics still include
  unsupported backdrop-filter, overflow-wrap, scrollbar-color/width, stroke-width,
  text-overflow, touch-action and user-select, plus partial features. No stylesheet
  source modifications or runtime HTML parsing were introduced. The production
  sample remains typed and this preview still uses shared value interpretation.
  Full Kestrel logic, resources, browser parity and presented 60fps remain open.

- Shared CSS standard scrollbars: connected scrollbar-width auto/thin/none and
  scrollbar-color explicit color pairs/auto/inheritance to existing native overlay
  geometry. Added independent priority masks and parent color inheritance during
  cascade reset. Values validate before updating priority state. Parsed and
  generated stylesheet integration tests verify four-pixel blue rails inherited
  from a parent, author-important versus inline-normal width, hidden rails with
  retained scrolling, default colors, inline-important six-pixel rails, and style
  removal restoring stylesheet values. Shared style/document, CSS service and
  compiler suites pass; rebuilt reference runtime and scrollbar-style-drag passes.
  This addresses two diagnostics from the shared preview; the packaged preview
  has not been rebuilt for this checkpoint. CurrentColor/extended color functions,
  forced-color behavior and full interaction with vendor scrollbar pseudo styling
  are not established by these tests. Original-app parity and presented 60fps
  remain unfinished.

- Shared CSS SVG stroke width: connected stroke-width to the existing SVG
  serialization field, with its own precedence mask and reset on recascade.
  Nonnegative numbers, px and percentage tokens are accepted; initial and explicit
  inheritance resolve before projection. Validation precedes priority updates so
  invalid important widths cannot block valid inline values. Parsed/generated
  stylesheet tests verify an authored presentation width is overridden by CSS,
  class removal restores it, explicit inheritance uses the ancestor width, and an
  invalid important declaration leaves a valid inline width effective. Shared
  style/document, CSS service and compiler suites pass. Evidence covers emitted
  SVG data; no new Foco pixel comparison or packaged preview capture was made.
  Font-relative units, full SVG styling parity, full Kestrel behavior and presented
  60fps remain unverified or unfinished.

- SVG paint recascade: reproduced stale fill/stroke after removing an icon class
  in both parsed/generated shared stylesheet tests. Cascade reset now clears
  non-inline SVG paint overrides so authored presentation attributes become
  effective again. Tests cover class removal, inline paint taking precedence,
  inline paint surviving class changes, and removing the inline attribute.
  Shared style/document, CSS service and compiler suites pass. Rebuilt reference
  runtime; tradingview-svg-checker and canvas-svg-image regressions pass. This is
  live scene-data update evidence, not a new Foco pixel comparison. Full original
  Kestrel behavior and presented 60fps remain unfinished.

- Native original layer panel: added four predefined layer-row templates from
  pinned upstream app.js and UI.icon geometry (visible/hidden, locked/unlocked),
  retaining original structure/classes and adding data-ref binding metadata.
  New C++ module kestrel.layer_panel binds the native drawing's layers, colors,
  entity counts, current selection, visibility/lock icons and accessible labels.
  It handles current-layer clicks, transactional visibility/locking, row cleanup,
  explorer badges and summary fields. The shared/GPU preview mounts it and marks
  GPU content dirty after changes. Native layer tests pass for toggles without
  accidental row selection, current-layer changes, lock undo, repeated refresh and
  obsolete-node removal. Rebuilt and captured Foco preview successfully at
  /tmp/kestrel-native-layers.png (GPU serial 1, clean exit), then inspected the image:
  populated original layer rows, current highlight, icons, counts and summary are
  visible alongside the GPU demo. No runtime HTML parsing or JavaScript was added.
  Filtering, Shift-click selection, ribbon synchronization, object/property panels,
  complete app behavior and presented 60fps remain unfinished. The demo geometry
  is still a box, not the original full browser demonstration document.

- Native modifier propagation and layer selection: native events now carry Shift,
  Control, Alt and Meta state; Foco pointer/wheel routing preserves the host flags,
  including the synthesized click on release. Shift-clicking an original layer row
  replaces selection with visible entities on that layer without changing the
  current layer, matching the pinned upstream explorer handler. Tests cover hidden
  entities/layers, selection replacement and current-layer preservation. Hosted
  motion/input tests verify all four flags on pointerdown, click and wheel; layer,
  native contract, transition and compiler suites pass. This verifies injected Foco
  input events, not a physical keyboard session. Keyboard-generated click modifier
  coverage and broader keyboard input remain separate work; full app parity and
  presented 60fps remain unfinished.

- Keyboard modifier consistency: added a modifier-aware native key overload while
  retaining the existing bool-Shift API. Foco forwards all four modifiers for
  Enter/Space activation, and synthesized clicks carry them to native handlers.
  Hosted tests verify modifier-preserving activation and document disposal from a
  focus callback during Tab navigation. The host now checks disposal before
  querying final focus. Native contracts, transitions, layer selection and compiler
  suites pass alongside hosted motion/input tests. This does not implement general
  text editing, shortcuts or IME; full app parity and presented 60fps remain open.

- Native committed text: reused form-control value/selection/caret storage and
  extracted authored-value initialization for both the existing runtime and native
  documents. Added live value/set_value APIs, focus for text controls, committed
  text insertion with beforeinput cancellation and input data/type metadata, and
  Foco text_input_event forwarding. Read-only controls reject edits; hidden inputs
  do not become focusable via tabindex. Untouched defaults follow value attributes
  and textarea child updates, while dirty live values remain independent. Tests
  cover Unicode text, cancellation, read-only state, default/live value separation,
  textarea newline defaults, removal/disposal during beforeinput and hosted text
  delivery. Native text/contracts/shared-document/compiler and hosted input tests
  pass; rebuilt runtime native-text-input and textarea-value-lifecycle regressions
  pass. No runtime HTML parsing was added. This is committed-text groundwork:
  deletion/navigation, selection APIs, clipboard, input-type sanitization, caret
  blinking and IME composition remain incomplete, as do full Kestrel and 60fps.

- Native text deletion and selection: shared UTF-8 boundary helpers now serve the
  existing runtime and native Backspace/Delete path. Added explicit selection
  get/set APIs using UTF-8 byte offsets, rejecting offsets inside a scalar.
  Deletion emits cancellable beforeinput and input metadata, rechecks target
  lifetime/read-only/inert state after callbacks, and updates the live caret.
  Foco forwards Backspace/Delete. Tests verify emoji/accent deletion, forward and
  selected-range deletion, no-op boundaries, invalid offsets, cancellation,
  read-only controls, replacement of selected text, and hosted keyboard delivery.
  Native text/contracts/compiler and hosted input tests pass; rebuilt runtime
  native-text-input regression passes. This is scalar-safe, not full grapheme or
  word editing. Modifier-based deletion, caret navigation, clipboard, IME, full
  application parity and presented 60fps remain unfinished.

- Native layer filtering: the original explorer-search input now filters native
  rows through input events. A persistent filter subscription survives row refresh;
  the original empty-state markup is a predefined compiled template. macOS uses
  CoreFoundation Unicode lowercase; the additional-platform fallback is explicitly
  ASCII-only. Tests pass for matching, empty-state scene text, restored lists,
  accented names on macOS, retained focus and no drawing/GPU change notification.
  Built and ran --exercise-layer-filter --capture successfully; inspected
  /tmp/kestrel-native-filter.png, showing a-wall in the native field and only A-WALL
  in the original panel (GPU serial 1, clean exit). This uses injected Foco committed
  text, not a physical keyboard session. The capture also exposes a form-rendering
  gap after value initialization: input[type=color] paints its hex value as text.
  Correct non-text control rendering next; this is not a full app-parity result.
  Ribbon/object/property integration, broader text editing and presented 60fps
  remain unfinished.

- Native color-input painting: the shared native scene now paints input[type=color]
  as an inset swatch instead of exposing the live hexadecimal value as text. Native
  set_value updates repaint the swatch; malformed simple color values paint black.
  The native text-input test checks authored mixed-case hex, value changes, fallback,
  swatch dimensions and absence of text/caret commands. Rebuilt native_web_text and
  its CTest passes. This is painting support only: picker activation, full input-type
  value sanitization and browser-specific color-control appearance remain unfinished.

- Original Objects explorer: extended the native explorer module with tab handlers,
  original object-row templates and all twelve original entity icon variants copied
  from the pinned UI source. C++ now supplies names/type labels, layer names, ID
  suffixes, selected classes, the original combined search fields and a 500-row cap.
  Click replaces selection; Shift-click toggles membership. Template construction
  performs no runtime HTML parsing; model text is assigned through native APIs.
  Native explorer tests pass for switching tabs, selection toggles, layer-name
  filtering, empty matches and the cap message. Large-number locale formatting,
  inspector/ribbon synchronization and full original model/application integration
  remain unfinished; these tests do not establish visual parity or presented FPS.
  The FocoKestrelPreview application also rebuilds and links successfully with the
  updated templates and native explorer module; interactive capture is pending.

- Hosted object-selection capture: added --exercise-objects to the diagnostic
  Foco preview. It activates the original Objects tab, selects the generated row
  through native dispatch and asserts the drawing selection and active row class.
  Rebuilt and ran with --capture /tmp/kestrel-native-objects.png; clean exit and
  GPU serial 1. Inspected capture: Box icon/name/A-WALL/suffix, active Objects tab,
  highlighted row, summary selected=1 and highlighted GPU box are present. The
  prior color swatch fix is visible too. This uses synthetic document events,
  not a physical pointer test or presented-frame benchmark. Empty search input
  still omits placeholder painting, the inspector is empty and renderer status
  remains the original startup text; broader application parity is unfinished.

- Native placeholder painting: eligible empty input/textarea controls now emit
  their authored placeholder through the native text scene, while keeping the
  live value and caret calculations independent. Rebuilt native_web_text; CTest
  passes focused/unfocused display, typing suppression, clearing restoration,
  attribute replacement/removal and exclusion for range controls. This closes the
  missing text seen in the explorer capture, but does not implement ::placeholder
  styling, placeholder-specific line handling, or case-insensitive input types;
  current painting inherits the field text style. Visual recapture remains pending.

- Hosted panning publication probe: FocoKestrelPreview --benchmark-pan drives
  camera motion for 360 host ticks and reports publication count, elapsed time,
  CPU tick cost and native geometry rebuild count. Rebuilt app and ran successfully:
  360 images / 5.98349 seconds = 60.1655 images/second; mean tick 0.495921 ms,
  maximum 0.770667 ms, zero scene rebuilds during motion. Log:
  /tmp/kestrel-pan-pipeline.log. This is a single small demo-box run with synthetic
  camera movement, not physical pointer latency, display presentation timestamps,
  full Kestrel model throughput or live-resize validation. It establishes that
  this preview's single-pending-snapshot pipeline can sustain 60Hz publication.

- Larger hosted panning workload: --benchmark-pan-large populates 1,000 native
  mesh entities, then runs the same 360-tick camera/publication probe. Rebuilt
  FocoKestrelPreview and ran successfully: 360 images in 5.9784 seconds (60.2168/s),
  mean CPU tick 0.386052 ms, maximum 0.779708 ms and zero geometry rebuilds.
  /tmp/kestrel-pan-large.log contains the result. This supports cached-scene
  panning scalability for this synthetic workload, not full original Kestrel
  geometry coverage or display-presented FPS. Single-run timing differences
  from the one-box run are not evidence of a performance improvement.

- Compositor counter probe: panning benchmarks now sample the view publisher's
  compositor diagnostics before/after motion, printing successful backend
  presentations, skipped attempts and occlusion separately from GPU publications.
  Source inspection shows presented_frame_count increments after backend success
  in compositor-tick.cpp, not on physical display timestamps. Rebuilt and ran
  --benchmark-pan-large: 360 publications / 5.97845 seconds, mean CPU tick
  0.506081 ms, maximum 0.844125 ms, zero scene rebuilds. Diagnostics report available
  but presented=0, skipped=0, occluded=0. Log /tmp/kestrel-pan-compositor.log.
  This is an unresolved diagnostic-path mismatch, NOT evidence of zero displayed
  frames or proof of 60 displayed FPS. Trace the active platform presentation path
  and counter ownership before relying on these diagnostics for acceptance.

- Direct Metal presentation evidence: discovered existing Cocoa Graphite host
  trace_metal_presentation, enabled by FOCO_PRESENT_TRACE_JSONL. It attaches an
  addPresentedHandler to CAMetalDrawable and records presentedTime. Ran the
  1,000-entity pan probe with trace /tmp/kestrel-metal-present.jsonl and log
  /tmp/kestrel-metal-pan.log. GPU publication remained 360 / 5.97837 seconds, but
  trace contains exactly one callback with presentedTime=0 (no valid display
  timestamps). Consequently no displayed FPS can be calculated. Next investigate
  active Cocoa host presentation scheduling/invalidation and drawable delivery;
  do not infer successful on-screen animation from GPU publications or forced
  compositor captures. Existing generic presentation counters also stayed zero.

- Host callback change reporting fix: preview_window::advance_host_frame no longer
  unconditionally returns false after invoking the native GPU callback; it reports
  has_pending_scene_changes(), allowing Foco's host_changed commit path to publish
  the updated subtree. Rebuilt and repeated the traced 1,000-entity benchmark.
  GPU publications: 360 / 5.98299 seconds (60.1706/s); compositor successful
  presentations: 179 (~29.9181/s). Metal trace has 180 callbacks, 177 valid positive
  presentedTime values; valid interval FPS 29.99985, median 33.33350 ms,
  p95 33.333583 ms, maximum 33.333625 ms. Logs:
  /tmp/kestrel-host-change.log and /tmp/kestrel-host-change-present.jsonl.
  This fixes missing presentation caused by false host-change reporting and exposes
  a real half-rate presentation issue. Actual 60fps acceptance still FAILS;
  investigate scheduling/commit-to-display latency rather than claiming publication
  throughput as displayed FPS. No debugger tools were available; used controlled
  before/after runtime traces instead.

- Half-rate localization: pan probes now also sample scene_publisher commit,
  publication and no-op counters. Rebuilt and ran the traced 1,000-entity workload:
  359 GPU images / 5.98297 seconds, 358 scene commits and 358 publications,
  zero no-op commits, but only 178 compositor presentations. Logs:
  /tmp/kestrel-scene-publish.log and /tmp/kestrel-scene-publish-present.jsonl.
  This localizes the half-rate loss AFTER native/Foco scene publication. Next
  inspect compositor scheduling and drawable presentation, rather than changing
  native geometry caching or assuming document invalidation still loses half
  the updates. Actual displayed 60fps remains unachieved.

- Half-rate CPU timing investigation: temporary timing around the active Cocoa
  presentation worker's compositor_->tick localized an over-budget render call.
  1,000-entity run: 180 successful ticks, median 20.1494 ms, p95 21.1624 ms,
  max 59.1078 ms (includes startup); 181 non-rendering ticks, median 2.6968 ms,
  p95 3.3933 ms. GPU publications 360, scene publications 359, compositor
  presentations 179. Log /tmp/present-cpu.log. Temporary host instrumentation
  removed after measurement (built executable still includes the probe until
  next rebuild). Next profile phases inside render: drawable acquisition,
  painter, GPU waits/submission. Do not assume this is an explicit 30Hz throttle;
  successful calls exceed the 16.7ms frame budget.

- Render-phase and backing investigation: existing FOCO_RESIZE_TRACE_JSON spans
  show median ensure-backing 16.502 ms, paint-scene 2.297 ms, snap-recording
  2.016 ms; drawable acquisition effectively zero. Log
  /tmp/kestrel-render-phases.log. Temporary backing-allocation logging then found
  180 allocations, ALL starting with no texture, dimensions 0x0 and scale 0,
  allocating the same 2560x1600 backing at scale 2. Log
  /tmp/kestrel-backing-probe.log. Thus stable-window panning is repeatedly losing
  the retained backing, not resizing between different dimensions. Trace reset/
  recovery paths (including renderer status handling) next. Temporary logging
  removed from Foco source; last-built binary retains it until rebuild. This is
  stronger evidence than the earlier scheduler hypothesis; do not change frame
  cadence to hide backing recreation. Displayed 60fps remains unmet.

- Recovery trigger found: logs contain semantic replay skipped: Graphite semantic
  image resource is unavailable. Cocoa mapped ALL non-presented painter statuses
  to device_lost, forcing recovery/backing destruction. A working-tree diagnostic
  change in Foco cocoa_graphite_host.mm preserves render_status. Rebuilt/run:
  359 GPU images, 358 scene publications, ZERO presentations and 358 skipped
  attempts. /tmp/kestrel-preserve-skip.log. Thus device recovery masked a persistent
  semantic image resolution failure; preserving status alone is not a working
  fix. Foco change intentionally uncommitted pending resource-resolution fix.
  Next inspect skia_graphite_renderer::draw_image images_ and command-surface
  fallback, which return unavailable for the updated native GPU packet resource.

- Semantic resource narrowing: temporary logging in update_command_surface finds
  packet compilation failures (40,449-byte packets, zero GPU images), not failed
  Metal imports. There are 64,261 such attempts in /tmp/kestrel-resource-probe.log
  during one 360-tick run, suggesting repeated scans of accumulated resources.
  Do not assume attachment loss is proven: failing packets may include stale
  resources, and current draw_image's failing ID must be correlated with its
  resource attachment/generation. Next inspect scene resource attachment retention
  and cache population. Temporary renderer logging removed; Foco's uncommitted
  render-status preservation experiment remains. Rendering still incomplete.

- Root cause fixed in Foco: single-leaf publication append_resource copied bytes
  but omitted resource_attachments, unlike the general publication paths. It now
  retains attachment ID/generation/value. Cocoa also preserves painter skipped
  status instead of falsely triggering device recovery. Rebuilt/reran the traced
  1,000-entity native pan benchmark: 360 GPU publications, 359 scene publications,
  359 compositor presentations, zero skips. Metal trace contains 357 positive
  presentation timestamps: 59.999762 FPS, median 16.666750 ms, p95 16.666792 ms,
  max 16.666875 ms. /tmp/kestrel-leaf-fixed.log and
  /tmp/kestrel-leaf-fixed.jsonl. This is actual presentation evidence for the
  synthetic panning workload; physical input latency, live resize, original model
  and full Kestrel parity remain incomplete. Add focused leaf-attachment lifetime
  regression coverage next, beyond this successful hosted reproduction.

- Native/Foco attachment regression: native_web_foco_motion now publishes four
  successive GPU attachment generations through a native document canvas. It
  verifies the single-leaf fast path (publisher_visit_count==1) after initial
  publication, applies each mailbox diff to a retained scene, acknowledges and
  releases the lease, then verifies exact attachment identity and continued
  ownership. Rebuilt target and CTest passes. This guards the actual attachment
  omission behind the panning failure; it does not exercise Metal imports or
  substitute for the separately recorded 60fps presentation trace.

- Native GPU resize validity: view packet generation now suppresses the GPU draw
  command when the image's logical allocation dimensions differ from the current
  canvas dimensions (using the host's integer sizing convention). Previously it
  stretched the old image into the new bounds. Matching content restores drawing.
  Native Foco regression verifies matching image -> resize suppression -> new
  matching image; rebuilt native_web_foco_motion and CTest passes. This is a
  correctness guard, not smooth-resize completion: DOM/background can remain
  briefly visible without GPU content while replacement is pending. Next ensure
  correctly sized content arrives on resize presentation deadlines; live 60fps
  resize and no blank intervals still require measurement/integration work.

- Same-tick resize submission investigation: WebScene snapshots already expose
  resolve_with_gpu_waits, but native Foco make_gpu_image explicitly rejects
  requires_producer_wait and omits dependency_count/get_metal_event callbacks.
  Experiment using immediate GPU-wait resolution aborts with Native frame
  requires a completed GPU image (/tmp/kestrel-gpu-waits.log). Reverted viewport/
  preview experiment, retaining completed-image behavior in source. Last-built
  preview binary contains the experiment and must be rebuilt before running.
  Next extend the native adapter's consumer lifetime and dependency callbacks
  using existing lease ABI, then retry same-tick publication with synchronization.
  Do not remove the rejection alone: that would permit unfinished GPU sampling.

- Native GPU fence handoff: make_gpu_image now validates producer dependencies,
  retains them in consumers, and exposes dependency_count/get_metal_event to Foco.
  Required waits without valid dependencies are rejected. Viewport poll has an
  explicit GPU-wait option (completed-only remains default); preview uses it and
  publishes again immediately after submit so resized frames need not wait an
  extra host tick. Rebuilt and ran 1,000-entity pan: 360 image publications, 359
  compositor presentations, zero skipped; 358 valid Metal timestamps, aggregate
  59.8321 FPS, p95 16.666833 ms, maximum 33.333542 ms (one doubled interval).
  /tmp/kestrel-native-fences.log and /tmp/kestrel-native-fences.jsonl. This verifies
  actual hosted fence import without the earlier adapter rejection; focused
  dependency lifetime tests and live-resize deadlines remain to be verified.

- Native Foco fence regression: new native_web_foco_gpu_dependencies test covers
  pending-image dependency acceptance, count/event callbacks, invalid index/null
  arguments and rejection of required waits without dependencies. It drops frame,
  lease and producer references while retaining a consumer, verifies its dependency
  still exists, then verifies destruction on consumer completion. Uses a fake
  fence pointer only for callback/lifetime checks, never GPU submission. Rebuilt
  target and CTest passes; actual Metal synchronization is covered separately by
  the hosted probe. Live resize acceptance remains unfinished.

- Hosted canvas-resize workload: --benchmark-canvas-resize drives three 120-tick
  shrink/grow cycles via native inline dimensions while panning 1,000 meshes.
  Rebuilt/reran with Metal trace: 360 image publications, 359 scene publications,
  359 compositor presentations, zero skipped, zero geometry rebuilds. 359 valid
  presentedTime timestamps give 59.999605 FPS, p95 16.666875 ms and maximum
  16.666917 ms. Logs /tmp/kestrel-canvas-resize.log and
  /tmp/kestrel-canvas-resize.jsonl. This is canvas resizing within a fixed window,
  not AppKit live window dragging; visual verification of per-frame matching
  content and native window resize acceptance remain unfinished.

- Resize workload validity checks: preview now validates every published lease's
  actual dimensions against current canvas dimensions. Canvas-resize benchmark
  counts real viewport dimension changes and host ticks ending without a matching
  image, failing if fewer than 300 changes occur or any matching image is missing.
  Rebuilt/reran: 359 dimension changes, zero ticks without matching content,
  360 image publications, 360 compositor presentations, zero skips over 5.98677s.
  /tmp/kestrel-resize-validation.log and accompanying .jsonl Metal trace. This
  strengthens canvas-resize evidence without substituting it for AppKit window
  dragging or visual stale-frame inspection, which remain unfinished.

- Shrunk-canvas visual check: resize benchmark capture now triggers at tick 61,
  the first minimum-size point, instead of near the restored size. Rebuilt and
  captured /tmp/kestrel-resize-minimum.png (clean exit). Inspected: native mesh grid
  and primary box remain visible within reduced canvas, with right/bottom unused
  space rather than an image stretched to the original viewport. Search/command
  placeholders are now visible too. This single forced capture is visual evidence
  at one point; it does not prove all transition frames or AppKit live resizing.

- Original Courtyard drawing fixture: generated the pinned upstream examples.js
  courtyard through its original math/geometry/model implementation offline, and
  embedded the resulting project JSON in kestrel.examples C++ module. Native
  load_courtyard applies it through the native drawing validator: 265 entities,
  nine layers, original project name; all entity layers resolve. Rebuilt
  kestrel_native_layers and CTest passes. Offline JavaScript generation is not
  deployed or run by the app; runtime parsing is drawing JSON, never HTML.
  Preview still uses its diagnostic box until this fixture is connected with
  camera fitting and original render-style selection. Full drawing render parity
  and application behavior remain unverified.

- Courtyard connected to hosted preview: ordinary launch now loads the original
  embedded 265-entity fixture, selects wireframe and fits visible geometry. Existing
  synthetic benchmarks and interaction probes retain their deterministic box
  fixtures. Rebuilt/captured /tmp/kestrel-native-courtyard.png with clean exit.
  Inspected walls, openings/door arcs, furniture, hatches and dimension lines;
  explorer shows nine layers and 265 objects with native summary title. Drawing
  labels and dimension text are absent: native render_data collects text, but
  hosted text drawing requires investigation. Full visual parity remains unmet.

- Native Canvas text API: added fill_text using existing shared display-list
  commands for paint/font/alignment/baseline/text. This supplies the missing native
  entry point needed by Courtyard's text overlay without V8 or HTML parsing.
  Rebuilt native_web_text and CTest passes Unicode content/coordinates, clear and
  no layout-pass increase for drawing-only updates. Native render_data already
  collects drawing labels; projecting and painting those onto the preview overlay
  is next. This API addition alone does not restore visible Courtyard labels.
