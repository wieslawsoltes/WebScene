# Native Web first

Deliver HTML/CSS compiled to C++, C++ application logic, and no JavaScript runtime,
running on Foco's platform and compositor. WebScene owns the compiler and document
semantics; Foco supplies the host. Start on macOS, reusing the existing native DOM,
layout and scene output. Do not turn HTML into Foco controls.

Implementation order: native document API; HTML/CSS compiler; working Foco sample;
build/run, diagnostics and packaging. Acceptance includes responsive layout,
native events, dynamic text/classes/nodes, keyboard focus, Canvas, parser-free and
V8-free application linkage, parsed/compiled equivalence and measured costs.

Generated views have explicit construction/disposal boundaries. Application state
lives outside them. Keep source/named-element metadata for future hot design;
hot replacement and state restoration are not part of the first implementation.

## Future idea: typed HTML data contexts and bindings

Consider XAML-like data contexts and `{Binding ...}` expressions in authored HTML,
backed by C++ view models. A future compiler could validate a declared context type
and generate direct typed property/command access and change subscriptions,
without JavaScript, reflection or runtime string-path lookup. Reuse Foco's existing
view-model/property infrastructure where appropriate through optional adapters;
keep WebScene independently embeddable. The syntax, context inheritance, binding
modes and adapter contracts are deliberately not specified or implemented yet.
This is a future feature, not a prerequisite for Native Web.

Later work: optional JS on the same native document, broader platform/language
coverage, integrated SDK packaging and hot design.

## Initial implementation

The first macOS Native Web slice is implemented in `src/WebScene.NativeWeb`, with
`tooling/webscene-uic` and the copyable `samples/NativeWeb` application. The sample
README documents its deliberately bounded HTML/CSS profile; `validation.md` records
actual acceptance results and limitations. Foco integration uses one scene carrier,
its compositor, and an embedded-focus navigation hook. Broader host extraction and
full web API coverage remain subsequent work.

## Optional mixed-language application logic

Accepted: compiled views can support C++, JavaScript and TypeScript application
logic together, enabling gradual migration to C++ through generated interop on
one native document. Pure-native deployment remains runtime-free. Kestrel's
current target remains entirely C++. See [ADR 0014](../architecture/adr/0014-mixed-language-compiled-applications.md)
for ownership, identity, deployment and verification requirements.

## Kestrel fidelity and delivery order

Finish the current native POC first. Then extend compiler/engine support so the
original Kestrel HTML and CSS are consumed unmodified. Only application JS/TS is
ported to C++; dynamic HTML strings in that logic become predefined compiled
templates preserving their original structure. The simplified POC markup is not
the intended final UI and must not become the compatibility target.

## Embedded application resources

Images, fonts and other static resources referenced by compiled views/styles must
be embedded in the final application artifact, with executable-embedded resource
bytes as the default native deployment model. Rendering must not depend on network
requests or loose files from the source checkout. Preserve original HTML/CSS URLs:
the compiler resolves them at build time and generates a resource table mapping
logical URLs to embedded bytes, MIME types and source metadata.

Resolve relative references against the originating HTML or stylesheet URL,
including font-face sources and CSS image references. Deduplicate resource bytes,
track them as build dependencies and diagnose missing assets at build time.
Remote references require an explicit build-time vendoring/pinning step; do not
silently fetch changing remote content during builds or fall back to the network
at runtime. Preserve required third-party notices and embedding license terms.

Native image/font loaders consume resource bytes through WebScene's resource
provider seam; Foco integration must not force WebScene to depend on Foco-specific
resource formats. Font registration and image decode/upload lifetimes remain
explicit. Compression and lazy decoding may reduce memory without changing the
no-network deployment guarantee. Verify packaged applications with network access
unavailable and source directories absent. This is an accepted requirement;
general resource embedding and font/image compiler support are not implemented yet.
