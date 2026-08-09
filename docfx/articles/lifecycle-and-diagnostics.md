# Lifecycle and diagnostics

Each `NativeWebSceneView` or `UnoNativeWebSceneView` owns one native engine context.
Treat loading, interop handles, navigation, and disposal as one lifetime.

## Lifecycle sequence

```text
create host view
      |
      v
attach to a sized visual tree
      |
      v
LoadAsync(document, native library, cache)
      |
      v
create interop proxies and use the document
      |
      +----> LoadAsync(new document) disposes the previous engine
      |
      v
dispose proxies and invoker
      |
      v
UnloadAsync or DisposeAsync the host view
```

`LoadAsync` prewarms the process runtime, creates an engine, queues navigation, checks
for immediate script errors, and waits for a document barrier. Avalonia additionally
waits for the first native scene publication. A failed or canceled load tears down the
partially created engine before rethrowing.

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

## Navigation and cancellation

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

## Shutdown

Dispose interop objects from the leaves inward, then dispose the host view:

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

    await WebContent.DisposeAsync();
}
```

Avalonia applications commonly call this from `Window.Closed`. Uno applications use
their page/window unload or application shutdown path. Guard an `async void` event
handler with `try`/`catch` and log shutdown failures; application logic should otherwise
prefer `Task`-returning methods.

Both presenters expose `UnloadAsync` to release the current document while keeping the
view reusable; a later `LoadAsync` creates a new document. `DisposeAsync` ends the view
lifetime and should be the final operation. The framework-specific
`WebSceneComponentHost` controls own this sequence automatically for component
mount/unmount/reload.

## Diagnostic surfaces

Both presenters expose native scene and engine information, though the exact public
properties differ:

| Need | Avalonia | Uno |
| --- | --- | --- |
| Scene publication/render counts | `RenderDiagnostics` | `RenderDiagnostics` |
| Engine counters | `CapturePerformanceSnapshot()` | `EngineMetrics` |
| Diagnostic JavaScript | `EvaluateTextAsync` | `EvaluateTextAsync` |
| Raw V8 Inspector | `OpenV8InspectorSession` | `OpenV8InspectorSession` |
| Wait for Inspector startup | `WaitForV8InspectorAvailableAsync` | `WaitForV8InspectorAvailableAsync` |
| Console messages | `DrainConsoleMessages()` | Not currently exposed by the Uno view |
| Native last error and feature report | `LastError`, `FeatureUseReport` | Not currently exposed by the Uno view |

Sample scene health without resetting counters:

```csharp
var diagnostics = WebContent.RenderDiagnostics;
Console.WriteLine(
    $"rendered={diagnostics.RenderedSceneCount}, " +
    $"published={diagnostics.PublishedSceneCount}");
```

On Avalonia, `CapturePerformanceSnapshot()` opts that context into detailed runtime-work
counters on first use. Capture a baseline, then use `Since(previous)` on a later
snapshot rather than resetting process state.

Drain console messages on a timer or diagnostic command instead of once at shutdown:

```csharp
foreach (var message in WebContent.DrainConsoleMessages())
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
