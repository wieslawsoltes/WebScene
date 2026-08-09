# API documentation

The API reference is generated from the public Component Profile 1 SDK, Avalonia and
Uno component hosts, backend contracts, presenters, JavaScript interop, and CDP
diagnostics assemblies.

Start with the framework-specific `WebScene.Sdk.Avalonia.WebSceneComponentHost` or
`WebScene.Sdk.Uno.WebSceneComponentHost` for application integration. Their underlying
`NativeWebSceneView` and `UnoNativeWebSceneView` surfaces remain available as advanced
direct-view APIs through `WebSceneComponentHost.View`.

The presenters share native-host implementation code while retaining framework-native
public entry points. See the [Avalonia guide](../articles/avalonia.md) and
[Uno Platform guide](../articles/uno.md) for complete setup examples.
