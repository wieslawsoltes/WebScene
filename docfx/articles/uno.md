# Use WebScene with Uno Platform

`WebScene.Backend.Uno` adapts the native scene engine to Uno's Skia renderer.
`UnoNativeWebSceneView` is a WinUI `ContentControl` backed by an
`UnoNativeSceneSurface`.

> [!WARNING]
> The Uno backend is a presenter proof, not a production-ready drop-in backend. It
> requires Uno's Skia renderer and does not yet carry complete input, text, IME,
> accessibility, resource, lifecycle, packaging, or conformance guarantees.

## 1. Configure a Skia desktop project

Declare a supported desktop RID, enable the Skia renderer, and reference the backend
and matching runtime package.

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
    <PackageReference Include="WebScene.Backend.Uno" Version="1.0.20" />
    <PackageReference Include="WebScene.NativeEngine.Runtime.osx-arm64" Version="1.0.20" />
  </ItemGroup>
</Project>
```

Change `osx-arm64` to `linux-x64` or `win-x64` in both places for the other published
desktop runtimes. Browser, Android, and iOS targets are not supported by the current
native runtime packages.

See [Packages and deployment](packages-and-deployment.md) for the runtime files that
must remain beside the application executable.

## 2. Host the view

The reference sample creates the view in code and places it in a stretching content
host. This keeps the experimental surface easy to replace as the backend evolves.

```xml
<Page
    x:Class="WebSceneUnoDemo.MainPage"
    xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
    xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
  <ContentControl
      x:Name="WebContentHost"
      HorizontalContentAlignment="Stretch"
      VerticalContentAlignment="Stretch" />
</Page>
```

```csharp
using System.Runtime.InteropServices;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using WebScene.Backends.Uno.Native;

namespace WebSceneUnoDemo;

public sealed partial class MainPage : Page
{
    private readonly UnoNativeWebSceneView _webContent = new();

    public MainPage()
    {
        InitializeComponent();
        WebContentHost.Content = _webContent;
        Loaded += OnLoaded;
        Unloaded += OnUnloaded;
    }

    private async void OnLoaded(object sender, RoutedEventArgs e)
    {
        Loaded -= OnLoaded;

        var document = new Uri(
            Path.Combine(AppContext.BaseDirectory, "web", "index.html"));
        var cache = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "WebSceneUnoDemo",
            "V8Cache");

        await _webContent.LoadAsync(
            document.AbsoluteUri,
            NativeLibraryPath(),
            cache);
    }

    private async void OnUnloaded(object sender, RoutedEventArgs e)
    {
        Unloaded -= OnUnloaded;
        await _webContent.DisposeAsync();
    }

    private static string NativeLibraryPath()
    {
        var fileName = RuntimeInformation.IsOSPlatform(OSPlatform.Windows)
            ? "webscene_native_engine.dll"
            : RuntimeInformation.IsOSPlatform(OSPlatform.OSX)
                ? "libwebscene_native_engine.dylib"
                : "libwebscene_native_engine.so";
        return Path.Combine(AppContext.BaseDirectory, fileName);
    }
}
```

Copy local content to the output directory just as you would for the Avalonia host:

```xml
<ItemGroup>
  <Content Include="web/**" CopyToOutputDirectory="PreserveNewest" />
</ItemGroup>
```

Uno and Avalonia do not expose an identical resource-loader surface. Review
[Content and resource loading](content-and-resources.md) before sharing document URLs
between the hosts.

Wait until the content host has a non-zero size before the first load. Keep view
creation, loading, and disposal on Uno's UI synchronization context; the surface is a
UI-bound `SKCanvasElement`.

## Interoperate with JavaScript

The Uno view exposes the same native ABI 3 interop boundary as the Avalonia view:

```csharp
string result = await _webContent.EvaluateTextAsync(
    "({ title: document.title, readyState: document.readyState })");

using var interop = _webContent.CreateJavaScriptInvoker();
// Pass interop to a generated, strongly typed JavaScript facade.
```

The same API manifest, policy, and generated facade can be placed in a framework-neutral
class library and used by both host projects. See
[.NET and JavaScript interop](javascript-interop.md).

## Diagnostics and current limitations

`RenderDiagnostics` and `EngineMetrics` expose scene, input, frame, and cache counters.
`OpenV8InspectorSession` provides a raw V8 Inspector session that can be forwarded by
`WebScene.Diagnostics.Cdp`.

The current proof should be validated against the exact Uno Skia platform and workload
you plan to ship. Do not infer support for non-Skia targets or for web-platform features
outside WebScene's bounded compatibility profile.

The complete integration authority is the
[Uno native runtime showcase](https://github.com/wieslawsoltes/WebScene/tree/main/samples/NativeRuntimeShowcase.Uno).
For shutdown, navigation, and failure-handling patterns, continue with
[Lifecycle and diagnostics](lifecycle-and-diagnostics.md).
