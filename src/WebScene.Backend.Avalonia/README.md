# WebScene.Backend.Avalonia

For production exception logging, opt-in console capture and terminal error UI,
see [Runtime diagnostics](../../docs/runtime-diagnostics.md). Subscribe to
`JavaScriptException` and `RuntimeFailed` before loading; `ConsoleMessage` is optional.
Legacy console draining now requires `CaptureLegacyConsoleMessages = true`.

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

Advanced integrations can use this package directly. Packaged Component Profile 1
applications should use `WebScene.Sdk.Avalonia.WebSceneComponentHost`, which composes
this view with package validation, isolated resources, host capabilities, lifecycle,
recovery, and diagnostics.

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
