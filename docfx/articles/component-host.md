# Package and host a component

`WebSceneComponentHost` is the recommended Avalonia and Uno Platform integration. The
framework-specific controls turn a Component Profile 1 package into one reusable XAML
surface and own the repetitive runtime work:

- manifest and asset validation;
- compatibility preflight before authored code runs;
- a per-instance virtual origin that can serve only declared assets;
- native engine creation and component mount, unmount, and reload;
- capability-gated calls from JavaScript to application services; and
- state, failure, compatibility, and diagnostic reporting.

> [!IMPORTANT]
> The native component-host implementations are available on `main`. Until matching
> `WebScene.Sdk.Avalonia` and `WebScene.Sdk.Uno` packages are published, consume the
> corresponding project from `main`; older package versions may not contain these
> controls.

## 1. Create a component package

A package is a directory containing `webscene-component.json` and every asset named
by that manifest:

```text
components/
  StatusPanel/
    webscene-component.json
    dist/
      main.js
```

```json
{
  "schemaVersion": "1.0",
  "id": "com.example.status-panel",
  "displayName": "Status panel",
  "version": "1.0.0",
  "profileVersion": "1.0",
  "entryPoint": "dist/main.js",
  "assets": [
    "dist/main.js"
  ],
  "capabilities": [
    "dom",
    "css.layout",
    "input.pointer"
  ],
  "lifecycle": {
    "mountExport": "mount",
    "unmountExport": "unmount"
  }
}
```

The entry point must publish the lifecycle functions named by the manifest. A bundler
may produce the file, but its final output must make those functions available on
`globalThis`:

```javascript
let root;

globalThis.mount = async options => {
  root = document.createElement("main");
  root.textContent = `Mounted instance ${options.instanceId}`;
  document.body.appendChild(root);
};

globalThis.unmount = async () => {
  root?.remove();
  root = undefined;
};
```

All paths are normalized, relative package paths. `assets` must include the entry
point. Component Profile 1 currently serves declared UTF-8 text assets; undeclared,
binary, cross-origin, and directory-escaping requests fail closed.

The repository's
[ComponentHost.Basic package](https://github.com/wieslawsoltes/WebScene/tree/main/samples/components/ComponentHost.Basic)
is a complete React example.

## 2. Reference the host and native runtime

Set one explicit supported RID and reference the component host plus the matching
native runtime. Replace `VERSION` with one version shared by all WebScene packages:

```xml
<PropertyGroup>
  <TargetFramework>net10.0</TargetFramework>
  <RuntimeIdentifier>osx-arm64</RuntimeIdentifier>
  <BuiltInComInteropSupport>true</BuiltInComInteropSupport>
</PropertyGroup>

<ItemGroup>
  <!-- Use WebScene.Sdk.Uno in an Uno Skia desktop application. -->
  <PackageReference Include="WebScene.Sdk.Avalonia" Version="VERSION" />
  <PackageReference Include="WebScene.NativeEngine.Runtime.osx-arm64"
                    Version="VERSION" />
</ItemGroup>
```

Use `linux-x64` or `win-x64` in both places for the other published desktop
runtimes. `WebScene.Sdk.Avalonia` brings in the portable SDK and Avalonia presenter;
`WebScene.Sdk.Uno` brings in the same SDK and the Uno Skia presenter.

When consuming the repository before the package release, replace the SDK package
reference with a project reference to the corresponding
`src/WebScene.Sdk.Avalonia/WebScene.Sdk.Avalonia.csproj` or
`src/WebScene.Sdk.Uno/WebScene.Sdk.Uno.csproj`.

## 3. Copy the package to application output

`PackagePath` is resolved relative to `AppContext.BaseDirectory` unless it is
absolute. Preserve the package layout in build and publish output:

```xml
<ItemGroup>
  <Content Include="components/**"
           CopyToOutputDirectory="PreserveNewest"
           CopyToPublishDirectory="PreserveNewest" />
</ItemGroup>
```

## 4. Add one XAML control

Avalonia uses the Avalonia SDK namespace:

```xml
<Window
    x:Class="WebSceneDemo.MainWindow"
    xmlns="https://github.com/avaloniaui"
    xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
    xmlns:ws="using:WebScene.Sdk.Avalonia">
  <ws:WebSceneComponentHost
      x:Name="ComponentHost"
      PackagePath="components/StatusPanel" />
</Window>
```

Uno uses the WinUI XAML namespace and Uno SDK host:

```xml
<Page
    x:Class="WebSceneDemo.MainPage"
    xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
    xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
    xmlns:ws="using:WebScene.Sdk.Uno">
  <ws:WebSceneComponentHost
      x:Name="ComponentHost"
      PackagePath="components/StatusPanel" />
</Page>
```

That is the complete basic integration. `AutoMount` defaults to `true`: attaching
the control mounts the component and detaching it unmounts the component. The host
finds the native library beside the application. For development builds only, set
`NativeLibraryPath` or `WEBSCENE_NATIVE_ENGINE_LIBRARY` when the library is
elsewhere.

Dispose the host when its owning window, page, or application lifetime ends. For
example, an Avalonia window can use:

```csharp
Closed += async (_, _) => await ComponentHost.DisposeAsync();
```

## Grant application capabilities

A component can call an application service only when:

1. its manifest declares the corresponding `host.*` capability; and
2. the application registers a handler before mounting.

Use explicit mounting when handlers or document-start scripts must be installed first:

```xml
<ws:WebSceneComponentHost
    x:Name="ComponentHost"
    PackagePath="components/StatusPanel"
    AutoMount="False" />
```

```csharp
using System.Text.Json;
using WebScene.Sdk;

ComponentHost.RegisterHostCapability(
    new WebSceneDelegateCapabilityHandler(
        WebSceneComponentCapabilities.Commands,
        HandleCommandAsync));

// Call after the host is attached, for example from Window.Opened.
await ComponentHost.MountAsync(cancellationToken);

static ValueTask<JsonElement?> HandleCommandAsync(
    string method,
    JsonElement arguments,
    CancellationToken cancellationToken)
{
    cancellationToken.ThrowIfCancellationRequested();
    if (method != "refresh")
    {
        throw new InvalidOperationException($"Unknown command '{method}'.");
    }

    return ValueTask.FromResult<JsonElement?>(
        JsonSerializer.SerializeToElement(new { accepted = true }));
}
```

The component calls the handler through the installed, asynchronous JSON bridge:

```javascript
const result = await webscene.host.commands.invoke("refresh", {
  source: "status-panel"
});
```

Available host capabilities are `host.commands`, `host.settings`,
`host.notifications`, `host.network`, `host.clipboard`, and `host.files`.
Register only the capabilities and methods the component needs, validate arguments at
the .NET boundary, and do not treat this in-process bridge as a sandbox for untrusted
code.

## Lifecycle, errors, and diagnostics

For manual lifecycle control, use `MountAsync`, `UnmountAsync`, and `ReloadAsync`.
The `State` property moves through `Idle`, `Mounting`, `Mounted`, `Unmounting`,
`Faulted`, and `Disposed`.

Subscribe to `StateChanged`, `ComponentMounted`, `ComponentUnmounted`,
`MountFailed`, and `DiagnosticReported` for application-level observability.
`LastException`, `CompatibilityReport`, and `Diagnostics` retain the latest
failure and preflight details.

The underlying `NativeWebSceneView` is available through `ComponentHost.View` when
you need generated typed interop, `EvaluateTextAsync` diagnostics, performance
snapshots, console draining, or a V8 Inspector session. Application code should not
navigate or dispose that view independently while the component host owns it.

Continue with [.NET and JavaScript interop](javascript-interop.md),
[Lifecycle and diagnostics](lifecycle-and-diagnostics.md), and
[Compatibility and security](compatibility-and-security.md).
