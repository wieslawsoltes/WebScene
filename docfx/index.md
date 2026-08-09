# WebScene

WebScene embeds trusted, packaged web-authored UI in .NET applications without a
WebView. JavaScript runs in V8; WebScene owns its bounded DOM, CSS, layout, Canvas,
and SVG implementation; and immutable scenes are presented by the native host UI.

For Avalonia, `WebSceneComponentHost` is the recommended entry point: add one XAML
control, point it at a component package, and let the host validate, mount, isolate,
diagnose, and unmount it.

> [!IMPORTANT]
> WebScene is pre-production and is not a browser or a security sandbox. Use it only
> for content your application owns and trusts.

## Choose a host

| Host | Status | Start here |
| --- | --- | --- |
| Avalonia | Component host and reference presenter; recommended integration | [Package and host a component](articles/component-host.md) |
| Uno Platform | Skia presenter proof; not yet production-ready | [Use WebScene with Uno Platform](articles/uno.md) |

The Avalonia component host also installs a capability-gated service bridge. Use that
bridge for component-to-application requests, generated typed bindings for repeated
.NET-to-JavaScript calls, and
[`EvaluateTextAsync`](articles/javascript-interop.md#evaluate-small-diagnostic-expressions)
for diagnostics.

## Documentation

- [Introduction and support boundaries](articles/intro.md)
- [Package and host a component](articles/component-host.md)
- [Avalonia integration](articles/avalonia.md)
- [Uno Platform integration](articles/uno.md)
- [.NET and JavaScript interop](articles/javascript-interop.md)
- [Packages and deployment](articles/packages-and-deployment.md)
- [Content and resource loading](articles/content-and-resources.md)
- [Lifecycle and diagnostics](articles/lifecycle-and-diagnostics.md)
- [Compatibility and security](articles/compatibility-and-security.md)
- [Troubleshooting](articles/troubleshooting.md)
- [Repository design, validation, and release reference](articles/repository-reference.md)
- [API documentation](api/index.md)
