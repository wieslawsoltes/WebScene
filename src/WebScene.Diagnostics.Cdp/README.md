# WebScene.Diagnostics.Cdp

This optional package forwards WebScene's native V8 Inspector Protocol session
unchanged through the Chrome-compatible discovery and WebSocket host from
`Chrome.DevTools.Protocol`.

```csharp
var options = new WebSceneV8InspectorOptions
{
    Enabled = true,
    Address = IPAddress.Loopback,
    Port = 9229
};

await view.LoadAsync(documentUri, nativeLibraryPath, cacheDirectory);
await using var inspector = new WebSceneV8InspectorHost(view, options);
await inspector.StartAsync();
```

Open `chrome://inspect`, add `localhost:9229` under **Discover network
targets**, and select **inspect** for the WebScene target. The generated access
token is available as `inspector.Server.AccessToken` when a direct WebSocket
client is used.

Inspector sessions require WebScene's normal dedicated-isolate mode. The
opt-in `WEBSCENE_V8_SHARED_ISOLATE` lane intentionally reports the inspector as
unavailable because independent engine workers cannot safely drive the same
isolate inspector.
