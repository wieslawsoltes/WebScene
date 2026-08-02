# ADR 0013: The native engine is the only execution engine

- **Status:** Accepted
- **Date:** 2026-08-02
- **Supersedes:** ADR 0010

## Context

WebScene had two independent implementations of JavaScript integration, DOM/CSS/layout,
input, lifecycle, and presentation behavior. The managed implementation used
ClearScript/V8 and Avalonia objects; the native implementation owns V8 and browser-shaped
state on an engine thread and publishes immutable scenes.

The project has no external users requiring compatibility with the former packages or
host APIs. Maintaining both implementations divides compatibility, performance,
packaging, documentation, and test work. The managed implementation also was the only
implementation behind the public component-host templates, which made the product story
different from the intended native architecture.

Independent browser behavior is better measured against pinned WPT inputs, reduced
contracts, product fixtures, and browser reference results than against a second
WebScene implementation.

## Decision

1. WebScene supports one execution engine: the native V8 scene engine.
2. The ClearScript adapter, its native packages and patches, managed component host,
   templates, samples, benchmarks, CI lanes, and WPT adapter are removed.
3. The native WPT runner has no engine selector and no compatibility fallback.
4. The managed Avalonia DOM, CSS, layout, Canvas, WebGL, browser host, backend host,
   and their tests are removed. Only native presenter code and framework host/resource
   services remain in `WebScene.Backend.Avalonia`.
5. The engine-neutral `WebScene.JavaScript` package and its managed-runtime adapter
   contracts are removed. Native host interop remains in
   `WebScene.JavaScript.Interop`.
6. Generic V8/build/ICU patches required by the native runtime are copied into
   `third-party/v8-patches` and owned directly by WebScene. The ClearScript submodule
   is removed.
7. A general component-host package and templates may return only when implemented on
   the native engine. No managed fallback will be reintroduced.
8. Compatibility claims use the bounded component profile. Broader WPT execution is a
   non-gating discovery signal until tests are reviewed and promoted.

## Consequences

- The source, package, CI, and documentation surface becomes smaller and matches the
  performance architecture.
- Former managed package and host APIs break without deprecation. This is acceptable
  because there are no known consumers.
- Some compatibility coverage formerly existed only as managed implementation tests.
  Valuable cases must be expressed as native contracts, WPT cases, or scene/pixel tests.
- Native compatibility regressions cannot be hidden by a fallback and therefore block
  the relevant capability or release claim.
- Contributor setup for engine work requires the native toolchain or a verified
  RID-specific runtime artifact.
- Native host lifecycle, diagnostics, accessibility, IME, clipboard, recovery, and
  developer tooling become higher-priority product work because there is no alternate
  host path.
