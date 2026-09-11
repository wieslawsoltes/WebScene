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
