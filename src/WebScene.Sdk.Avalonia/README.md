# WebScene.Sdk.Avalonia

Subscribe to `host.View.JavaScriptException` and `host.View.RuntimeFailed` before
mounting to log page-initiated failures. Console capture remains optional via
`host.View.ConsoleMessage`. See [Runtime diagnostics](../../docs/runtime-diagnostics.md)
for production logging, callback threading and opt-in failure UI.

`WebSceneComponentHost` is the reusable native Avalonia host for WebScene
Component Profile 1 packages. It validates the package, runs compatibility
preflight, isolates declared assets behind a per-instance virtual origin,
installs the capability-gated host bridge, and owns mount/unmount/reload.

```xml
<UserControl xmlns:ws="using:WebScene.Sdk.Avalonia">
<ws:WebSceneComponentHost
    x:Name="ComponentHost"
    PackagePath="components/ComponentHost.Basic"
    NativeLibraryPath="/absolute/path/to/libwebscene_native_engine.dylib" />
</UserControl>
```

Register application services before mounting:

```csharp
ComponentHost.RegisterHostCapability(
    new WebSceneDelegateCapabilityHandler(
        WebSceneComponentCapabilities.Commands,
        (method, arguments, cancellationToken) => HandleCommandAsync(
            method, arguments, cancellationToken)));

await ComponentHost.MountAsync();
```

`AutoMount` defaults to `true` and follows visual-tree attachment. Use
`AutoMount="False"` for explicit lifecycle control. `StateChanged`,
`ComponentMounted`, `ComponentUnmounted`, `MountFailed`, `DiagnosticReported`,
`Diagnostics`, and `LastException` provide host-level observability. The
underlying `NativeWebSceneView` remains available through `View` for generated
interop, performance snapshots, console draining, and V8 Inspector sessions.

The native engine path may also be supplied through
`WEBSCENE_NATIVE_ENGINE_LIBRARY` or by deploying its platform library beside the
application.
