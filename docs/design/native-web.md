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
