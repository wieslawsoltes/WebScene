# ADR 0010: Managed and native engines are first-class modes

- **Status:** Superseded by ADR 0013
- **Date:** 2026-07-18

## Context

This ADR records the earlier dual-engine decision. It is retained as architectural
history; ADR 0013 removes the managed engine and replaces this decision.

WebScene has a working managed DOM/CSS/JavaScript/Avalonia engine with broad behavioral,
pixel, component-integration, and curated WPT coverage. The native V8 scene-engine work removes
hot-path managed dispatch and UI-thread DOM/CSS work, but it is intentionally a subset
while compatibility is established.

Replacing the managed engine during the investigation would remove the strongest
behavioral oracle and force product adoption to depend on native parity being complete.
Maintaining separate test implementations would allow the engines to drift and would
make native failures difficult to classify.

## Decision

WebScene supports both execution modes as first-class product modes:

1. **Managed mode** remains supported. It is the compatibility implementation and the
   differential oracle for behavior already covered by its tests.
2. **Native mode** owns V8, DOM/CSS state, layout, input dispatch, and immutable scene
   publication off the UI thread. It is selected where its capability profile is
   sufficient and performance justifies it.
3. Tests target an engine-neutral adapter. Test documents, JavaScript, input sequences,
   semantic assertions, reference images, and artifact formats are shared; only engine
   construction, pumping, input injection, state evaluation, and frame capture vary.
4. The curated WPT profile runs through the same runner in both modes. Existing managed
   xUnit fixtures are progressively lifted into shared cases and executed once per
   adapter. A native failure is recorded as a parity gap, not hidden by changing the
   managed expectation.
5. Managed and native results are reported separately. Native support is promoted by
   capability/test group; neither mode silently falls back to the other inside a test.
   Matrix runs use separate processes because ClearScript V8 and the directly linked
   native V8 both own process-global platform state.
6. Product compatibility workloads remain integration gates above reduced shared
   fixtures and WPT. Every newly fixed product defect receives a reduced shared
   regression where practical and an exact-reference assertion in the owning product.
7. Products expose their own managed facades and domain contracts for both modes.
   WebScene owns engine selection primitives, JavaScript affinity, rendering/input,
   diagnostics, lifecycle, and persistent compilation-unit caches; product wrappers,
   assets, commands, and data adapters remain outside this repository.

The minimum adapter contract is:

- load a prepared HTML document at a fixed viewport;
- execute/evaluate JavaScript and return JSON state;
- pump tasks until a predicate or timeout;
- inject pointer, wheel, keyboard, and resize input;
- settle and capture a deterministic BGRA frame;
- expose diagnostics and dispose all engine-owned resources.

## Consequences

- Managed mode cannot be deleted as an incidental part of the native optimization.
- New browser contracts are implemented with shared tests before broad component fixes.
- Native gaps become visible early when the managed suite is run against the native
  adapter.
- Product code can switch managed/native mode without reintroducing an embedded
  browser bridge or maintaining two callback implementations.
- Some managed tests currently instantiate concrete Avalonia/DOM classes. Their fixture
  and assertions are reusable, but they must be lifted to observable DOM/scene/pixel
  contracts before both engines can execute them.
- Native-only ABI/ownership/performance tests and managed-only backend-internal tests
  remain valid, but they do not substitute for shared compatibility tests.
