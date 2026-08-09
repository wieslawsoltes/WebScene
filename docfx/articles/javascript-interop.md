# .NET and JavaScript interop

Avalonia's `WebSceneComponentHost` provides two complementary interop paths:

- a capability-gated, JSON-only bridge for component-to-application service requests;
- the underlying native ABI 3 invoker for generated, typed .NET-to-JavaScript APIs.

`NativeWebSceneView` and Uno's `UnoNativeWebSceneView` expose the same native
invoker. The host view owns the V8 isolate; object references are isolate-local
handles; and generated calls use tagged request and result codecs rather than building
JavaScript source or serializing every call through JSON.

## Choose the right path

| Need | API |
| --- | --- |
| Let a packaged component request an application service | `webscene.host.*.invoke` plus `RegisterHostCapability` |
| Inspect a small JSON-compatible value while debugging | `EvaluateTextAsync` |
| Read a leased tagged result with a custom decoder | `EvaluateAsync` |
| Call a JavaScript application or library API repeatedly from .NET | Generated bindings from `WebScene.JavaScript.Interop.Generator` |
| Receive JavaScript callbacks in .NET | Generated binary callback adapters |

## Handle component service requests

Component Profile 1 installs `webscene.host` before the component entry point runs.
The component can request only a `host.*` capability declared in its manifest and
registered by the application before mount.

```javascript
const settings = await webscene.host.settings.invoke("read", {
  keys: ["theme", "density"]
});
```

```csharp
using System.Text.Json;
using WebScene.Sdk;

ComponentHost.RegisterHostCapability(
    new WebSceneDelegateCapabilityHandler(
        WebSceneComponentCapabilities.Settings,
        async (method, arguments, cancellationToken) =>
        {
            if (method != "read")
            {
                throw new InvalidOperationException($"Unknown settings method '{method}'.");
            }

            return JsonSerializer.SerializeToElement(new
            {
                theme = "dark",
                density = "comfortable"
            });
        }));
```

Use `AutoMount="False"` when the handler cannot be registered before visual-tree
attachment, then call `MountAsync` explicitly after the host is attached. The bridge
is asynchronous, supports cancellation, validates declared capability grants, and
reports failures through the component host's diagnostics. Its JSON shape is suitable
for coarse application services, not high-frequency object interop.

## Evaluate small diagnostic expressions

Call evaluation only after `WebSceneComponentHost.State` is `Mounted` or a direct
view's `LoadAsync` has completed:

```csharp
string json = await ComponentHost.View.EvaluateTextAsync(
    "({ title: document.title, itemCount: document.querySelectorAll('li').length })",
    "host-diagnostics.js",
    cancellationToken);
```

`EvaluateTextAsync` materializes the native tagged result as JSON-compatible text. It is
useful for diagnostics, probes, and occasional host checks. It is not the preferred hot
path for an application API.

## Generate typed bindings

The generator consumes two reviewed files:

1. A deterministic API manifest discovered from one or more TypeScript declaration
   files.
2. An application-owned policy that selects proxies, models, constructors, functions,
   properties, adapters, and .NET names.

Reference the runtime-neutral interop package and the generator in a class library:

```xml
<ItemGroup>
  <PackageReference Include="WebScene.JavaScript.Interop" Version="1.0.20" />
  <PackageReference Include="WebScene.JavaScript.Interop.Generator"
                    Version="1.0.20"
                    PrivateAssets="all" />
</ItemGroup>

<PropertyGroup>
  <WebSceneInteropApiManifest>Interop/App.webscene-interop-api.json</WebSceneInteropApiManifest>
  <WebSceneInteropPolicy>Interop/App.webscene-interop-policy.json</WebSceneInteropPolicy>
</PropertyGroup>
```

Both properties are required. The build fails on a missing file, invalid schema, stale
API fingerprint, or selected declaration shape for which the native transport cannot
generate a safe codec. Unsupported shapes do not silently become `dynamic`.

The repository contains the discovery tool and full schema workflow. For example:

```bash
webscene-interop-discover \
  --declarations Interop/App.d.ts \
  --output Interop/App.webscene-interop-api.json \
  --report-output Interop/App.coverage.json \
  --policy-output Interop/App.webscene-interop-policy.json \
  --namespace MyApplication.Interop \
  --fail-on-fallbacks
```

Review the generated policy into source control. When a declaration file changes, the
API fingerprint forces an explicit policy review.

## Use a generated facade from any native view

Create the invoker after the document has published the JavaScript object or function
that the generated facade expects:

```csharp
using WebScene.JavaScript.Interop;

using NativeJavaScriptInvoker invoker =
    ComponentHost.View.CreateJavaScriptInvoker();

await using var editor = await AppEditor.CreateAsync(
    invoker,
    new AppEditorOptions { Theme = "dark" },
    cancellationToken);

await editor.SetValueAsync("Hello from .NET", cancellationToken);
string value = await editor.GetValueAsync(cancellationToken);
```

`AppEditor` and `AppEditorOptions` in this example represent generated types selected
by the application's policy. The generated class library is independent of the
presenter; use `ComponentHost.View`, a direct Avalonia `NativeWebSceneView`, or an Uno
`UnoNativeWebSceneView` to create the invoker.

Dispose generated proxy objects and the invoker before unloading or disposing the host
view. A retained object handle belongs to one loaded view and must never be reused after
navigation.

## Document-start bridge

If authored JavaScript must observe a host-defined global from its first statement,
set `WebSceneComponentHost.DocumentStartScripts` before mounting:

```csharp
ComponentHost.DocumentStartScripts =
[
    new WebSceneDocumentScript(
        "globalThis.hostEnvironment = Object.freeze({ channel: 'stable' });",
        "host-environment.js",
        AllFrames: false)
];
```

For direct view hosting, put the same scripts in
`NativeWebSceneLoadOptions.DocumentStartScripts`:

```csharp
var options = new NativeWebSceneLoadOptions
{
    Source = documentUri,
    NativeLibraryPath = nativeLibraryPath,
    DocumentStartScripts =
    [
        new WebSceneDocumentScript(
            "globalThis.webSceneHost = Object.freeze({ version: '1' });",
            "webscene-host.js",
            AllFrames: false)
    ]
};

await webSceneView.LoadAsync(options, cancellationToken);
```

Document-start scripts are ordered and fail closed: an exception prevents the initial
load from being reported as successful. Treat their source as application code and keep
the bridge deliberately small.

## Callbacks from JavaScript

Generated adapters can expose binary-compatible .NET callback targets as JavaScript
objects or functions. The view provides a callback notification signal, and the native
invoker dispatches tagged callback arguments without polling. Prefer generated adapters
because manually registered callbacks cannot provide the required native binary codecs.

Dispose callback registrations and generated function references before the invoker.
Avoid blocking the UI thread while awaiting a callback that itself needs UI work.

## Reference implementation

The
[NativeRuntimeShowcase.Interop sample](https://github.com/wieslawsoltes/WebScene/tree/main/samples/NativeRuntimeShowcase.Interop)
generates Monaco proxies once and uses them from both the Avalonia and Uno showcases.
For the complete discovery, policy, type mapping, callback, and performance model, see
the repository's
[source-generation design and status](https://github.com/wieslawsoltes/WebScene/blob/main/docs/native-javascript-interop-source-generation.md).
