# WebScene backend authoring kit

Implement `IWebSceneBackendHost` (normally by deriving from `WebSceneBackendHostBase`) and
publish a validated `webscene-backend.json` capability manifest. A backend owns platform
windows/surfaces, persistent visual projection, intrinsic text/image measurement,
Canvas/SVG replay, input/focus, clipboard/IME/accessibility, frame scheduling, and
platform lifecycle. It must not fork DOM, CSS, layout, or JavaScript semantics.

Use `WebSceneBackendContractVerifier` as the profile preflight and run the shared
mount/tree/layout/invalidation/input/cancellation/disposal contract suite before making
a support-level claim. Capability flags describe implementations; L0–L3 evidence proves
their behavior.

Hot paths should use typed structures and batched packets. Do not wrap each DOM node or
Canvas command in another backend object merely to cross a package boundary.

Native presenters share `NativeWebSceneLoadOptions` and `WebSceneDocumentScript` from
the `WebScene.Backends.Native` namespace. Document-start scripts are ordered, copied
into native navigation work, and may opt into child frames with `AllFrames`.
