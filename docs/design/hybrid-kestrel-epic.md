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

## Implementation and validation (2026-09-11)

Implemented the optional compiled engine package, versioned host ABI, Foco hybrid
sample/packager and Kestrel-specific migration frontend. The unchanged root HTML
and CSS produce a C++ module; 238 templates cover dynamic markup producers,
including native SVG variants for rich text and print/export content. All 37
application scripts retain CAD behavior. Packaged Blob workers now capture their
same-origin source before entering the worker isolate, preserving original async IO.

The bundle starts with 265 entities and 781 DOM elements. Runtime tests cover
shared node identity, listener cleanup, focus, compiled lifecycle and strict versus
compatible HTML policies. Compiler regression covers deterministic module output,
inert template CSS/scripts, raw-text slots, namespaces and dependencies. Replacing
children now flattens fragments and applies structural selectors after all siblings
are attached. This fixes missing nested controls and the one-pixel ribbon-border
parity defect.

Evidence:

- Native-only suite: 22/22 passed; rebuilt FocoKestrelPreview dependency/symbol/asset
  audit passes with no V8 or HTML parser symbols.
- Foco WebScene regressions: 4/4 passed (Graphite, modules, adapter, native storage).
- Hybrid runtime and compiler tests pass.
- Compiled/parsed parity: zero differences in the selected ribbon/explorer/inspector
  nodes at 1280×800 dark/Home (376 nodes), 980×620 dark/Text (298), and 1440×900
  light/Drafting (311). This is targeted DOM/style/layout parity, not exhaustive
  browser pixel equivalence.
- Expanded ABI smoke: all ribbon tabs, 20 command/dialog paths, MTEXT native SVG,
  strict rejection, undo/redo and 53,542-byte DXF worker export pass. Original CAD
  code owns the state throughout.
- Packaged window visually verified with WebGPU, 4× MSAA and the original layout;
  real macOS drag-resize reflows the UI. Bundle dependency/signature audit passes;
  copied bundle launches from a temporary directory without loose source assets.
- Initial 12-second GPU pan trace: 720 actual presentations, 59.9997 Hz; median
  16.66675 ms and p95 16.66679 ms intervals. Median submit-to-present is 63.22 ms;
  that is not input-to-photon latency. No equivalent old-host latency baseline was
  established by this run. Rebuild/package takes about 22–28 seconds on the warm
  configured trees; package size is approximately 103 MB. These are development
  builds, not stripped release-size or cold-build claims.
- Final relocated build with the IO worker active: 719 actual presentations in
  12 seconds (59.916 Hz), one 33.33 ms interval; median submit-to-present 47.39 ms.
  RSS after the pan was 297,872 KiB (291 MiB). First host presentation was observed
  at about 0.88 seconds; this includes an initial frame and is not CAD-ready time.
  The final warm packaging run took 17.13 seconds. Raw test summaries are in
  `evidence/hybrid-kestrel/`; use `tests/NativeWeb/analyze_hybrid_pan.py` with the
  benchmark log and `FOCO_PRESENT_TRACE_JSONL` output to repeat frame analysis.

## Remaining acceptance and existing platform limits

The hybrid implementation is running end to end; **the complete acceptance matrix
is not closed**. WebScene's existing `window.open` only supports external URL
handoff. Kestrel's blank-window print preview and browser printing therefore remain
unsupported, even though their markup and SVG producers now compile. Adding native
print/multiwindow support is still required for that part of original behavior.
Do not call compilation of those paths proof that printing works.

The sample packages its required assets offline (system fonts/inline SVG for the
base UI, embedded scripts/worker, local runtime dependencies). A general resource
compiler for arbitrary additional image/font URLs is still broader work. The
optional DWG/OpenCascade bridges remain governed by upstream availability checks.
True input-to-photon latency, exhaustive CAD command coverage and full browser
pixel conformance are not established by the targeted acceptance tests.

Keep these limitations distinct from hybrid optionality: the V8-free C++ profile
remains buildable and tested. Future C++ viewmodels/DataContext and `{Binding}` in
HTML, compiled SVG path data, broader SDK packaging and hot design remain separate
design work; hybrid execution does not make any of them prerequisites.
