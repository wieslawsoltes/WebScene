# WebScene

WebScene embeds trusted, packaged web-authored UI in .NET applications without a
WebView. JavaScript runs in V8; WebScene owns its bounded DOM, CSS, layout, Canvas,
and SVG implementation; and immutable scenes are presented by the native host UI.

> [!IMPORTANT]
> WebScene is pre-production and is not a browser or a security sandbox. Use it only
> for content your application owns and trusts.

## Choose a host

| Host | Status | Start here |
| --- | --- | --- |
| Avalonia | Supported reference presenter | [Use WebScene with Avalonia](articles/avalonia.md) |
| Uno Platform | Supported Skia desktop presenter | [Use WebScene with Uno Platform](articles/uno.md) |

Both presenters expose the same native JavaScript boundary. Use
[`EvaluateTextAsync`](articles/javascript-interop.md#evaluate-small-diagnostic-expressions)
for diagnostics and generated, typed bindings for application interop.

## Documentation

- [Introduction and support boundaries](articles/intro.md)
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
