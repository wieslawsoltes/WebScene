# API documentation

The API reference is generated from the public Component Profile 1 SDK, Avalonia
component host, backend contracts, Avalonia presenter, JavaScript interop, and CDP
diagnostics assemblies.

Start with `WebScene.Sdk.Avalonia.WebSceneComponentHost` for application integration.
The `WebScene.Backends.Avalonia.Native.NativeWebSceneView` surface is the advanced
direct-view API and is also exposed through `WebSceneComponentHost.View`.

The Uno presenter reuses much of the Avalonia native scene implementation by linked
source. Its supported entry points are documented in the [Uno Platform guide](../articles/uno.md)
to avoid publishing duplicate API identifiers for the shared implementation types.
