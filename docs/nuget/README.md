# WebScene packages

WebScene provides portable HTML, DOM, CSS, graphics, and JavaScript contracts together
with an Avalonia presentation backend and an opt-in native V8/DOM/CSS/scene engine.

Start with the package matching the product surface:

- `WebScene` for HTML-like Avalonia markup;
- `WebScene.Backend.Avalonia` for the complete Avalonia backend;
- `WebScene.Sdk` and `WebScene.Sdk.Avalonia` for packaged React/TypeScript components;
- `WebScene.NativeEngine.Runtime.<rid>` for the native engine on a published RID.

The complete release inventory is:

- managed libraries: `WebScene`, `WebScene.Core`, `WebScene.Dom`, `WebScene.Css`,
  `WebScene.Graphics`, `WebScene.JavaScript`, `WebScene.Backend.Abstractions`,
  `WebScene.Backend.Avalonia`, `JavaScript.Avalonia.ClearScript`, `WebScene.Sdk`,
  and `WebScene.Sdk.Avalonia`;
- project templates: `WebScene.Templates`;
- native runtimes: `WebScene.NativeEngine.Runtime.osx-arm64`.

The package line is prerelease. Managed packages in one release must use the same
version. Native applications must select the runtime package matching their explicit
`RuntimeIdentifier`.

`win-x64` and `linux-x64` publishing are temporarily deferred while their pinned V8
builds move to faster, independently validated lanes. Both remain source-build
targets but are not part of the current NuGet release inventory.

The release workflow caches a minimal pinned V8 SDK independently for each RID. The
cache contains only the V8 headers, monolithic library, ICU data, and licenses needed
to link WebScene's native bridge. Its key includes the hosted-runner image, ClearScript
revision, V8 build scripts, and WebScene compatibility patches. Consequently, ordinary
WebScene changes rebuild and relink only the native bridge; V8 is rebuilt whenever any
input capable of changing its ABI or binary output changes. Completed SDK inputs are
saved independently of later bridge, package, or smoke-test failures, so a successful
multi-hour V8 build is not repeated merely because a downstream step fails.

Documentation, compatibility policy, source, and issue tracking are available from
the [WebScene repository](https://github.com/wieslawsoltes/WebScene).
