# Remaining 55 CSS constructs: integrated implementation plan

Status: active. Requested as one integrated implementation pass, without stopping
for approval or treating individual commits as completion. Preserve original
Kestrel HTML/CSS and use independent fixtures to develop each feature family.
Baseline: native-web-css-checkpoint.txt, 397 rules / 1475 declarations, 55 distinct
unsupported constructs. These are distinct source usages, not 55 separate engines.

## Architecture and boundaries

Group/picking integration verified: the native picking fixture now creates its
group through group_entities instead of manually assigning metadata. It verifies
ordinary group selection, individual selection with ignore-group, Ungroup from
one member, single-object picking after ungroup and restored group picking after
exact undo. The existing pick implementation already matches the original app's
visible-peer selection and modifiers; no new selection algorithm was needed.
The native layer suite passes.

V8 backdrop scene verification: new modal-backdrop runtime test opens a JavaScript
dialog, acquires emitted scene records, requires one viewport-sized authored
backdrop, toggles a transparent class off/on and closes the dialog. It requires
new scene revisions for each state and acknowledges/releases acquired scenes.
The rebuilt V8 engine test passes. This is reference-path verification only;
native Kestrel still uses compiled templates with no JS or runtime HTML parsing.
Pixel equivalence, blur and broader backdrop syntax remain unverified.

Backdrop cascade convergence: extracted solid backdrop application into the
shared pseudo-declaration helper, using the existing CSS color-token validator.
Both native cascade and the two V8 cascade paths now call it, resetting backdrop
color before each matched pseudo pass. This also prevents V8 from treating the
new backdrop pseudo kind as ::after. Native shared-style tests pass; V8 engine
and tests build, and fixed-auto-height-dialog regression passes. Direct V8
backdrop scene/pixel parity and blur remain unverified.

Shared native backdrop color: ::backdrop now has a recognized pseudo kind, native
dialog color storage, shared cascade handling for solid background/background-color
with important precedence, and viewport-sized foreground paint immediately before
each modal. Tests cover closed/open/resize, transparent overrides, priority and
parsed/generated stylesheet equivalence. The hosted original Group capture
/tmp/kestrel-group-backdrop.png visibly dims the UI and WebGPU canvas behind the
dialog. No app HTML/CSS was rewritten. Backdrop blur, broader background syntax,
currentColor and the legacy V8 cascade path remain outside this implementation;
unsupported declarations continue to be reported.

Native label activation: pointer activation now resolves explicit for/id targets
and implicit wrapped controls, focuses and activates the associated control after
an uncancelled label click, and avoids reactivation from direct control clicks.
Disabled/inert targets are excluded and callback removal/disposal is rechecked.
Native input tests cover Group-style explicit labels, preventDefault, disabled
inputs and wrapping checkbox labels without double toggles; the suite passes.
This closes the basic native label behavior gap, not all HTML activation edge
cases or accessibility/browser parity.

Group Unicode trim parity: group creation now trims the 25 ECMAScript whitespace
code points using UTF-8 boundaries, preserving accented/astral name content and
falling back to Group for all-whitespace names. A development Node reference
enumeration confirms the whitespace set and expected nonbreaking/ideographic/BOM
fixtures. Native tests verify the names and exact undo; the drawing suite passes.
No JavaScript dependency is added to the application.

Hosted Group interaction verified: --exercise-group-dialog opens the compiled
original dialog, sends Foco text input into its focused name field, presses/releases
the rendered Create group button, requires applied names and closed modal, then
sends platform-Z and compares the complete drawing to its original snapshot.
The rebuilt app passes and captures /tmp/kestrel-group-submit.png (exit 0,
GPU serial 2). Input is injected through host methods; this is not physical OS
mouse/keyboard latency evidence. Backdrop visuals and broader dialog parity remain
open.

Modal layout correction: runtime diagnostics located the original Group dialog
at (0,800), 510x278.4, below the viewport. Shared native layout now takes
default-positioned registered modals out of flow, sizes them against the viewport
and centers their subtree; explicitly positioned dialogs keep the CSS path.
Native tests verify centering at two viewport sizes and retain focus/isolation
checks. The hosted capture /tmp/kestrel-group-centered.png now visibly shows the
centered original Group form and focused input. Original HTML/CSS is unchanged.
Backdrop paint/blur, authored static-position distinctions, oversized-dialog
behavior and full modal browser parity still need coverage.

Compiled Group dialog integration in progress: original body/field markup is a
predefined template; native handlers populate the name/count, submit captured
entities, cancel/Escape, restore focus and display validation errors. The existing
Group ribbon action is connected. Compiler templates now preserve authored IDs
(with named references), required by the original label/input association. Native
dialog creation/cancel/validation/focus tests and all 114 compiler tests pass.
The hosted build and --show-group-dialog capture run succeed, but the captured
original app does NOT visibly show the modal. Native modal registration and
top-layer painting exist; UA modal positioning/layout needs investigation before
this UI is usable. Do not count Group as complete. Unicode trim parity, label
activation and browser-equivalent form validation remain open as well.

Native modal scope API: document::set_modal connects a native HTML dialog to the
existing engine modal stack and open attribute. It clears focus when blocked or
closed; the caller owns initial/restored focus and cancel/submit behavior. Native
tests cover background focus rejection, Tab wrap, nested modal precedence,
close/removal cleanup, non-dialog rejection and disposal during blur. The native
text/input suite passes. This is a structural native seam, not a complete browser
showModal implementation; the compiled Group dialog still needs connection.

Modal prerequisite: native document focus and Tab candidate collection now consult
the existing engine is_inert check, which covers explicit inert ancestors and
registered modal scopes. Tests verify direct focus rejection, forward/reverse Tab
skipping (including positive tabindex), direct inert attributes and restoring
eligibility after removing inert. The native text/input suite passes. Public
show/close modal APIs, focus restoration, Escape cancellation and the compiled
Group dialog still need implementation; this is not complete dialog support.

Group model foundation: group_entities accepts the dialog's captured entity IDs,
requires at least two distinct editable members, generates a shared group ID/name,
and commits one Create group undo transaction. Tests verify captured versus live
selection, regrouping without altering old peers, duplicate/missing/noneditable
members, ASCII whitespace trimming, blank-name fallback and exact undo. The
native drawing suite passes. This operation is not yet connected to the original
Group dialog; compiled dialog markup, modal focus/submit/cancel behavior, default
name/count, maxlength validation and Unicode trim parity remain open.

Native Ungroup action: the existing compiled ribbon action now calls the native
model, collecting groups from editable selected entities and removing group and
groupName from all editable members of those groups, matching the original
app.js action. Model tests verify unselected peers, unrelated groups, locked and
hidden members, exact undo and empty selection. The native drawing suite and
hosted app build pass. Browser-style empty-selection feedback, interactive ribbon
verification and the separate Group dialog remain open.

Compiled color-scheme coverage: SharedStyles.css now includes a dark media rule;
the parsed/generated integration sequence switches light/dark/light and requires
80/95/80px widths plus identical serialized scenes for both stylesheet producers.
The regenerated C++ CSS module builds and the shared-style suite passes. This
closes the generated-data verification gap from the previous runtime-only fixture,
without claiming OS preference notifications or broader browser parity.

Color-scheme environment gap fixed: shared_css previously hardcoded the light
media environment. Native documents now expose set_dark_color_scheme, invalidate
styles when it changes, and pass it to the existing shared media evaluator. Foco's
view supplies its effective theme and requests a refresh when that preference
changes. A native shared-CSS test verifies light/dark/light widths and unchanged
preference layout stability. The hosted app builds and its existing theme/capture
exercise passes; that exercise changes Kestrel's data-theme and is not an OS theme
notification test. Typed-backend color-scheme lowering and OS theme transitions
remain outside this verification.

Shared CSS parity check strengthened: shared_styles now records serialized scene
bytes at every render checkpoint and requires identical sequences for runtime
stylesheet preparation and compiler-generated stylesheet data on compiled HTML.
This includes hover/focus, class/text changes, viewport and reduced-motion changes,
template insertion/removal, scrollbar styling, SVG paint and stylesheet replacement.
The strengthened test passes. The compiler, shared document, native contract and
text/input suites also pass after rebuilding their targets. This compares fixture
scene serialization, not browser pixels or complete CSS/Kestrel coverage.

Bundle symbol isolation: a broader audit found html5ever code linked into the
preview from the shared Rust archive despite no HTML entry-point call in native
authoring. Native macOS app targets now dead-strip unused code. The executable
shrinks from 18,120,944 to 14,353,808 bytes; Dawn remains 9,222,256 bytes. The new
tests/NativeWeb/audit_macos_bundle.py flags the old bundle's html5ever symbols and
passes the new bundle's source-asset, dependency/rpath, symbol-family and local
signature checks. The rebuilt app also passes the routed spectrum/undo capture
exercise (exit 0, GPU serial 3). This is structural evidence, not proof of every
runtime path or complete application parity; shared CSS interpretation remains.

Native app relocation fix: FocoKestrel and FocoKestrelPreview now bundle Dawn in
Contents/Frameworks, use @executable_path/../Frameworks instead of an absolute
external SDK rpath, and receive local ad-hoc signatures after copying the library.
The rebuilt preview copied to /tmp/KestrelRelocatedNative.app passes strict/deep
signature verification and its routed spectrum drag/undo/capture exercise (exit
0, GPU serial 3). DYLD_PRINT_LIBRARIES confirms Dawn loads from that copied bundle.
Dawn's inspected dependencies are system libraries/frameworks. This establishes
local relocation for the preview; release identity signing/notarization, resource
coverage and complete application acceptance remain open.

Startup-gap repeatability check: three consecutive fresh process launches of the
same Courtyard benchmark produce approximately 59.9998fps with no positive
presentation interval above 16.667ms. Raw traces and revision/hash metadata live
in performance/native-kestrel-pan-repeat-summary.json. The earlier 50ms startup
gap did not recur; preserve its trace and leave startup variability unresolved.
No renderer scheduling change is justified from these observations alone. These
synthetic pan runs do not prove live-resize cadence or physical pointer latency.

Post-inspector hosted performance check: latest NativeKestrel builds and runs the
Courtyard pan benchmark. Evidence in performance/native-kestrel-pan-post-inspector.json
and its raw trace records 360 callbacks, 358 positive presentation timestamps,
59.665fps average, 16.667ms p95 and one 50ms interval between the first two positive
timestamps. No other interval exceeds 25ms. Geometry rebuilds remain zero and
the compositor reports zero skips. Preserve this startup gap as a remaining issue;
do not replace it with the earlier clean trace or claim uninterrupted 60fps.
Live window-resize presentation and full browser parity remain unproven.

MTEXT paragraph information: native inspector now emits the original Paragraphs
readonly row through the compiled template. A boundary scanner follows pinned
Kestrel src/mtext.js handling for CR/LF, paragraph/column controls, escaped slashes,
literal fields and semicolon-terminated formatting arguments (including escaped
stack arguments). Six fixtures produce 1/6/1/2/2/1 in both the original JS parser
executed as a development reference and the native tests; the layer suite passes.
The shipped native app adds no JS. This scanner only counts boundaries: formatting
validation, warnings, resource limits, rich-text composition and layout still need
the full native MTEXT implementation.

MTEXT inspector scalar editing: compiled coordinate templates now expose Position
XYZ, Text height and Paragraph width for native multiline-text entities. Width
accepts zero and rejects negative/nonfinite values; height remains positive.
The native inspector test enters each field through text input and Enter, checks
invalid width rejection and exact undo restoration while preserving rich-text
source. The native layer suite passes. Original HTML/CSS remains unchanged and
no runtime HTML parsing is introduced. Paragraph counting and the original rich
text composition action/dialog remain unimplemented; this is partial MTEXT
inspector coverage, not multiline layout/rendering parity.

Spectrum routed-input verification: --exercise-color-picker now waits for popup
layout, locates its native spectrum, verifies window hit testing at two positions,
and raises pressed/moved/released routed pointer events on the hit control. Both
press and drag must change document color while keeping the picker open; the
selected entity color must match the spectrum and two undos restore exact states.
The hosted run passes and captures `/tmp/kestrel-picker-pointer.png` (exit 0,
GPU serial 3). This replaces direct set_color calls in the exercise. It verifies
native hit testing/control event routing, not OS event ingestion, out-of-bounds
pointer capture or physical interaction latency.

Picker editing lifetime: the NativeKestrel host no longer closes the flyout from
its color-change callback, allowing successive edits in one open session. A
selection mismatch closes it without applying the stale edit. The hosted exercise
now applies two colors, requires the picker to remain open after both, dismisses
it and verifies exact intermediate and original document states through two undo
operations. Build and capture `/tmp/kestrel-picker-continuous.png` pass (exit 0).
These remain API-driven changes, not physical pointer-routing evidence. Each
change currently creates an undo entry; drag-session undo coalescing and native
mouse interaction remain to be reviewed against browser behavior.

Picker background follow-up: the Foco editor panel now resolves Fluent.Surface
as its background. NativeKestrel rebuilt and the open-picker GPU capture
(`/tmp/kestrel-picker-background.png`, exit 0) shows an opaque panel with readable
RGB/hex fields. At 1280x800 it fits flush against the bottom/right window edges.
Small-window overflow and actual pointer selection remain unverified; no original
HTML/CSS changed.

Foco spectrum box correction: retained rendering now uses the selected hue with
white-to-hue saturation and a vertical value shade, matching the control's native
pointer coordinates. Its selection marker follows saturation/value instead of
remaining at the center. The rebuilt NativeKestrel open-picker capture
(`/tmp/kestrel-spectrum-selection.png`) verifies the gradient and marker for the
selected color. Ring marker behavior, flyout placement/background and actual
pointer-driven selection still need work; this does not establish picker parity.

Foco picker retained-rendering correction: color_spectrum now publishes reusable
gradient paint resources and complete retained commands for its existing box/ring
visuals. Basic headerless sliders without ticks/tooltips now emit retained tracks
and thumbs; unsupported slider variants remain explicitly incomplete. The open
Kestrel picker capture now succeeds and shows the spectrum/RGB controls, and the
open/commit/undo exercise passes again with a GPU capture. The flyout's chrome,
placement, spectrum selection marker and actual pointer color selection still
need review; this is not full picker appearance/interaction parity or 60fps resize
evidence. Foco owns these rendering fixes; WebScene remains unchanged by them.

Color-picker integration work in progress: selected swatches now anchor Foco's
existing compact color_picker flyout; native color changes apply to the captured
selection only and refresh the model/DOM. --exercise-color-picker opens the flyout,
sets a color through the picker API and verifies exact undo; that path passes.
However --show-color-picker --capture fails while the flyout remains open:
Graphite reports an incomplete semantic record for color_spectrum (kind 15,
geometry box). The legacy Graphite node painter supports color_spectrum, but
scene-storage.cpp does not emit its retained semantic drawing record. This is a
rendering gap to fix before considering the picker usable; no visual success is
claimed. The selection guard and first-color close behavior also need interactive
review after rendering works. No source HTML/CSS changes or JS were introduced.

Selected Color row restored in its original position before Linetype. Native color
updates validate hex/bylayer values; ByLayer changes editable selections with the
original undo label and refreshes the swatch. A native button-keyboard test checks
model state, inherited layer-color display and exact undo. Native color-picker
dialog activation, persisted/default color behavior and hosted verification remain
open; the swatch alone is not a complete color editor.

Spline optional-property correction: adding degree to an ordered-json entity
could invalidate the referenced point-array entry before knot generation. The
editor now captures count and prepares knots before mutation. Native inspector
tests cover both points and controlPoints entities with omitted degree/knots,
enter degree through the compiled control and require exact undo. This removes a
native imported-entity lifetime hazard; full import/application parity remains open.

Mesh/dimension native-control verification: a reusable compiled-inspector fixture
now enters Mesh Center X and dimension Precision, Text override and Offset using
text input plus Enter. Tests retain geometry/volume preservation, measurement
text, clamping and exact undo checks. The native inspector suite passes. Hosted
mesh/dimension interaction and remaining application parity remain open.

Mesh inspector: compiled Center fields translate through the existing native
geometry transform; Width/Depth/Height, Vertices/Faces and Signed volume readouts
are populated. A model test verifies center translation, unchanged dimensions and
volume, and exact undo. Count formatting currently uses plain decimal rather than
locale grouping. Native-control/host mesh inspector verification and full parity
remain open.

Dimension inspector: compiled Measurement, Offset, Text height, Text override and
Precision fields now bind to native edits. Precision rounds/clamps to 0..6; text
height must be positive. Model tests verify precision clamping, signed offset,
override text, validation and exact undo. The absent precision display uses the
original initial default 2; persisted application defaults and hosted dimension
control verification remain open.

TEXT native control verification: the inspector test now types multiline content
into the compiled textarea, confirms Enter does not commit it, and commits on
focus departure. Rotation and height are entered through numeric controls and
committed with Enter. Direction removal, stored values and exact undo pass without
manually dispatching change. Hosted text editing, IME and full parity remain open.

TEXT inspector: original Position, Text height, Rotation and textarea content now
use compiled rows and native change handlers. Rotation converts degrees to radians
and removes explicit direction as the original app does. Model tests cover content,
height validation, direction removal and exact undo. Native document/host tests
for the new text inspector and rich MTEXT editing remain open.

Hatch control verification: tests now type Spacing and commit with Enter, select
Pattern using ArrowDown and verify model values and exact undo. Removed the added
wrapper around hatch rows so each original property-row is directly in its section;
the test checks this parent structure. Spacing display now trims trailing decimal
zeros. Native inspector regressions pass; hosted hatch interaction remains open.

Hatch inspector: HATCH now shares Vertices/Length/Plan area measurements and exposes
compiled Spacing and Pattern controls. Native edits validate positive finite
spacing and supported pattern names and use undo transactions. Model tests cover
edits, invalid input and exact undo. Native document/host interaction verification
for these new controls remains open, as does complete application parity.

Spline inspector: compiled Degree field and shared Vertices/Length/Plan area
readouts now cover SPLINE alongside polyline metrics. Native degree edits round
as in the original app, clamp to 1..min(10, point count - 1), regenerate uniform
knots and record undo. Model tests verify rounding, clamping, knot values and exact
undo. Hosted spline interaction and remaining application behavior remain open.

Checkbox paint correction: the shared native scene builder now draws a check
indicator from live checkedness (falling back to the checked attribute), and does
not paint a checkbox's submitted value as text. Native scene tests verify checked,
unchecked and attribute/property precedence. Rebuilt Foco and repeated hosted
polyline click/undo: the capture now visibly shows the restored check mark and
the app exits successfully. This is a basic native indicator, not complete themed
form-control appearance or browser pixel parity.

Hosted Closed checkbox verification: the Courtyard inspector exercise now finds
the compiled checkbox's rendered bounds, sends Foco pointer press/release at its
center, verifies the selected polyline's closed flag changed, and invokes platform
undo with exact drawing restoration. The run passed with GPU capture and clean
exit. This exercises host hit testing/activation, not physical mouse latency or
full application parity.

Polyline geometry inspector: original Vertices, Closed, Length and conditional
Plan area rows instantiate through compiled templates. Closed binds native checked
state to an editable-selection undo transaction; length/area use the native path
and polygon calculations. A native keyboard test closes a 3–4–5 path, checks length
7 to 12 and area 6, then requires exact undo. Hosted pointer checkbox verification
and other remaining application behavior are still open.

Native checkbox prerequisite: added checked/set_checked and checkbox focusability.
Pointer click activation and Space toggle checkedness before click listeners, roll
back when canceled, then emit input/change with removal/disposal guards. Tests
cover Space, cancellation, programmatic state, disabled controls and disposal in
input. Radio groups, labels, OS pointer verification and the polyline Closed
binding remain open; this is not complete form-control parity.

Ellipse Major/Minor radius now use compiled coordinate fields and the existing
native conic-axis calculation. Editing normalizes/scales the selected axis while
preserving the other axis and writes both explicit axes, matching the original
application approach. Native inspector tests type a minor radius for a tilted
ellipse, verify axis direction/other-axis preservation and exact undo. Full hosted
conic interaction, other entity inspectors and application parity remain open.

Post-inspector panning check: the current original Courtyard benchmark ran 360
ticks with 265 entities and 30 text records, no geometry rebuilds and no compositor
skips. Of 361 Metal callbacks, 359 had positive presentation timestamps: 59.999700
FPS, p95 16.666833 ms, maximum 16.666875 ms, no intervals over 25 ms. Two invalid
timestamps are excluded rather than inferred. Raw trace, source revisions and
summary are retained in performance/native-kestrel-pan-current.{json,jsonl}.
This is evidence for synthetic panning only, not physical input latency, continuous
OS resize performance or full browser parity.

Arc Start/End angle rows now use compiled numeric fields with native degree/radian
conversion and undo. Circle Area uses the original radius-based formula and
three-decimal squared-unit display. Tests type 270 degrees into End angle, verify
the stored radians and exact undo, and verify radius-5 area text. Ellipse radii,
remaining inspector sections and hosted conic interaction verification remain open.

Circle/arc Radius now uses the compiled coordinate row and native change handler.
The model requires a positive finite radius and editable single selection, derives
the previous conic X-axis magnitude, and scales explicit axes as the original app
does. Model tests cover rotated explicit axes, zero rejection and exact undo for
both entity types; native inspector regressions pass. Ellipse radii, arc angles,
area display and hosted radius interaction verification remain open.

Conic center inspector: CIRCLE, ARC and ELLIPSE expose original Center X/Y/Z
fields through the shared compiled coordinate template. Native center edits
require finite values and an editable single selection. Tests instantiate each
entity's inspector, type Center X through text input, commit via Enter and require
exact undo restoration. Radius, axes, angles and area fields remain to be ported;
this does not complete conic inspector parity.

Point geometry inspector: POINT Position X/Y/Z now reuse the compiled coordinate
template used by lines. Native position editing checks finite values and editable
single selection, and records undo transactions. A native document test types a
negative Z coordinate, commits through Enter and verifies exact undo restoration.
The complete native layer/inspector regression target passes. Other entity editors,
full application behavior and physical resize performance remain open.

Hosted line edit: `--exercise-line-edit --capture` selects an original Courtyard
LINE through pointer events, enters an End X change through Foco text/Enter events,
checks native geometry, waits for a subsequent GPU image publication, and invokes
platform undo with exact drawing restoration. The run passed and captured GPU
serial 3 with exit status zero. Publication is not a pixel comparison or physical
presentation timestamp, so browser visual parity and 60fps gates remain open.

Line geometry inspector: compiled section/coordinate templates now expose the
original six Start/End coordinates and calculated Length for a single LINE.
Native finite endpoint edits respect editable selection and use undo transactions.
The native document test types End X, commits with Enter, verifies changed geometry
and displayed length, and requires exact undo restoration. Missing endpoint arrays
are guarded; absent Z components display zero. Other entity geometry editors and
full browser interaction parity remain open.

Inspector information: single selections now show the original Layer state row
(locked, visible/editable or hidden) and optional Group row, using a predefined
compiled read-only property template. Group names fall back to group IDs.
Lineweight display uses four decimal places with trailing zeros removed instead
of std::to_string's six fixed decimals. Native tests verify the visible and locked
labels, group display, zero formatting and the existing editing/undo behavior.

Hosted text-commit verification: the inspector exercise now sends Foco
text_input_event for Lineweight and Name, commits each through Foco's Enter key
event, checks the selected entity's property, then sends platform-Z and requires
exact drawing JSON restoration. It passed in FocoKestrelPreview with a delivered
GPU frame and exit status zero. This covers host event forwarding; text selection
is set through the native API, so OS text selection and IME remain unverified.

Native text commit correction: inspector number/name tests previously dispatched
change explicitly, masking that real text entry only emitted input. Native text
controls now track user edits and emit change on focus departure; single-line
inputs also commit on Enter. Unchanged edits and programmatic set_value do not
create a commit, and pending records are cleared on removal/disposal. Tests cover
deletion, textarea blur, repeated commits, and change handlers that remove the
control or dispose the document. Inspector tests now type through text_input and
commit via Enter/focus, then verify model updates and exact undo. Full input
validation, IME and original application parity remain separate open work.

Inspector continuation: original Linetype and Lineweight rows and the single-object
Name row now instantiate from predefined compiled templates. Native edits apply
only to editable selected entities and create undo transactions. Native document
tests exercise keyboard selection of Center linetype, weight and name changes,
exact undo restoration, invalid values and locked-layer rejection. Color controls,
geometry fields, validation feedback and complete control behavior remain open;
this is not full inspector parity.

Latest hosted verification: `--exercise-layer-edit --capture` selected Courtyard
geometry through the viewport, changed the selected object's layer using the
inspector select's Home key, and sent the platform undo shortcut through Foco.
The drawing JSON was restored exactly and the rebuilt inspector displayed the
original layer. The application captured a GPU frame and exited successfully.
Shared stylesheet, shared document and native layer regression tests also pass.
This verifies that interaction path only; remaining inspector fields, application
commands, full browser parity and continuous displayed resize cadence are open.

### CSS architecture review

Delivery decision: prioritize compiled HTML and predefined templates with
build-time CSS preparation and the existing native CSS runtime. The original-HTML
FocoKestrelPreview now defaults NATIVE_WEB_PREVIEW_SHARED_CSS to ON; existing CMake
caches retain their explicit selection. The typed backend remains available for
comparison and incremental optimization. This changes the preview delivery path,
not the claim of full Kestrel parity or CSS compliance. Runtime value/inline-style
interpretation remains and must be distinguished from stylesheet syntax parsing
at build time. No JavaScript or runtime HTML parsing is introduced.

Audit remaining gaps as compiler handoff, shared CSS semantics, or host behavior.
Prioritize missing engine behavior and original application functionality before
eliminating textual CSS value interpretation. Fully typed CSS lowering is a
subsequent optimization; the earlier parser-free CSS gates below describe that
stricter backend, not a prerequisite for testing the shared-runtime application.

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
no-runtime-CSS-parsing requirement of the stricter typed backend.

An embedded-CSS runtime-parser route is a proposed intermediate comparison mode,
not a change to the final acceptance contract. It can establish which original UI
features the shared engine actually renders before completing build-time lowering.
Keep HTML construction and dynamic templates compiled in every mode. The original
preview now selects prepared shared CSS by default; the separate typed sample
remains available.

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

- Courtyard native text overlay: exposes cached viewport text records and projects
  their axes using original renderer.js visibility, scale, orientation and
  dimension-flip rules. Native Canvas fill_text supports a scoped affine transform;
  preview paints aligned multiline text onto the original overlay canvas with
  matching bitmap dimensions when publishing GPU frames. Rebuilt and captured
  /tmp/kestrel-courtyard-text.png, clean exit; inspected room labels, dimension
  numbers, north label, drawing title and scale annotations now visible. Rebuilt
  native_web_text and CTest passes. Dimension background masks, formatted MText,
  custom font handling, complete original renderer overlays and performance with
  text are still unverified/incomplete; this is not full browser parity.

- Original drawing panning workload: --benchmark-courtyard fits the original
  drawing and pans back/forth through 360 ticks, keeping labels within view.
  Rebuilt/reran: 265 entities, 30 text records, 360 GPU publications, zero geometry
  rebuilds, mean CPU tick 3.49867 ms, max 4.33971 ms. Scene publications 359;
  compositor presentations 358 and one skipped attempt. 356 valid Metal times
  yield 59.999662 FPS, p95 16.666833 ms, max 16.666917 ms. Logs
  /tmp/kestrel-courtyard-pan.log and /tmp/kestrel-courtyard-pan.jsonl. The positive
  timestamp series sustains 60fps with the current text overlay, but the isolated
  skipped attempt is not characterized, and this does not prove physical input
  latency, live window resize or complete original app functionality.

- Original view action wiring: Zoom Extents data-action=fit now calls shared
  native fit_drawing logic (also used at Courtyard startup), fitting visible
  geometry. Zoom In/Out now use original app.js factors 1.35 and 1/1.35 instead
  of the diagnostic 1.25/0.8 values. FocoKestrelPreview rebuilds successfully.
  Physical action clicks and wider command/ribbon synchronization remain to be
  verified; this change does not establish full UI behavior parity.

- Original selection actions: native drawing now exposes select_all_editable,
  replacing selection with visible entities on unlocked layers as original app.js
  does. Preview wires selectall and clear-selection data-actions, refreshes explorer
  selection/summary state and invalidates GPU content. Regression verifies prior
  selection replacement, hidden entity exclusion and locked layer exclusion;
  rebuilt kestrel_native_layers CTest passes and FocoKestrelPreview builds.
  Clear-selection's original tool-cancellation behavior and inspector updates
  remain pending with the broader native application controller port.

- Native existing-selection erase/history actions: drawing.erase_selected filters
  through selected(true), removes editable IDs in one named transaction and skips
  empty selection. Preview routes original erase/undo/redo actions into native
  history, refreshes explorer and invalidates GPU content. Courtyard regression
  verifies erase count, exact data restoration by Undo, Redo removal and empty
  no-op. Rebuilt native layer test passes and FocoKestrelPreview builds. Original
  interactive erase tool on empty selection, command history messages, cancellation
  and inspector synchronization remain incomplete.

- Original active-document tab view: added predefined clean/dirty tab templates
  copied from original refreshTabs markup/icon, with native named references.
  Native panel refresh supplies active document name, ID, close accessible label
  and dirty marker from drawing state. Tests with document-tabs verify initial
  name and dirty marker after a transaction; rebuilt native layer CTest passes.
  This is the single active document view only: switching/closing and multi-document
  lifecycle still require application controller work. No runtime HTML parsing.

- Native document status bindings: title-name, selection-status and units-status
  now follow the native drawing during explorer refresh. The renderer badge
  changes to WebGPU only after the first native GPU image arrives. The native
  layer test checks initial title/status and selection updates and passes.
  Rebuilt FocoKestrelPreview and captured /tmp/kestrel-status-current.png with
  clean exit; inspected Courtyard title, active document tab, units and renderer
  badge. This does not establish full command parity or OS live-resize timing.

- Native orbit navigation: Shift+middle-drag now selects orbit at pointer-down,
  using the original app.js yaw/pitch sensitivity and pole clamps. Ordinary
  middle-drag still pans. Orbit updates the original view selector to iso.
  Native camera tests verify direction, sensitivity, fixed target, revision and
  both pitch limits; existing browser camera-reference comparisons pass.
  This does not yet port the browser workspace/navigation class updates or prove
  physical pointer capture outside the window.

- Hosted navigation exercise: --exercise-navigation injects Foco pointer events
  into the mounted original UI and checks orbit/pan mode remains determined by
  pointer-down modifiers, release and cancellation stop navigation, and a new
  GPU image is published. It exposed set_value rejecting the original select;
  native value/set_value now supports selects using live option selectedness,
  normalized fallback text and explicit empty selection for unmatched values.
  Native text tests pass, including programmatic change-event suppression.
  Rebuilt/reran hosted exercise: success marker and GPU serial 2 capture at
  /tmp/kestrel-native-orbit.png; inspected rotated Courtyard and SE isometric
  selector. Physical pointer capture, full select interaction and window resize
  performance remain unproven. Failure shutdown currently returned process zero
  in the initial failed exercise; inspect explicit success markers until the
  preview exit-code propagation is corrected.

- Original view/style dropdown change handlers now update the native camera and
  renderer. Hosted navigation exercises front/iso and shaded-edges changes and
  requires a subsequent GPU image; success marker confirmed after rebuild in
  /tmp/kestrel-dropdown-final.log. Inspected /tmp/kestrel-dropdown-current.png.
  Native popup/keyboard select interaction and workspace side effects still need
  implementation. Added --exercise-failure to reproduce failure-exit handling.
  Foco Cocoa terminate exits zero before returning the lifetime result. Replacing
  terminate with stop plus a wake event exposed exit 139 on both success/failure;
  that host change was reverted, Foco worktree is clean, and final hosted exercise
  succeeds again. Teardown requires dedicated debugging; exit codes alone remain
  insufficient evidence for preview validation.

- Cocoa shutdown root cause verified from macOS crash report: the active display
  callback continued after application host-frame shutdown cleared renderer and
  display-link resources. Foco now checks callback lifetime after reentrant input,
  layout and host-frame work. Cocoa shutdown stops/wakes the AppKit loop instead
  of terminating the process, allowing its requested exit code to return. Rebuilt
  preview: --exercise-failure returns 5; --exercise-navigation returns 0 with its
  success marker. Courtyard pan also exits 0 after 360 frames, 359 compositor
  presentations, zero skips in 5.98628 s, zero geometry rebuilds. These are backend
  counts, not physical display timestamp measurements or OS resize evidence.

- Native single-select keyboard navigation: selects are focusable; Up/Down and
  Home/End change live selection, skip disabled options/optgroups, stop at list
  boundaries, and emit input followed by change only for an actual change.
  Disposal from input prevents the subsequent change dispatch. Native text tests
  cover these behaviors and pass. Foco forwards the four navigation keys.
  Multiple selection, native popup interaction and typeahead remain unfinished.

- Hosted select keyboard verification: navigation exercise now uses Foco key
  events (Home, Down, Up) for original view/style controls instead of setting
  values and dispatching change manually. It asserts focus, handled events, front
  camera orientation, iso selection and shaded-edges renderer state, followed by
  a new GPU image. Rebuilt preview exits 0 with success marker and GPU serial 2;
  inspected /tmp/kestrel-keyboard-select.png including focused control styling.
  This verifies injected host events, not physical keyboard or popup operation.

- Native panel actions now toggle the original workbench hide-explorer and
  hide-properties classes, preserving unrelated classes and unchanged CSS. Hosted
  navigation injects temporary native action buttons, verifies each hide expands
  canvas width and restoring both recovers the initial width; run exits 0 with
  success marker. The original painted-button lookup failed, so this is action/
  layout validation only, not proof original icon buttons are visible/clickable.
  Original shell data-icon hydration remains an interaction/visual gap. This
  same-tick layout check does not measure intermediate GPU sizes or resize FPS.

- Original shell icon hydration: extracted the 17 data-icon names used in the
  pinned index.html and expanded original UI.icon definitions into predefined
  SVG templates in LayerRows.html. Native construction traverses child snapshots
  and instantiates icons into authored placeholders; panel disposal removes owned
  icon roots. Added native document children snapshots. No runtime HTML parser
  or JS is used; original index.html/style.css are unchanged. Layer tests verify
  SVG construction and disposal and pass. Rebuilt preview exits 0 and capture
  /tmp/kestrel-shell-icons.png visibly includes search/settings/panel arrows and
  other previously missing shell icons. Icon visibility is proven; individual
  commands still require their own port/interaction verification.

- Original panel pointer verification replaces temporary test buttons: locate
  authored viewport-controls/statusbar actions, require nonzero hit areas, then
  inject Foco primary pointer press/release at their centers. The hosted exercise
  verifies both hide actions expand canvas width and both show actions restore it;
  original controls remain reachable with panels hidden. Rebuilt run exits 0 with
  success marker in /tmp/kestrel-original-panel-pointer.log. This closes the
  original-control hit-routing gap for these two actions, not OS event delivery
  or intermediate resize presentation timing.

- Actual macOS window resize exercised using CGEvent corner drag: 360 steps
  shrink 300x180 points and restore. Native trace reports 361 resize events,
  357 content frames, zero deadline misses, selected/native cadence 60 Hz.
  Presentation timestamps are almost entirely zero, so displayed FPS cannot be
  established from this run. See native-web-os-resize-evidence.json for isolated
  gesture counts. No claim of visual no-stretch or displayed 60fps. Raw logs are
  /tmp/kestrel-window-resize-current.log and matching .jsonl; app remains running
  under exec session 91177 for further investigation.

- Live-resize presentation trace now records transaction mode, drawable ID and
  dimensions plus callback arrival separately from presentedTime. Repeated real
  OS drag: 381 transaction content callbacks had one valid presentation timestamp;
  58 retained transaction callbacks had none. Content used 181 distinct sizes.
  Callback arrival must not be substituted for display timing. Results added to
  native-web-os-resize-evidence.json; no displayed-FPS conclusion. Current app
  remains running under exec session 56253, PID 16828.

- Actual window midpoint capture during held OS resize: screencapture targets the
  Kestrel CGWindow, pauses 0.2 s at the inward endpoint, then restores the window.
  /tmp/kestrel-live-resize-midpoint.png shows narrow-window media layout hiding
  Properties, horizontal ribbon overflow, and clipped drawing without obvious
  stretching in that frame. This is sampled visual evidence, not continuous
  motion or 60fps proof; midpoint capture metadata added to resize evidence.

- Native theme action now updates authored root data-theme, GPU light-theme
  options and original sun/moon SVG path. Hosted light-theme capture initially
  exposed :root failing for native parentless html nodes: shared compound matcher
  recognized wrapper-based roots only. Added parentless html recognition and a
  shared CSS regression proving root-attribute custom-variable recascade dark to
  light and back. Rebuilt shared-style suite passes. Hosted capture exits 0;
  /tmp/kestrel-native-light-fixed.png confirms light shell, grid, geometry, text
  overlay and moon icon. Original stylesheet remains unchanged. Preferences and
  sheet-specific theme policy remain unported.

- Native Show all layers action: original all-layers-on command invokes an
  undoable Show all layers transaction, refreshing explorer and GPU state. Tests
  verify visibility restoration, preserved locks, no redundant history entry,
  exact undo restoration and redo; layer suite passes. Preview rebuilt. Also
  rebuilt/reran shared generated-document CSS suite after parentless :root fix;
  linked/embedded/inline CSS test passes. Physical command interaction and full
  layer manager remain separate gaps.

- Native viewport click selection: added screen-space segment distance/depth
  picking, basic multiline text bounds and shaded mesh-face depth, with browser
  distance thresholds/tie-breaking. Selection handles visible groups, modifier
  toggling, Ctrl/Meta group bypass and empty-click clearing. Native tests cover
  lines, tolerance, visibility, groups and empty clicks and pass. Primary pointer
  clicks invoke picking; movement over four pixels cancels click selection.
  Hosted test exposed original canvas pointer-events:none, so viewport itself
  is accepted as the authored click target. Rebuilt hosted --exercise-picking
  exits 0 and capture /tmp/kestrel-native-picked.png shows selected count 1 and
  updated drawing. Spatial indexing, rich/composed text hit bounds, dedicated
  mesh/text picking tests, drag-box selection, grips and hover remain incomplete.

- Picking validation expanded: native tests now cover mesh interior exclusion in
  wireframe, face picking in shaded mode, nearer overlapping face depth, hidden
  mesh exclusion, and centered multiline text hit/miss bounds. Rebuilt layer/
  picking test passes. This closes the dedicated basic mesh/text test gap from
  aa2ce684; composed text, spatial indexing and comprehensive browser differential
  picking coverage remain open.

- Native keyboard event seam: document.key now dispatches bubbling keydown with
  explicit key and modifiers before built-in keyboard defaults. prevent_default
  suppresses default editing; disposal from the listener stops processing safely.
  Native text suite rebuilt/passes, including root bubbling, modifier preservation,
  cancelled deletion, ordinary deletion and disposal. Kestrel shortcut routing
  and full host key coverage still need implementation; this is their native API
  prerequisite, not a claim that erase/undo shortcuts are working yet.

- Native editing shortcuts: Foco forwards A/Z/Y, Escape and F7 in addition to
  existing delete keys. Root keydown handles select-all, erase, undo/redo, Escape
  selection/navigation cancellation and grid toggle, while input/textarea/select
  and contenteditable ancestors retain editing behavior. Open modal/palette guards
  prevent underlying shortcuts. Added native tag_name introspection. Hosted
  --exercise-shortcuts verifies select-all/erase, Ctrl and platform-modifier undo,
  redo, exact document restoration, Backspace text isolation, Escape and grid.
  Rebuilt run exits 0 with success marker and GPU serial 2 capture. Full tool
  cancellation, remaining shortcut commands and physical-key delivery remain open.

- Native window/crossing selection: added containment and crossing tests using
  projected entity bounds, segment intersections, basic text overlap and shaded
  face containment. Editable group expansion and additive selection follow the
  original selectWindow model. Native tests prove crossing can select a segment
  with both endpoints outside, containment rejects partial entities, full bounds
  selects and locked layers are excluded. Suite passes. Preview primary drags
  update original selection-window style/class, select on release, and hide on
  release/cancel/Escape; preview builds. Hosted drag/capture verification remains
  required, along with broader mesh/text/group differential coverage.

- Hosted drag selection verification: --exercise-drag-selection injects primary
  press/move/release into the original viewport, checks visible crossing rectangle
  geometry/class, verifies multiple selected entities and hidden rectangle after
  release, then cancels another drag and checks unchanged selection/cleanup.
  Rebuilt run exits 0, selects 257 Courtyard entities and captures GPU serial 2 at
  /tmp/kestrel-drag-selected.png; inspected cyan geometry/text highlights and
  selected count. Physical pointer capture outside the window and full browser
  differential rectangle selection remain open.

- Inspector no-selection state: compiled original inspector header/general/option
  templates, populated current layer, color swatch, units and visual-style summary
  through native APIs. Current-layer select change updates the drawing and panel.
  Native layer tests verify populated no-selection content and disposal; pass.
  Rebuilt preview exits 0 and /tmp/kestrel-inspector-empty.png shows original
  Properties layout. Selected-object inspector, color/default editing, ByLayer
  action and workspace-dependent summary refresh remain incomplete. The temporary
  workspace label is still 2D drafting until native workspace state is ported.

- Native view-state synchronization: named view changes now follow original
  2D/3D workspace policy and enable shaded edges for wireframe mesh drawings
  entering non-top/bottom views. Orbit enters modeling state. Style/workspace
  selectors, model-space label and inspector summary update together, guarded by
  actual state changes to avoid per-orbit-frame inspector rebuilds. Rebuilt
  --exercise-navigation exits 0; /tmp/kestrel-view-state.png confirms iso, shaded
  edges and consistent 3D modeling labels. Full workspace-switch defaults, ribbon
  changes and selected-object inspector remain unported.

- Selected-object inspector layer editing: compiled original type-header variants
  and Layer row, with multi-selection placeholder and native option population.
  Change applies Edit layer transaction to the precomputed editable selection;
  invalid target and fully locked selection make no change. Tests verify layer
  changes, destination lock behavior and exact undo restoration; layer suite
  passes. Rebuilt hosted picking capture exits 0 and shows Polyline/A-WALL in
  /tmp/kestrel-inspector-selected.png. Remaining general/geometry property rows,
  locked-edit error feedback and hosted layer-change interaction remain open.

### Native continuous Line command state

Ported the original app's continuous Line point/segment state into the exported
C++ drawing module. Each nonduplicate segment uses a Line transaction and current
layer, color and lineweight; command Undo removes the last segment/point, and
cancel clears pending points without deleting committed geometry. Locked-layer
failure preserves both document and pending endpoint. The native drawing suite
passes, covering chained segments, 1e-8 duplicate tolerance, defaults, Undo,
locked-layer rejection and cancellation. This is command state only: connecting
the original ribbon, viewport coordinates, snapping, rubber-band preview, numeric
input and command prompts remains necessary before claiming Line tool parity.

### Original ribbon and viewport Line wiring

The original compiled ribbon Line action now starts native line_command; primary
viewport pointer input unprojects onto the XY plane and commits connected
segments, bypassing selection while the command is active. Near-edge-on views
switch to top, Escape cancels, and the original banner/prefix display prompts or
locked-layer errors. Existing middle-button navigation is retained. Foco preview
build and startup/capture smoke pass (GPU serial 1, clean exit). This smoke does
not verify physical Line pointer interaction. Rubber-band preview, snapping,
command-line Undo/numeric entry, drawing-default UI synchronization and complete
application tool transitions remain unfinished; do not claim Line parity yet.

### Hosted original Line interaction evidence

Added --exercise-line-draw to the Foco preview. The check locates the original
compiled ribbon button, sends press/release events through the Foco WebScene
view, then clicks two viewport positions. It verifies the first point leaves the
model unchanged and the second creates one LINE with endpoints matching camera
unprojection. Native Escape cancels pending state; platform Undo restores the
exact document snapshot. The rebuilt hosted run passed and captured with GPU
serial 2, exiting 0. This exercises synthetic Foco view input, not physical OS
mouse ingestion, snapping, rubber-band feedback, or a pixel check of the new line.

### Line command-field options

The existing compiled command input handles Line U/UNDO and empty Enter/ENTER
finish, plus ESC/CANCEL aliases. Recognized options clear the input, update the
original prompt and refresh native model/viewport state. The hosted Line check
now types lowercase u through Foco text input, presses Enter through Foco key
input, verifies exact drawing restoration with the previous endpoint retained,
and creates another segment by pointer before Escape/global Undo. Build and
hosted check pass (GPU serial 2, exit 0). Numeric coordinates, semicolon command
sequences, history/suggestions and general command dispatch remain unported;
finish aliases have not yet received a dedicated hosted assertion.

### Native drafting coordinate parser

Added parse_drafting_point in the exported drawing module for absolute XY/XYZ,
relative @ coordinates and distance<angle polar input. It follows original
parsePoint elevation handling and the finite +/-1e12 operand bound. Native
drawing tests pass for inherited Z, explicit Z, relative/polar values and malformed
or nonfinite input. This helper is not yet wired to the command field; persisted
lastPoint and scalar distance entry remain open. std::stod numeric conversion
also requires a separate JavaScript Number compatibility audit (binary/octal,
Unicode whitespace, locale and hex forms); these tests do not prove full grammar
parity. No runtime HTML or JavaScript was introduced.

### Hosted typed Line coordinates

Connected parse_drafting_point to the original compiled command input. Pending
endpoints take precedence over the last committed drafting point for relative
coordinates. Pointer and typed segment commits both update that stored base;
malformed coordinate input leaves the model and pending points unchanged and
shows the error in the existing banner. The hosted Line exercise now types
100,200,5 then @10,20, verifies exact XYZ endpoints, rejects 1x,2 without mutation,
finishes with empty Enter without deleting geometry, and undoes to the exact
original document. Rebuilt hosted run passes, GPU serial 2, exit 0. Polar input
uses the same native parser but has only unit-level evidence so far. Scalar
lengths, snapping, preview, general command dispatch and JavaScript Number grammar
parity remain open.

### Drafting Number grammar alignment

Replaced locale-dependent stod with validated decimal syntax and classic-locale
conversion plus unsigned 0x/0o/0b integer handling. Coordinate trimming recognizes
the same 25 ECMAScript whitespace tokens used for group names. Explicit tests
cover radix integers, exponent/leading-dot/trailing-dot decimals, Unicode trim,
signed-hex and hex-float rejection, invalid radix digits and decimal underflow.
Node Number reference outputs agree for the tested forms. Initial native test
exposed libc++ underflow failbit; validated zero underflow now follows Number.
Native drawing suite passes after that correction. This is tested grammar
alignment, not exhaustive floating-point conversion conformance or hosted proof;
the next hosted build must incorporate the updated drawing module.

### Native Canvas stroke API for drafting overlays

Exposed document::stroke_line using the existing Canvas command stream (saved
paint state, identity transform, stroke color/width, begin/move/line/stroke,
restore). Invalid coordinates and nonpositive widths are ignored; non-canvas
nodes reject the operation. Native library build and existing text regression
suite pass. Dedicated stroke-command/pixel tests and Line rubber-band integration
are still required; this change alone does not provide visible preview behavior.

### Initial native Line overlay wiring

Extracted overlay text drawing into redraw_overlay and added pending Line preview
from the last endpoint to the current unprojected pointer. It uses native Canvas
strokes with original dark/light colors, 1.45 width and 5/4 dash lengths. Pointer
motion marks only overlay state dirty; GPU publication redraws text and preview
for camera changes. Prompt changes invalidate preview cleanup. Hosted build and
existing Line creation/typed-input/Undo regression pass, GPU serial 2. These
checks do not yet assert a rendered preview: dedicated motion/capture, clipping
for distant endpoints, stroke command verification and preview latency remain
required. Snapping and dynamic dimension labels remain open.

### Bounded Line preview clipping

Moved preview dash expansion into the native render-data module. Parametric
viewport clipping precedes dash generation, preserving the original 5/4 phase
while making command count depend on visible length rather than endpoint distance.
Native render-data tests pass for a two-billion-unit segment, edge phase,
fully offscreen and zero-length segments. Floating comparisons use a 1e-9
tolerance. Preview calls this shared helper; hosted rebuild and visual motion
verification remain required. This closes excessive offscreen dash generation,
not the broader preview fidelity or performance acceptance gates.

### Hosted pending Line preview capture

Added --show-line-preview, which runs existing Line regressions then clicks a
first point and sends Foco pointer movement. It checks unchanged model state,
overlay invalidation and emitted stroke commands. Hosted run passed with 40
Canvas strokes and GPU serial 2, exit 0. Inspected /tmp/kestrel-pending-line.png:
the cyan dashed segment is visible over the drawing alongside the original Line
banner and command prompt. This is static dark-theme render evidence, not a
continuous-motion latency or 60fps proof. Light theme, cleanup capture, snapping
and physical pointer motion still need verification.

### Preview camera reprojection and cancellation

Pending Line pointer state now retains client coordinates and reprojects against
the current camera/scene bounds on overlay redraw. Previously its saved world
position could detach from the cursor during camera movement. Hosted preview
exercise passes a 40/15 camera pan projection check (within .01 screen units),
restores the camera, dispatches Escape through Foco input, verifies zero remaining
stroke commands and unchanged model, then recreates the visible capture. Build
and hosted run pass, GPU serial 2, exit 0. This tests direct camera mutation and
synthetic host input; actual continuous navigation, resize latency and snapping
remain unverified.

### Native grid/ortho/polar constraint semantics

Added constrain_drafting_point in the drawing module, porting the portion of
original snapPoint after object-snap selection. Grid rounding precedes ortho or
polar restriction; XY constraints preserve incoming Z. Math.round negative ties
use floor(x+.5), and ortho equal-axis ties choose horizontal. Native drawing suite
passes horizontal/vertical/tie, negative grid tie and negative polar tie cases.
This is a reusable native helper only: original status toggles, Shift tracking,
shared preview/commit integration and object-snap candidate selection remain to
be connected. No snapping capability is claimed in the running app yet.

### Shared pointer constraint path and Shift wiring

Native Line preview reprojection and pointer commit now use drafting_pointer,
which applies the tested native ortho constraint when Shift is held. Pointer
modifiers update state, and keydown/keyup modifier changes invalidate the overlay
for stationary-pointer feedback. Typed coordinates remain explicit and bypass
pointer constraints as in the original app. Foco build and existing hosted Line,
camera alignment and cleanup regressions pass (GPU serial 2). A dedicated hosted
Shift press/release/commit assertion is still required. Grid/polar/status toggles
are not wired yet; original status-toggles HTML is generated in upstream app.js
and needs a predefined compiled template. Object snaps remain separate work.

### Hosted Shift preview/commit agreement

Extended hosted preview exercise to send Shift-modified pointer movement, require
an axis-constrained preview, commit through Shift pointer press/release and compare
the resulting endpoint against that preview. Command-field Undo restores the
exact prior document and anchor. A subsequent unmodified pointer movement restores
unconstrained preview. Rebuilt hosted run passes, GPU serial 2, exit 0.
Stationary Shift release remains unproven: Foco key_event has no press/release
field, so a DOM keyup listener alone does not prove host release delivery. Audit
that input seam before claiming stationary modifier fidelity. Grid/polar toggles,
object snaps and overall performance/parity gates remain open.

### Native key-release delivery seam

Foco already routes ordinary Cocoa keyUp through route_key_released and virtual
key_released_received; the native WebScene view lacked that override. Added
native document::key_release (keyup only, no default editing), host forwarding
and a shared key-name mapping for press/release. Native text tests pass focused
target/bubbling, modifier propagation, no Backspace edit on release and disposal
during dispatch. The hosted adapter requires rebuild/verification. Modifier-only
Cocoa flagsChanged delivery and missing modifier key names remain open; this
change does not yet prove stationary Shift release works.

### Hosted ordinary key-release forwarding

Rebuilt Foco preview with the native release adapter. Hosted check sends a key
press with Shift modifiers then ordinary key release without them, without moving
the pointer; it verifies modifier clearing and unconstrained reprojection.
Run passes with existing Line checks, GPU serial 2, exit 0. This synthetic pair
uses key::a solely to exercise the release callback and is not proof of physical
Shift release. Cocoa audit confirms input key enum lacks modifier key values and
Foco's input view lacks flagsChanged. Add modifier values/mapping and explicit
flagsChanged routing in Foco next, then verify physical or injected Cocoa modifier
events through the complete route.

### Cocoa modifier routing implementation

Foco appends Shift/Control/Alt/Meta key values and maps both Cocoa sides. Its input
view now handles flagsChanged with device-specific left/right mask bits, routing
press/release through existing input callbacks. Native WebScene maps these keys
to DOM names. Hosted test uses actual key::shift instead of the previous surrogate
letter and passes stationary preview release, pointer commit and cleanup checks.
Full Foco preview rebuild passes, hosted run exits 0 with GPU serial 2. Physical
Cocoa event injection, both-side chord transitions and focus-loss reconciliation
remain unverified; this hosted test enters at the Foco view API.

### Original compiled status toggle markup

Copied original app.js status markup output using the original UI icon/command
metadata into a predefined RibbonTabs template, instantiated at status-toggles.
Compiler now preserves standard title attributes in templates. Native SNAP,
ORTHO and POLAR action handlers feed the shared pointer constraints and enforce
ortho/polar mutual exclusion; spacing is currently 100 and polar step 15 degrees.
Full hosted build and existing Line regression pass; inspected /tmp/kestrel-status.png
shows original six buttons. Dedicated toggle interaction checks, grid active-state
synchronization, settings persistence/editors, OSNAP and lineweight behavior remain
open. Title preservation is not proof of native tooltip display.

### Hosted status toggle verification

Status active classes now synchronize Grid at viewport creation, pointer toggle
and F7, alongside Snap/Ortho/Polar. Hosted preview check clicks original compiled
buttons through Foco input, verifies Ortho/Polar mutual exclusion and classes,
checks snapped preview coordinates are multiples of 100, and compares Grid class
with renderer state before restoring settings. Build and hosted run pass, GPU
serial 2, exit 0. Polar angle geometry is unit-tested but not yet independently
asserted in this hosted toggle check; configurable spacing/angles, persistence,
OSNAP and LWT remain unfinished.

### Native object-snap candidate baseline

Added nearest_object_snap to render-data using existing native geometry snaps,
projected distance <11 pixels, visibility filtering and excluded entity IDs.
It returns world/screen point, type, entity ID and distance. Native render-data
tests pass endpoint selection, exclusion, hidden entities and visible locked
references. This baseline scans/rebuilds geometry and is not connected to pointer
motion: retain geometry/spatial indexing before enabling OSNAP in the application.
Original intersection candidate handling, nearby-index ordering/ties and marker
rendering remain open. This is not full object-snap parity.

### Retained native object-snap candidates

Added object_snap_index retaining visible native geometry snap candidates by
model identity/revision. Pointer/camera changes reuse geometry; query projection
uses the current camera. Transactions and Undo rebuild candidates; direct writes
to public model.data require explicit invalidate(). Native render-data tests
verify reuse, pan reprojection, transaction/Undo refresh and explicit invalidation.
The class currently scans retained candidates; despite its index name it is not
yet a spatial index. Screen-space lookup, intersection candidates and application
OSNAP wiring/markers remain open. No pointer performance acceptance claim yet.

### Screen-space object-snap cells

object_snap_index now caches projected candidates in 11-pixel cells and queries
only the surrounding 3x3 cells. Camera identity/revision changes rebuild screen
projection, while model changes rebuild geometry and projection. Equal-distance
candidates preserve source order explicitly across cell traversal. Native
render-data tests pass pointer projection reuse, camera reprojection, existing
invalidation checks and zero examined candidates for an empty-area query.
This is candidate spatial lookup, not intersection indexing or measured complete
OSNAP frame performance. Camera changes must use revision-updating camera APIs;
direct model writes still require explicit invalidation.

### Native intersection snaps and hosted OSNAP commit

Retained LINE/POLYLINE segments now supply same-elevation intersections using
original segment-distance (<22), 41-candidate cap, intersection bounds tolerance
and nearest-snap (<11) rules. Existing equal-distance midpoint wins, as upstream.
Native render-data tests pass off-midpoint crossing, midpoint tie priority and
different-elevation rejection. Segment nearby filtering currently scans retained
segments, unlike upstream's entity index; dense-scene order/performance parity
needs further work.

Connected original OSNAP status action to native Line pointer/preview, enabled
by default and taking priority over grid/ortho constraints. Hosted check toggles
OSNAP via original button, clicks offset from a visible candidate, verifies the
committed endpoint equals the snap and undoes to the exact prior document.
Build and hosted check pass, GPU serial 2, exit 0. Existing geometric test fixtures
explicitly disable OSNAP while testing raw coordinate projection. Snap marker
rendering, default-on interaction captures, direct-model-write invalidation audit
and OSNAP latency remain open; full Kestrel parity is not established.

### Initial native snap marker feedback

Drafting pointer retains selected snap metadata; overlay draws original 6-pixel
marker shapes (midpoint triangle, intersection cross, other square) and 10px type
label with original light/dark colors. Center/quadrant circles currently use 32
line segments, so exact native arc rendering remains a fidelity task. Marker
visibility follows active Line, valid pointer, OSNAP and selected target. Hosted
build and existing OSNAP click/Undo regressions pass, GPU serial 2. The current
capture fixture disables OSNAP before its final image, so it does not visually
verify markers. Dedicated selected-target capture and marker cleanup checks are
still required.

### Delivery steering: hybrid application now

User explicitly selected hybrid JS/C++ support now, retaining existing JS CAD
logic and pausing further general feature ports to C++ today. HTML-producing JS
must be moved to compiled templates/C++ construction; runtime HTML parsing remains
forbidden. Added complete pinned 37-script migration inputs and source/hash/API
inventory under samples/NativeKestrel/hybrid. This corrects the earlier eight-file
source-size picture: entry scripts total 9,621 lines, with 27 HTML API occurrence
sites before auditing all producers. Hybrid is not running yet. See hybrid README
for shared-DOM, single-state ownership and implementation/verification gates.
