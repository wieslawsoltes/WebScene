# Lifecycle and diagnostics

Each `WebSceneComponentHost`, `NativeWebSceneView`, or `UnoNativeWebSceneView`
owns one native engine context. Treat component lifecycle, interop handles, navigation,
and disposal as one lifetime.

## Recommended component-host lifecycle

```text
create WebSceneComponentHost
      |
      v
configure capabilities and startup scripts
      |
      v
attach -> automatic MountAsync
      |
      v
manifest + asset validation + compatibility preflight
      |
      v
native load + bridge installation + JavaScript mount export
      |
      +----> ReloadAsync = UnmountAsync + MountAsync
      |
      v
detach -> automatic UnmountAsync
      |
      v
dispose the host at the end of its owner lifetime
```

`AutoMount` defaults to `true`. Visual-tree attachment starts mounting, and
detachment unmounts the component. Use `AutoMount="False"` and call `MountAsync`
when the application must register capabilities, document-start scripts, or an
Inspector hook first.

`MountAsync` opens and validates the package, runs compatibility preflight, loads the
generated component document, installs the capability bridge, evaluates the entry
point, invokes its mount export, and records the component instance as mounted. A
failed or canceled mount cleans up the partial engine and moves the host to `Faulted`.

The host exposes `Idle`, `Mounting`, `Mounted`, `Unmounting`, `Faulted`, and
`Disposed` states. Observe `StateChanged`, `ComponentMounted`,
`ComponentUnmounted`, `MountFailed`, and `DiagnosticReported` rather than
inferring state from visual attachment.

## Component reload and cancellation

`ReloadAsync` unmounts the existing instance and mounts a fresh instance from
`PackagePath`. Generated proxies, JavaScript object references, callback functions,
and invokers belong to the old isolate and must be disposed before reload.

Pass a cancellation token to explicit mount, unmount, or reload operations. The host
serializes lifecycle transitions and cancels an in-progress mount when unmount begins.
Host capabilities can be added or removed only while the host is `Idle` or
`Faulted`.

## Component shutdown

Detachment unmounts the component but does not end the control's reusable lifetime.
Dispose generated interop objects from the leaves inward, then dispose the component
host when its owning window or application ends:

```csharp
private async Task ShutDownAsync()
{
    if (_editor is not null)
    {
        await _editor.DisposeAsync();
        _editor = null;
    }

    _invoker?.Dispose();
    _invoker = null;

    await ComponentHost.DisposeAsync();
}
```

The host disposes its underlying `NativeWebSceneView`; application code must not
dispose `ComponentHost.View` separately.

## Advanced direct-view lifecycle

`NativeWebSceneView` and `UnoNativeWebSceneView` remain the low-level surfaces.
`LoadAsync` prewarms the process runtime, creates an engine, queues navigation,
checks for immediate script errors, and waits for a document barrier. Avalonia
additionally waits for the first native scene publication. A failed or canceled load
tears down the partially created engine before rethrowing.

## Attach before loading

Create and attach the view before loading so it has a real size and UI context.

- In Avalonia desktop applications, load from `Window.Opened` or an equivalent
  view-activation path.
- In Uno, load after `Loaded` and after the content host has non-zero `ActualWidth` and
  `ActualHeight`.
- Keep Uno creation, surface attachment, and disposal on the UI synchronization
  context.

Do not start document work in a view constructor. Constructors should establish the
visual tree; asynchronous loading belongs to the host lifecycle.

### Navigation and cancellation

Calling `LoadAsync` again unloads the current document before creating the replacement.
Pass a cancellation token owned by the navigation or view activation:

```csharp
private CancellationTokenSource? _navigation;

private async Task NavigateAsync(string documentUri)
{
    _navigation?.Cancel();
    _navigation?.Dispose();
    _navigation = new CancellationTokenSource();

    await WebContent.LoadAsync(
        documentUri,
        nativeLibraryPath,
        compilationCacheDirectory,
        _navigation.Token);
}
```

Do not reuse JavaScript object references, generated proxy objects, callback functions,
or invokers after navigation. They belong to the previous isolate. Dispose them before
starting the next load.

### Shutdown

Dispose interop objects from the leaves inward, then dispose the host view:

```csharp
private async Task ShutDownDirectViewAsync()
{
    if (_editor is not null)
    {
        await _editor.DisposeAsync();
        _editor = null;
    }

    _invoker?.Dispose();
    _invoker = null;

    await WebContent.DisposeAsync();
}
```

Avalonia applications commonly call this from `Window.Closed`. Uno applications use
their page/window unload or application shutdown path. Guard an `async void` event
handler with `try`/`catch` and log shutdown failures; application logic should otherwise
prefer `Task`-returning methods.

Avalonia's `UnloadAsync` releases the current document while keeping the view reusable;
a later `LoadAsync` can create a new document. The Uno proof does not currently expose
a public reusable unload operation, although a second `LoadAsync` replaces its current
document internally. On both hosts, `DisposeAsync` ends the view lifetime and should be
the final operation.

## Diagnostic surfaces

Start with the component host's `LastException`, `CompatibilityReport`,
`Diagnostics`, and `DiagnosticReported` event. These distinguish manifest,
compatibility, capability, mount-export, and unmount-export failures before you inspect
the engine.

Use `ComponentHost.View` for native scene and engine information. Both presenters
expose that information, though the exact public properties differ:

| Need | Avalonia | Uno |
| --- | --- | --- |
| Scene publication/render counts | `RenderDiagnostics` | `RenderDiagnostics` |
| Engine counters | `CapturePerformanceSnapshot()` | `EngineMetrics` |
| Diagnostic JavaScript | `EvaluateTextAsync` | `EvaluateTextAsync` |
| Raw V8 Inspector | `OpenV8InspectorSession` | `OpenV8InspectorSession` |
| Wait for Inspector startup | `WaitForV8InspectorAvailableAsync` | `WaitForV8InspectorAvailableAsync` |
| Console messages | `DrainConsoleMessages()` | Not currently exposed by the proof view |
| Native last error and feature report | `LastError`, `FeatureUseReport` | Not currently exposed by the proof view |

Sample component scene health without resetting counters:

```csharp
var diagnostics = ComponentHost.View.RenderDiagnostics;
Console.WriteLine(
    $"rendered={diagnostics.RenderedSceneCount}, " +
    $"published={diagnostics.PublishedSceneCount}");
```

On Avalonia, `CapturePerformanceSnapshot()` opts that context into detailed runtime-work
counters on first use. Capture a baseline, then use `Since(previous)` on a later
snapshot rather than resetting process state.

Drain console messages on a timer or diagnostic command instead of once at shutdown:

```csharp
foreach (var message in ComponentHost.View.DrainConsoleMessages())
{
    logger.LogInformation("WebScene console: {Message}", message);
}
```

## Chrome DevTools

Add `WebScene.Diagnostics.Cdp` when raw V8 protocol access should be discoverable by
Chrome. The repository samples understand these command-line forms:

```bash
--webscene-inspect
--webscene-inspect=127.0.0.1:9229
--webscene-inspect-brk=127.0.0.1:9229
```

The corresponding environment variables are `WEBSCENE_INSPECT`,
`WEBSCENE_INSPECT_BRK`, and the legacy `WEBSCENE_V8_INSPECTOR`. Loopback is the safe
default. A remote binding requires an explicit allow-remote option and a
`WEBSCENE_INSPECT_TOKEN` of at least 32 characters.

For break-before-navigation, create and start `WebSceneV8InspectorHost` inside the
`LoadAsync` before-navigation hook and use an infinite first-scene/document-barrier
timeout. This lets V8 wait for `Runtime.runIfWaitingForDebugger` without freezing the UI
thread.

Open `chrome://inspect`, configure the reported loopback endpoint, and select the
WebScene target. Dispose the CDP host before disposing its view.

See the full
[V8 Inspector guide](https://github.com/wieslawsoltes/WebScene/blob/main/docs/v8-inspector-debugging.md)
and [Troubleshooting](troubleshooting.md).
