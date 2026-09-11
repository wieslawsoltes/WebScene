# ADR 0014: Mixed-language application logic over compiled views

Status: Accepted design; mixed-language implementation is subsequent work.
Date: 2026-09-10

## Decision

Compiled HTML/CSS does not prescribe the language of application logic. WebScene
must support C++ and optional JavaScript/TypeScript logic together on the same
native document. Developers can migrate an existing application incrementally,
component by component, while retaining its compiled views and templates.
TypeScript is compiled to JavaScript by application tooling; it is not a separate
runtime or an automatic TypeScript-to-C++ conversion path.

Use WebScene's annotation-driven interop generator for typed calls, callbacks and
native object access in both directions. Both language paths must operate on the
same document, node identities, events, CSS/layout engine and Canvas/WebGPU
resources. Adding JavaScript must not create a parallel DOM, duplicate scene,
second renderer or require runtime HTML/CSS parsing of compiled views.

Application state remains outside generated views. Each state object has an
explicit owner; generated bridges expose access and change notifications rather
than silently copying authoritative state between languages. This also leaves a
seam for future typed C++ view models and HTML bindings.

## Required integration contracts

- Preserve node identity through native handles and JavaScript wrappers.
- Define ownership, weak references, callback disposal and runtime shutdown so
  neither language can retain usable references to disposed native objects.
- Respect document thread affinity; cross-thread calls require explicit dispatch.
- Define error propagation and reentrancy for native-to-JS and JS-to-native calls.
- Keep document mutation, event ordering and rendering behavior consistent across
  languages. Native Canvas/WebGPU objects must be exposed through the same backend.
- Generated view APIs use C++20 modules; interop remains a language-neutral seam
  and does not require other languages to consume C++ module binaries.

## Deployment and migration

Pure-native applications continue to require no JavaScript runtime, V8 linkage,
initialization or deployed runtime assets. The mixed-language profile opts into
those dependencies explicitly. Interop wrappers are optional dependencies of the
native document engine, not prerequisites for compiled view construction.

A migration can retain existing JS/TS controllers, use compiled templates for
view creation, and replace individual controllers/services with C++ behind typed
interop contracts. Converting application logic must not require replacing the
view or changing its document identity. Arbitrary runtime HTML construction is
outside the compiled-view contract; dynamic native UI uses predefined templates.

## Scope and verification

This decision expands the platform design, not the current Kestrel acceptance
criteria. Kestrel remains an HTML/CSS/C++ application with no JavaScript runtime
and no runtime HTML parsing. Mixed-language support must later be verified with
bidirectional calls, shared-node mutations, events, lifetime/cleanup tests and a
real incremental migration example. This ADR does not claim those tests or the
complete integration exist today.
