# WebScene packages

**Bring web components into native applications.**

WebScene runs React, TypeScript, JavaScript, DOM, CSS, Canvas, SVG, and advanced
existing web controls inside native application frameworks. It is designed for
Flutter, Uno Platform, WPF, WinUI, Avalonia, and other native hosts while preserving
native windows, composition, input, lifecycle, and platform integration.

WebScene is not a WebView, embedded browser, Chromium/WebKit runtime, or Electron
shell. Web-authored components execute against a native scene runtime and integrate
through explicit host contracts.

## Choose a package

- [`WebScene`](https://www.nuget.org/packages/WebScene/) provides the Avalonia
  HTML-like authoring layer and managed presentation backend.
- [`WebScene.Backend.Uno`](https://www.nuget.org/packages/WebScene.Backend.Uno/)
  and [`WebScene.Backend.Avalonia`](https://www.nuget.org/packages/WebScene.Backend.Avalonia/)
  integrate the portable runtime with native framework presentation.
- [`WebScene.Sdk`](https://www.nuget.org/packages/WebScene.Sdk/) and
  [`WebScene.Sdk.Avalonia`](https://www.nuget.org/packages/WebScene.Sdk.Avalonia/)
  package and host React, TypeScript, and JavaScript components with compatibility,
  lifecycle, diagnostics, and host-bridge contracts.
- [`WebScene.JavaScript.Interop`](https://www.nuget.org/packages/WebScene.JavaScript.Interop/)
  and [`WebScene.JavaScript.Interop.Generator`](https://www.nuget.org/packages/WebScene.JavaScript.Interop.Generator/)
  generate strongly typed .NET interop APIs from reviewed TypeScript declaration
  (`.d.ts`) files.
- `WebScene.NativeEngine.Runtime.<rid>` supplies the verified native V8, DOM, CSS,
  layout, Canvas, SVG, and immutable-scene runtime for a deployment platform.

## Native runtime packages

| Target platform | Runtime identifier | Package |
| --- | --- | --- |
| macOS on Apple silicon | `osx-arm64` | [`WebScene.NativeEngine.Runtime.osx-arm64`](https://www.nuget.org/packages/WebScene.NativeEngine.Runtime.osx-arm64/) |
| Linux x64 | `linux-x64` | [`WebScene.NativeEngine.Runtime.linux-x64`](https://www.nuget.org/packages/WebScene.NativeEngine.Runtime.linux-x64/) |
| Windows x64 | `win-x64` | [`WebScene.NativeEngine.Runtime.win-x64`](https://www.nuget.org/packages/WebScene.NativeEngine.Runtime.win-x64/) |

Native applications must reference the runtime package matching their explicit
`RuntimeIdentifier`. Each package includes the native module, ICU data, license
notices, ABI information, and a reproducible build manifest.

## Release inventory

The release contains the `WebScene`, `WebScene.Core`, `WebScene.Dom`, `WebScene.Css`,
`WebScene.Graphics`, `WebScene.JavaScript`, `WebScene.Backend.Abstractions`,
`WebScene.Backend.Avalonia`, `WebScene.Backend.Uno`, `WebScene.JavaScript.Interop`,
`WebScene.JavaScript.Interop.Generator`, `JavaScript.Avalonia.ClearScript`,
`WebScene.Sdk`, `WebScene.Sdk.Avalonia`, and `WebScene.Templates` packages, plus the
three native runtime packages listed above.

Documentation, samples, compatibility policy, and issue tracking are available from
the [WebScene repository](https://github.com/wieslawsoltes/WebScene).

## License

These packages use an MIT-based license with an additional Restricted Party Clause.
See the packaged `LICENSE` file for the full terms.
