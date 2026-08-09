# Use WebScene with Uno Platform

Uno Platform is a first-class WebScene host for Skia desktop applications.
`WebScene.Backend.Uno` presents the native scene through Uno's `SKCanvasElement`, and
`WebScene.Sdk.Uno` provides the reusable WinUI `WebSceneComponentHost` for packaged
Component Profile 1 applications.

The Uno host exposes the same package validation, compatibility preflight, virtual
origin, lifecycle, capability bridge, interop, and diagnostics model as the Avalonia
host through Uno-native WinUI XAML. It requires Uno's Skia renderer and currently
supports desktop applications on `osx-arm64`, `linux-x64`, and `win-x64`; Uno browser,
mobile, and non-Skia targets are outside this support statement.

## 1. Configure a Skia desktop project

Declare a supported desktop RID, enable the Skia renderer, and reference the Uno SDK
host plus the matching runtime package.

```xml
<Project Sdk="Uno.Sdk/6.5.31">
  <PropertyGroup>
    <TargetFrameworks>net10.0-desktop</TargetFrameworks>
    <RuntimeIdentifier>osx-arm64</RuntimeIdentifier>
    <OutputType>Exe</OutputType>
    <UnoSingleProject>true</UnoSingleProject>
    <UnoFeatures>SkiaRenderer;</UnoFeatures>
    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>
  </PropertyGroup>

  <ItemGroup>
    <PackageReference Include="WebScene.Sdk.Uno" Version="1.0.20" />
    <PackageReference Include="WebScene.NativeEngine.Runtime.osx-arm64" Version="1.0.20" />
  </ItemGroup>
</Project>
```

Change `osx-arm64` to `linux-x64` or `win-x64` in both places for the other published
desktop runtimes. See [Packages and deployment](packages-and-deployment.md) for the
runtime files that must remain beside the application executable.

## 2. Mount a packaged component

Copy a component package to output without changing its manifest-relative layout:

```xml
<ItemGroup>
  <Content Include="components/MyComponent/**"
           CopyToOutputDirectory="PreserveNewest" />
</ItemGroup>
```

Place the host directly in Uno XAML. `AutoMount` defaults to `true`; the host validates
the package, creates an isolated virtual origin, loads only declared assets, installs
the capability bridge, invokes the manifest's mount export, and unmounts with the
control lifetime.

```xml
<Page
    x:Class="WebSceneUnoDemo.MainPage"
    xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
    xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
    xmlns:webscene="using:WebScene.Sdk.Uno">
  <webscene:WebSceneComponentHost
      x:Name="ComponentHost"
      PackagePath="components/MyComponent"
      HorizontalAlignment="Stretch"
      VerticalAlignment="Stretch" />
</Page>
```

Use explicit lifecycle when the application must register host capabilities first:

```csharp
using WebScene.Sdk;
using WebScene.Sdk.Uno;

ComponentHost.AutoMount = false;
ComponentHost.RegisterHostCapability(
    new WebSceneDelegateCapabilityHandler(
        WebSceneComponentCapabilities.Commands,
        (method, arguments, cancellationToken) =>
            HandleCommandAsync(method, arguments, cancellationToken)));

await ComponentHost.MountAsync();
```

`MountAsync`, `UnmountAsync`, and `ReloadAsync` are reusable lifecycle operations.
`StateChanged`, `ComponentMounted`, `ComponentUnmounted`, `MountFailed`,
`DiagnosticReported`, `Diagnostics`, and `LastException` expose the same host-level
observability as the Avalonia component host.

The native library is resolved from `WEBSCENE_NATIVE_ENGINE_LIBRARY` or the application
directory by default. Set `NativeLibraryPath` and `CompilationCacheDirectory` when the
application needs explicit locations.

## Host a document directly

Applications that own document navigation rather than a component package can use the
underlying `WebScene.Backend.Uno` view directly:

```csharp
using WebScene.Backends.Uno.Native;

var view = new UnoNativeWebSceneView();
await view.LoadAsync(
    documentUri.AbsoluteUri,
    nativeLibraryPath,
    compilationCacheDirectory);
```

Keep view creation, loading, reusable `UnloadAsync`, and final `DisposeAsync` on Uno's
UI synchronization context. Wait until the containing control has non-zero layout
before the first load.

## Interoperate with JavaScript

The component host exposes its underlying view for the same ABI 3 interop boundary as
Avalonia:

```csharp
string result = await ComponentHost.View.EvaluateTextAsync(
    "({ title: document.title, readyState: document.readyState })");

using var interop = ComponentHost.View.CreateJavaScriptInvoker();
// Pass interop to a generated, strongly typed JavaScript facade.
```

The same typed interop API manifest, policy, and generated facade can be placed in a
framework-neutral class library and used by both host projects. The component host
installs the same `webscene.host.*` capability bridge and Component Profile 1 mount
lifecycle used by Avalonia. See [.NET and JavaScript interop](javascript-interop.md).

## Diagnostics and validation

`RenderDiagnostics` and `EngineMetrics` expose scene, input, frame, and cache counters.
`OpenV8InspectorSession` provides a raw V8 Inspector session that can be forwarded by
`WebScene.Diagnostics.Cdp`.

First-class presenter support does not broaden WebScene's bounded compatibility or
security profile. Validate the exact component, package version, native RID, input,
text, IME, accessibility, and shutdown behavior your product requires.

The integration authority is the
[Uno native runtime showcase](https://github.com/wieslawsoltes/WebScene/tree/main/samples/NativeRuntimeShowcase.Uno),
which mounts `ComponentHost.Basic` through `WebScene.Sdk.Uno` by default and retains
Monaco and TradingView workload modes. For shutdown, navigation, and failure-handling
patterns, continue with [Lifecycle and diagnostics](lifecycle-and-diagnostics.md).
