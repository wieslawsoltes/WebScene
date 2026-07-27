# WebScene.Backend.Avalonia

The complete Avalonia backend for WebScene. It owns Avalonia visual projection, native
layout integration, Canvas/SVG replay, input, focus, text/image services, clipboard,
window lifecycle, OpenGL surfaces, frame scheduling, and headless composition.

Applications reference this package directly. The implementation currently retains
the established `JavaScript.Avalonia` CLR namespace, but there is no separate
`JavaScript.Avalonia` package or assembly.

The package also owns the ABI 2 native presentation host under
`WebScene.Backends.Avalonia.Native`:

- `NativeWebSceneRuntime` validates and prewarms the native runtime.
- `NativeWebSceneView` hosts an arbitrary absolute document URL.
- `NativeSceneSurface` is the shared focusable Avalonia scene presenter.
- `NativeWebSceneApi` exposes the low-level engine operations needed by advanced
  component integrations.

Applications should not copy the scene renderer. Keeping SVG, shaped text,
clipping, input, cursor, animation-frame, and resize projection here ensures
every native WebScene consumer uses the same implementation.

Native scene publication uses a lock-free mailbox consumed on Avalonia's
compositor clock. Ordinary scene traffic does not enter the UI dispatcher; a
single coalesced UI-to-compositor wake is retained only for first presentation
and cooperative live resize, where a platform nested event loop can suspend
normal animation callbacks.
