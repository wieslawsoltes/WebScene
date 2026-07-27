# JavaScript.Avalonia.ClearScript

ClearScript/V8 runtime for `JavaScript.Avalonia`. Applications reference and construct
this runtime explicitly; the reusable DOM/browser-services package does not impose an
engine. The repository's JavaScript Playground uses it for all script execution.

The runtime provides separate owner and virtual-iframe V8 contexts, a generic DOM
callback adapter, and a typed-array Canvas2D command buffer that replays through the
existing `CanvasRenderingContext2D` implementation.

```csharp
using JavaScript.Avalonia;
using JavaScript.Avalonia.ClearScript;

using var host = new AvaloniaBrowserHost(window);
using var runtime = new ClearScriptV8Runtime(
    host,
    new ClearScriptV8RuntimeOptions
    {
        EnableTrustedSameOriginContextSharing = true
    });

runtime.Execute("require('./app.js');");
```

## Multiple independent charts

`ClearScriptV8RuntimeOptions.SharedCache` defaults to the process-wide
`ClearScriptV8SharedCache.ProcessWide` instance. Multiple runtimes therefore retain
separate V8 globals, module exports, DOM documents, event callbacks, and disposal
lifetimes while reusing immutable external-script source text and V8 code-cache bytes.
This is the recommended shape for multiple complex JavaScript components: one host/runtime per
chart and one shared cache for the application.

```csharp
var sharedCache = new ClearScriptV8SharedCache();

foreach (var chartRoot in chartRoots)
{
    var host = CreateHostFor(chartRoot);
    var runtime = new ClearScriptV8Runtime(
        host,
        new ClearScriptV8RuntimeOptions
        {
            EnableTrustedSameOriginContextSharing = true,
            SharedCache = sharedCache
        });
    chartSessions.Add((host, runtime));
}
```

Do not share a chart's mutable exports, window, document, or vendor widget object with
another chart. Sharing one V8 isolate would couple heap pressure, security tokens, and
failure/disposal behavior. The bounded source/code cache captures the compilation win
without that coupling. Set `SharedCache = null` only for diagnostic A/B comparison;
`GetMetrics()` exposes hit, miss, accepted/verified/updated, entry, and byte counts.

## Background precompilation and persistence

Applications with several charts should prepare immutable compilation units in
parallel, then create and execute each chart runtime on Avalonia's UI thread. The
runtime provides a single-flight gate per content key: concurrent requests for the same
source elect one compiler and the other callers reuse its result. It never shares
globals, CommonJS exports, DOM nodes, or callbacks.

```csharp
var cache = new ClearScriptV8SharedCache(new ClearScriptV8SharedCacheOptions
{
    PersistentDirectory = Path.Combine(appCacheDirectory, "v8"),
    CompatibilityTag = reviewedNativeBuildIdentity,
    MaxPersistentEntries = 4096,
    MaxPersistentBytes = 768L * 1024 * 1024
});

var sources = new[]
{
    new V8CompilationSource("app.js", appSource),
    ClearScriptV8Runtime.CreateCommonJsCompilationSource(modulePath, moduleSource)
};
await ClearScriptV8Runtime.PrecompileAsync(cache, sources, cancellationToken: token);
```

`PrecompileAsync` performs V8 cache generation on a thread-pool thread. Source loading
can also be partitioned across workers, as the Playground does. Actual script execution
that can call the Avalonia DOM remains UI-thread-affine. Arbitrary synchronous
JavaScript cannot be paused and resumed safely, so this API does not claim to move DOM
evaluation off-thread.

Persistent entries contain only V8 code-cache bytes. Their keys use the exact document
name and SHA-256 source content; the compatibility directory also covers the cache
schema, ClearScript managed assembly identity, RID, process architecture, and optional
application/native-build tag. Writes use a unique temporary file followed by atomic
replacement. Headers and payload hashes are validated, corrupt entries are deleted and
recompiled, and V8 remains the final validator through its accepted/verified/updated
result. Entry and byte limits bound both memory and disk storage.

`Clear()` drops memory and metrics while preserving disk data. `ClearPersistent()` is
the explicit destructive operation. The process-wide cache remains memory-only unless
`WEBSCENE_V8_CACHE_DIRECTORY` is set; applications can instead construct and own a cache
as above. `GetMetrics()` includes disk I/O and single-flight leader/waiter counters.

Single-flight is deliberately process-local. Separate application processes can both
compile the same brand-new key, but unique temporary files and atomic replacement keep
their concurrent writes valid; a later process consumes the resulting entry normally.
This avoids an operating-system lock on normal runtime compilation. Multiple chart
instances inside one application—the primary use case—share the exact single-flight
gate and never generate the same content key twice.

## Experimental native requirement

Virtual iframes exchange direct JavaScript objects only when ClearScript gives the
contexts in this dedicated runtime a shared security token. Stock ClearScript 7.5.1
rejects that access. The exact proof patch is documented under
`third-party/clearscript-patches/`.

`EnableTrustedSameOriginContextSharing` is deliberately disabled by default. Enable
it only for contexts that the application treats as one trusted same-origin group.
The runtime does not attach an external iframe factory without this explicit option.

Before production use, ship reviewed native builds for each supported RID, retain
default isolation for unrelated runtimes, and validate native memory and disposal on
every target platform.

This managed package intentionally depends on `Microsoft.ClearScript.V8` rather than
`Microsoft.ClearScript.Complete`. The complete package supplies Microsoft's stock
native binaries for every RID and could bypass WebScene's reviewed context-sharing patch.
Reference the RID-specific `JavaScript.Avalonia.ClearScript.Native.<rid>` package for a
reviewed deployment, or provide the validated native path/RID explicitly during local
development. The native package includes the exact patch, source/V8 provenance, and
SHA-256 metadata; see `third-party/clearscript-patches/README.md`.

## Playground default

The sample compiles this package and uses V8 without an engine-selection flag. See
`samples/JavaScriptPlayground/README.md` for native-path, build, and run commands.
