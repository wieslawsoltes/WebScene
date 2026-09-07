# WebScene.Backend.Uno

The Uno Skia presenter hosts WebScene's native DOM/runtime and immutable scenes.

`UnoNativeWebSceneView` exposes `JavaScriptException`, `ConsoleMessage`,
`RuntimeFailed`, retained `LastFailure` and opt-in failure UI. See
[Runtime diagnostics](../../docs/runtime-diagnostics.md) for threading, console
capture migration, SDK integration and failure recovery limits.
