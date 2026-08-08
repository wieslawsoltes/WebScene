# WebScene.Diagnostics.Cdp

This optional package combines WebScene's renderer-owned Elements domains with
its native V8 Inspector session behind one Chrome-compatible discovery and
WebSocket endpoint. Runtime, Debugger, Profiler, and other V8 messages are
forwarded byte-for-byte; only DOM, CSS, and Overlay commands are handled by
WebScene's native DOM diagnostics.

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
        inspector = new WebSceneV8InspectorHost(
            readyView.OpenV8InspectorSession,
            () => readyView.Source,
            options,
            domInspector: readyView);
        await inspector.StartAsync(cancellationToken);
    },
    Timeout.InfiniteTimeSpan);
```

The before-navigation hook is required only for `WaitForDebugger`: it opens a
V8 session before the document request is queued. V8 then waits until the first
client sends `Runtime.runIfWaitingForDebugger`. The wait occurs on WebScene's
engine worker, so the UI dispatcher, discovery endpoint, and window remain
responsive. The raw session interface lives in `WebScene.Backend.Abstractions`,
so the same host works with both Avalonia and Uno native views. Dispose
`inspector` during application shutdown.

Passing `domInspector: view` enables the Elements tree, computed style, box
model, node highlighting, and the hover/click element picker. Snapshots are
produced on the engine worker from the authored DOM; the host never walks live
V8 objects or an Avalonia/Uno visual tree. Native ids remain stable until the
next navigation, and DOM mutations publish `DOM.documentUpdated` so clients
refresh React-rendered content.

Open `chrome://inspect`, add `localhost:9229` under **Discover network
targets**, and select **inspect** for the WebScene target. The generated access
token is available as `inspector.AccessToken` when a direct WebSocket client is
used. Set `Port = 0` to request an ephemeral loopback port, then log or read
`inspector.DiscoveryUri`/`inspector.BoundPort` after `StartAsync`.

Inspector hosting is disabled unless `Enabled = true`. Loopback is the default;
non-loopback bindings require `AllowRemoteConnections = true`. Remote discovery
requests and WebSocket clients must present the generated token as a `token`
query parameter or `Authorization: Bearer` header; unauthenticated remote
discovery never publishes the bearer secret. Chrome DevTools origins are the
only non-empty origins accepted.

Inspector sessions require WebScene's normal dedicated-isolate mode. The
opt-in `WEBSCENE_V8_SHARED_ISOLATE` lane intentionally reports the inspector as
unavailable because independent engine workers cannot safely drive the same
isolate inspector.
