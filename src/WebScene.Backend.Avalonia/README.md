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

## Document-start scripts and storage

Use `LoadAsync(NativeWebSceneLoadOptions)` to install ordered compatibility scripts
after the loading document and location exist but before authored JavaScript. Scripts
whose `AllFrames` value is true also run before authored scripts in each subsequently
created frame. A document-start exception fails the initial load and includes the
configured script name in native diagnostics.

The 1.0.19 native runtime supplies synchronous, in-memory `localStorage` and
`sessionStorage` objects for the current engine/page lifetime. It implements
`length`, `key`, `getItem`, `setItem`, `removeItem`, and `clear`, including JavaScript
string coercion, null results, and stable insertion order. This is a compatibility
subset: it does not promise persistence, quotas, storage events, origin/reload
semantics, cross-engine profiles, or IndexedDB.
