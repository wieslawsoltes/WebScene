# Troubleshooting

Start with the first exception produced by `LoadAsync`, then collect the view's native
error, console, scene, and feature diagnostics before retrying or disposing it.

## Native engine was not found

Typical exception:

```text
The WebScene native engine was not found.
```

Check that:

1. The application declares an explicit supported `RuntimeIdentifier`.
2. It references the matching `WebScene.NativeEngine.Runtime.<RID>` package.
3. The platform library is present in `AppContext.BaseDirectory` after build or publish.
4. The absolute path passed to `LoadAsync` names that file rather than a source-tree
   artifact from another RID.

See [Packages and deployment](packages-and-deployment.md) for the required output files.

## Wrong operating system, architecture, or ABI

Typical exceptions mention `BadImageFormatException`, a missing ABI export, or an ABI
other than 3.

Call `NativeWebSceneRuntime.InspectLibrary(path)` at startup to isolate the failure from
document loading. Verify the process architecture, runtime package suffix, and managed
package versions. All WebScene packages and the native runtime should have the same
version.

Do not work around an ABI mismatch by suppressing validation. The managed structures
and native exports must agree.

## V8 prewarm failed

```text
The WebScene native engine could not prewarm its V8 process runtime.
```

Verify that `icudtl.dat`, the bootstrap snapshot, snapshot metadata, and runtime
manifest are beside the native library and readable. Run the published application
from a clean directory to detect undeclared dependencies.

## Document load was rejected

```text
Native WebScene rejected <URL>: <native error>
```

Confirm that the document URL is absolute, exists, and uses a scheme supported by the
selected host. For local paths, create the URI with `new Uri(fullPath).AbsoluteUri`.
For HTTP(S), check status codes and redirects at the origin.

On Avalonia, inspect `LastError`, `SceneDiagnostics`, and `FeatureUseReport`. Drain
console messages before unloading the failed view when possible.

## A document-start script failed

An exception in any document-start script fails the initial load. The native diagnostic
includes the script's configured `Name`.

Run the script as static application source, reduce it to the smallest failing
statement, and confirm that it uses only APIs present at document start. Do not catch
and hide the exception in the host; startup should fail closed.

## First scene or document barrier timed out

Avalonia waits for the first document scene; Uno waits for a constructed document
barrier. A timeout can indicate:

- an authored script blocking the engine worker;
- a missing or slow required resource;
- a document that never creates renderable content;
- an Inspector break waiting for a debugger; or
- a canceled/unloaded host view.

When using `WaitForDebugger`, pass an infinite barrier timeout intentionally and attach
through Chrome DevTools. Otherwise, retain a finite timeout and diagnose the content;
do not simply increase it until the symptom disappears.

## Blank or zero-sized output

Ensure the host view is attached, visible, and arranged with non-zero width and height.

- Avalonia: place `NativeWebSceneView` in a stretching panel and load from `Opened` or
  view activation.
- Uno: use stretching horizontal and vertical content alignment, wait for `Loaded`, and
  ensure the containing `FrameworkElement` has non-zero `ActualWidth` and
  `ActualHeight`.

Check `RenderDiagnostics.PublishedSceneCount` and `RenderedSceneCount`. Publications
without renders point toward the presenter/visual lifecycle; neither counter advancing
points toward loading or engine work.

## Relative resource cannot be resolved

The base document must be an absolute URI. Preserve the bundle directory structure in
publish output and match filename casing on Linux.

Remember that `avares:` is Avalonia-only. Uno supports `file:`, `data:`, and HTTP(S)
through its native resource loader. Packaged components use the isolated loader owned
by `WebScene.Sdk.Uno`. See [Content and resource loading](content-and-resources.md).

## Generated interop is unavailable

`CreateJavaScriptInvoker()` throws until a native document is loaded. Create it only
after `LoadAsync` completes and dispose it before navigation.

If generation fails, check both MSBuild properties, file paths, JSON schemas, and the
API fingerprint. A changed `.d.ts` file requires discovery plus an explicit policy
review. Unsupported declaration shapes must be handled in the policy or generator;
they cannot fall back to the runtime-neutral JSON invoker on the native transport.

## A proxy fails after navigation

Generated proxies and `JavaScriptObjectReference` values belong to one V8 isolate. A
second `LoadAsync` destroys that isolate. Recreate the invoker and every proxy after
each successful navigation.

Cancel application operations, release callbacks and proxies, dispose the invoker, and
only then unload or navigate the view.

## Inspector is not discoverable

Check that:

1. `WebScene.Diagnostics.Cdp` is referenced and the host was started.
2. The view uses dedicated-isolate mode; shared-isolate mode intentionally has no
   Inspector support.
3. Chrome is configured for the host's reported address and bound port.
4. Loopback or firewall rules permit the connection.
5. A remote connection supplies the configured bearer token.

Use port `0` to request an available loopback port and log `DiscoveryUri` after startup.

## Capture a useful report

Include:

- WebScene managed and native runtime versions;
- operating system, architecture, RID, and .NET version;
- Avalonia or Uno version and renderer;
- absolute document scheme without credentials;
- the first exception and inner exception;
- `LastError`, console messages, feature report, and scene diagnostics when available;
- scene publication/render counts and a performance snapshot; and
- a minimal trusted content bundle or unchanged repository fixture that reproduces the
  issue.

Do not attach licensed third-party declarations, credentials, access tokens, or
proprietary content to a public issue.
