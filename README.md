<p align="center">
  <img src="docs/assets/webscene-logo.jpg" alt="WebScene" width="900">
</p>

<h1 align="center">WebScene</h1>

<p align="center"><strong>Web components. Native performance.</strong></p>

<p align="center">
  Bring React, TypeScript, and JavaScript components into native application frameworks—<strong>without a WebView or embedded browser.</strong>
</p>

<p align="center"><strong>No WebView · No embedded browser · No Chromium · No Electron</strong></p>

<table align="center">
  <tr>
    <td align="center" width="120">
      <a href="https://avaloniaui.net/"><img src="docs/assets/platforms/avalonia.svg" alt="Avalonia" height="52"></a><br>
      <strong>Avalonia</strong>
    </td>
    <td align="center" width="120">
      <a href="https://platform.uno/"><img src="docs/assets/platforms/uno.svg" alt="Uno Platform" height="52"></a><br>
      <strong>Uno Platform</strong>
    </td>
    <td align="center" width="120">
      <a href="https://learn.microsoft.com/dotnet/desktop/wpf/"><img src="docs/assets/platforms/wpf-dotnet.svg" alt="WPF" height="52"></a><br>
      <strong>WPF</strong>
    </td>
    <td align="center" width="120">
      <a href="https://learn.microsoft.com/windows/apps/winui/"><img src="docs/assets/platforms/winui.svg" alt="WinUI" height="52"></a><br>
      <strong>WinUI</strong>
    </td>
    <td align="center" width="120">
      <a href="https://flutter.dev/"><img src="docs/assets/platforms/flutter.svg" alt="Flutter" height="52"></a><br>
      <strong>Flutter</strong>
    </td>
  </tr>
</table>

<p align="center">
  <a href="https://www.nuget.org/packages/WebScene/"><img src="https://img.shields.io/nuget/vpre/WebScene.svg" alt="WebScene NuGet"></a>
  <a href="https://www.nuget.org/packages/WebScene.Backend.Avalonia/"><img src="https://img.shields.io/nuget/vpre/WebScene.Backend.Avalonia.svg" alt="WebScene Backend NuGet"></a>
</p>

## Positioning

**WebScene brings packaged web components into native application frameworks—including Avalonia, Uno Platform, WPF, WinUI, and Flutter—without embedding a browser.** Teams can build component interfaces with React, TypeScript, JavaScript, DOM, CSS, Canvas, and SVG while each host retains its native windows, composition, input, lifecycle, and platform integration.

**It is not a WebView, browser control, or embedded browser.** WebScene does not ship Chromium, WebKit, Electron, or a hidden browser process. Its native engine runs V8, DOM/CSS state, layout, input dispatch, Canvas, and SVG off the UI thread, then publishes immutable scene diffs to a framework-specific native presenter. The result is a browser-shaped component model backed by native rendering and application composition.

The platform is designed for trusted, versioned, offline component bundles and application-owned experiences—not arbitrary websites or full browser compatibility. Avalonia is the reference implementation today; Uno Platform and Flutter provide integration proofs, while WPF and WinUI are planned presenters built on the same portable contracts and immutable scene ABI. Support maturity is documented in [Managed and native backends](docs/backends.md).

The WebScene product family includes:

- **WebScene** – the product brand and HTML-like direct-authoring layer.
- **WebScene.NativeEngine.Runtime** – the native V8, DOM, CSS, layout, and scene engine.
- **WebScene.Backend.Avalonia** – native scene presentation and Avalonia host integration.
- **WebScene.Backend.Uno** and **WebScene.Backend.Flutter** – cross-framework integration proofs.
- **WebScene.Sdk** – versioned component packaging, compatibility, lifecycle, and host-bridge contracts.
- **WebScene.Sdk.Avalonia** – the XAML-first host for packaged React, TypeScript, and JavaScript components.
- **JavaScript.Avalonia.ClearScript** – the managed compatibility engine and behavioral reference.

## Usage Restriction Notice

At maintainer request, AvaloniaUI OÜ may not use this repository in any form.

This restriction is defined in the repository [LICENSE](LICENSE).

## Highlights

- 🪶 **No embedded browser**: Deliver web-authored components without a WebView, Chromium/WebKit runtime, Electron shell, or hidden browser process.
- 🚀 **Native scene engine**: Run V8, DOM/CSS, layout, input, Canvas, and SVG off the UI thread and publish immutable, damage-aware scene diffs to a framework-native presenter.
- ⚡ **Native application composition**: Combine web components with XAML/C# or Flutter/Dart UI, native controls, menus, settings, and operating-system services.
- 🧩 **Component hosting**: Mount versioned, offline React/TypeScript/JavaScript bundles through an engine-neutral component profile and framework host.
- 🧠 **Compatibility by contract**: Share DOM, CSS, rendering, input, lifecycle, and cache contracts between native and managed engines; promote support through conformance gates.
- 🔌 **Capability-based host bridge**: Expose selected asynchronous .NET services to trusted components without giving them an implicit application-wide API.
- 🕹️ **DOM and event integration**: Query and mutate the projected visual surface and route pointer, keyboard, text, focus, and routed-event behavior to JavaScript.
- 🖼️ **HTML-like authoring and Canvas**: Use familiar markup, styling, and Canvas APIs directly when a packaged component is not the right shape.

## Repository Layout

| Path | Description |
| --- | --- |
| `src/WebScene.Core` | UI-framework-neutral values and host/backend contracts. |
| `src/WebScene.Backend.Abstractions` | Backend manifests, validation, and capability negotiation. |
| `src/WebScene.Backend.Avalonia` | Current Avalonia presentation implementation. |
| `src/WebScene.Backend.Uno` | Uno Platform native-scene integration proof. |
| `src/WebScene.Backend.Flutter` | Flutter native-scene integration proof. |
| `src/WebScene` | WebScene markup library and HTML element implementations. |
| `src/JavaScript.Avalonia` | Engine-neutral browser/DOM services for Avalonia. |
| `src/JavaScript.Avalonia.ClearScript` | ClearScript/V8 execution adapter and shared compilation cache. |
| `src/WebScene.Sdk` | Portable Component Profile 1 product contracts and host bridge. |
| `src/WebScene.Sdk.Avalonia` | Avalonia `WebSceneComponentHost` for packaged components. |
| `tooling/webscene` | Bounded TypeScript declarations, checker, and Vite/esbuild plugins. |
| `templates/WebScene.Templates` | Component-host, hybrid, and TypeScript `dotnet new` templates. |
| `samples/components` | Twelve versioned, offline component packages shared by backends. |
| `samples/hosts/Avalonia` | Runnable `.csproj` hosts: the R5 catalog and three standalone product shapes. |
| `third-party/clearscript` | ClearScript 7.5.1 source submodule on the WebScene native patch branch. |
| `third-party/v8` | V8 14.7.173.23 source submodule on ClearScript's compatibility patch branch. |
| `packaging/WebScene.NativeEngine.Runtime` | RID-specific native V8/DOM/CSS/scene runtime package definition. |
| `samples/website` | WebScene showcase demonstrating markup, styling, and canvas scripting. |
| `samples/JavaScriptPlayground` | Interactive playground with editable XAML, live preview, and JavaScript console for `JavaScript.Avalonia`. |
| `samples/NativeRuntimeShowcase.*` | Native TradingView canvas and generated-.NET-API Monaco showcase for Avalonia and Uno. |

## Getting Started

### Prerequisites

- .NET SDK 8.0 or later (see `global.json` for the tested version).
- A platform supported by Avalonia (Windows, macOS, Linux).

### Building the repository

Initialize source dependencies before producing reviewed native runtime packages:

```bash
git submodule update --init --recursive
```

```bash
# Restore and build everything (libraries + samples)
dotnet build WebScene.sln
```

### Running the samples

```bash
# Browse and run all 12 R5 component packages
dotnet run --project samples/hosts/Avalonia/WebScene.Sdk.SampleCatalog

# Run one of the copyable R5 product-shape hosts
dotnet run --project samples/hosts/Avalonia/ComponentHost.Basic
dotnet run --project samples/hosts/Avalonia/Hybrid.ReactIslands
dotnet run --project samples/hosts/Avalonia/TypeScriptDesktop

# WebScene website sample
dotnet run --project samples/website/website.csproj

# JavaScript.Avalonia playground
dotnet run --project samples/JavaScriptPlayground/JavaScriptPlayground.csproj

# Validate the complete R5 SDK/template/sample workflow
scripts/run-r5-sdk-smoke.sh

# Execute all 12 catalog bundles through real Avalonia + V8 (native runtime required)
scripts/run-r5-catalog-runtime-smoke.sh
```

The R5 hosts use the reviewed patched ClearScript V8 native library and automatically
copy it from the repository's stable per-RID cache. See
[`samples/hosts/Avalonia/README.md`](samples/hosts/Avalonia/README.md) for the one-time
native preparation command and optional environment overrides.

### Creating a React/TypeScript application

After installing the `WebScene.Templates` package, create one of the supported product
shapes:

```bash
dotnet new webscene-component-host -n MyComponentHost
dotnet new webscene-hybrid -n MyHybridApp
dotnet new webscene-typescript -n MyTypeScriptApp
cd MyTypeScriptApp/web
npm install
npm run build
cd ..
dotnet run
```

The web build runs the bounded compatibility checker and emits a versioned
`webscene-component.json`. Host services are available only through declared,
asynchronous `webscene.host.*` capabilities. Applications must also ship the reviewed
RID-specific ClearScript/V8 native package used by the component-host workflow.

### Consuming the libraries

The current package line is prerelease. The native scene engine is the flagship runtime
for production workloads: it owns V8, DOM/CSS, layout, input, and scene construction
off the UI thread, and the Avalonia host consumes immutable scene handles. The native
engine is promoted by capability and performance gates rather than silent fallback.

The packaged component-host samples currently use the managed ClearScript/Avalonia path
as the compatibility reference. The component profile is engine-neutral, so the same
packaged assets and conformance tests can be promoted to the native scene host as each
capability group is validated. See [Managed and native backends](docs/backends.md) and
[the native scene-engine design](docs/architecture/native-v8-scene-engine.md).

An Avalonia host using the opt-in native scene engine on macOS ARM64 uses:

```xml
<ItemGroup>
  <PackageReference Include="WebScene.Backend.Avalonia" Version="11.3.4-alpha.6" />
  <PackageReference Include="WebScene.NativeEngine.Runtime.osx-arm64" Version="11.3.4-alpha.6" />
</ItemGroup>
```

The runtime package copies the native module, ICU data, and version/ABI manifest to
build and publish output. `win-x64` and `linux-x64` publishing are temporarily
deferred while their pinned V8 builds move to faster, independently validated lanes.

## Using the HTML-like authoring layer

WebScene also supports direct authoring with HTML-like tags (heading levels, paragraphs, lists, sections, navigation, Canvas, and more) mapped to Avalonia presentation services. Packaged React/TypeScript components normally enter through `WebSceneComponentHost`; use this lower-level surface when you want to author the document directly:

```xml
<html xmlns="https://github.com/avaloniaui"
      xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
      x:Class="Demo.index"
      title="WebScene Demo">
  <head>
    <link rel="stylesheet" href="avares://website/Assets/demo.css" type="text/css" />
    <script type="text/javascript">
      <![CDATA[
      document.addEventListener('DOMContentLoaded', () => {
        const canvas = document.getElementById('draw');
        const ctx = canvas.getContext('2d');
        canvas.addEventListener('pointermove', evt => {
          ctx.lineTo(evt.x, evt.y);
          ctx.stroke();
        });
      });
      ]]>
    </script>
  </head>
  <body>
    <section class="card">
      <h1>Hello from WebScene</h1>
      <canvas id="draw" width="400" height="200" />
    </section>
  </body>
</html>
```

WebScene parses the markup, applies classes and inline styles, wires `<canvas>` pointers to JavaScript, and allows scripts to manipulate the resulting visual tree.

## Using JavaScript.Avalonia directly

`AvaloniaBrowserHost` supplies browser/DOM services and `ClearScriptV8Runtime` supplies
V8 execution:

```csharp
public partial class MainWindow : Window
{
    private readonly AvaloniaBrowserHost _browserHost;
    private readonly ClearScriptV8Runtime _runtime;

    public MainWindow()
    {
        InitializeComponent();
        _browserHost = new AvaloniaBrowserHost(this);
        _runtime = new ClearScriptV8Runtime(_browserHost);

        _runtime.Execute("""
const label = document.getElementById('OutputText');
const button = document.getElementById('RunButton');

if (button && label) {
  button.addEventListener('click', () => {
    label.textContent = 'Button clicked from JavaScript!';
    setTimeout(() => label.textContent = 'Ready', 1000);
  });
}
""");
    }
}
```

### Loading external scripts

`ClearScriptV8Runtime` includes a CommonJS-style module loader that can resolve local
files, Avalonia assets (`avares://`), or HTTP resources through the host's resource
resolver.

```javascript
// CommonJS-style modules
const math = require('./modules/math.js');
const result = math.add(2, 3);

// Execute a script for its side effects (e.g. UMD builds)
window.importScripts('./vendor/charting.js');
```

Modules are executed once per host and cached; repeated `require` calls return the same `module.exports` instance.

### Event payloads

Handlers receive simple objects that expose `handled` flags for two-way communication:

```js
textBox.addEventListener('keydown', evt => {
  if (evt.key === 'Enter') {
    evt.handled = true; // stop Avalonia routing
  }
});
```

| Event | Payload |
| --- | --- |
| `pointer*`, `mouse*`, `click` | `{ x, y, button?, handled }` |
| `keydown`, `keyup` | `{ key?, handled }` |
| `textinput`, `input` | `{ text?, handled }` |

## Architecture Overview

```text
WebScene.Sdk (components, profile, lifecycle, host bridge)
                ↓
WebScene portable cores + native V8/immutable-scene runtime
                ↓
Framework presenters and hosts
                ↓
Avalonia · Uno Platform · Flutter · WPF · WinUI
```

R0 through R5 are complete: the semantic cores are portable, Avalonia is the reference
backend package, and the React/TypeScript SDK, compatibility profile, templates, and
component catalog are packaged and tested. The native V8/immutable-scene engine is the
performance path. The next milestone is to mature that path through real application
lifecycle, compatibility, reliability, and certification evidence, then turn the
existing portable contracts into a stable presenter SDK for additional native
frameworks.

WebScene supports a managed ClearScript/Avalonia compatibility mode and a native V8 mode
that publishes immutable scene diffs. See [Managed and native backends](docs/backends.md)
for selection guidance, runtime packages, release automation, and the precise status of
Avalonia, Uno Platform, Flutter, WPF, WinUI, and direct GPU backend extensibility. The
portable contracts are ready for backend authoring, but the shared coordinators and
native scene-reader SDK still need extraction before every framework is a turnkey
integration.

## Roadmap

WebScene's immediate roadmap is to mature the native Avalonia reference path and extract
the stable presenter SDK shared by every host framework: close application lifecycle
and reliability gaps, promote native capability groups through shared compatibility
gates, complete differential and unsupported-feature evidence, and keep the reusable
runtime boundary exercised by real applications.

The Uno Platform and Flutter proofs validate that boundary across managed and native
host models. WPF and WinUI presenters follow once the scene-reader SDK and ABI are
stable. Each framework integration should consume the same tested runtime rather than
forking the DOM, CSS, JavaScript, or scene engines.

The first lifecycle decision is complete. The private TradingView sample now destroys
inactive engines and performs a clean warm-cache restart from retained host
configuration. Its saved-layout restore path was removed after manual failure and
after exceeding the ordinary warm-engine baseline. Clean restart was selected for
reliability and simplicity rather than a universal latency advantage.

The first normalized TradingView differential tranche has also produced and closed a
product-neutral engine defect: transitions between `transform:none` and a transform
list now expose their painted forward and reverse matrices, backed by a required
Chrome/managed/native contract. The current application graph classifies its complete
1,890-action denominator, but 1,021 actions remain blocked on reproducible state
traversal. The next application milestone is to expand reversible frontier traversal,
then rerun the isolated Chrome differential over the newly reachable edges.

See the [supported use cases](use-cases.md) and
[architecture decisions](docs/architecture/README.md).

## Contributing

Contributions, bug reports, and feature requests are welcome! Please open an issue or submit a pull request. When contributing code:

1. Fork the repository and create a feature branch.
2. Run `dotnet build` to ensure the solution compiles.
3. Include tests or sample updates when applicable.
4. Describe the motivation and details in your PR.

## License

This repository uses an MIT-based license with an additional Restricted Party
Clause. The restriction applies to the repository and its NuGet packages. See
[LICENSE](LICENSE) for the full terms.

If your organisation requires a different licensing arrangement, please reach out to discuss commercial options.

## Acknowledgements

- [AvaloniaUI](https://github.com/AvaloniaUI/Avalonia) for the cross-platform UI framework.
- [ClearScript](https://github.com/microsoft/ClearScript) for the V8 hosting layer.
- [AngleSharp](https://anglesharp.github.io/) for HTML/CSS parsing used by WebScene.

---

© Wiesław Šoltés. All rights reserved.
