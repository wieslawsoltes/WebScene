# Native engine and presenters

WebScene has one execution engine: the native V8 scene engine. A presenter integrates
that engine with a UI framework or headless renderer. Presenters are not alternate DOM,
CSS, layout, or JavaScript engines and must not implement compatibility fallbacks.

## Engine boundary

The native runtime owns:

- V8 isolates, contexts, tasks, and microtasks;
- HTML parsing and live DOM state;
- CSS parsing, selector matching, computed style, and layout;
- events, timers, observers, input dispatch, Canvas, and SVG; and
- immutable scene construction and publication.

A host owns:

- windows, platform lifecycle, display scale, and frame timestamps;
- focus, pointer capture, keyboard/IME, clipboard, and accessibility integration;
- resource policy and explicit host capabilities; and
- scene presentation and renderer-side resource caches.

The engine publishes opaque reference-counted scene handles. Presenters traverse
fixed-layout immutable tables and acknowledge applied revisions. They never receive or
mutate live DOM objects.

## Available integrations

### Avalonia

`WebScene.Backend.Avalonia` is the reference presenter. It contains the native runtime
wrapper, scene surface, text shaping, composition, input forwarding, and shared
Avalonia rendering services. The native showcase, Monaco, and TradingView samples are
the current integration authorities.

`WebScene.Sdk.Avalonia` publishes the native `WebSceneComponentHost` for packaged
Component Profile 1 content. It owns package preflight, isolated declared-asset
loading, capability bridge installation, mount/unmount/reload, recovery state, and
diagnostics while exposing its underlying `NativeWebSceneView` for typed interop,
performance sampling, and Inspector sessions.

### Uno

`WebScene.Backend.Uno` and `samples/NativeRuntimeShowcase.Uno` prove the portable
presenter contracts against a second framework. This is not yet a production support
claim. It still needs full input, text, IME, accessibility, resource, lifecycle,
packaging, and conformance gates.

### Other presenters

WPF, WinUI, Flutter, direct GPU, and other integrations are roadmap candidates. A
backend directory or experiment is not a support claim. A presenter becomes supported
only after it ships as a reusable package and passes:

1. scene ABI, lifetime, stale-base, and recovery tests;
2. required compatibility-profile tests;
3. deterministic rendering fixtures;
4. pointer, keyboard, focus, IME, clipboard, and accessibility contracts;
5. bounded queue, memory, resource-eviction, and frame-pacing tests; and
6. a product-scale workload on every advertised platform.

## Native runtime packages

Applications ship exactly one runtime package matching their explicit runtime
identifier:

```xml
<PackageReference Include="WebScene.NativeEngine.Runtime.osx-arm64" Version="VERSION" />
<PackageReference Include="WebScene.NativeEngine.Runtime.linux-x64" Version="VERSION" />
<PackageReference Include="WebScene.NativeEngine.Runtime.win-x64" Version="VERSION" />
```

Each package contains the native module, ICU data, third-party notices, and a manifest
with its RID, ABI version, V8 revision, and SHA-256 hashes. Build targets reject a
mismatched explicit `RuntimeIdentifier` and copy the native assets to build and publish
output.

macOS ARM64, Linux x64, and Windows x64 are the release workflow matrix. Other modeled
RIDs are not supported until dedicated runners and release evidence exist.

## Backend authoring status

The portable foundation includes:

- `IWebSceneBackendHost`, handles, geometry, invalidation, and capability contracts;
- backend manifests and capability negotiation;
- portable DOM/CSS/graphics/JavaScript value and policy contracts;
- the immutable native scene ABI; and
- deterministic compatibility and rendering fixtures.

The main missing product surface is a stable presenter SDK around safe scene handles,
typed read-only views, input queues, diagnostics, resource loading, and lifetime
validation. The current native wrappers and reference presenters should be extracted
only after the Avalonia path has proved the complete contract.

## Validation

```bash
dotnet test tests/WebScene.Core.Tests/WebScene.Core.Tests.csproj -c Release
dotnet test tests/WebScene.Backend.Abstractions.Tests/WebScene.Backend.Abstractions.Tests.csproj -c Release
dotnet test tests/WebScene.Backend.Avalonia.Tests/WebScene.Backend.Avalonia.Tests.csproj -c Release
dotnet test tests/WebScene.Sdk.Avalonia.Tests/WebScene.Sdk.Avalonia.Tests.csproj -c Release
dotnet test tests/WebScene.Architecture.Tests/WebScene.Architecture.Tests.csproj -c Release
```

Build a matching native package with:

```bash
scripts/build-native-engine-runtime.sh --rid osx-arm64
scripts/build-native-engine-runtime.sh --rid linux-x64
```

```powershell
./scripts/build-native-engine-runtime.ps1 -Rid win-x64
```

The scripts build or reuse the pinned V8 SDK, link the engine, package it, extract it
into a clean consumer layout, and run native smoke and compatibility gates. No managed
engine or fallback participates.
