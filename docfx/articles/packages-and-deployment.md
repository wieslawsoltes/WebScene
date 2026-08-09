# Packages and deployment

A WebScene application combines a managed host with exactly one native runtime package.
Keep every WebScene package on the same version and make the target runtime identifier
explicit.

## Package roles

| Package | Use it for |
| --- | --- |
| `WebScene.Sdk` | Component manifest, package, compatibility, lifecycle, diagnostics, and host-bridge contracts |
| `WebScene.Sdk.Avalonia` | First-class Avalonia `WebSceneComponentHost`; brings in the SDK and reference presenter |
| `WebScene.Sdk.Uno` | First-class Uno Skia desktop `WebSceneComponentHost`; brings in the SDK and Uno presenter |
| `WebScene.Backend.Avalonia` | Advanced direct Avalonia presenter and native runtime wrapper |
| `WebScene.Backend.Uno` | Advanced direct Uno Skia desktop presenter and native runtime wrapper |
| `WebScene.NativeEngine.Runtime.<RID>` | V8, ICU data, bootstrap snapshot, ABI metadata, and native licenses for one RID |
| `WebScene.JavaScript.Interop` | Runtime-neutral typed interop contracts |
| `WebScene.JavaScript.Interop.Generator` | Build-time C# generation from reviewed TypeScript APIs |
| `WebScene.Diagnostics.Cdp` | Optional Chrome discovery and WebSocket host for V8 Inspector |

The portable `WebScene.Core`, `WebScene.Dom`, `WebScene.Css`, and
`WebScene.Graphics` packages support backend and testing scenarios. A normal
application obtains these and its framework presenter transitively from
`WebScene.Sdk.Avalonia` or `WebScene.Sdk.Uno`.

`WebScene` is a separate HTML-inspired Avalonia authoring layer. It does not host the
native V8 engine and should not be substituted for `WebScene.Backend.Avalonia` in these
guides.

## Choose one supported RID

| Target | Project RID | Runtime package |
| --- | --- | --- |
| macOS, Apple silicon | `osx-arm64` | `WebScene.NativeEngine.Runtime.osx-arm64` |
| Linux, x64 | `linux-x64` | `WebScene.NativeEngine.Runtime.linux-x64` |
| Windows, x64 | `win-x64` | `WebScene.NativeEngine.Runtime.win-x64` |

For example, a component-hosted Avalonia application targeting Windows x64 uses the
following. An Uno application substitutes `WebScene.Sdk.Uno` while retaining the same
RID package. Replace `VERSION` with a version that contains the native component host
and use it for both references:

```xml
<PropertyGroup>
  <TargetFramework>net10.0</TargetFramework>
  <RuntimeIdentifier>win-x64</RuntimeIdentifier>
  <SelfContained>true</SelfContained>
  <BuiltInComInteropSupport>true</BuiltInComInteropSupport>
</PropertyGroup>

<ItemGroup>
  <PackageReference Include="WebScene.Sdk.Avalonia" Version="VERSION" />
  <PackageReference Include="WebScene.NativeEngine.Runtime.win-x64" Version="VERSION" />
</ItemGroup>
```

Do not reference multiple native runtime packages in one application project. The
runtime package build targets reject a mismatch between its packaged RID and the
project's explicit `RuntimeIdentifier`.

## Files produced by the runtime package

Build and publish output contains the platform library plus its runtime data:

| File | Purpose |
| --- | --- |
| `webscene_native_engine.dll`, `libwebscene_native_engine.so`, or `libwebscene_native_engine.dylib` | Native engine |
| `icudtl.dat` | V8 internationalization data |
| `webscene_bootstrap_snapshot.bin` | V8 bootstrap snapshot |
| `webscene_bootstrap_snapshot.meta` | Snapshot metadata |
| `webscene-native-runtime.json` | RID, ABI, parser, V8, and content metadata |

The package marks these files for build and publish output and excludes them from
single-file bundling. Even when the managed application uses single-file publishing,
the native runtime and data files must remain beside the executable.

`WebSceneComponentHost` resolves the library from `AppContext.BaseDirectory`
automatically. Set `NativeLibraryPath` or
`WEBSCENE_NATIVE_ENGINE_LIBRARY` only when a development build stores it elsewhere.

Direct view integrations resolve the path themselves and pass it to `LoadAsync`.
`NativeWebSceneRuntime.InspectLibrary(path)` can validate the file and ABI before
creating a view; `PrewarmAsync(path)` additionally initializes the process-wide V8
platform.

```csharp
var runtime = NativeWebSceneRuntime.InspectLibrary(nativeLibraryPath);
Console.WriteLine($"WebScene ABI {runtime.AbiVersion}: {runtime.LibraryPath}");
await NativeWebSceneRuntime.PrewarmAsync(nativeLibraryPath, cancellationToken);
```

The managed presenter currently requires native ABI 3. A missing version export,
wrong architecture, or different ABI produces a descriptive exception before document
navigation.

## Publish and verify

Publish for one explicit RID:

```bash
dotnet publish src/MyApp/MyApp.csproj \
  -c Release \
  -r osx-arm64 \
  --self-contained true
```

Before packaging the application, verify that the publish directory contains the five
runtime files listed above. Then run the published executable from a clean directory;
do not rely on a native library from the repository build tree or an environment
variable left over from development.

For framework-dependent publishing, retain the explicit RID and matching runtime
package. The target machine must also have the selected .NET runtime installed.

## Compilation cache

`WebSceneComponentHost.CompilationCacheDirectory` and direct `LoadAsync` calls can
select a compilation-cache directory. Use an application-specific, writable directory
rather than the installation folder:

```csharp
var cacheDirectory = Path.Combine(
    Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
    "MyCompany",
    "MyApp",
    "WebScene",
    "V8Cache");
```

WebScene creates the directory when necessary. Give different products or incompatible
content bundles separate cache directories. Cache contents are an optimization, not
application state; the application must remain correct if the directory is absent or
cleared.

## Development from source

When consuming `main` before component-host packages are published, use a project
reference to `src/WebScene.Sdk.Avalonia/WebScene.Sdk.Avalonia.csproj` or
`src/WebScene.Sdk.Uno/WebScene.Sdk.Uno.csproj`. Do not install an older package with a
different component-host implementation.

When consuming a locally built native engine instead of a runtime package, build the
matching RID and set the host's `NativeLibraryPath` to the absolute output path:

```bash
scripts/build-native-engine-runtime.sh --rid osx-arm64
```

```powershell
./scripts/build-native-engine-runtime.ps1 -Rid win-x64
```

The `WEBSCENE_NATIVE_ENGINE_LIBRARY` convention is used by repository samples and is
also understood by `WebSceneComponentHost`. Prefer an explicit command-line or
application configuration value during development so logs show which binary was
loaded.

Continue with [Troubleshooting](troubleshooting.md) for native load, RID, and ABI
failures.
