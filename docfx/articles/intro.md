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
 Avalonia or Uno WebSceneComponentHost
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
 Avalonia or Uno Skia presenter
```

## Supported hosts

`WebScene.Sdk.Avalonia.WebSceneComponentHost` and
`WebScene.Sdk.Uno.WebSceneComponentHost` are the recommended application surfaces.
Both compose the portable Component Profile 1 SDK with a framework-native presenter
and expose the same package, lifecycle, capability, interop, and diagnostics model.
Use `NativeWebSceneView` or `UnoNativeWebSceneView` directly only for advanced document
or backend-level integration.

Avalonia remains the reference presenter. Uno is a first-class supported host for
Skia desktop applications on the published native RIDs. This does not imply Uno
browser, mobile, non-Skia, or general browser compatibility.

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
- [Use WebScene with Uno Platform](uno.md)
- [Call JavaScript from .NET with generated bindings](javascript-interop.md)
- [Package and publish an application](packages-and-deployment.md)
- [Choose a content and resource strategy](content-and-resources.md)
- [Integrate lifecycle and diagnostics](lifecycle-and-diagnostics.md)
- [Review compatibility and security boundaries](compatibility-and-security.md)
- Review the repository's [backend status](https://github.com/wieslawsoltes/WebScene/blob/main/docs/backends.md)
  and [compatibility profile](https://github.com/wieslawsoltes/WebScene/blob/main/tests/WebPlatformSubset/README.md)
  before choosing WebScene for an application.
