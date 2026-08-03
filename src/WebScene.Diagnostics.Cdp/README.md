# WebScene.Diagnostics.Cdp

This optional package forwards WebScene's native V8 Inspector Protocol session
unchanged through a Chrome-compatible discovery and WebSocket endpoint.

```csharp
var options = new WebSceneV8InspectorOptions
{
    Enabled = true,
    Address = IPAddress.Loopback,
    Port = 9229,
    WaitForDebugger = true
};

WebSceneV8InspectorHost? inspector = null;
await view.LoadAsync(
    documentUri,
    nativeLibraryPath,
    cacheDirectory,
    async (readyView, cancellationToken) =>
    {
        inspector = new WebSceneV8InspectorHost(readyView, options);
        await inspector.StartAsync(cancellationToken);
    },
    Timeout.InfiniteTimeSpan);
```

The before-navigation hook is required only for `WaitForDebugger`: it opens a
V8 session before the document request is queued. V8 then waits until the first
client sends `Runtime.runIfWaitingForDebugger`. The wait occurs on WebScene's
engine worker, so the Avalonia dispatcher, discovery endpoint, and window remain
responsive. Dispose `inspector` during application shutdown.

Open `chrome://inspect`, add `localhost:9229` under **Discover network
targets**, and select **inspect** for the WebScene target. The generated access
token is available as `inspector.AccessToken` when a direct WebSocket client is
used. Set `Port = 0` to request an ephemeral loopback port, then log or read
`inspector.DiscoveryUri`/`inspector.BoundPort` after `StartAsync`.

Inspector hosting is disabled unless `Enabled = true`. Loopback is the default;
non-loopback bindings require `AllowRemoteConnections = true`. WebSocket
clients must present the generated token and Chrome DevTools origins are the
only non-empty origins accepted.

Inspector sessions require WebScene's normal dedicated-isolate mode. The
opt-in `WEBSCENE_V8_SHARED_ISOLATE` lane intentionally reports the inspector as
unavailable because independent engine workers cannot safely drive the same
isolate inspector.
