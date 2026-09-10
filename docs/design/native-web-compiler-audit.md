# Native Web compiler support audit

Status: open. Compiler audit and gap closure take priority over further Kestrel
application porting. Preserve original inputs; preview omission is not support.

Baseline regenerated on 2026-09-10 at revision 4883452c: the reference stylesheet
contains 397 rules and 1475 declarations, with 80 distinct unsupported constructs.
This corpus is a compatibility input, not the complete compiler specification.
Audit supported features too: accepting syntax does not prove correct semantics.

## Work order and closure gates

| Order | Feature family | Required closure evidence | Status |
| --- | --- | --- | --- |
| 1 | Lexing, numeric grammar, declaration extraction and diagnostics | Valid CSS numeric forms; nested functions; invalid-source diagnostics with locations; inspect apparent truncated `color:var(` before assigning blame | Open |
| 2 | HTML semantics, attributes and compiled templates | Inventory elements/attributes; native form/control behavior; preserved template structure, references and disposal; no runtime HTML parsing | Open |
| 3 | Cascade, selectors, variables and conditional rules | Initial/inherited/unset behavior, specificity, pseudo-elements, media conditions, dynamic mutations; parsed/compiled differential tests | Open |
| 4 | Layout and typography | Insets, calc, grid placement, wrapping, ellipsis, font behavior; resize and mutation comparisons | Open |
| 5 | Paint and SVG | Gradients, color mixing, borders/outlines, shadows, transforms, filters, SVG stroke/text; rendered comparisons | Open |
| 6 | Input and native control presentation | Cursor, selection, touch action, scrollbars, resizing and form colors; interaction tests | Open |
| 7 | Animation and resource pipeline | Keyframes/transitions/reduced motion; embedded images/fonts; packaged offline verification | Open |
| 8 | Strict compilation and regression closure | Original inputs compile without preview omissions; full tests and rendered/interactive comparisons; remaining exclusions explicitly resolved | Open |

For each gap, record the source construct, responsible layer (compiler, native
engine, host or resource pipeline), implementation commit, regression fixture and
validation result. Group related grammar/semantic changes rather than adding
isolated sample-specific exceptions. Do not mark a group complete from a lower
warning count alone. Deliberately unsupported browser-only behavior needs an
explicit semantic decision, not a silent no-op.

## Reproduction

Run `webscene-uic --check-css samples/NativeKestrel/reference/src/style.css`.
For structural diagnostics, compile the original index with `--preview` into a
temporary module and inspect all warnings. Preview is an inventory aid only.
Extend this audit to template files, inline styles and independent feature fixtures.
The HTML inventory below is provisional; warning coverage itself must be checked.

## Execution discipline

Complete this audit before resuming Kestrel-driven feature porting. Work through
feature families in the order above, using independent HTML/CSS fixtures first;
Kestrel remains an unchanged compatibility corpus. Within each family:

1. Inventory accepted and rejected syntax, including inline styles and templates.
2. Separate missing compiler lowering from missing engine or host semantics.
3. Implement coherent grammar/behavior groups and test invalid inputs as well as
   successful compilation, mutation, layout and paint where applicable.
4. Record remaining exclusions and evidence here. A family remains open until its
   closure gate is met; fewer preview warnings alone do not close it.

Next audit work should return to the earliest open gates: source locations and
numeric/function grammar, followed by the HTML element/attribute/template
inventory. Paint investigations below identify later engine work rather than
changing this order. Browser differential and packaged-resource verification
remain required before overall closure.

## CSS baseline diagnostics

```text
samples/NativeKestrel/reference/src/style.css: error: #command-input::placeholder: unsupported selector: #command-input::placeholder
samples/NativeKestrel/reference/src/style.css: error: #ribbon-tab-list button.active:after: unsupported selector: #ribbon-tab-list button.active::after
samples/NativeKestrel/reference/src/style.css: error: -webkit-font-smoothing:antialiased: unsupported Native Web CSS property: -webkit-font-smoothing
samples/NativeKestrel/reference/src/style.css: error: .document-tab.active:after: unsupported selector: .document-tab.active::after
samples/NativeKestrel/reference/src/style.css: error: .panel-tabs button.active:after: unsupported selector: .panel-tabs button.active::after
samples/NativeKestrel/reference/src/style.css: error: .progress-line:after: unsupported selector: .progress-line::after
samples/NativeKestrel/reference/src/style.css: error: .property-section-title:before: unsupported selector: .property-section-title::before
samples/NativeKestrel/reference/src/style.css: error: @keyframes progress: unsupported at-rule or condition
samples/NativeKestrel/reference/src/style.css: error: @keyframes toast-in: unsupported at-rule or condition
samples/NativeKestrel/reference/src/style.css: error: @media (prefers-reduced-motion:reduce): unsupported at-rule or condition
samples/NativeKestrel/reference/src/style.css: error: @media print: unsupported at-rule or condition
samples/NativeKestrel/reference/src/style.css: error: accent-color:var(--accent): compiled var() not supported for property: accent-color
samples/NativeKestrel/reference/src/style.css: error: animation:none: unsupported Native Web CSS property: animation
samples/NativeKestrel/reference/src/style.css: error: animation:progress 1.2s infinite ease-in-out: unsupported Native Web CSS property: animation
samples/NativeKestrel/reference/src/style.css: error: animation:toast-in .15s ease-out: unsupported Native Web CSS property: animation
samples/NativeKestrel/reference/src/style.css: error: backdrop-filter:blur(3px): unsupported Native Web CSS property: backdrop-filter
samples/NativeKestrel/reference/src/style.css: error: backdrop-filter:blur(9px): unsupported Native Web CSS property: backdrop-filter
samples/NativeKestrel/reference/src/style.css: error: background:color-mix(in srgb,var(--active) 63%,var(--panel)): compiled color-mix currently supports an sRGB color percentage mixed with transparent
samples/NativeKestrel/reference/src/style.css: error: background:linear-gradient(125deg,var(--panel2),var(--panel)): unsupported custom-value token: linear-gradient(125deg,var(--panel2),var(--panel))
samples/NativeKestrel/reference/src/style.css: error: background:linear-gradient(145deg,var(--panel2),var(--panel)): unsupported custom-value token: linear-gradient(145deg,var(--panel2),var(--panel))
samples/NativeKestrel/reference/src/style.css: error: background:none: color profile requires #rgb, #rgba, #rrggbb, #rrggbbaa, black, white or transparent
samples/NativeKestrel/reference/src/style.css: error: border-collapse:collapse: unsupported Native Web CSS property: border-collapse
samples/NativeKestrel/reference/src/style.css: error: border:1px dashed #65c5a4: border shorthand currently requires width solid color, 0 or none
samples/NativeKestrel/reference/src/style.css: error: border:2px dashed var(--accent): border shorthand currently requires width solid color, 0 or none
samples/NativeKestrel/reference/src/style.css: error: bottom:calc(var(--command-height) + 74px): unsupported custom-value token: calc(var(--command-height)
samples/NativeKestrel/reference/src/style.css: error: box-shadow:0 0 0 1px color-mix(in srgb,var(--accent) 25%,transparent): unsupported custom-value token: color-mix(in
samples/NativeKestrel/reference/src/style.css: error: box-shadow:inset 0 -2px 0 var(--accent): native compiled inset shadows are not supported yet
samples/NativeKestrel/reference/src/style.css: error: box-shadow:inset 0 0 0 1px var(--accent-dim): native compiled inset shadows are not supported yet
samples/NativeKestrel/reference/src/style.css: error: box-shadow:inset 2px 0 0 var(--accent): native compiled inset shadows are not supported yet
samples/NativeKestrel/reference/src/style.css: error: color-scheme:dark: unsupported Native Web CSS property: color-scheme
samples/NativeKestrel/reference/src/style.css: error: color-scheme:light: unsupported Native Web CSS property: color-scheme
samples/NativeKestrel/reference/src/style.css: error: color:var(: unclosed variable reference
samples/NativeKestrel/reference/src/style.css: error: content:"": unsupported Native Web CSS property: content
samples/NativeKestrel/reference/src/style.css: error: content:"⌄": unsupported Native Web CSS property: content
samples/NativeKestrel/reference/src/style.css: error: cursor:col-resize: unsupported Native Web CSS property: cursor
samples/NativeKestrel/reference/src/style.css: error: cursor:crosshair: unsupported Native Web CSS property: cursor
samples/NativeKestrel/reference/src/style.css: error: cursor:grabbing: unsupported Native Web CSS property: cursor
samples/NativeKestrel/reference/src/style.css: error: cursor:move: unsupported Native Web CSS property: cursor
samples/NativeKestrel/reference/src/style.css: error: cursor:not-allowed: unsupported Native Web CSS property: cursor
samples/NativeKestrel/reference/src/style.css: error: cursor:pointer: unsupported Native Web CSS property: cursor
samples/NativeKestrel/reference/src/style.css: error: cursor:row-resize: unsupported Native Web CSS property: cursor
samples/NativeKestrel/reference/src/style.css: error: dialog::backdrop: unsupported selector: dialog::backdrop
samples/NativeKestrel/reference/src/style.css: error: filter:brightness(1.1): unsupported Native Web CSS property: filter
samples/NativeKestrel/reference/src/style.css: error: font-synthesis:none: unsupported Native Web CSS property: font-synthesis
samples/NativeKestrel/reference/src/style.css: error: grid-column:1/-1: unsupported Native Web CSS property: grid-column
samples/NativeKestrel/reference/src/style.css: error: inset:0: unsupported Native Web CSS property: inset
samples/NativeKestrel/reference/src/style.css: error: inset:15px: unsupported Native Web CSS property: inset
samples/NativeKestrel/reference/src/style.css: error: letter-spacing:-.4px: unsupported length: -.4px
samples/NativeKestrel/reference/src/style.css: error: letter-spacing:.1px: unsupported length: .1px
samples/NativeKestrel/reference/src/style.css: error: letter-spacing:.25px: unsupported length: .25px
samples/NativeKestrel/reference/src/style.css: error: letter-spacing:.2px: unsupported length: .2px
samples/NativeKestrel/reference/src/style.css: error: letter-spacing:.35px: unsupported length: .35px
samples/NativeKestrel/reference/src/style.css: error: letter-spacing:.5px: unsupported length: .5px
samples/NativeKestrel/reference/src/style.css: error: letter-spacing:.6px: unsupported length: .6px
samples/NativeKestrel/reference/src/style.css: error: letter-spacing:.7px: unsupported length: .7px
samples/NativeKestrel/reference/src/style.css: error: opacity:.34: invalid numeric value
samples/NativeKestrel/reference/src/style.css: error: opacity:.45: invalid numeric value
samples/NativeKestrel/reference/src/style.css: error: opacity:.8: invalid numeric value
samples/NativeKestrel/reference/src/style.css: error: outline-offset:-2px: unsupported Native Web CSS property: outline-offset
samples/NativeKestrel/reference/src/style.css: error: outline:2px solid var(--accent): compiled var() not supported for property: outline
samples/NativeKestrel/reference/src/style.css: error: outline:none: unsupported Native Web CSS property: outline
samples/NativeKestrel/reference/src/style.css: error: overflow-wrap:anywhere: unsupported Native Web CSS property: overflow-wrap
samples/NativeKestrel/reference/src/style.css: error: resize:vertical: unsupported Native Web CSS property: resize
samples/NativeKestrel/reference/src/style.css: error: right:calc(var(--right-width) + 16px): unsupported custom-value token: calc(var(--right-width)
samples/NativeKestrel/reference/src/style.css: error: scrollbar-color:var(--line) transparent: compiled var() not supported for property: scrollbar-color
samples/NativeKestrel/reference/src/style.css: error: scrollbar-width:none: unsupported Native Web CSS property: scrollbar-width
samples/NativeKestrel/reference/src/style.css: error: scrollbar-width:thin: unsupported Native Web CSS property: scrollbar-width
samples/NativeKestrel/reference/src/style.css: error: stroke-width:.8: unsupported Native Web CSS property: stroke-width
samples/NativeKestrel/reference/src/style.css: error: stroke-width:1.25: unsupported Native Web CSS property: stroke-width
samples/NativeKestrel/reference/src/style.css: error: stroke-width:1.4: unsupported Native Web CSS property: stroke-width
samples/NativeKestrel/reference/src/style.css: error: text-anchor:middle: unsupported Native Web CSS property: text-anchor
samples/NativeKestrel/reference/src/style.css: error: text-overflow:ellipsis: unsupported Native Web CSS property: text-overflow
samples/NativeKestrel/reference/src/style.css: error: touch-action:none: unsupported Native Web CSS property: touch-action
samples/NativeKestrel/reference/src/style.css: error: transform:none: unsupported Native Web CSS property: transform
samples/NativeKestrel/reference/src/style.css: error: transform:translateX(-50%): unsupported Native Web CSS property: transform
samples/NativeKestrel/reference/src/style.css: error: transform:translateY(7px): unsupported Native Web CSS property: transform
samples/NativeKestrel/reference/src/style.css: error: transition:fill .15s: unsupported Native Web CSS property: transition
samples/NativeKestrel/reference/src/style.css: error: transition:none: unsupported Native Web CSS property: transition
samples/NativeKestrel/reference/src/style.css: error: user-select:none: unsupported Native Web CSS property: user-select
samples/NativeKestrel/reference/src/style.css: error: user-select:text: unsupported Native Web CSS property: user-select
397 rules, 1475 declarations, 80 distinct unsupported constructs
```

## HTML structural baseline diagnostics

```text
generic native attribute: accept
generic native attribute: autocapitalize
generic native attribute: autocomplete
generic native attribute: hidden
generic native attribute: href
generic native attribute: placeholder
generic native attribute: spellcheck
generic native attribute: title
generic native attribute: transform
generic native attribute: value
generic native element: a
generic native element: aside
generic native element: dd
generic native element: dialog
generic native element: dl
generic native element: dt
generic native element: form
generic native element: i
generic native element: input
generic native element: kbd
generic native element: option
generic native element: select
generic native element: strong
generic native element: text
skipped noscript
skipped script
```

## Closure log

### Numeric grammar: leading decimals (2026-09-10)

Implemented signed leading-decimal lengths (`.5px`, `+.5px`, `-.25px`) and
leading-decimal scalar values (`opacity:.8`). Length token recognition in compiled
custom-property expressions uses the same expanded grammar. Regression fixture:
`tests/NativeWeb/Contracts.html` (`numeric-length`); native contract assertions
verify generated dimensions. Compiler and native contract suites pass.

Fresh corpus result: 397 rules, 1475 declarations, **69 distinct unsupported
constructs**, down from the preserved 80-construct baseline. Numeric grammar is
not closed: exponent notation and property-specific range/normalization behavior
still require review. The `color:var(` diagnostic also remains open; a scan of the
original source found no unclosed `color:var(...)` declaration, so declaration
extraction/serialization needs investigation before classifying it as bad input.

### Nested function followed by !important (2026-09-10)

Resolved the apparent `color:var(` truncation in the shared Rust CSS declaration
parser. Reproduced with `.history-error{color:var(--danger)!important}` and two
other original declarations. cssparser deferred skipping the function body until
its next token read, but the declaration reader saved the end-state beforehand.
It consequently sliced the value at the opening function rather than before `!`.
The reader now consumes nested blocks before recording the next token state.

Regression: the compiled SVG fixture now uses nested variable fallbacks directly
adjacent to `!important`; existing scene assertions verify both initial paint and
native theme mutation. Compiler and native contract tests pass. Fresh corpus:
397 rules, 1475 declarations, **68 distinct unsupported constructs**. This closes
this extraction defect only; the grammar/diagnostic audit remains open.

### Numeric grammar: exponent notation (2026-09-10)

Added exponent forms to direct lengths, typed custom-property length tokens and
scalar numeric properties. Build-time length conversion separates the numeric
prefix from the unit before producing the native length, avoiding confusion
between an exponent's `e` and a CSS unit. Non-finite length results are rejected.
The native fixture verifies `+.5e1px` becomes 5px and `7.5e-1px` becomes .75px;
`opacity:8e-1` compiles. Compiler and native contract tests pass. Numeric grammar
remains open for property-specific ranges and other numeric consumers such as
fractional grid tracks, shorthand parsers and function arguments.

### Numeric ranges: opacity and flex (2026-09-10)

Opacity now accepts signed finite numbers and clamps generated values to [0,1].
Negative flex-grow/flex-shrink values remain errors; line-height retains its
nonnegative constraint. Compiler regressions cover negative and over-one opacity,
exponent notation, negative flex factors and negative line height. All 37 compiler
tests pass. Percentage opacity, fractional font weights and range behavior in
other property families still require review; this does not close numeric ranges.

### Numeric ranges: percentage opacity (2026-09-10)

Direct opacity percentages now normalize at build time and clamp to [0,1],
including signed and exponent forms. Regression cases cover 50%, -20%, +2e2%,
and malformed percentage tokens. All 37 compiler tests pass. This closes direct
percentage literals only; variable-based opacity and CSS-wide keyword semantics
remain part of the cascade/value audit.

### Numeric consumers: fractional grid tracks (2026-09-10)

Fractional tracks now accept leading decimals, explicit plus signs and exponent
notation, both directly and as a minmax maximum. Tests cover `.5fr`, `5e-1fr`,
`+1fr`, `1E+0fr`, and `minmax(0px,.5fr)`, and reject negative/malformed fractions.
All 38 compiler tests pass. This closes these literal grammar cases, not grid
layout semantics or variable substitution within minmax.

### Layout shorthand: inset literals (2026-09-10)

Implemented one-to-four-value inset expansion into supported native top/right/
bottom/left setters. Regression cases cover every expansion arity, auto, negative
lengths, percentages and rejection of five values. All 39 compiler tests pass.
Variable-bearing inset and calc expressions remain open; literal support does not
close the positioning/layout family.

### Layout shorthand: variable inset and native geometry (2026-09-10)

Inset now evaluates compiled variable token lists and expands one-to-four typed
length/auto values at runtime without CSS parsing. Invalid computed lists reset
the four sides to auto. Native layout checks verify a four-value fallback places
and sizes the child correctly, then a theme change to a single 5px inset produces
190×90 content inside a 200×100 parent. Compiler and native contract suites pass.
Calc expressions and explicit CSS-wide keywords remain open.

### Invalid computed inset regression (2026-09-10)

Native layout regression switches a valid four-sided inset to a variable containing
an invalid identifier, verifies old offsets are cleared while independent width/
height survive, and restores the valid cascade before the theme-update checks.
The native contract suite passes. This validates the invalid-computed-value path
for this case; explicit inherit/revert semantics are still open.

### SVG paint: stroke-width literals (2026-09-10)

Fresh pre-change baseline was 66 unsupported constructs. Added nonnegative SVG
stroke-width literals (unitless, px and percentage, including decimal/exponent
forms). The compiler validates them and the native SVG serializer projects the
cascaded value as a presentation attribute, overriding an authored attribute when
CSS specifies the property. The existing SVG serialization contract now checks
`.8` stroke width. Compiler and native contract suites pass. Variable values,
CSS-wide keywords and broader SVG unit coverage remain open; SVG path compilation
remains the separately documented future optimization.

### SVG text: text-anchor and text elements (2026-09-10)

Strict compilation now accepts SVG text/tspan elements and the start/middle/end
text-anchor keywords. The public style writer uses the native engine's existing
SVG text-anchor field and serializer. A compiled text element regression verifies
that middle alignment reaches serialized SVG. Compiler and native contract tests
pass. Full SVG text layout, additional positioning attributes, variable values
and CSS-wide keywords remain open; this is not a general SVG conformance claim.

### Background reset (2026-09-10)

Fresh baseline after SVG additions: 62 unsupported constructs. `background:none`
now generates a native reset of background color and background-image state,
while `background-color:none` remains invalid. Compiler regression covers the
shorthand distinction. All 40 compiler tests and the native contract suite pass.
Gradient/image authoring and other background shorthand combinations remain open.

### Typography dependency review and font smoothing (2026-09-10)

`text-overflow:ellipsis` is an engine gap, not merely a missing compiler setter:
no native text-overflow field or ellipsis implementation was found, and text is
emitted through both inline-fragment and direct-text paths. Correct implementation
must cover both paths and clipping/line direction before compiler acceptance.

The existing native font-smoothing field and serialized text metadata are now
exposed to compiled `-webkit-font-smoothing` declarations (auto, none, antialiased,
subpixel-antialiased, inherit/unset). A native contract verifies inherited
antialiased metadata reaches text output. Compiler and native contract suites pass.
This verifies propagation, not identical glyph rasterization across platforms.

## Current grouped CSS backlog (2026-09-10)

Regenerated after 5b14f19a: **60 distinct unsupported constructs**, 397 rules,
1475 declarations. The original 80-item baseline above remains historical.
These counts describe this stylesheet only; no feature family is closed by them.

| Family | Distinct diagnostics |
| --- | ---: |
| Generated content and pseudo-elements | 9 |
| Animation and conditional rules | 9 |
| Input and native control presentation | 17 |
| Paint, effects and transforms | 18 |
| Layout and compiled expressions | 4 |
| Text layout and font behavior | 3 |

Next dependency to resolve: compiled expressions used by layout and paint.
Variable evaluation currently handles typed token lists, but nested calc and
color functions need typed expression evaluation rather than runtime parsing.
Pseudo-element support also requires generated-content lifecycle and scene
semantics, not selector acceptance alone. Input/control presentation requires
engine/host review; text ellipsis is a confirmed engine gap. Ownership of the
remaining items is provisional until their native paths are inspected.

### Generated content and pseudo-elements

```text
#command-input::placeholder: unsupported selector: #command-input::placeholder
#ribbon-tab-list button.active:after: unsupported selector: #ribbon-tab-list button.active::after
.document-tab.active:after: unsupported selector: .document-tab.active::after
.panel-tabs button.active:after: unsupported selector: .panel-tabs button.active::after
.progress-line:after: unsupported selector: .progress-line::after
.property-section-title:before: unsupported selector: .property-section-title::before
content:"": unsupported Native Web CSS property: content
content:"⌄": unsupported Native Web CSS property: content
dialog::backdrop: unsupported selector: dialog::backdrop
```

### Animation and conditional rules

```text
@keyframes progress: unsupported at-rule or condition
@keyframes toast-in: unsupported at-rule or condition
@media (prefers-reduced-motion:reduce): unsupported at-rule or condition
@media print: unsupported at-rule or condition
animation:none: unsupported Native Web CSS property: animation
animation:progress 1.2s infinite ease-in-out: unsupported Native Web CSS property: animation
animation:toast-in .15s ease-out: unsupported Native Web CSS property: animation
transition:fill .15s: unsupported Native Web CSS property: transition
transition:none: unsupported Native Web CSS property: transition
```

### Input and native control presentation

```text
accent-color:var(--accent): compiled var() not supported for property: accent-color
color-scheme:dark: unsupported Native Web CSS property: color-scheme
color-scheme:light: unsupported Native Web CSS property: color-scheme
cursor:col-resize: unsupported Native Web CSS property: cursor
cursor:crosshair: unsupported Native Web CSS property: cursor
cursor:grabbing: unsupported Native Web CSS property: cursor
cursor:move: unsupported Native Web CSS property: cursor
cursor:not-allowed: unsupported Native Web CSS property: cursor
cursor:pointer: unsupported Native Web CSS property: cursor
cursor:row-resize: unsupported Native Web CSS property: cursor
resize:vertical: unsupported Native Web CSS property: resize
scrollbar-color:var(--line) transparent: compiled var() not supported for property: scrollbar-color
scrollbar-width:none: unsupported Native Web CSS property: scrollbar-width
scrollbar-width:thin: unsupported Native Web CSS property: scrollbar-width
touch-action:none: unsupported Native Web CSS property: touch-action
user-select:none: unsupported Native Web CSS property: user-select
user-select:text: unsupported Native Web CSS property: user-select
```

### Paint, effects and transforms

```text
backdrop-filter:blur(3px): unsupported Native Web CSS property: backdrop-filter
backdrop-filter:blur(9px): unsupported Native Web CSS property: backdrop-filter
background:color-mix(in srgb,var(--active) 63%,var(--panel)): compiled color-mix currently supports an sRGB color percentage mixed with transparent
background:linear-gradient(125deg,var(--panel2),var(--panel)): unsupported custom-value token: linear-gradient(125deg,var(--panel2),var(--panel))
background:linear-gradient(145deg,var(--panel2),var(--panel)): unsupported custom-value token: linear-gradient(145deg,var(--panel2),var(--panel))
border:1px dashed #65c5a4: border shorthand currently requires width solid color, 0 or none
border:2px dashed var(--accent): border shorthand currently requires width solid color, 0 or none
box-shadow:0 0 0 1px color-mix(in srgb,var(--accent) 25%,transparent): unsupported custom-value token: color-mix(in
box-shadow:inset 0 -2px 0 var(--accent): native compiled inset shadows are not supported yet
box-shadow:inset 0 0 0 1px var(--accent-dim): native compiled inset shadows are not supported yet
box-shadow:inset 2px 0 0 var(--accent): native compiled inset shadows are not supported yet
filter:brightness(1.1): unsupported Native Web CSS property: filter
outline-offset:-2px: unsupported Native Web CSS property: outline-offset
outline:2px solid var(--accent): compiled var() not supported for property: outline
outline:none: unsupported Native Web CSS property: outline
transform:none: unsupported Native Web CSS property: transform
transform:translateX(-50%): unsupported Native Web CSS property: transform
transform:translateY(7px): unsupported Native Web CSS property: transform
```

### Layout and compiled expressions

```text
border-collapse:collapse: unsupported Native Web CSS property: border-collapse
bottom:calc(var(--command-height) + 74px): unsupported custom-value token: calc(var(--command-height)
grid-column:1/-1: unsupported Native Web CSS property: grid-column
right:calc(var(--right-width) + 16px): unsupported custom-value token: calc(var(--right-width)
```

### Text layout and font behavior

```text
font-synthesis:none: unsupported Native Web CSS property: font-synthesis
overflow-wrap:anywhere: unsupported Native Web CSS property: overflow-wrap
text-overflow:ellipsis: unsupported Native Web CSS property: text-overflow
```

### Compiled expression foundation: length arithmetic (2026-09-10)

Added typed addition/subtraction for the native relative-term-plus-pixel-offset
representation. Tests verify percentage minus pixels, reversed subtraction,
rejection of auto and rejection of mixed relative units that cannot be represented.
Native contract tests pass. No CSS text is parsed by this helper. This is the
runtime arithmetic foundation only: the compiler does not yet lower calc into it.
General mixed-unit expressions require a richer representation and remain open.

### Positional calc lowering (2026-09-10)

The compiler now lowers additive/subtractive calc expressions for left/right/top/
bottom into typed native arithmetic, including variable operands and fallbacks.
Native layout tests verify `calc(50% - 20px)` in a 200px containing block and
`calc(var(--missing-offset, 5px) + 10px)`. Compiler and native contract suites pass.
Fresh corpus count: **58 distinct unsupported constructs** (397 rules, 1475
 declarations). No runtime CSS parsing is introduced.

This is partial calc support: multiplication/division, ordinary grouped arithmetic,
additional consuming properties and expressions mixing two relative units remain
open. Unrepresentable evaluated sums currently become auto through the positional
consumer; strict compilation must eventually diagnose statically unsupported unit
combinations rather than treating them as implemented CSS. Do not close the
expression family based on the two removed corpus diagnostics.

### Grouped additive calc (2026-09-10)

The compiler now strips only parentheses enclosing the entire operand, accepts
ordinary nested arithmetic groups, and retains left associativity for chained
addition/subtraction. Native geometry tests use `calc(50% - (15px + 5px))` and
`calc(var(--missing-offset, 5px) + 20px - 10px)`; expected positions remain 80px
and 15px. Compiler and native contract tests pass. Product/division and richer
mixed-unit representation remain open.

### Scalar products in positional calc (2026-09-10)

Added typed length scaling and compiler lowering for multiplication by literal
scalars (either side) and division by literal scalars. Operator precedence and
chained product evaluation are exercised by native geometry fixtures that mix
percentages, pixel offsets, groups and variable length operands. Compiler tests
reject division by zero, length×length and scalar÷length. All 41 compiler tests
and native contracts pass. Scalar variables/expressions, general dimensional
algebra and additional calc-consuming properties remain open.

### Calc resize semantics (2026-09-10)

The calc layout fixture now uses a 25vw parent and checks two viewport widths.
The parent grows from 200px to 300px, and its child's percentage-minus-pixel offset
changes from 80px to 130px without rebuilding the generated document. The native
contract suite passes. This verifies dynamic percentage resolution, not host
window-resize frame pacing.

### Function-preserving box shorthand splitting (2026-09-10)

Box shorthand splitting now preserves parenthesized function values rather than
splitting their internal whitespace. Inset can therefore contain multiple literal
calc functions alongside auto values. Generated positional calc locals are scoped
per assignment so shorthand expansion compiles without duplicate declarations.
The native geometry fixture exercises this combination; compiler and native
contract suites pass. This splitter is for the supported length shorthand grammar,
not a general CSS string/token parser. Variable-containing calc inside an inset
still requires integration with the variable-shorthand evaluator.

### Combined inset variables and calc (2026-09-10)

Variable-bearing inset now gathers complete shorthand components before expansion.
Calc components produce typed lengths, while variable components may expand into
multiple typed tokens; the final list receives one arity/type validation before
all four sides are assigned. The native fixture combines variable fallback inside
calc, a two-value auto fallback, and percentage arithmetic. Existing resize,
invalid-value and theme checks pass, along with the compiler suite. This closes
the previously noted combination gap for the supported expression grammar, not
all custom-property function values or CSS-wide keywords.

### Zero token normalization through variables (2026-09-10)

Custom-property tokens now recognize signed, decimal and exponent spellings of
unitless zero as typed zero lengths, matching direct length consumption rather
than recognizing only the exact string `0`. A native regression verifies
`--zero:+0e0; width:var(--zero)` produces zero width. Compiler and native contract
suites pass. Nonzero unitless numbers remain distinct from lengths.

### Audit diagnostic ownership (2026-09-10)

CSS audit errors now list the rule owning each unsupported declaration, while
preserving deduplication and the distinct-construct count. Repeated constructs
can report multiple owners without inflating the baseline count. The compiler
audit regression verifies owner output; all 41 compiler tests pass. Exact source
line/column ranges and nested conditional ancestry remain diagnostic gaps.

### Conditional ancestry in diagnostics (2026-09-10)

Declaration ownership now includes the parent-rule chain, distinguishing e.g.
`@media (max-width:400px) > div` from unconditional `div`. A regression verifies
both contexts still deduplicate to one unsupported construct. All 41 compiler
tests pass. Exact line/column source spans remain open.

### Border shorthand family and engine ownership (2026-09-10)

Compiler lowering now expands one-to-four `border-width` and `border-style`
values into the existing side setters. Pixel widths accept signed zero, leading
fractional digits and exponent notation in both side declarations and the
supported width/solid/color shorthand. Negative widths, nonzero unitless widths,
percentages, excess components and unsupported styles remain errors.

All 42 compiler tests and native contracts pass. The native fixture uses the new
width/style shorthands and retains its verified 14px border-inclusive height.
This verifies lowering and geometry, not a complete rendered border comparison.

Dashed borders are an engine/API gap: the compiled style facade exposes only a
boolean solid/hidden distinction, with no dash pattern state. Do not lower dashed
to solid. Outline fields and scene drawing exist internally but still need a
semantic/API audit before compiler exposure. Relative/keyword widths, arbitrary
border shorthand ordering, variable-bearing width/style shorthands, CSS-wide
keywords and the remaining border styles are still open.

### Declaration source locations (2026-09-10)

The Rust CSS parser now carries each declaration's one-based line and column
through its native callback and collected syntax model. The C++ streaming sink
has a default located-declaration adapter so existing sink implementations retain
their behavior. The internal Rust/C callback signature changes together and must
be rebuilt together.

CSS audit declaration errors retain each source occurrence alongside conditional
ancestry while still counting distinct unsupported constructs only once. A
multiline nested-media regression verifies two identical declarations report
lines 4 and 5, column 5, with one distinct gap. All 42 compiler tests and native
contracts pass after rebuilding the parser and compiler. Rule/selector locations,
syntax-error locations and mapping inline-style offsets into the owning HTML
source remain open; this does not close the diagnostic gate.

### Rule source locations (2026-09-10)

CSS syntax rules now retain parser-provided one-based line/column locations for
qualified rules and at-rules, including rules without blocks. Unsupported selector
and at-rule audit diagnostics report those locations. Streaming sinks use a
default adapter, and the internal Rust/C callback signatures are updated together.
A multiline regression checks a media rule at 2:1, a nested pseudo-element rule
at 3:3 and an import rule at 5:1. All 42 compiler tests and native contracts pass.
Syntax-error locations and mapping embedded CSS back into HTML remain open.

### Syntax-error locations and ABI guard (2026-09-10)

CSS streaming results preserve the first parser error location alongside the total
error count. `--check-css` reports that location rather than an empty generic
stylesheet error. A two-error declaration fixture verifies the first location
(2:9) and count (2). All 43 compiler tests and native contracts pass.

The internal CSS streaming ABI is now version 2, covering the located callbacks
and extended result structure; the C++ wrapper rejects stale version-1 libraries.
This reports parser-detected syntax errors only. Full error lists, semantic source
locations during normal compilation, and inline CSS-to-HTML mapping remain open.

### Linked stylesheet build diagnostics (2026-09-10)

Normal compilation now uses parser line/column locations for linked stylesheet
rules, declarations and syntax errors. This replaces the first-substring search
for external CSS diagnostics; locations point into the actual linked file.
A build-path regression covers an unsupported declaration, pseudo-element selector
and malformed declaration. All 44 compiler tests and native contracts pass.
Embedded style blocks and style attributes retain their previous approximate
mapping and require HTML parser source spans before claiming exact locations.

### HTML whitespace preservation (2026-09-10)

The compiler previously discarded every whitespace-only text node, losing the
separator between adjacent inline elements and whitespace-only preformatted
content. It now emits all nonempty parsed text nodes; CSS whitespace processing
remains the native engine's responsibility. This also preserves content for later
white-space style changes rather than baking the initial style into construction.

An independent compiler fixture verifies emitted separator and two-space nodes.
Native contracts verify `text_content` is `A B` for adjacent spans and retains
both spaces in a preformatted element. All 45 compiler tests and native contracts
pass. These checks prove DOM preservation, not full inline shaping or rendered
whitespace parity; those remain part of layout differential coverage.

### Native attribute removal (2026-09-10)

HTML boolean attributes correctly use presence, including `disabled="false"`,
but C++ authoring lacked removal. Added `document::remove_attribute`, clearing
special id/class storage and invalidating native styles after removal. Missing
attributes are a no-op. No parsing or node reconstruction is involved.

Native contracts verify disabled presence prevents focus, removal restores focus
eligibility and removes the disabled selector's width, and removing id clears
lookup state. All 45 compiler tests and native contracts pass. General HTML form
semantics, tabindex ordering and focus cleanup on disabling an already-focused
control still require audit; this closes only the missing mutation operation.

### Native tabindex ordering (2026-09-10)

Sequential focus previously excluded only the literal `-1` and ignored positive
priorities. Native focus now parses signed HTML integer prefixes, excludes all
negative values from sequential navigation, and stably orders positive values
before zero/default values. Invalid tabindex does not make a generic element
focusable. Large numeric values saturate safely to the native integer range.

Native regressions verify priority ordering, document-order ties, reverse wrap,
programmatic negative focus, invalid input and dynamic tabindex removal. All 45
compiler tests and native contracts pass. Shadow-tree focus scopes, full form
control coverage, hidden-ancestor programmatic focus and focused-control disabling
remain open; this is not complete browser focus parity.

### Mixed template root ownership (2026-09-10)

Template root whitespace was emitted without inclusion in the instance root list,
so removing an instance could leave text behind. The compiler now records every
nonempty root text node in that list and permits mixed text/element template
content without introducing wrapper elements. This remains predefined generated
construction with no runtime parsing.

Both generated-header and C++ module template tests instantiate a whitespace,
element and text sequence, verify its text and three roots, then remove every
root and verify no text remains. Both template suites, native contracts and all
45 compiler tests pass. Nested template support and transactional construction
failure cleanup remain open.

### Hidden ancestor focus eligibility (2026-09-10)

Programmatic focus previously checked only the target's display mode. It now
rejects targets with any `display:none` ancestor. A native regression hides a
container through a class rule, verifies focus is rejected, removes the class,
recomputes layout and verifies focus succeeds. All 45 compiler tests and native
contracts pass. This uses computed style; style flushing before focus and clearing
existing focus when an ancestor becomes hidden remain open.

Inspection also confirmed subtree removal already removes inline-target rules
and listeners belonging to removed nodes; no duplicate cleanup mechanism added.

### Compiled hidden attribute (2026-09-10)

Strict compilation now permits ordinary HTML hidden state, reusing the native
engine's existing default-display behavior. The `until-found` keyword is rejected
explicitly because native find/reveal semantics are not implemented. Other values,
including `false`, preserve HTML hidden-state behavior.

Compiler tests cover accepted values and case-insensitive until-found rejection.
Native contracts verify the compiled element has no layout height, removing the
attribute restores its 17px height, and reapplying hidden suppresses it again.
All 46 compiler tests and native contracts pass. Find/reveal and focus cleanup
on hidden-state changes remain open.

### Compiled explicit line breaks (2026-09-10)

Strict HTML compilation now accepts `br`, constructing the existing native element
without translating it to another layout primitive. A compiled fixture verifies
that a break separates adjacent spans vertically and native removal restores a
shared line. All 46 compiler tests and native contracts pass. Consecutive/trailing
breaks, mixed font baselines and browser-rendered comparisons remain part of the
broader inline formatting audit.

### Layout keyword casing (2026-09-10)

Enum lowering now performs ASCII case-insensitive lookup for display,
flex-direction, align-items, justify-content, position and box-sizing keywords.
Previously valid uppercase spellings were rejected. Independent compiler tests
verify uppercase property/value forms generate identical rule code to lowercase
forms across all six families. All 47 compiler tests and native contracts pass.
This normalization is confined to keyword lookup; custom-property names and
case-sensitive strings are untouched. Other property grammars, function names,
units and escaped keyword identifiers still need case-handling coverage.

### Common length unit casing (2026-09-10)

The shared compiler length parser normalizes ASCII case in numeric dimensions,
and custom-property token lowering recognizes the same unit variants while
preserving custom-property names. Tests cover PX/EM/REM/VW/VH/DVW/DVH in direct
widths and variable dimensions, plus overflow and malformed unit rejection.
The native numeric fixture now uses uppercase PX and retains its geometry check.
All 48 compiler tests and native contracts pass. Property-specific prevalidators
(e.g. border widths and spacing), escaped units and keyword casing outside the
previously audited enum families remain open.

### Shared pixel-property validation (2026-09-10)

Border side widths, font size, pixel line height and letter/word spacing now share
one finite pixel-value validator. It accepts case-insensitive units, exponent and
fractional numbers and signed zero, with nonnegative constraints for widths/font
size/line height and signed spacing. Font-size emission uses the validated numeric
value instead of reparsing through a separate native utility.

Regression coverage exercises each property with valid numeric forms, overflow,
unsupported relative units and separated units; font-size output is checked for
its exact 5px value. All 49 compiler tests and native contracts pass. Relative
units for these property profiles and broader keyword/function grammar remain open.

### Literal length range validation (2026-09-10)

Common literal length lowering now rejects negative dimensions, padding, gaps,
flex basis and corner radii, and rejects auto for padding/gaps/radii. Shorthand
expansion shares these checks. Signed zero remains valid; negative margins and
positional offsets remain supported. Previously these invalid declarations could
be accepted and delegated to engine clamping or auto behavior.

Independent compiler regressions exercise pixels, percentages, uppercase relative
units, signed zero, invalid auto and valid signed offsets. All 50 compiler tests
and native contracts pass. Computed-value range semantics through variables/calc
and browser-style invalid-declaration recovery remain separate open audit items.

### Typed variable padding shorthand (2026-09-10)

Padding now accepts variable-token shorthand values, validates one-to-four
nonnegative typed lengths, and expands sides only after whole-value validation.
Invalid computed values reset all four sides to initial zero rather than keeping
partially valid sides. Evaluation uses typed native tokens, not runtime CSS parsing.

Native geometry checks verify two-axis padding, a class mutation introducing a
negative component, all-side reset, and recovery after class removal. All 50
compiler tests and native contracts pass. Padding longhand variables, calc values
and remaining box shorthand families still require coverage.

### Variable padding longhands (2026-09-10)

All four padding longhands now lower variable expressions into one nonnegative
typed length, resetting to initial zero for missing, multi-token or invalid
values. A native regression verifies fallback overrides an earlier shorthand,
a negative custom-property mutation resets only that side (rather than restoring
the earlier declaration), and removing the mutation restores the fallback.
All 50 compiler tests and native contracts pass. Calc, CSS-wide keywords and
broader computed-value range handling remain open.

### Typed variable gaps (2026-09-10)

Gap shorthand now evaluates one or two nonnegative typed lengths and validates
both before assignment. Invalid values reset both axes to zero for the current
flex/grid profile. Row-gap and column-gap reuse typed single-value lowering.
A compiled fixed-track grid regression verifies distinct row/column spacing and
whole-shorthand reset after a negative custom-property mutation. All 50 compiler
tests and native contracts pass. Longhand interaction coverage, normal keyword,
calc and multicolumn initial-gap semantics remain open.

### Gap longhand cascade regression (2026-09-10)

Extended native grid coverage verifies column-gap variable fallback overrides only
its shorthand axis. Existing multi-token and negative variable values reset the
column gap to zero while retaining the row gap, without incorrectly selecting the
var fallback. Removing the longhand's class restores shorthand spacing. The native
contract suite passes. This closes the previously missing longhand interaction
regression for typed length gaps; normal/calc/multicolumn semantics remain open.

### Variable margin lowering (2026-09-10)

Margin shorthand and longhands now evaluate typed lengths or auto, accepting
negative lengths and updating both side values and native auto flags. Invalid
computed values reset affected sides to zero and clear auto flags. Shorthand
validation covers the whole one-to-four-token value before expansion.

Native regression verifies block centering via variable auto margins, transition
to a negative left margin without stale auto state, and invalid-value reset.
All 50 compiler tests and native contracts pass. Margin longhand cascade tests,
case-insensitive variable auto, calc and collapsing-margin parity remain open.

### Margin longhand cascade coverage (2026-09-10)

Native contracts now verify a variable margin-left overrides one auto shorthand
side, a multi-token invalid value resets that side without selecting fallback,
a subsequent auto value restores centering, and class removal retains shorthand
auto behavior. The native contract suite passes without a lowering change. This
closes the missing longhand interaction regression for the supported token
profile; collapsing margins and broader keyword/expression support remain open.

### Variable auto keyword casing (2026-09-10)

Typed variable tokens now expose ASCII case-insensitive keyword comparison without
normalizing stored text or custom-property names. Margin, inset and variable grid
track auto consumers use it. Existing margin centering/cascade regressions now
exercise uppercase AUTO and mixed-case AuTo. All 50 compiler tests and native
contracts pass. Escaped identifiers and other keyword consumers remain open;
this helper compares already compiled tokens and does not parse CSS at runtime.

### Literal auto case handling (2026-09-10)

Supported length-property consumers normalize the auto keyword before lowering,
so margin auto flags agree with the emitted lengths for mixed-case spellings.
Shorthands inherit this through side expansion. Tests cover dimensions, offsets,
margin/inset/flex basis, continued rejection for padding/gaps/radii, and preservation
of an unrelated font-family value named AUTO. All 51 compiler tests and native
contracts pass. This is scoped keyword normalization, not general CSS token
normalization; other keyword families and escaped forms remain open.

### Font weight absolute keywords (2026-09-10)

Font-weight normal/bold now lower to native weights 400/700. Keyword matching is
ASCII case-insensitive, including existing inherit/unset handling. Compiler
regressions verify exact generated weights and rejection of invalid values.
All 52 compiler tests and native contracts pass. Relative bolder/lighter,
fractional native weight representation, variable values and font selection
parity remain open. Numeric line-height overflow was inspected: stof rejects
out-of-range inputs before C++ emission; no change was required there.

### Native translation transform lowering (2026-09-10)

Refreshed the unchanged reference corpus: 397 rules, 1475 declarations and 58
unsupported constructs before this change. Added a native typed translation
setter and compiler lowering for translateX/translateY lengths or percentages
and transform:none. The setter resets scale/rotation and records authored
transform versus stacking-context state separately. No runtime CSS parsing added.

Native geometry verifies percentage translation resolves against the element's
own width and class-applied none resets it. All 52 compiler tests and native
contracts pass. Transform lists, variables, rotation/scale lowering, transformed
hit testing and rendered stacking comparisons remain open; this is not full
transform-family closure.

### Translation input and grammar regression (2026-09-10)

The compiled translation fixture now verifies pointer targeting at its displaced
right edge and absence of targeting at its original left edge. Native contracts
pass. Compiler tests cover signed percentages/pixels, case variants and rejection
of auto, nonzero unitless values, unsupported transform lists and excess arguments;
all 53 compiler tests pass. This verifies simple translated hit geometry, not
nested transforms, clipping or rendered stacking-context parity.

### Two-axis translate function (2026-09-10)

Compiler transform lowering now accepts translate(x) and translate(x,y), with the
one-argument form supplying zero Y. Native regression checks percentages resolve
against each axis of the element's own box; compiler tests reject excess arguments,
missing commas and auto. All 53 compiler tests and native contracts pass. Function
lists, calc/variables and broader transform composition remain open.

### Translation resize semantics (2026-09-10)

The compiled translation fixture now has viewport-relative width. Native contracts
verify widening the viewport doubles its width and percentage X offset while Y
retains its own-height basis, pointer targeting follows the resized geometry,
and shrinking restores the original offset without accumulation. Native contracts
pass. This proves dynamic geometry/input only, not compositor presentation timing,
60fps resize or absence of host image stretching; those goal requirements remain
unverified and unchanged.

### Translation flow isolation (2026-09-10)

Added a following block sibling to the compiled transform fixture. Native
contracts verify its normal-flow position is unchanged when a vertical translation
is applied and cleared. Also corrected transform:none keyword case handling;
compiler coverage now includes NONE. Native contracts and all 53 compiler tests
pass. Overflow/clipping, nested transforms and rendered stacking remain open.

### Nested translation geometry and targeting (2026-09-10)

Extended the compiled translation fixture with a child carrying its own X
translation. Native contracts verify combined parent/child offsets, pointer
targeting of the descendant, and descendant Y movement when the parent gains a
vertical transform. Native contracts pass without engine changes. This covers
nested translations only; clipping, stacking paint and rotation/scale composition
remain open.

### Native overflow derived state (2026-09-10)

A translated-child clipping regression initially failed: native cascade stored
axis overflow values but omitted the derived clipping/scroll flags populated by
the JS-backed path. Native cascade now derives those flags after declarations,
including cross-axis visible/clip normalization for that calculation.

The regression now passes: visible translated content is targetable, content
outside overflow:hidden is excluded, and changing overflow to visible restores
outside targeting. Native contracts pass. Rendered clipping parity, mixed-axis
clipping and actual scroll interaction remain open and require further coverage.

### Overflow scene clip coverage (2026-09-10)

The compiled overflow fixture now paints its translated child. Native contracts
verify clip begin/end commands bracket that child's paint and use the ancestor's
20x10 viewport. Changing overflow to visible removes both ancestor clip commands.
Native contracts pass. This verifies scene ordering and metadata, not final
rasterized pixels; renderer differential and mixed-axis clipping remain open.

### Empty overflow viewport regression (2026-09-10)

Native compiled overflow coverage now includes a class-driven zero-height
viewport. The scene retains a zero-height clip, descendant geometry remains
allocated, outside pointer targeting is suppressed, and restoring the height
restores targeting. Native contracts pass. This checks retained-scene/input
behavior, not renderer pixel output or animated height transitions.

### Computed overflow axes (2026-09-10)

Native cascade now stores normalized overflow axis values as well as deriving
clipping/scroll flags, so downstream layout/scene consumers see computed values.
A mixed visible/hidden compiled fixture verifies outside targeting is excluded
and scene clipping remains present. Native contracts pass. The visible/clip
combination requires genuinely axis-specific clipping rather than the current
single clip flag and remains an engine/scene audit gap.

### Native programmatic scrolling (2026-09-10)

Added scroll_to and scroll_offset APIs for C++ document authoring. Scroll requests
use the latest rendered layout, reject non-finite inputs, clamp to content extent,
permit hidden/auto/scroll containers and invalidate layout when offsets change.
Compiled fixture regressions verify child movement and upper/lower clamping;
native contracts pass. Calls before initial layout do not flush layout. Default
wheel scrolling, scroll events, smooth scrolling, RTL offsets and host interaction
coverage remain open.

### Native vertical wheel default action (2026-09-10)

Wheel dispatch now performs default vertical scrolling after listeners unless
preventDefault or document disposal cancels it. The nearest surviving ancestor
that can move handles the delta; hidden-only containers are not wheel-scrollable.
Ancestor IDs are captured before dispatch and revalidated to tolerate removal.
Native contracts verify scrolling, cancellation and resumption after listener
disposal. Contracts pass. Horizontal/delta-mode support, scroll events, overscroll
policy, gesture momentum and host frame pacing remain open; current API deltas
are treated as pixel distances.

### Scroll modes and wheel removal safety (2026-09-10)

Native regressions verify overflow:hidden accepts programmatic scrolling but
suppresses wheel default, overflow:clip rejects a programmatic scroll request,
and a wheel handler can remove its scroll subtree without the default action
accessing deleted nodes. Native contracts pass. Automatic offset reset solely
from overflow changes, queued scroll events and nested scroll chaining remain
open; the clip test explicitly calls scroll_to after changing mode.

### Overflow mode changes reset scrolling (2026-09-10)

After computing overflow axes, native cascade now clears retained scroll offsets
on visible/clip axes. Hidden/auto/scroll offsets remain eligible for layout
clamping. Native regressions switch a previously scrolled compiled container to
clip and visible and verify both offset and child geometry reset without an
explicit scroll request. Native contracts pass. Root viewport scrolling, RTL and
scroll-event scheduling remain open.

### Scroll extent mutation regression (2026-09-10)

Compiled class mutations now exercise shrinking scrolled content and enlarging
its viewport. Native contracts verify the offset clamps to the new content extent
and child geometry agrees in the same render, then resets to zero when content
fits. Contracts pass without engine changes. This is layout correctness coverage,
not asynchronous scroll-event or frame-presentation timing verification.

### Nested wheel container regression (2026-09-10)

A compiled nested-scroller fixture verifies inner-container preference, ancestor
scrolling when the inner container is already at its boundary, and return to inner
scrolling when direction reverses. Native contracts pass. This tests discrete
pixel wheel events; residual deltas within one event, overscroll-behavior, gesture
latching and momentum remain open and must not be inferred from this regression.

### Overflow and text keyword casing (2026-09-10)

Overflow axis lookup and text-align/white-space/text-transform lowering now use
ASCII case-insensitive keyword matching. Overflow shorthand inherits this through
axis expansion. Independent tests compare uppercase/lowercase generated rules
across these families. All 54 compiler tests and native contracts pass. Variable
keyword values and escaped identifiers remain separate gaps.

### Variable text keyword lowering (2026-09-10)

Text-align, white-space and text-transform now evaluate compiled variable tokens,
match supported keywords case-insensitively, and reset invalid computed values to
inheritance. Variable names retain case; normalization occurs only at keyword
consumption. Native geometry verifies uppercase alignment fallback and inherited
right alignment after an invalid custom-property mutation. All 54 compiler tests
and native contracts pass. Whitespace/transformation variable rendering coverage,
CSS-wide custom-property keywords and escaped identifiers remain open.

### Variable text transformation and discovered newline gap (2026-09-10)

Native scene regressions verify uppercase transformation through variable fallback,
invalid-value inheritance to lowercase, recovery after mutation, and unchanged
DOM text. Native contracts pass.

A separate attempted preformatted-newline regression failed: with font-size 10px,
line-height 20px and white-space:var(--White, PRE), A/newline/B measured 20px high
instead of the expected 40px. Retained standalone reproduction in
`tests/NativeWeb/audit/Whitespace.html`; it is not registered as a passing test.
Next investigate literal-versus-variable behavior and native text measurement;
do not claim whitespace variable rendering closure from successful compilation.

### Preformatted newline layout fix (2026-09-10)

The preserved newline reproduction is now covered by passing native contracts:
A/newline/B under variable PRE measures 40px with 20px line height; invalidating
the variable restores normal whitespace and 20px height. Two native paths were
collapsing preformatted text: wrap_text_lines and flattened inline fragment layout.
Both now preserve preformatted line segments and explicit newlines. Contracts pass.
Pre-wrap/pre-line/break-spaces, tabs, trailing-newline edge cases, mixed inline
styles and pixel comparisons still need coverage; this closes the recorded
preformatted two-line failure, not the whole whitespace family.

### Preformatted blank lines and spaces (2026-09-10)

Extended native contracts after the preformatted fix: A/newline/newline/B occupies
three 20px lines, and native text mutation preserves leading/trailing spaces in
scene text. Restoring the original text also renders successfully. Contracts pass.
Whitespace-only content, tab expansion, trailing newline boundaries and the other
preserving whitespace modes remain open.

### Preformatted whitespace boundaries (2026-09-10)

Native contracts now cover whitespace-only spaces, a leading newline and a
trailing newline after text. The cases produce one line, two lines and one line
respectively at the fixture's 20px line height. Contracts pass without further
layout changes. Tab stops, cross-element whitespace boundaries, pre-wrap,
pre-line and break-spaces remain open.

### Pre-line explicit newlines (2026-09-10)

Native text wrapping and flattened inline layout now preserve explicit newline
boundaries for pre-line while collapsing spaces within each line. Compiled
variable-style regression verifies three lines including an internal blank line
and absence of repeated spaces in paint text. Native contracts pass. Soft-wrap
boundaries, mixed inline styles, tabs and renderer pixel comparisons remain open.

### Pre-line soft wrapping and width mutation (2026-09-10)

An independent native contract combines two words and an explicit newline under
compiled `white-space:var(--White, pre-line)`. At 15px width it occupies three
20px lines; restoring 100px width removes the soft wrap and retains the explicit
newline, producing two lines. Both native contracts and the compiler suite pass.
This is geometry evidence only, not pixel parity or whitespace-family closure.
The current whitespace regression is complete; resume the earliest open audit
gate (source diagnostics and numeric/function grammar) before further layout or
Kestrel work.

### Flex shorthand numeric grammar (2026-09-10)

The numeric-grammar audit reproduced a compiler-only inconsistency: `flex:.5 +2
10px` failed although its equivalent longhands compiled. Shorthand factor
classification and numeric longhand validation now share the CSS number grammar,
including signs, leading decimals and exponents. Range validation remains in the
longhand lowering. Removed the shorthand's textual negative-basis check so valid
negative zero is accepted while negative nonzero bases still fail validation.

The independent compiler regression compares generated grow/shrink/basis setters
against equivalent longhands, covers single-factor forms and negative zero, and
rejects negative factors/bases, malformed numbers, excess components and overflow.
The regression failed before the fix; all 55 compiler tests and native contracts
pass afterward. This closes this shorthand grammar inconsistency only; broader
function grammar and source-location gates remain open.

### Shared media-condition numeric validation (2026-09-10)

Compiler lowering and `--check-css` previously duplicated an integer-only media
condition regex; the audit path also omitted numeric conversion/range checks.
Both now use one min/max width/height condition parser and the validated pixel
length grammar. Supported values include signs, decimals, exponents, unitless
zero, case-insensitive keywords/units and surrounding whitespace. Negative bounds
are retained so their comparisons have their natural always/never-match result
for nonnegative viewport dimensions.

Independent tests verify audit/compiler agreement for accepted and rejected
values and inspect generated axis/bound values. Overflow, malformed numbers,
nonzero unitless values and unsupported units fail both paths. All 56 compiler
tests and native contracts passed; the additional generated-bound assertions pass
in the focused regression. General media expressions, relative media lengths and
browser differential coverage remain open; this is numeric validation closure
for the existing pixel min/max condition subset only.

### Preview diagnostic locations and HTML mapping gap (2026-09-10)

Preview warnings now retain the compiler's current source line and column instead
of printing only a filename. A linked-stylesheet regression verifies the at-rule
location and two separate repeated-property locations in generated-module preview
mode. All 57 compiler tests and native contracts pass.

HTML diagnostics remain approximate: dom_node and the HTML parser bridge expose
no authored source spans, and compiler::locate searches the first matching text.
Repeated tags/properties, decoded attribute entities and reordered HTML tree nodes
therefore cannot be mapped reliably with that mechanism. Closing this gate needs
parser-provided source provenance (including attribute-value mapping) propagated
into compiler diagnostics and generated source mappings; do not replace it with
another substring-search heuristic or claim inline location accuracy. Linked CSS
locations are parser-derived and are covered by the regression above.

### Parser-derived element line mappings (2026-09-10)

The HTML tree sink now captures html5ever's current-line callback and passes the
creation line through the native bridge into dom_node. Generated element #line
mappings use this provenance instead of the first matching tag substring. An
independent fixture verifies distinct mappings for repeated div elements and a
nested section. All 58 compiler tests and rebuilt native contracts pass.

The HTML bridge ABI is version 2 because create_element now receives a line
argument. CSS and selector ABI versions are unchanged. Parser lines describe the
tree-builder position at creation, not exact opening-token spans: multiline tags,
implicit/reconstructed elements, attribute columns, entity decoding and inline
CSS offsets still require richer provenance. This is a foundation for source
mapping, not closure of the precise HTML diagnostics gate.

### Element validation diagnostic provenance (2026-09-10)

Compiler traversal now selects the current node's parser line before validating
it, including the metadata pass. Previously unsupported elements could report a
previous sibling's location, while scripts/templates/links rejected in the
metadata pass could retain unrelated locations. One regression checks all four
rejection paths after repeated valid siblings and verifies no output is emitted.
All 59 compiler tests and native contracts pass. The column remains an element
line anchor; exact authored spans and attribute diagnostics remain open.

### Inline declaration owner anchoring (2026-09-10)

Inline declaration lowering now receives its owning node and retains that node's
parser-line anchor for errors and preview warnings. It no longer searches the
whole HTML source for the first matching property name. A repeated-width fixture
checks strict failure on the second element and the same location in preview.
All 60 compiler tests and native contracts pass. This provides an owner location,
not the exact attribute/property span; multiline attributes and decoded source
mapping remain part of the open diagnostics gate.

### Font shorthand numeric forms (2026-09-10)

The existing size/line-height/family font shorthand now accepts signed numeric
forms, leading decimals, exponents, unitless zero and case-insensitive px/normal.
Family spelling is preserved. Longhand lowering validates size and line-height
ranges; unitless line-height uses the shared CSS number grammar, including
negative zero as a factor rather than accidentally treating it as a pixel value.
Tests cover valid forms, retained family case, invalid negative/overflow values
and zero-factor representation. All 61 compiler tests and native contracts pass.
Optional font style/variant/weight/stretch fields, relative sizes, percentages and
full shorthand reset semantics remain open; this closes numeric consistency for
the existing shorthand subset only.

### SVG stroke-width numeric validation (2026-09-10)

Stroke-width now uses the shared CSS number grammar and explicit nonnegative
range validation, accepts negative zero and case-insensitive PX, and normalizes
unit spelling before passing it to the existing native API. Independent tests
cover signed/exponent forms, percentages, negative ranges, malformed units and
overflow. All 62 compiler tests and native contracts pass.

The current SVG style API still stores stroke width as text. Numeric acceptance
is not evidence of a fully preprocessed SVG pipeline: typed SVG lengths, compiled
path data, inherited percentage resolution and rendered parity remain open under
the paint/SVG gate. This work changes compiler validation only.

### Calc function-name casing (2026-09-10)

Inset longhands, nested compiled length expressions and variable inset shorthands
now recognize calc function names case-insensitively. Only the function prefix
is compared in normalized form; custom-property names retain their authored case.
Independent regressions cover uppercase/mixed-case calc, nesting and --Offset
preservation in both longhand and shorthand output. All 63 compiler tests and
native contracts pass. Escaped function identifiers and general component-value
lexing remain open; this does not extend calc to unsupported properties.

### Var function-name casing across lowering paths (2026-09-10)

Variable-expression parsing and property dispatch now recognize var names
case-insensitively, including nested fallbacks, grid tracks and calc operands.
Comparison uses normalized temporary strings; emitted custom-property names and
fallback tokens retain their original case. An independent matrix compares
uppercase/mixed-case generated rules with lowercase-function equivalents for
width, margin, padding, gap, alignment, calc, grid and nested color fallbacks.
All 64 compiler tests and native contracts pass. Escaped identifiers, richer
custom-value token forms and CSS-wide custom-property semantics remain open.

### Custom-value keyword classification (2026-09-10)

Custom-value token classification now compares keywords case-insensitively while
preserving token text. Uppercase/mixed-case CSS-wide keywords no longer bypass the
existing unsupported diagnostic. Supported named colors (black, white,
transparent) now acquire typed color data regardless of capitalization, fixing
uppercase variable fallbacks that previously lacked color metadata. Independent
regressions verify both rejection and emitted typed color values. All 65 compiler
tests and native contracts pass. Full CSS-wide custom-property semantics, broader
named colors and context-sensitive fallback handling remain open.

### Literal grid function casing and repeat integers (2026-09-10)

Literal grid track lowering now accepts case-insensitive repeat/minmax names,
track keywords and dimension units. Repeat counts accept an explicit positive
sign while retaining integer-only syntax and the existing 1..1024 bound.
Independent regressions compare uppercase and lowercase generated track rules,
and reject zero, decimal/exponent integer spellings and duplicate signs. All 66
compiler tests and native contracts pass. This does not add named grid lines,
auto-repeat, additional intrinsic sizing or escaped identifiers.

### Grid numeric range validation (2026-09-10)

Grid fixed/minmax length bounds now validate numeric sign instead of rejecting
any leading minus character, so negative zero is accepted. Fractional tracks and
minmax fractional maxima use shared CSS number grammar with finite, nonnegative
range checks. Regressions cover zero across units, negative values, overflow and
invalid fractional minima. All 67 compiler tests and native contracts pass.
These checks validate compiler input and typed output construction, not complete
grid layout parity; intrinsic track sizing and browser comparisons remain open.

### Grid function argument rejection coverage (2026-09-10)

Audited empty/missing/excess repeat and minmax arguments, including malformed
minmax nested in repeat. Existing guards reject all nine cases; no engine or
compiler change was necessary. The new regression verifies both strict compiler
failure without an output artifact and --check-css rejection. All 68 compiler
tests pass. This records verified existing behavior rather than new support.
Variable grid tracks still accept only typed lengths and auto: fractional values
in custom properties remain an identified lowering gap requiring typed metadata,
not runtime string parsing.

### Typed fractional grid variables (2026-09-10)

Variable expressions/tokens now carry optional fractional-track metadata compiled
from fr dimensions. Both variable evaluation paths preserve it, equality includes
it, and grid lowering consumes nonnegative fractions directly without parsing
runtime strings. A native fixture verifies var fallback 1FR/2fr divides a 300px
grid into 100px/200px tracks. All 68 compiler tests and rebuilt native contracts
pass. Dynamic invalidation/recovery and minmax expressions inside variable token
sequences still need coverage/support; this closes simple fractional tokens only.

### Fractional grid variable mutation and recovery (2026-09-10)

Native contracts now change the fractional track custom property from fallback
1fr/2fr to 2fr/1fr and verify the widths reverse. A negative fraction invalidates
the complete declaration: both children use the implicit full-width column.
Removing the class restores the original fallback ratio with no stale tracks.
Rebuilt native contracts pass. This adds dynamic geometry evidence; nested
minmax token expressions, raster parity and broader grid behavior remain open.

### Inherited fractional references (2026-09-10)

Native geometry now verifies a child custom property referencing inherited
3fr/1fr tokens produces 225px/75px columns in a 300px grid. Removing the ancestor
attribute removes that declaration and activates the nested 1fr/1fr fallback,
producing 150px/150px columns. Rebuilt contracts pass, confirming fractional
metadata survives reference evaluation, inheritance and ancestor mutation.
Full variable token grammar and browser differential closure remain open.

### Dependency output failure reporting (2026-09-10)

Compiler dependency-file creation and final flush are now checked. Previously a
failed depfile write could report success, undermining subsequent rebuilds. A
regression makes the dependency path a directory, verifies failure with its path,
then removes the obstruction and verifies successful retry and source dependency.
All 69 compiler tests pass. Generated source may already exist when depfile output
fails; callers must honor the nonzero exit. Atomic source/depfile publication is
not established by this change.

### HTML root attribute bypass (2026-09-10)

The html root previously copied every attribute directly, bypassing both script
attribute rejection and inline CSS compilation. Root event attributes now fail
strict compilation and are omitted with a preview warning. Root inline styles
now use typed declaration lowering targeted at d.root(), including diagnostics.
Compiler regressions verify typed width output, root targeting and event rejection
/preview omission. All 70 compiler tests pass. Other root attributes still retain
the existing generic-copy behavior; the complete root attribute inventory, named
root references and native geometry coverage remain open.

### Root IDs and named references (2026-09-10)

HTML root IDs now enter the generated view's named references and the shared
ID uniqueness set. Previously the attribute existed on the native root but its
reference was missing and duplicates on body/descendants escaped validation.
Native contracts verify named("html-root") equals d.root(); compiler tests reject
both body and descendant duplicates. All 71 compiler tests and rebuilt native
contracts pass. Broader root attribute semantics remain open.

### Root inline custom-property cascade (2026-09-10)

Native contracts verify a compiled root inline custom property inherits into a
child's width and outranks a normal ID-selector declaration (23px versus 41px).
A class-selected important declaration overrides it to 37px; removing the class
restores 23px. Rebuilt contracts pass. This provides native cascade/inheritance
and mutation evidence for the root inline lowering; exact source spans and the
remaining root attribute inventory are still open.

### Semantic block containers (2026-09-10)

Strict HTML compilation now permits article, aside, hgroup and search. The shared
native UA display defaults already classify all four as blocks. A compiled native
fixture verifies full containing width, explicit heights and vertical source-order
stacking. All 71 compiler tests and rebuilt native contracts pass. This closes
construction/basic layout for these containers only; accessibility landmarks,
complete UA styles and full HTML coverage are not established by these checks.

### HTML stylesheet media attributes (2026-09-10)

Style/link media attributes now seed compiled rule bounds using the existing
pixel min/max width/height condition parser. Empty/all media remains unrestricted;
unsupported media fails strict compilation and omits the whole stylesheet with a
preview warning. Previously media was ignored and styles became unconditional.
Compiler fixtures cover both embedded/linked styles and unsupported print media.
All 72 compiler tests pass. Native resize validation, general media expressions,
type/disabled/alternate stylesheet behavior remain open.

### HTML media resize and nested-condition evidence (2026-09-10)

A compiled embedded stylesheet with min-width:500px and nested max-width:700px
now has native geometry coverage. Viewports 400, 500, 700, 701 and 400px verify
base style, inclusive boundaries, intersection and restoration on shrinking.
Rebuilt contracts pass. These are layout checks for the supported conditions;
they do not establish presentation frame rate or general media-query coverage.

### Static stylesheet activation (2026-09-10)

Non-CSS style/link types and disabled stylesheet links no longer contribute
compiled rules. Disabled uses attribute presence, including disabled="false";
empty type and case-insensitive text/css remain active. Tests verify inactive
content is not parsed/read and active CSS still lowers. All 73 compiler tests
pass. This implements initial compiled activation only: runtime stylesheet
activation, alternate stylesheet sets and stylesheet DOM objects remain open.

### Stylesheet relationship token grammar (2026-09-10)

Link rel recognition now handles case-insensitive whitespace-separated stylesheet
tokens, including duplicates. Unsupported relationship tokens receive a specific
diagnostic rather than being treated as active stylesheet semantics. Tests cover
case, spaces/tabs/newlines, duplicates and explicit alternate rejection. All 74
compiler tests pass. Alternate stylesheet selection and other link relationships
remain unsupported; this closes token grammar for ordinary stylesheet links.

### Input file diagnostics (2026-09-10)

The shared compiler input reader now requires a regular file, reads binary bytes,
and reports stream failures with the source path. Regression coverage checks a
directory supplied as HTML, as linked CSS and to --check-css; each fails without
output. All 75 compiler tests pass. This verifies directory rejection; simulated
mid-read I/O faults and atomic artifact publication remain untested.

### Generated module/template integration checkpoint (2026-09-10)

Rebuilt native_web_smoke, native_web_templates, native_web_module_templates and
native_web_contracts against current compiler/HTML ABI/variable metadata changes.
All four executables and the 75-test compiler suite pass. Template checks exercise
instance-local references, native events, independent-instance survival after
removal, mixed text roots and cleanup. This confirms current generated headers
and C++ modules integrate with the native API; it is not SDK packaging, V8 linkage,
GPU presentation or browser parity evidence. The unbuilt native_web_gpu target
observed in the test inventory remains outside this compiler checkpoint.

### HTML table construction (2026-09-10)

Strict compilation now admits the native table element family and cell-specific
colspan/rowspan attributes. Existing native defaults/layout provide table roles
and span processing. A compiled fixture verifies two separate columns and a
following colspan=2 cell covering their combined width. All 75 compiler tests
and rebuilt native contracts pass. Rowspan edge cases, caption/column behavior,
cell header accessibility, border models and browser differential table coverage
remain open; admission of these elements does not establish full table parity.

### Compiled table colspan mutation (2026-09-10)

Native contracts change the compiled table cell from colspan=2 to colspan=1,
verify it adopts the first column's width, then restore colspan=2 and verify the
original width returns. Rebuilt contracts pass. This establishes live span
mutation for the simple fixture only; complex spans, row groups and browser
parity remain open.

### Table-layout property lowering (2026-09-10)

Added a supported native style writer for the existing fixed-table-layout flag
and compiler lowering for table-layout:auto/fixed with case-insensitive keywords.
Compiler regressions verify both generated values and invalid keyword rejection.
All 76 compiler tests and rebuilt native contracts pass. Fixed-layout geometry,
first-row sizing and overflow comparisons remain necessary; existing table
contracts exercise auto layout and do not prove those behaviors.

### Fixed table first-row sizing defect (2026-09-10)

A native fixture failed when a fixed 200px table's first cell requested 50px and
a later-row cell requested 150px: the later row expanded the column. Fixed mode
now considers only first-row cell sizing and ignores intrinsic text sizing in
that pass; auto mode retains its existing behavior. The regression now passes
with the first cell at 50px, alongside native contracts. Column-element priority,
auto-width tables, span distribution and browser differential tests remain open.
The fix is in the shared native layout engine and may affect other hosts using it.

### Fixed table auto-width qualification (2026-09-10)

The fixed column-sizing path now requires a specified table width. With width:auto,
the existing intrinsic sizing path remains active despite table-layout:fixed.
Native contracts switch the fixture to auto width, verify later-row content can
expand the first column, then restore specified width and verify the 50px fixed
column returns. Rebuilt contracts pass. Percentage definiteness and column-element
precedence remain open table sizing cases.

### Fixed table column precedence (2026-09-10)

A regression reproduced a first-row 120px cell overriding an authored 60px col.
The layout pass now records specified column widths independently from numeric
width (so zero is distinguishable from auto), protects them from single-column
cell sizing, and excludes them from unspecified-track excess distribution.
The 60px regression and native contracts pass. Spanning cells crossing authored
columns, percentage-column distribution and column-group widths remain open.
