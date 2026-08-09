# Content and resource loading

WebScene loads an absolute document URL and resolves the document's scripts, styles,
fonts, images, and other resources relative to that URL. Choose a content strategy that
is deterministic, testable, and appropriate for trusted application-owned content.

## Recommended packaged-content layout

Keep the web bundle together and copy it without flattening its directory structure:

```text
MyApp/
  web/
    index.html
    app.js
    styles.css
    assets/
      logo.svg
      app.woff2
```

```xml
<ItemGroup>
  <Content Include="web/**" CopyToOutputDirectory="PreserveNewest" />
</ItemGroup>
```

Load `index.html` through an absolute file URI:

```csharp
var documentPath = Path.Combine(
    AppContext.BaseDirectory,
    "web",
    "index.html");
var documentUri = new Uri(documentPath).AbsoluteUri;

await webSceneView.LoadAsync(
    documentUri,
    nativeLibraryPath,
    compilationCacheDirectory,
    cancellationToken);
```

Relative references such as `./app.js` and `./assets/logo.svg` then resolve against the
document directory. Avoid building `file:` URLs by string concatenation; `Uri` handles
platform separators and escaping.

## Resource schemes by host

The current presenters do not expose identical resource loaders:

| Scheme | Avalonia | Uno Skia | Notes |
| --- | --- | --- | --- |
| `file:` | Yes | Yes | Best-supported packaged-content path |
| `http:` / `https:` | Yes | Yes | Uses an internal `HttpClient`; HTTP cache validators are honored |
| `data:` | Yes | Yes | Text and inline data; use only for bounded content |
| `avares:` | Yes | No | Loads Avalonia application resources through `AssetLoader` |

Use `file:` when the same bundle must run unchanged in Avalonia and Uno. Avalonia-only
applications can load an `avares:` resource, but external files are easier to inspect,
update during development, and share with native/headless fixtures.

## HTTP and HTTPS content

Both loaders follow relative URLs from an absolute HTTP(S) document and retain common
cache metadata such as `ETag`, `Last-Modified`, `Cache-Control`, and `Expires`. An HTTP
error fails the resource request rather than silently substituting empty content.

Remote loading does not turn WebScene into a safe general-purpose browser. Only load
origins and content controlled by the application. The built-in views do not expose a
public per-request allow/deny callback, certificate policy, cookie profile, or browser
permission model.

For reproducible UI and offline behavior, prefer a versioned local bundle. If remote
content is necessary, pin the endpoint, define an application-level update policy, and
test failure and stale-cache behavior.

## Avalonia resources

The Avalonia loader understands `avares:` URLs:

```csharp
await webSceneView.LoadAsync(
    "avares://MyApp/Assets/Web/index.html",
    nativeLibraryPath,
    compilationCacheDirectory,
    cancellationToken);
```

Mark the files as `AvaloniaResource` in the application project. Every relative
resource must be reachable through the same asset URI structure. This path is specific
to Avalonia and should not be used in a framework-neutral document configuration.

`AvaloniaResourceLoader` also exposes search-directory and mounted-directory helpers
for advanced backend integrations. `NativeWebSceneView` creates and owns its default
loader internally, so its standard public loading surface does not currently offer a
hook to configure those helpers.

## Document-start scripts

Use `NativeWebSceneLoadOptions.DocumentStartScripts` for small compatibility or host
bootstrap scripts that must run before authored JavaScript:

```csharp
var options = new NativeWebSceneLoadOptions
{
    Source = documentUri,
    NativeLibraryPath = nativeLibraryPath,
    CompilationCacheDirectory = compilationCacheDirectory,
    DocumentStartScripts =
    [
        new WebSceneDocumentScript(
            "globalThis.appHost = Object.freeze({ version: '1.0' });",
            "app-host.js",
            AllFrames: false)
    ]
};

await webSceneView.LoadAsync(options, cancellationToken);
```

Scripts execute in list order. A script exception fails the initial load and records
its configured name in native diagnostics. `AllFrames: true` also injects the script
before authored code in subsequently created frames.

Keep these scripts static and application-owned. Do not interpolate unescaped user or
network data into JavaScript source. Use generated typed interop for runtime values and
commands.

## Storage behavior

The current native runtime supplies synchronous in-memory `localStorage` and
`sessionStorage` for the engine/page lifetime. It implements the common item methods
and stable insertion order, but does not promise persistence, quotas, storage events,
origin/reload semantics, shared profiles, or IndexedDB.

Do not store durable application state in the WebScene storage subset. Persist through
an explicit application service and expose only the narrow operations the trusted
document needs.

## Content update checklist

When replacing a packaged web bundle:

1. Review the changed HTML, JavaScript, CSS, fonts, and binary assets as application
   code.
2. Run the required WebScene compatibility profile plus a product-specific fixture.
3. Regenerate and review typed interop manifests if TypeScript declarations changed.
4. Verify the bundle from publish output, not only from the source directory.
5. Exercise missing-resource, offline, and slow-resource behavior.

See [Compatibility and security](compatibility-and-security.md) for the support and
trust boundaries that apply to every content strategy.
