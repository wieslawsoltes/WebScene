# WebScene packages

WebScene is a native web-UI runtime for trusted application content. It is not a WebView
or general browser, and the package inventory does not include a managed engine or
fallback.

## .NET packages

- `WebScene.Core` — portable values and contracts.
- `WebScene.Dom`, `WebScene.Css`, and `WebScene.Graphics` — portable supporting
  semantics and contracts.
- `WebScene.Backend.Abstractions` — presenter capabilities and manifests.
- `WebScene.Backend.Avalonia` — the reference native scene presenter.
- `WebScene.Backend.Uno` — the current Uno presenter proof.
- `WebScene.JavaScript.Interop` and
  `WebScene.JavaScript.Interop.Generator` — strongly typed .NET interop generated
  from reviewed TypeScript declarations.
- `WebScene.Sdk` — component manifest, asset, lifecycle, diagnostics, and host-bridge
  contracts.
- `WebScene` — a separate HTML-inspired Avalonia authoring layer.

`WebScene.JavaScript`, `JavaScript.Avalonia.ClearScript`,
`WebScene.Sdk.Avalonia`, and `WebScene.Templates` are discontinued. The former
managed JavaScript/DOM host is not shipped as source or as a package. A reusable
component host and templates will be published again only after a native
implementation exists.

## Native runtime packages

| Target | RID | Package |
| --- | --- | --- |
| macOS Apple silicon | `osx-arm64` | `WebScene.NativeEngine.Runtime.osx-arm64` |
| Linux x64 | `linux-x64` | `WebScene.NativeEngine.Runtime.linux-x64` |
| Windows x64 | `win-x64` | `WebScene.NativeEngine.Runtime.win-x64` |

Reference exactly one runtime package matching the application's explicit
`RuntimeIdentifier`. Each package includes the native engine, ICU data, notices, ABI
metadata, V8 revision, and content hashes. Build targets reject a mismatched RID.

Other RIDs modeled in build code are not release support claims until they have
dedicated runners and verified packages.

## Building the package set

```bash
scripts/pack-packages.sh --output artifacts/nuget-packages
scripts/build-native-engine-runtime.sh --rid osx-arm64 \
  --output artifacts/nuget-packages
```

The `NuGet packages` workflow builds the .NET packages and the three supported RID
packages, verifies versions/dependencies/symbols/inventory, then runs clean consumers
on the matching operating systems.

## License

Packages carry the repository [license](../../LICENSE), including its Restricted Party
Clause. It is a custom source-available license rather than unqualified MIT or an
OSI-approved open-source license.
