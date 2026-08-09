# Use WebScene with Avalonia

`WebScene.Backend.Avalonia` is the reference presenter. `NativeWebSceneView` is an
Avalonia `ContentControl` that owns a native engine instance and presents its immutable
scene updates with Skia.

## 1. Add the packages

Declare one of the supported RIDs and reference the Avalonia backend plus the matching
native runtime. Keep every WebScene package on the same version.

```xml
<PropertyGroup>
  <TargetFramework>net10.0</TargetFramework>
  <RuntimeIdentifier>osx-arm64</RuntimeIdentifier>
  <BuiltInComInteropSupport>true</BuiltInComInteropSupport>
</PropertyGroup>

<ItemGroup>
  <PackageReference Include="Avalonia.Desktop" Version="11.3.4" />
  <PackageReference Include="Avalonia.Skia" Version="11.3.4" />
  <PackageReference Include="WebScene.Backend.Avalonia" Version="1.0.20" />
  <PackageReference Include="WebScene.NativeEngine.Runtime.osx-arm64" Version="1.0.20" />
</ItemGroup>
```

For Linux x64 or Windows x64, change both `RuntimeIdentifier` and the runtime package
to `linux-x64` or `win-x64` respectively.

See [Packages and deployment](packages-and-deployment.md) for framework-dependent
deployment, single-file behavior, and native asset verification.

## 2. Add the view in XAML

```xml
<Window
    x:Class="WebSceneDemo.MainWindow"
    xmlns="https://github.com/avaloniaui"
    xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
    xmlns:webscene="clr-namespace:WebScene.Backends.Avalonia.Native;assembly=WebScene.Backend.Avalonia">
  <webscene:NativeWebSceneView x:Name="WebContent" />
</Window>
```

The view stretches with its parent and forwards focus, pointer, wheel, and keyboard
input to the native document. It also follows the Avalonia light/dark theme variant.

## 3. Load a document

Load an absolute `file:`, `http:`, or `https:` URL after the window has opened. The
runtime package copies the platform library beside the application, so resolve it from
`AppContext.BaseDirectory`.

```csharp
using System.Runtime.InteropServices;
using Avalonia.Controls;
using WebScene.Backends.Avalonia.Native;

namespace WebSceneDemo;

public sealed partial class MainWindow : Window
{
    public MainWindow()
    {
        InitializeComponent();
        Opened += OnOpened;
        Closed += OnClosed;
    }

    private async void OnOpened(object? sender, EventArgs e)
    {
        Opened -= OnOpened;

        var document = new Uri(
            Path.Combine(AppContext.BaseDirectory, "web", "index.html"));
        var cache = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "WebSceneDemo",
            "V8Cache");

        await WebContent.LoadAsync(
            document.AbsoluteUri,
            NativeLibraryPath(),
            cache);
    }

    private async void OnClosed(object? sender, EventArgs e)
    {
        await WebContent.DisposeAsync();
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

Copy the document and its assets to the output directory:

```xml
<ItemGroup>
  <Content Include="web/**" CopyToOutputDirectory="PreserveNewest" />
</ItemGroup>
```

See [Content and resource loading](content-and-resources.md) before choosing between
`file:`, Avalonia `avares:`, and remote HTTP(S) content.

`LoadAsync` completes after navigation has constructed and published the first document
scene. A new load unloads the current document first. Dispose the view when its owning
window or view lifetime ends.

## Install document-start scripts

Use `NativeWebSceneLoadOptions` for a compatibility shim or a small application-owned
bridge that must exist before authored JavaScript executes.

```csharp
using WebScene.Backends.Native;

await WebContent.LoadAsync(new NativeWebSceneLoadOptions
{
    Source = document.AbsoluteUri,
    NativeLibraryPath = NativeLibraryPath(),
    CompilationCacheDirectory = cache,
    DocumentStartScripts =
    [
        new WebSceneDocumentScript(
            "globalThis.hostEnvironment = Object.freeze({ platform: 'desktop' });",
            "host-environment.js",
            AllFrames: false)
    ]
});
```

Scripts run in order after the document and location exist but before authored scripts.
An exception fails the load. Set `AllFrames` to `true` only when the script should also
run in subsequently created child frames.

## Interoperate with JavaScript

After `LoadAsync` completes, the view supports diagnostic evaluation and generated
typed bindings:

```csharp
string result = await WebContent.EvaluateTextAsync(
    "({ title: document.title, readyState: document.readyState })");

using var interop = WebContent.CreateJavaScriptInvoker();
// Pass interop to a facade generated by WebScene.JavaScript.Interop.Generator.
```

Generated bindings are the application interop path; `EvaluateTextAsync` is intended
for small diagnostics and returns JSON-compatible text. See
[.NET and JavaScript interop](javascript-interop.md).

## Diagnostics

Use `RenderDiagnostics`, `CapturePerformanceSnapshot()`, `DrainConsoleMessages()`,
`LastError`, and `FeatureUseReport` when diagnosing a loaded view. Raw V8 Inspector
sessions are available through `OpenV8InspectorSession`; the optional
`WebScene.Diagnostics.Cdp` package can expose them to Chrome DevTools.

The complete integration authority is the
[Avalonia native runtime showcase](https://github.com/wieslawsoltes/WebScene/tree/main/samples/NativeRuntimeShowcase.Avalonia).
For shutdown, navigation, and failure-handling patterns, continue with
[Lifecycle and diagnostics](lifecycle-and-diagnostics.md).
