# HtmlML

[![HtmlML NuGet](https://img.shields.io/nuget/vpre/HtmlML.svg)](https://www.nuget.org/packages/HtmlML/) [![HtmlML Backend NuGet](https://img.shields.io/nuget/vpre/HtmlML.Backend.Avalonia.svg)](https://www.nuget.org/packages/HtmlML.Backend.Avalonia/)

HtmlML brings HTML-inspired markup and V8 scripting capabilities to [Avalonia](https://avaloniaui.net/). The repository contains reusable markup, browser-services, and runtime libraries:

- **HtmlML** – a markup layer that renders HTML-like tags inside Avalonia applications, complete with styling and canvas support.
- **HtmlML.Backend.Avalonia** – DOM, browser, event, canvas, layout, and presentation services for Avalonia.
- **JavaScript.Avalonia.ClearScript** – the ClearScript/V8 execution adapter, module loader, virtual-iframe runtime, and compilation cache.
- **HtmlML.Sdk** – versioned component manifests, compatibility checks, offline assets, lifecycle diagnostics, and the capability-based host bridge.
- **HtmlML.Sdk.Avalonia** – the XAML-first packaged React/TypeScript component host.

Together they enable you to describe user interfaces with familiar HTML semantics while orchestrating dynamic behaviour from JavaScript—no browser required.

## Usage Restriction Notice

At maintainer request, AvaloniaUI OÜ may not use this repository in any form.

This restriction is defined in the repository [LICENSE](LICENSE).

## Highlights

- ⚡ **Avalonia-first**: Render HTML-inspired controls natively, respecting Avalonia layout, styling, and theming.
- 🧠 **V8 JavaScript engine**: Run scripts through ClearScript/V8 with `window`, `document`, timers, animation frames, modules, and console access.
- 🧩 **DOM abstraction**: Query, create, and mutate Avalonia controls through a DOM-like API (`getElementById`, `querySelector`, `appendChild`, `setAttribute`, etc.).
- 🕹️ **Event bridge**: Wire Avalonia routed events (`click`, `pointerdown`, `keydown`, `input`, …) to JavaScript callbacks with strongly-typed payloads.
- 🖼️ **Canvas integration**: HtmlML ships with a `<canvas>` element that mirrors the familiar 2D drawing API.
- 🧱 **Extensible architecture**: Override document/element factories or compose custom hosts to tailor the experience for your application.

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
native V8 package for their target RID.

### Consuming the libraries

The current package line is prerelease. Reference the packages needed by the selected
engine; for example, an Avalonia host using the native engine on macOS ARM64 uses:

```xml
<ItemGroup>
  <PackageReference Include="HtmlML.Backend.Avalonia" Version="11.3.4-alpha.4" />
  <PackageReference Include="HtmlML.NativeEngine.Runtime.osx-arm64" Version="11.3.4-alpha.4" />
</ItemGroup>
```

The runtime package copies the native module, ICU data, and version/ABI manifest to
build and publish output. `win-x64` and `linux-x64` publishing are temporarily
deferred while their pinned V8 builds move to faster, independently validated lanes.

## Using HtmlML

HtmlML exposes HTML-like tags (heading levels, paragraphs, lists, sections, navigation, canvas, etc.) that map to Avalonia controls. Example:

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
backend package, and the React/TypeScript SDK is packaged and template-tested. R6 is
the direct ProGPU backend proof using the same component assets and profile contracts.

HtmlML supports a managed ClearScript/Avalonia mode and an opt-in native V8 mode that
publishes immutable scene diffs. See [Managed and native backends](docs/backends.md) for
selection guidance, runtime packages, release automation, and the precise status of
Uno, WPF, and direct GPU backend extensibility. The portable contracts are ready for
backend authoring, but the shared coordinators and native scene-reader SDK still need
extraction before those backends are turnkey integrations.

## Roadmap

HtmlML's roadmap is to extract reusable JavaScript, DOM, CSS/layout, and graphics cores
from the current Avalonia implementation while preserving Avalonia as the reference
backend and adding direct ProGPU, WPF, WinUI, and Uno backends. React/TypeScript
tooling, an explicit component compatibility profile, and an executable sample for
every supported product use case are part of the same plan.

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
