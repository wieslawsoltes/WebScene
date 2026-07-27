# WebScene.Sdk.Avalonia

`WebSceneComponentHost` is the XAML-first Avalonia host for a packaged WebScene
component. Set `PackagePath`, optionally register explicit host capability handlers,
and attach the control to a window. The control validates the manifest and profile,
loads only declared local assets, creates an isolated V8/DOM instance, uses the
process-wide immutable V8 and asset caches, invokes the declared mount/unmount hooks,
and disposes deterministically when detached.

Applications must ship a reviewed RID-specific ClearScript V8 native package. WebScene
components are trusted application code; this control does not create a browser-grade
security sandbox.
