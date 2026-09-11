# Hybrid Kestrel delivery epic

## Decision and ownership

Deliver the complete existing JavaScript CAD application on Foco's established
WebScene VSync/WebGPU host, with compiled original HTML/CSS and native template
construction. JavaScript owns CAD state, commands, selection, history and renderer
state. Do not run the separate native CAD controller alongside it. General CAD
ports to C++ are paused. The independent native-only sample remains supported.

Hybrid execution is optional. A pure C++ application must build, run and package
without V8 linkage, initialization or deployed JavaScript assets. Compiler output
and the native document library must not acquire a dependency on the optional V8
adapter. Retain separate native-only package audits and runtime tests.

WebScene owns compilation, DOM, CSS, layout and optional scripting. Foco supplies
platform services, composition and rendering. Both authoring languages operate on
one engine-owned document on its owner thread. Generated views do not own CAD
state. C++ modules remain the generated authoring format. Preserve named refs and
source metadata for later hot design; implement no reload machinery here.

## One delivery, ordered implementation

1. **Shared runtime:** native factories return normal DOM nodes; compiled roots
   enter the existing navigation lifecycle before scripts. Preserve focus,
   listeners, node identity, stylesheet ownership, base URLs and readiness events.
2. **Compiler/package integration:** emit engine-compatible constructors and
   prepared stylesheet data from unchanged upstream markup. Add a versioned
   language-neutral host entry point, copied package descriptors and explicit
   worker-thread construction/disposal. Package resources and scripts locally.
3. **Kestrel templates throughout:** audit producers as well as HTML API consumers.
   Convert dynamic ribbon, explorer, properties, menus, dialogs, extension UI,
   icons and print/export UI into compiled templates with structured data and
   normal JS event callbacks. Preserve original HTML/CSS. Retain the complete
   original CAD script set and its ordering. Do not treat the 27 API sites as a
   complete producer count.
4. **Foco hybrid application:** use the existing WebScene integration and renderer.
   Wire one model and renderer, resource loading, file operations and truthful
   optional-service availability. Keep browser-compatible source authoring.
5. **Acceptance:** verify commands and extension dialogs, visual parity at multiple
   sizes/themes, offline relocated bundle, shutdown and input ownership. Measure
   presented-frame panning and actual OS resize, startup, memory, package size,
   build cost and interaction latency. Run native-only and existing-host regressions.

These are internal checkpoints of one epic, not separate accepted deliveries.

## Runtime HTML compatibility

The framework hybrid profile permits runtime HTML by default: existing JavaScript
can use `innerHTML`, `insertAdjacentHTML`, DOMParser and contextual fragments while
migrating gradually. Those nodes use the same DOM as native templates.

Kestrel selects a strict compiled-template profile. Nonempty HTML parser requests
must fail with actionable errors before destroying existing content. Empty clears
and text/DOM operations remain usable. Block indirect frame/document parsing too;
no silent parsed-shell or template fallback. This restriction belongs to the
application profile, not all WebScene embedders. A pure C++ package does not include
the runtime parser or V8 at all.

## Current implementation evidence

The internal V8 runtime now has named native template factories and compiled-root
navigation using its existing lifecycle. Dedicated tests cover shared identity,
JS listener removal, focus, mutations visible to native code, runtime HTML beside
native-created nodes, constructor-before-script ordering, readiness events and
base URLs without an HTML document fetch. Strict-mode tests cover common parser
entry points and preservation of existing content after rejection.

This is runtime foundation coverage, not a running hybrid Kestrel delivery.
The compiler-to-engine package adapter, public host ABI, complete template
migration, packaged Foco integration and full acceptance matrix remain open.
The current compiled navigation implementation uses the HTML5 DOM activation
backend; it skips document HTML parsing but has not removed CSS interpretation.
