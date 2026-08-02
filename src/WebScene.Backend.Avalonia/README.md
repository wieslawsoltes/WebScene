# WebScene.Backend.Avalonia

The reference Avalonia presenter for WebScene's native ABI 3 engine. It consumes
immutable native scene snapshots and owns Skia rendering, text shaping, input
forwarding, resource loading, frame scheduling, and Avalonia lifecycle integration.

The package contains:

- `NativeWebSceneRuntime`, which validates and prewarms the native runtime;
- `NativeWebSceneView`, which hosts an absolute document URL;
- `NativeSceneSurface`, the focusable Avalonia scene presenter; and
- `NativeWebSceneApi`, the low-level operations used by advanced integrations.

There is no managed DOM, CSS, layout, Canvas, WebGL, or JavaScript engine in this
package and no fallback to one. The former `AvaloniaBrowserHost` and
`AvaloniaBackendHost` APIs have been removed.

Native scene publication uses a lock-free mailbox consumed on Avalonia's compositor
clock. Ordinary scene traffic does not enter the UI dispatcher; a coalesced
UI-to-compositor wake remains only for first presentation and cooperative live resize.

This is currently an advanced integration surface rather than a turnkey component
host. See the repository's native showcase, Monaco, and TradingView samples for the
supported composition pattern.
