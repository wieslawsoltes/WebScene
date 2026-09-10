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
