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
| 18 | `background:linear-gradient(125deg,var(--panel2),var(--panel))` | P | Open |
| 19 | `background:linear-gradient(145deg,var(--panel2),var(--panel))` | P | Open |
| 20 | `border-collapse:collapse` | L | Open |
| 21 | `border:1px dashed #65c5a4` | P | Open |
| 22 | `border:2px dashed var(--accent)` | P | Open |
| 23 | `box-shadow:0 0 0 1px color-mix(in srgb,var(--accent) 25%,transparent)` | P | Open |
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
