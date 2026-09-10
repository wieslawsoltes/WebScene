# WebScene.Sdk.Uno

Subscribe to `host.View.JavaScriptException` and `host.View.RuntimeFailed` before
mounting to log page-initiated failures. Console capture remains optional via
`host.View.ConsoleMessage`. See [Runtime diagnostics](../../docs/runtime-diagnostics.md)
for production logging, callback threading and opt-in failure UI.

`WebSceneComponentHost` is the reusable native Uno Platform host for WebScene
Component Profile 1 packages. It validates the package, runs compatibility
preflight, isolates declared assets behind a per-instance virtual origin,
installs the capability-gated host bridge, and owns mount/unmount/reload.

```xml
<Page xmlns:ws="using:WebScene.Sdk.Uno">
  <ws:WebSceneComponentHost
      x:Name="ComponentHost"
      PackagePath="components/ComponentHost.Basic"
      NativeLibraryPath="/absolute/path/to/libwebscene_native_engine.dylib" />
</Page>
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

`AutoMount` defaults to `true` and follows the Uno control's loaded lifetime. Use
`AutoMount="False"` for explicit lifecycle control. `StateChanged`,
`ComponentMounted`, `ComponentUnmounted`, `MountFailed`, `DiagnosticReported`,
`Diagnostics`, and `LastException` provide host-level observability. The underlying
`UnoNativeWebSceneView` remains available through `View` for generated interop,
performance snapshots, and V8 Inspector sessions.

The Uno host requires an Uno Skia desktop target. The native engine path may also be
supplied through `WEBSCENE_NATIVE_ENGINE_LIBRARY` or by deploying its platform library
beside the application.
