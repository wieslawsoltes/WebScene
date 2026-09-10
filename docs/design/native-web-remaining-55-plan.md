# Remaining 55 CSS constructs: integrated implementation plan

Status: active. Requested as one integrated implementation pass, without stopping
for approval or treating individual commits as completion. Preserve original
Kestrel HTML/CSS and use independent fixtures to develop each feature family.
Baseline: native-web-css-checkpoint.txt, 397 rules / 1475 declarations, 55 distinct
unsupported constructs. These are distinct source usages, not 55 separate engines.

## Architecture and boundaries

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
