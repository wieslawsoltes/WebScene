# HtmlML

[![HtmlML NuGet](https://img.shields.io/nuget/vpre/HtmlML.svg)](https://www.nuget.org/packages/HtmlML/) [![HtmlML Backend NuGet](https://img.shields.io/nuget/vpre/HtmlML.Backend.Avalonia.svg)](https://www.nuget.org/packages/HtmlML.Backend.Avalonia/)

HtmlML is a native V8 and immutable-scene runtime for [Avalonia](https://avaloniaui.net/). Its flagship path runs JavaScript, DOM/CSS state, layout, input dispatch, Canvas, and SVG on a native engine thread, then publishes immutable scene diffs to the Avalonia compositor. This keeps hot UI work out of the managed object graph and UI dispatcher while preserving a browser-shaped compatibility surface for packaged React, TypeScript, and JavaScript components.

Avalonia remains responsible for the application window, scene presentation, platform input, lifecycle, and native .NET integration. The result is a high-performance native desktop surface without Chromium, WebKit, Electron, or a WebView. HtmlML's HTML-like markup and DOM APIs are the compatibility layer and direct authoring surface, not a promise to run arbitrary websites or to reproduce a complete browser.

Compatibility is explicit and testable: managed ClearScript/Avalonia mode remains the behavioral oracle and compatibility fallback, while native support is promoted by shared conformance, rendering, input, and performance gates.

The repository contains the runtime, component-hosting SDK, and Avalonia integration:

- **HtmlML** – the HTML-like markup and direct authoring layer, with styling and Canvas support.
- **HtmlML.Backend.Avalonia** – the Avalonia scene presenter, native runtime host, and managed presentation services.
- **HtmlML.NativeEngine.Runtime** – RID-specific native V8/DOM/CSS/layout/scene runtime packages.
- **JavaScript.Avalonia.ClearScript** – the managed ClearScript/V8 compatibility engine, module loader, virtual-iframe runtime, and compilation cache.
- **HtmlML.Sdk** – versioned component manifests, compatibility checks, offline assets, lifecycle diagnostics, and the capability-based host bridge.
- **HtmlML.Sdk.Avalonia** – the XAML-first packaged React/TypeScript component host.

Together they make JavaScript UI components first-class citizens in native Avalonia applications, combining native-scene performance with an explicit and intentionally bounded web-platform profile.

## Usage Restriction Notice

At maintainer request, AvaloniaUI OÜ may not use this repository in any form.

This restriction is defined in the repository [LICENSE](LICENSE).

## Highlights

- 🚀 **Native scene engine**: Run V8, DOM/CSS, layout, input, Canvas, and SVG off the UI thread and publish immutable, damage-aware scene diffs to the Avalonia compositor.
- ⚡ **Native application composition**: Combine JavaScript components with XAML, C#, native controls, menus, settings, and operating-system services.
- 🧩 **Component hosting**: Mount versioned, offline React/TypeScript/JavaScript bundles through an engine-neutral component profile and Avalonia host.
- 🧠 **Compatibility by contract**: Share DOM, CSS, rendering, input, lifecycle, and cache contracts between native and managed engines; promote support through conformance gates.
- 🔌 **Capability-based host bridge**: Expose selected asynchronous .NET services to trusted components without giving them an implicit application-wide API.
- 🕹️ **DOM and event integration**: Query and mutate the projected visual surface and route pointer, keyboard, text, focus, and routed-event behavior to JavaScript.
- 🖼️ **HTML-like authoring and Canvas**: Use familiar markup, styling, and Canvas APIs directly when a packaged component is not the right shape.

## Repository Layout

| Path | Description |
| --- | --- |
| `src/HtmlML.Core` | UI-framework-neutral values and host/backend contracts. |
| `src/HtmlML.Backend.Abstractions` | Backend manifests, validation, and capability negotiation. |
| `src/HtmlML.Backend.Avalonia` | Current Avalonia presentation implementation. |
| `src/HtmlML` | HtmlML markup library and HTML element implementations. |
| `src/JavaScript.Avalonia` | Engine-neutral browser/DOM services for Avalonia. |
| `src/JavaScript.Avalonia.ClearScript` | ClearScript/V8 execution adapter and shared compilation cache. |
| `src/HtmlML.Sdk` | Portable Component Profile 1 product contracts and host bridge. |
| `src/HtmlML.Sdk.Avalonia` | Avalonia `HtmlMlComponentHost` for packaged components. |
| `tooling/htmlml` | Bounded TypeScript declarations, checker, and Vite/esbuild plugins. |
| `templates/HtmlML.Templates` | Component-host, hybrid, and TypeScript `dotnet new` templates. |
| `samples/components` | Twelve versioned, offline component packages shared by backends. |
| `samples/hosts/Avalonia` | Runnable `.csproj` hosts: the R5 catalog and three standalone product shapes. |
| `third-party/clearscript` | ClearScript 7.5.1 source submodule on the HtmlML native patch branch. |
| `third-party/v8` | V8 14.7.173.23 source submodule on ClearScript's compatibility patch branch. |
| `packaging/HtmlML.NativeEngine.Runtime` | RID-specific native V8/DOM/CSS/scene runtime package definition. |
| `samples/website` | HtmlML showcase demonstrating markup, styling, and canvas scripting. |
| `samples/JavaScriptPlayground` | Interactive playground with editable XAML, live preview, and JavaScript console for `JavaScript.Avalonia`. |

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
dotnet build HtmlML.sln
```

### Running the samples

```bash
# Browse and run all 12 R5 component packages
dotnet run --project samples/hosts/Avalonia/HtmlML.Sdk.SampleCatalog

# Run one of the copyable R5 product-shape hosts
dotnet run --project samples/hosts/Avalonia/ComponentHost.Basic
dotnet run --project samples/hosts/Avalonia/Hybrid.ReactIslands
dotnet run --project samples/hosts/Avalonia/TypeScriptDesktop

# HtmlML website sample
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

After installing the `HtmlML.Templates` package, create one of the supported product
shapes:

```bash
dotnet new htmlml-component-host -n MyComponentHost
dotnet new htmlml-hybrid -n MyHybridApp
dotnet new htmlml-typescript -n MyTypeScriptApp
cd MyTypeScriptApp/web
npm install
npm run build
cd ..
dotnet run
```

The web build runs the bounded compatibility checker and emits a versioned
`htmlml-component.json`. Host services are available only through declared,
asynchronous `htmlml.host.*` capabilities. Applications must also ship the reviewed
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
  <PackageReference Include="HtmlML.Backend.Avalonia" Version="11.3.4-alpha.6" />
  <PackageReference Include="HtmlML.NativeEngine.Runtime.osx-arm64" Version="11.3.4-alpha.6" />
</ItemGroup>
```

The runtime package copies the native module, ICU data, and version/ABI manifest to
build and publish output. `win-x64` and `linux-x64` publishing are temporarily
deferred while their pinned V8 builds move to faster, independently validated lanes.

## Using the HTML-like authoring layer

HtmlML also supports direct authoring with HTML-like tags (heading levels, paragraphs, lists, sections, navigation, Canvas, and more) mapped to Avalonia presentation services. Packaged React/TypeScript components normally enter through `HtmlMlComponentHost`; use this lower-level surface when you want to author the document directly:

```xml
<html xmlns="https://github.com/avaloniaui"
      xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
      x:Class="Demo.index"
      title="HtmlML Demo">
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
      <h1>Hello from HtmlML</h1>
      <canvas id="draw" width="400" height="200" />
    </section>
  </body>
</html>
```

HtmlML parses the markup, applies classes and inline styles, wires `<canvas>` pointers to JavaScript, and allows scripts to manipulate the resulting visual tree.

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
HtmlML.Sdk (components, profile, lifecycle, host bridge)
        ↓                         ↓
HtmlML portable cores       JavaScript.Avalonia.ClearScript (V8)
        ↓                         ↓
HtmlML.Backend.Avalonia + HtmlML.Sdk.Avalonia
```

R0 through R5 are complete: the semantic cores are portable, Avalonia is the reference
backend package, and the React/TypeScript SDK, compatibility profile, templates, and
component catalog are packaged and tested. The native V8/immutable-scene engine is the
performance path. The next milestone is to mature that path through real application
lifecycle, compatibility, reliability, and certification evidence.

HtmlML supports a managed ClearScript/Avalonia compatibility mode and a native V8 mode
that publishes immutable scene diffs. See [Managed and native backends](docs/backends.md)
for selection guidance, runtime packages, release automation, and the precise status of
Uno, WPF, and direct GPU backend extensibility. The portable contracts are ready for
backend authoring, but the shared coordinators and native scene-reader SDK still need
extraction before those backends are turnkey integrations.

## Roadmap

HtmlML's immediate roadmap is to mature the native Avalonia product path: close
application lifecycle and reliability gaps, promote native capability groups through
shared compatibility gates, complete differential and unsupported-feature evidence,
and keep the reusable runtime boundary exercised by real private samples.

The Uno demo remains useful as an integration proof and a way to generate interest,
but expanding it is not a current engineering priority. Direct ProGPU, Flutter, WPF,
WinUI, and further backend work is deferred until the native runtime, scene ABI, and
certification process have matured. Those proofs should consume the stable shared
runtime rather than drive premature abstractions into it.

The first lifecycle item is to reassess chart suspension. Saved-layout resume has not
yet proved reliable in the private TradingView sample. Compare it with destroying the
inactive engine and creating a clean warm-cache engine from host configuration, without
saved-layout restoration. Prefer the simpler restart path if it restores a usable chart
more reliably or faster.

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
- [AngleSharp](https://anglesharp.github.io/) for HTML/CSS parsing used by HtmlML.

---

© Wiesław Šoltés. All rights reserved.
