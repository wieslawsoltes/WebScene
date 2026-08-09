# Repository reference

The guides in this site cover application setup and integration. The repository also
contains design records, validation evidence, release notes, and implementation-status
documents for contributors and evaluators.

## Architecture

- [Architecture index](https://github.com/wieslawsoltes/WebScene/tree/main/docs/architecture)
- [Native V8 scene engine](https://github.com/wieslawsoltes/WebScene/blob/main/docs/architecture/native-v8-scene-engine.md)
- [Backend status and presenter boundary](https://github.com/wieslawsoltes/WebScene/blob/main/docs/backends.md)
- [Package-boundary ADRs](https://github.com/wieslawsoltes/WebScene/tree/main/docs/architecture/adr)

## Compatibility and validation

- [Required and candidate web-platform profile](https://github.com/wieslawsoltes/WebScene/blob/main/tests/WebPlatformSubset/README.md)
- [7GUIs React validation](https://github.com/wieslawsoltes/WebScene/blob/main/docs/validation/7guis-react-v8-inspector.md)
- [Monaco compatibility notes](https://github.com/wieslawsoltes/WebScene/blob/main/docs/monaco-compatibilty.md)
- [Native Monaco sample](https://github.com/wieslawsoltes/WebScene/tree/main/samples/NativeMonacoEditor)
- [Native TradingView sample](https://github.com/wieslawsoltes/WebScene/tree/main/samples/NativeTradingViewTerminal)

## Interop and diagnostics

- [Component Profile 1 SDK](https://github.com/wieslawsoltes/WebScene/tree/main/src/WebScene.Sdk)
- [Avalonia component host](https://github.com/wieslawsoltes/WebScene/tree/main/src/WebScene.Sdk.Avalonia)
- [Uno component host](https://github.com/wieslawsoltes/WebScene/tree/main/src/WebScene.Sdk.Uno)
- [Typed JavaScript interop generation](https://github.com/wieslawsoltes/WebScene/blob/main/docs/native-javascript-interop-source-generation.md)
- [Generator package reference](https://github.com/wieslawsoltes/WebScene/tree/main/src/WebScene.JavaScript.Interop.Generator)
- [V8 Inspector debugging](https://github.com/wieslawsoltes/WebScene/blob/main/docs/v8-inspector-debugging.md)
- [CDP package reference](https://github.com/wieslawsoltes/WebScene/tree/main/src/WebScene.Diagnostics.Cdp)

## Performance evidence

- [Four-chart binary interop results](https://github.com/wieslawsoltes/WebScene/blob/main/docs/architecture/tradingview-four-chart-binary-interop-results.md)
- [V8 Inspector production performance](https://github.com/wieslawsoltes/WebScene/blob/main/docs/architecture/v8-inspector-production-performance.md)
- [Multi-chart performance recommendations](https://github.com/wieslawsoltes/WebScene/blob/main/docs/architecture/sandwich-multi-chart-performance-recommendations.md)

Performance results are evidence for the recorded commit, machine, workload, and
configuration. Reproduce them on the target product and platform before using them as a
capacity claim.

## Packages and releases

- [NuGet package inventory](https://github.com/wieslawsoltes/WebScene/blob/main/docs/nuget/README.md)
- [Release notes 1.0.20](https://github.com/wieslawsoltes/WebScene/blob/main/docs/release-notes-1.0.20.md)
- [Release notes 1.0.19](https://github.com/wieslawsoltes/WebScene/blob/main/docs/release-notes-1.0.19.md)
- [Runtime packaging workflow](https://github.com/wieslawsoltes/WebScene/blob/main/.github/workflows/native-runtime-packages.yml)

## Source entry points

| Path | Purpose |
| --- | --- |
| [`src/WebScene.Sdk`](https://github.com/wieslawsoltes/WebScene/tree/main/src/WebScene.Sdk) | Component manifest, validation, lifecycle, diagnostics, and capability contracts |
| [`src/WebScene.Sdk.Avalonia`](https://github.com/wieslawsoltes/WebScene/tree/main/src/WebScene.Sdk.Avalonia) | First-class Avalonia component host |
| [`src/WebScene.Backend.Avalonia`](https://github.com/wieslawsoltes/WebScene/tree/main/src/WebScene.Backend.Avalonia) | Reference presenter and native view |
| [`src/WebScene.Backend.Uno`](https://github.com/wieslawsoltes/WebScene/tree/main/src/WebScene.Backend.Uno) | Supported Uno Skia desktop presenter |
| [`src/WebScene.Sdk.Uno`](https://github.com/wieslawsoltes/WebScene/tree/main/src/WebScene.Sdk.Uno) | Reusable Uno packaged-component host |
| [`src/WebScene.JavaScript.Interop`](https://github.com/wieslawsoltes/WebScene/tree/main/src/WebScene.JavaScript.Interop) | Runtime-neutral interop contracts |
| [`src/WebScene.Diagnostics.Cdp`](https://github.com/wieslawsoltes/WebScene/tree/main/src/WebScene.Diagnostics.Cdp) | Chrome discovery and Inspector forwarding |
| [`experiments/WebScene.NativeEngine.Probe`](https://github.com/wieslawsoltes/WebScene/tree/main/experiments/WebScene.NativeEngine.Probe) | Native V8/DOM/CSS/layout/scene engine |
| [`tests/WebPlatformSubset`](https://github.com/wieslawsoltes/WebScene/tree/main/tests/WebPlatformSubset) | Curated WPT profile and runner |

Read status documents and code from the same version or commit. `main` describes current
development; a released NuGet package may have a narrower or older surface.
