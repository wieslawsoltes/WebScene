# Introduction

WebScene is a native web-UI runtime for trusted application content. Its native engine
owns V8, HTML parsing, the live DOM, CSS, layout, events, timers, Canvas, and SVG. A
component host validates and mounts packaged content; its presenter receives immutable
scene updates and integrates them with the application's window, input, frame clock,
and lifecycle.

```text
trusted component package
 manifest + declared assets
             |
             v
 Avalonia WebSceneComponentHost
 validation + lifecycle + bridge
             |
             v
    WebScene native engine
   V8 + DOM + CSS + layout
             |
             v
      immutable scenes
             |
             v
   Avalonia reference presenter
```

## Supported hosts

`WebScene.Sdk.Avalonia.WebSceneComponentHost` is the recommended application surface.
It composes the portable Component Profile 1 SDK with the
`WebScene.Backend.Avalonia` reference presenter. Most applications should use the
component host; use `NativeWebSceneView` directly only for advanced document or
backend-level integration.

`WebScene.Backend.Uno` proves that the presenter boundary can support another XAML
framework. It is currently experimental and requires Uno's Skia renderer. It does not
yet carry a production support claim for complete input, text, IME, accessibility,
resource, lifecycle, packaging, or conformance behavior. The reusable component host
is not yet available for Uno.

WebScene currently publishes runtime packages for these application RIDs:

| Platform | RID | Native runtime package |
| --- | --- | --- |
| macOS on Apple silicon | `osx-arm64` | `WebScene.NativeEngine.Runtime.osx-arm64` |
| Linux x64 | `linux-x64` | `WebScene.NativeEngine.Runtime.linux-x64` |
| Windows x64 | `win-x64` | `WebScene.NativeEngine.Runtime.win-x64` |

An application must declare an explicit `RuntimeIdentifier` and reference exactly one
matching runtime package. The package supplies the native engine, ICU data, ABI
metadata, licenses, and hashes.

## What WebScene is not

WebScene does not embed Chromium or a platform WebView, and it is not intended to load
arbitrary websites. It implements a versioned, bounded web component profile for UI
that an application packages, declares in `webscene-component.json`, tests, and
controls.

The native runtime is also not a browser-grade process, origin, navigation, permission,
or content sandbox. Never use it as the trust boundary for untrusted HTML or JavaScript.

## Next steps

- [Package and host a component](component-host.md)
- [Use WebScene with Avalonia](avalonia.md)
- [Host a document in Uno Platform](uno.md)
- [Call JavaScript from .NET with generated bindings](javascript-interop.md)
- [Package and publish an application](packages-and-deployment.md)
- [Choose a content and resource strategy](content-and-resources.md)
- [Integrate lifecycle and diagnostics](lifecycle-and-diagnostics.md)
- [Review compatibility and security boundaries](compatibility-and-security.md)
- Review the repository's [backend status](https://github.com/wieslawsoltes/WebScene/blob/main/docs/backends.md)
  and [compatibility profile](https://github.com/wieslawsoltes/WebScene/blob/main/tests/WebPlatformSubset/README.md)
  before choosing WebScene for an application.
