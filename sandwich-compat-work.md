# Sandwich compatibility work for WebScene 1.0.19

## Objective

WebScene 1.0.19 should load and run the existing hosted Sandwich TradingView page unchanged:

`https://tv.sandwichtrading.com/tp-v1/index.html`

Chromium is the behavioural reference. Sandwich should not need to package a modified copy of the page or inject TradingView-specific compatibility JavaScript. WebScene production code and packages must not contain Sandwich or TradingView assets, URLs, global names, API definitions, or product-specific branches.

The integration must continue to use WebScene's native runtime/native DOM and the generated binary JavaScript interop API.

## Current compatibility gap

The hosted page works in Chromium. The local Sandwich copy currently contains compatibility changes that should not be required:

- a `localStorage` and `sessionStorage` shim, including frame handling;
- some product configuration changes and a different broker script revision;
- removal of Sandwich's additional loading overlay.

Only the storage shim indicates a WebScene platform compatibility gap. The product configuration, loading UI, and broker synchronisation changes are separate Sandwich/deployment decisions.

WebScene currently reports `localStorage`, `sessionStorage`, and `indexedDB` together as unsupported in `src/WebScene.Sdk/CompatibilityChecker.cs`. `NativeWebSceneView.LoadAsync` and the Uno equivalent also navigate immediately after creating the engine, so a consumer cannot install a general document-start compatibility script before authored page scripts run.

TradingView probes browser storage from the top document and from its frames. Storage therefore needs to exist before the first page or frame script executes; installing it after navigation is too late.

## Required work

### 1. Implement Web Storage in the native runtime

Provide browser-compatible `localStorage` and `sessionStorage` objects on every applicable `Window`, including frame windows, before authored scripts execute.

#### Sandwich-required scope for 1.0.19

Sandwich does **not** need durable browser storage. It disables TradingView's `use_localstorage_for_settings` feature and sends durable user settings, layouts, drawings, and templates through its generated persistence bridge. Orders, positions, executions, and market data also come from the broker and datafeed APIs rather than Web Storage.

For Sandwich, TradingView Trading Core needs a coherent Storage surface during startup and for the lifetime of the current page. This can be implemented as small in-memory maps owned by the native runtime. They are compatibility objects, but they must be stateful rather than no-op stubs: a value written with `setItem` must be returned by `getItem` until it is removed, cleared, or the owning storage context ends.

The release-blocking Sandwich requirement is therefore:

- expose both objects before the first top-document or frame script runs;
- implement the six members listed below with synchronous, stateful behaviour;
- retain values for the lifetime of the active page/engine, including ordinary same-document use;
- make the implementation entirely native/in-process, with no managed callback or disk I/O on access;
- make absent and malformed access safe and browser-like enough that TradingView does not throw during startup.

The following are **not required by Sandwich for 1.0.19**:

- persistence across application restarts;
- disk-backed profiles;
- cross-process or cross-application sharing;
- `storage` events or cross-tab synchronisation;
- quota management and browser-specific quota exceptions;
- `indexedDB`;
- using Web Storage for Sandwich settings, layouts, orders, positions, executions, or market data.

If full origin and reload semantics would materially delay 1.0.19, ship the stateful in-memory subset first and document its lifetime. Do not block Sandwich compatibility on durable storage. Broader Web Storage conformance can be implemented incrementally behind the same JavaScript API.

The minimum supported `Storage` surface is:

- `length`;
- `key(index)`;
- `getItem(key)`;
- `setItem(key, value)`;
- `removeItem(key)`;
- `clear()`.

Method-level semantics required for the Sandwich subset:

- keys and values use browser-compatible string coercion;
- `getItem` and an out-of-range `key` return JavaScript `null`;
- key enumeration order is stable;
- storage access must not require a managed interop round trip for every operation.

Preferred general WebScene semantics beyond the minimum Sandwich subset:

- `localStorage` is shared by same-origin documents and frames in the same profile/storage context;
- `sessionStorage` is isolated to the top-level browsing session and shared only where normal same-origin frame rules permit it;
- storage is partitioned by origin and must not leak across origins or unrelated engine/profile contexts;
- navigating or reloading a document does not unexpectedly discard the current browsing session's storage.

A later host-configurable durable storage provider can add disk persistence without changing page-facing semantics.

`indexedDB` can remain unsupported. Update the SDK compatibility checker so `localStorage` and `sessionStorage` are no longer diagnosed as unsupported while `indexedDB` continues to produce an appropriate diagnostic.

### 2. Add a general document-start script facility

Add a platform-neutral load option for scripts that must execute after a window/global is created but before any authored script. This is useful for compatibility features that are not yet native and for host policy, but it must not contain or encourage hard-coded Sandwich logic.

An API shape similar to the following would be suitable:

```csharp
public sealed record NativeWebSceneLoadOptions
{
    public required string Source { get; init; }
    public required string NativeLibraryPath { get; init; }
    public string? CompilationCacheDirectory { get; init; }
    public IReadOnlyList<WebSceneDocumentScript> DocumentStartScripts { get; init; } = [];
}

public sealed record WebSceneDocumentScript(
    string Source,
    string Name,
    bool AllFrames = true);
```

The exact naming may follow existing WebScene conventions. Preserve existing `LoadAsync` overloads by forwarding them to the options-based implementation.

Required behaviour:

- scripts run before the earliest inline or external authored script;
- `AllFrames` scripts run in each newly created frame context before that frame's authored scripts;
- execution order is deterministic;
- a script error identifies the configured script name and is surfaced through normal WebScene diagnostics;
- Avalonia and Uno backends have equivalent behaviour;
- no TradingView-specific code is included in WebScene.

Native Web Storage is still the preferred fix for storage. The document-start API is a general fallback and should not be used to hide an incomplete storage implementation.

This API is strongly recommended but is not required for Sandwich 1.0.19 if native `localStorage` and `sessionStorage` are available at the correct time in every relevant window. It must not delay the minimum native in-memory Storage fix.

### 3. Verify early callback and frame timing

Confirm that generated binary callbacks are not lost when page code invokes the host immediately after the bridge property is assigned or while the initial scene is being produced. Verify browser-compatible ordering for the event loop features used during TradingView startup, especially:

- microtasks and promises;
- timers;
- mutation observers;
- same-origin frame creation and script execution;
- callback-signal delivery and pumping.

Do not implement broker state semantics in WebScene. If a broker behaviour differs between Chromium and WebScene, capture the callback/event trace in both runtimes and fix the generic timing or interop defect demonstrated by that trace.

### 4. Preserve concurrent chart startup

Four independent charts must be able to load concurrently. Avoid a process-wide navigation, parser, compilation-cache, or interop lock that serialises engine startup. Shared immutable resources and compilation caches are desirable, but each page must retain isolated DOM, JavaScript, storage-session, and broker state.

## Acceptance tests

### Web-platform tests

Add deterministic local fixtures for the Sandwich-required subset covering:

1. storage is visible to the earliest inline script;
2. all `Storage` methods and properties above;
3. string coercion and `null` return behaviour;
4. write/read/remove/clear consistency for the lifetime of a page;
5. availability in the top document and same-origin TradingView-style iframe;
6. isolation between unrelated engine contexts;
7. compatibility-checker diagnostics for supported storage and unsupported `indexedDB`.

When broader Web Storage support is implemented, extend the suite to cover reload/navigation lifetime, same-origin sharing, cross-origin isolation, and separate `sessionStorage` browsing sessions. If the optional document-start API is included in 1.0.19, also test its ordering in the main frame and child frames and its error reporting.

Exercise the native engine directly where possible, and cover the public Avalonia and Uno load APIs. Existing overloads must remain source-compatible.

### Sandwich integration validation

Using packaged 1.0.19 NuGets rather than project references:

1. load the unchanged hosted URL headlessly;
2. verify `typeof localStorage === "object"` and `typeof sessionStorage === "object"` in the top document and TradingView frame;
3. verify the chart reaches widget ready and calls the generated Sandwich host bridge;
4. verify a prompt `RequestTradingState` callback is delivered without polling races or a lost initial signal;
5. verify live candles, orders, positions, order changes, cancellations, resize, and view reparenting;
6. load four live charts concurrently and verify they are not serialised;
7. compare browser-visible behaviour and console output with Chromium;
8. run the established four-chart CPU and RSS benchmark against 1.0.18 and 1.0.19.

An optional network integration test may target the live URL, but CI should also have a deterministic local fixture reproducing its storage, frame, and early-callback requirements.

### Packaging validation

- Managed and native runtime versions, ABI metadata, manifests, and hashes must agree.
- Validate the produced NuGet packages on macOS arm64 at minimum, then on the other supported platforms.
- The test must use the packaged native runtime resolved in the same way as a consuming application.

## Broker revision 20 is separate work

The local `broker-v1.js` revision 20 contains a genuine steady-state synchronisation improvement, but it is not a WebScene compatibility fix.

Revision 18 calls TradingView's `ordersFullUpdate()` for every published trading-state snapshot. Revision 20 still compares the full JavaScript order and position arrays, but sends per-item `orderUpdate` and position updates only for changed items, emits an explicit terminal cancellation for a removed active order, and reserves full refreshes for initial synchronisation or an account-context change.

This changes downstream host work from repeatedly rebuilding all TradingView order state to approximately O(n) comparison plus O(changes) host notifications. It should reduce TradingView cache, order-line, and rendering work when Sandwich publishes state frequently. The benefit will be modest with few or infrequent orders and has not yet been quantified by a controlled benchmark. Sandwich still constructs and sends a full trading-state snapshot, and JavaScript still maps and compares that snapshot, so revision 20 does not eliminate the C# projection or interop payload cost.

Treat revision 20 as a separately deployable broker correctness/performance improvement. Do not copy it into WebScene. First prove that the unchanged revision 18 deployment behaves the same in WebScene and Chromium; then decide independently whether to deploy revision 20 to improve both runtimes.

## Explicitly out of scope for WebScene

- TradingView or Sandwich scripts, assets, type declarations, manifests, URLs, and bridge names;
- TradingView widget feature flags or settings overrides;
- order/position synchronisation policy;
- Sandwich loading overlays;
- persistent TradingView chart layouts or templates;
- copying the hosted page into a WebScene sample or runtime package.

## Completion criteria

This work is complete when Sandwich can remove its WebScene-only hosted-page workarounds, continue loading `tv.sandwichtrading.com/tp-v1` directly, and obtain behaviour equivalent to Chromium using WebScene 1.0.19 native runtime packages. Any remaining product changes must be demonstrably independent of the rendering runtime.
