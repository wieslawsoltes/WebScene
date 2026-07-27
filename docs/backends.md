# Managed and native backends

WebScene supports two engine modes. They share portable value types, capability names,
backend manifests, component-profile contracts, and a managed/native conformance suite.
They do not yet share every host and renderer implementation.

## Choosing an engine

| | Managed engine | Native engine |
| --- | --- | --- |
| JavaScript | ClearScript/V8 | V8 linked into the native WebScene runtime |
| DOM, CSS, layout | Managed objects | Native engine thread |
| Presentation today | Avalonia controls and drawing | Immutable native scene projected by a host renderer |
| Application API calls | Direct managed APIs and ClearScript | Ordered command/evaluation calls on the engine queue |
| Main strength | Compatibility, diagnostics, and straightforward .NET extension | Hot DOM/Canvas calls stay inside V8/native code; UI thread receives immutable scene diffs |
| Main cost | Fine-grained JavaScript-to-managed calls can dominate component hot paths | A larger native distribution and a still-maturing compatibility surface |
| Recommended use | General WebScene applications and compatibility fallback | Opt-in workloads that have passed the native capability and performance gates |

Both modes remain first class. An application must select a mode explicitly; a native
test failure is not hidden by silently falling back to managed mode.

## Managed backend

The reference managed stack consists of:

- `WebScene.Core` for portable backend contracts and value types;
- `WebScene.Backend.Abstractions` for backend manifests and capability validation;
- `WebScene.Backend.Avalonia` for the current presentation implementation;
- `JavaScript.Avalonia.ClearScript` for ClearScript/V8 execution, module loading,
  virtual frames, and the persistent compilation-unit cache.

The managed DOM and presentation implementation is still coupled to Avalonia. In
particular, `WebScene.Backend.Avalonia` currently owns the implementation sources rather
than consuming a fully extracted, UI-neutral managed engine coordinator.

## Native backend

The native engine owns its V8 isolate, live DOM/CSS state, layout, Canvas/SVG state,
input dispatch, task queue, and scene construction on an engine thread. A host acquires
an opaque, reference-counted immutable scene-diff handle and traverses fixed-layout
tables without converting the scene into a per-frame managed object graph.

The native NuGet packages are RID-specific:

```xml
<PackageReference Include="WebScene.NativeEngine.Runtime.osx-arm64" Version="VERSION" />
<PackageReference Include="WebScene.NativeEngine.Runtime.linux-x64" Version="VERSION" />
<PackageReference Include="WebScene.NativeEngine.Runtime.win-x64" Version="VERSION" />
```

Each package contains the native module, colocated V8 ICU data, third-party notices,
and a manifest containing the RID, ABI, V8 revision, and SHA-256 hashes. The transitive
build targets reject a mismatched explicit `RuntimeIdentifier` and copy the native
module, ICU data, and manifest to build and publish output.

The first release workflow produces macOS ARM64, Linux x64, and Windows x64 packages.
The package definition and build scripts also model macOS x64, Linux ARM64, and Windows
ARM64, but those RIDs must not be advertised as released until their runner lanes and
release tests are enabled.

See [the native scene-engine design](architecture/native-v8-scene-engine.md) for
threading, input, scene lifetime, damage, and renderer details.

## Third-party backend status

The repository is ready for third parties to **start authoring** a backend against the
portable contracts, but it is not yet a turnkey backend SDK for Uno, WPF, or ProGPU.

Already portable and reusable:

- `IWebSceneBackendHost`, node handles, geometry, visibility, hit-test, invalidation,
  and capability contracts in `WebScene.Core`;
- backend manifest validation and capability negotiation;
- portable DOM, CSS/layout, graphics, JavaScript, and component-profile contracts;
- the managed/native conformance profile and deterministic test fixtures;
- the native C ABI's immutable scene and acknowledgement model.

Still required before claiming production-ready third-party implementations:

1. Extract the managed DOM/CSS/layout coordinator from the Avalonia implementation so
   it depends only on `IWebSceneBackendHost`.
2. Publish a managed native-engine host/ABI package with safe scene handles, typed
   read-only scene views, queueing, diagnostics, and lifetime validation. The current
   native adapter in the conformance runner is test-local.
3. Move the production Avalonia native scene projector into a reusable WebScene backend
   package instead of leaving renderer orchestration to an application.
4. Publish the backend conformance kit so an external package can run the same managed
   and native fixtures without copying test adapters.
5. Add a minimal recording/headless reference backend and backend-author template.
6. Stabilize versioning rules for the managed backend contract and native scene ABI.

Framework-specific work remains intentional. Uno and WPF adapters must implement their
own node projection, layout hand-off, text measurement, focus, pointer capture,
keyboard/IME, clipboard, accessibility, resource loading, and render invalidation.
A ProGPU adapter should consume the native immutable scene directly and implement
resource-generation caches, clipping, text, SVG/path, image, and damaged-layer drawing;
it should not recreate the DOM or CSS engine.

Until the extraction items above are complete, describe the architecture as a portable
backend-authoring foundation with Avalonia as the reference backend—not as drop-in Uno,
WPF, or ProGPU support.

## Validation

Run the portable contract and architecture gates with:

```bash
dotnet test tests/WebScene.Core.Tests/WebScene.Core.Tests.csproj -c Release
dotnet test tests/WebScene.Backend.Abstractions.Tests/WebScene.Backend.Abstractions.Tests.csproj -c Release
dotnet test tests/WebScene.Architecture.Tests/WebScene.Architecture.Tests.csproj -c Release
```

Run the native package build on a matching host with:

```bash
scripts/build-native-engine-runtime.sh --rid osx-arm64
scripts/build-native-engine-runtime.sh --rid linux-x64
```

```powershell
./scripts/build-native-engine-runtime.ps1 -Rid win-x64
```

The build downloads the pinned V8 revision unless `--v8-root`/`-V8Root` names an
existing compatible build, links the engine, creates the RID package, extracts it to a
clean directory, and runs a startup/responsive-layout smoke from that extraction. The
full managed/native profile is a separate capability-promotion gate; known native parity
gaps are not hidden by package publication.

## Publishing

The `NuGet packages` GitHub Actions workflow packs all twelve managed/template packages,
builds the native runtime for macOS ARM64, Linux x64, and Windows x64, verifies the
complete package graph, then builds and executes a clean combined consumer on every RID.
Manual runs can stop after verification. A tag named `vVERSION` publishes only when
`VERSION` exactly matches the repository `PackageVersion`.

Configure the protected `nuget.org` GitHub environment with either:

- `NUGET_USER` and a matching nuget.org trusted-publishing policy for repository
  `wieslawsoltes/WebScene`, workflow `native-runtime-packages.yml`, environment
  `nuget.org`; or
- the legacy `NUGET_API_KEY` secret.

Trusted publishing uses a short-lived GitHub OIDC credential. Publishing uses
`--skip-duplicate`, but every NuGet package version remains immutable after it reaches
NuGet.org.
